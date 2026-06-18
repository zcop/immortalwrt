#!/usr/bin/env bash
set -euo pipefail

ROUTER="${ROUTER:-root@192.168.2.1}"
SSH_OPTS=(
  -F /dev/null
  -o StrictHostKeyChecking=no
  -o UserKnownHostsFile=/tmp/codex_known_hosts
)
IFACES=(lan1 lan2 eth1 eth0)
METRICS=(rx_packets rx_bytes tx_packets tx_bytes)
DEFAULT_PROBE=(ping -c 20 -W 1 192.168.2.176)
MODE="${MODE:-mib}"

usage() {
  cat <<'EOF'
Usage:
  MODE=ethtool scripts/yt921x-bridge-offload-test.sh [-- <probe command ...>]
  MODE=mib     scripts/yt921x-bridge-offload-test.sh [-- <probe command ...>]

Examples:
  scripts/yt921x-bridge-offload-test.sh
  scripts/yt921x-bridge-offload-test.sh -- ping -c 50 -W 1 192.168.2.176
  scripts/yt921x-bridge-offload-test.sh -- bash -lc 'payload=$(printf "%0100d" 0); for i in $(seq 1 10); do printf "%s" "$payload" | nc -u -w1 192.168.2.176 12345; done'

Notes:
  - Probe traffic is generated from this workspace machine.
  - Counter snapshots come from the live CR881x router.
  - The default target is the LAN2 Pi at 192.168.2.176.
  - MODE=mib reads raw switch counters via /sys/kernel/debug/yt921x_cmd.
EOF
}

snapshot_ethtool() {
  local router_cmd
  router_cmd=$(cat <<'EOF'
set -e
for ifc in lan1 lan2 eth1 eth0; do
  ethtool -S "$ifc" 2>/dev/null | awk -v ifc="$ifc" '
    /^( +)?(rx_packets|rx_bytes|tx_packets|tx_bytes):/ {
      gsub(":", "", $1)
      printf "%s %s %s\n", ifc, $1, $2
    }'
done
EOF
)
  ssh "${SSH_OPTS[@]}" "$ROUTER" "$router_cmd"
}

snapshot_mib() {
  local router_cmd
  router_cmd=$(cat <<'EOF'
set -e
CMD=/sys/kernel/debug/yt921x_cmd

read_reg() {
  local reg="$1" out
  for _ in 1 2 3 4 5; do
    printf 'reg read 0x%x\n' "$reg" > "$CMD"
    out=$(cat "$CMD")
    case "$out" in
      *"reg 0x"*)
        printf '%s\n' "$out" | sed -n 's/.*= 0x\([0-9a-fA-F]\+\).*/\1/p'
        return 0
        ;;
    esac
    sleep 1
  done
  return 1
}

read_u64() {
  local lo_reg="$1" hi_reg="$2" lo hi
  lo=$(read_reg "$lo_reg")
  hi=$(read_reg "$hi_reg")
  printf '%s\n' "$(( (0x$hi << 32) | 0x$lo ))"
}

read_rx_frames() {
  local base="$1" total=0 off val
  for off in 0x1c 0x20 0x24 0x28 0x2c 0x30 0x34; do
    val=$(read_reg "$((base + off))")
    total=$((total + 0x$val))
  done
  printf '%u\n' "$total"
}

read_tx_frames() {
  local base="$1" val
  val=$(read_reg "$((base + 0x9c))")
  printf '%u\n' "$((0x$val))"
}

emit_port() {
  local name="$1" port="$2" base
  base=$((0xc0100 + 0x100 * port))
  printf '%s rx_packets %s\n' "$name" "$(read_rx_frames "$base")"
  printf '%s rx_bytes %s\n' "$name" "$(read_u64 "$((base + 0x3c))" "$((base + 0x40))")"
  printf '%s tx_packets %s\n' "$name" "$(read_tx_frames "$base")"
  printf '%s tx_bytes %s\n' "$name" "$(read_u64 "$((base + 0x84))" "$((base + 0x88))")"
}

emit_port lan1 0
emit_port lan2 1
emit_port eth1 8
emit_port eth0 4
EOF
)
  ssh "${SSH_OPTS[@]}" "$ROUTER" "$router_cmd"
}

snapshot() {
  case "$MODE" in
    ethtool) snapshot_ethtool ;;
    mib) snapshot_mib ;;
    *)
      echo "Unsupported MODE=$MODE (use ethtool or mib)" >&2
      exit 1
      ;;
  esac
}

declare -A before after

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

probe_cmd=("$@")
if [[ ${#probe_cmd[@]} -eq 0 ]]; then
  probe_cmd=("${DEFAULT_PROBE[@]}")
fi

printf 'Counter mode: %s\n' "$MODE"

while read -r ifc metric value; do
  before["$ifc:$metric"]="$value"
done < <(snapshot)

printf 'Running probe:'
printf ' %q' "${probe_cmd[@]}"
printf '\n'
"${probe_cmd[@]}"

while read -r ifc metric value; do
  after["$ifc:$metric"]="$value"
done < <(snapshot)

printf '\n%-6s %-10s %12s %12s %12s\n' "iface" "metric" "before" "after" "delta"
for ifc in "${IFACES[@]}"; do
  for metric in "${METRICS[@]}"; do
    b=${before["$ifc:$metric"]:-0}
    a=${after["$ifc:$metric"]:-0}
    d=$((a - b))
    printf '%-6s %-10s %12s %12s %12s\n' "$ifc" "$metric" "$b" "$a" "$d"
  done
done
