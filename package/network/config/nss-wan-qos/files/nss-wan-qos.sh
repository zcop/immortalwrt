#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

state=/var/run/nss-wan-qos
ifb=ifb-nss-wan
sys=/sys/class/net

error() {
	logger -t nss-wan-qos "$*"
	echo "$*" >&2
	return 1
}

wan_conduit() {
	local dev port index path link tag

	dev="$(uci -q get network.wan.device)" || return 1
	port="${dev%%.*}"
	case "$port" in ''|*[!a-zA-Z0-9_-]*) return 1 ;; esac
	[ ! -e "$sys/$port/master" ] || return 1
	index="$(cat "$sys/$port/iflink" 2>/dev/null)" || return 1

	for path in "$sys"/*; do
		[ "$(cat "$path/ifindex" 2>/dev/null)" = "$index" ] || continue
		tag="$(cat "$path/dsa/tagging" 2>/dev/null)"
		case "$tag" in qca-qinq|yt921x-qinq) ;; *) return 1 ;; esac
		link="${path##*/}"
		break
	done
	[ -n "$link" ] && [ "$link" != "$port" ] || return 1

	for path in "$sys"/*; do
		case "${path##*/}" in "$port"|"$link") continue ;; esac
		[ "$(cat "$path/iflink" 2>/dev/null)" != "$index" ] || return 1
	done
	echo "$link"
}

valid_rate() {
	case "$1" in ''|*[!0-9]*) return 1 ;; esac
	[ "${#1}" -le 7 ] && [ "$1" -le 1000000 ]
}

clear_queues() {
	local dev queues filters

	[ -r "$state/conduit" ] || return 0
	read -r dev < "$state/conduit"
	if [ -e "$state/ifb" ] && [ -d "$sys/$ifb" ]; then
		queues="$(tc qdisc show dev "$ifb")" || return 1
		if echo "$queues" | grep -q '^qdisc nsstbl 7076: root'; then
			tc qdisc del dev "$ifb" root || return 1
		fi
	fi
	if [ -d "$sys/$dev" ]; then
		queues="$(tc qdisc show dev "$dev")" || return 1
		if [ -e "$state/ingress" ] && echo "$queues" | grep -q '^qdisc ingress ffff:'; then
			filters="$(tc filter show dev "$dev" parent ffff:)" || return 1
			if echo "$filters" | grep -q "nssmirred.* to device $ifb)"; then
				tc filter del dev "$dev" parent ffff: protocol all pref 49152 || return 1
			fi
			filters="$(tc filter show dev "$dev" parent ffff:)" || return 1
			[ -n "$filters" ] || tc qdisc del dev "$dev" ingress || return 1
		fi
		if [ -e "$state/egress" ] && echo "$queues" | grep -q '^qdisc nsstbl 7072: root'; then
			tc qdisc del dev "$dev" root || return 1
		fi
	fi
	if [ -e "$state/ifb" ] && [ -d "$sys/$ifb" ]; then
		ip link del dev "$ifb" || return 1
	fi
	rm -f "$state/conduit" "$state/ingress" "$state/egress" "$state/ifb" "$state/settings"
	rmdir "$state"
}

fair_queue() {
	tc qdisc add dev "$1" parent "$2:1" handle "$3:" nssfq_codel \
		target 5ms interval 100ms flows 256 limit 256 set_default
}

install_queues() {
	local dev="$1" up="$2" down="$3" mtu="$4"

	modprobe qca-nss-qdisc || return 1
	if [ "$down" -gt 0 ]; then
		modprobe ifb && modprobe act_nssmirred || return 1
		ip link add "$ifb" type ifb || return 1
		touch "$state/ifb"
		ip link set dev "$ifb" up || return 1
		tc qdisc add dev "$dev" handle ffff: ingress || return 1
		touch "$state/ingress"
		# The redirect registers the IFB as an NSS ingress interface.
		tc filter add dev "$dev" parent ffff: protocol all pref 49152 \
			u32 match u32 0 0 action nssmirred redirect dev "$ifb" fromdev "$dev" || return 1
		tc qdisc add dev "$ifb" root handle 7076: nsstbl rate "${down}kbit" \
			burst 128kb mtu "$mtu" || return 1
		fair_queue "$ifb" 7076 7077 || return 1
	fi
	[ "$up" -gt 0 ] || return 0
	tc qdisc add dev "$dev" root handle 7072: nsstbl rate "${up}kbit" \
		burst 128kb mtu "$mtu" || return 1
	touch "$state/egress"
	fair_queue "$dev" 7072 7073
}

apply() {
	local up down dev queues mtu settings

	if [ "$(uci -q get nss_wan_qos.wan.enabled)" != 1 ]; then
		clear_queues
		return
	fi
	up="$(uci -q get nss_wan_qos.wan.upload)"
	down="$(uci -q get nss_wan_qos.wan.download)"
	valid_rate "$up" && valid_rate "$down" || { error "Invalid WAN rate in kbit/s"; return 1; }
	if [ "$up" -eq 0 ] && [ "$down" -eq 0 ]; then
		clear_queues
		return
	fi
	dev="$(wan_conduit)" || {
		clear_queues
		error "WAN QoS requires a routed port on a dedicated QinQ conduit"
		return 1
	}
	mtu="$(cat "$sys/$dev/mtu")" || return 1
	mtu=$((mtu + 64))
	settings="$dev $up $down $mtu"
	if [ -r "$state/settings" ] && [ "$(cat "$state/settings")" = "$settings" ]; then
		queues="$(tc qdisc show dev "$dev")" || return 1
		if { [ "$up" -eq 0 ] || echo "$queues" | grep -q '^qdisc nssfq_codel 7073:'; } &&
		   { [ "$down" -eq 0 ] || { [ -e "$sys/$ifb" ] && echo "$queues" | grep -q '^qdisc ingress ffff:'; }; }; then
			return 0
		fi
	fi
	clear_queues || return 1
	queues="$(tc qdisc show dev "$dev")" || return 1
	if [ "$up" -gt 0 ] && echo "$queues" | grep ' root' | grep -vq ' 0: root'; then
		error "Custom root qdisc on $dev"
		return 1
	fi
	if [ "$down" -gt 0 ] && {
		[ -e "$sys/$ifb" ] || echo "$queues" | grep -Eq '^qdisc (ingress|clsact) ';
	}; then
		error "Existing ingress shaper on $dev or device $ifb"
		return 1
	fi
	mkdir -m 700 "$state" || return 1
	echo "$dev" > "$state/conduit"
	if ! install_queues "$dev" "$up" "$down" "$mtu"; then
		clear_queues
		error "Failed to configure NSS WAN queues on $dev"
		return 1
	fi
	echo "$settings" > "$state/settings"
}

exec 9>/var/lock/nss-wan-qos.lock
flock -x 9 || exit 1
case "$1" in
	apply) apply ;;
	clear) clear_queues ;;
	*) exit 1 ;;
esac
