#!/usr/bin/env bash
set -euo pipefail

# Decode stock field-table symbols (*_field) from yt_switch.ko rodata.
# Output TSV: symbol\tbyte_len\tentry\tfield\twidth\tword\tlsb
#
# Usage:
#   stock_field_decode.sh [path/to/yt_switch.ko] [regex]
#
# Example:
#   stock_field_decode.sh Collection-Data/cr881x/mtd22_rootfs/lib/modules/4.4.60/yt_switch.ko 'qsch|psch|qos|int_prio|dscp|meter|port_rate|mirror_qos'

KO_DEFAULT="Collection-Data/cr881x/mtd22_rootfs/lib/modules/4.4.60/yt_switch.ko"
KO="${1:-$KO_DEFAULT}"
PATTERN="${2:-_field$}"

if [[ ! -f "$KO" ]]; then
	echo "error: module not found: $KO" >&2
	exit 1
fi

read -r ro_off_hex ro_addr_hex < <(
	readelf -W -S "$KO" | awk '
	$2==".rodata" {
		addr=$4; off=$5;
		gsub(/^0+/, "", off); if (off=="") off="0";
		gsub(/^0+/, "", addr); if (addr=="") addr="0";
		print off, addr; exit
	}
	$3==".rodata" {
		addr=$5; off=$6;
		gsub(/^0+/, "", off); if (off=="") off="0";
		gsub(/^0+/, "", addr); if (addr=="") addr="0";
		print off, addr; exit
	}'
)

if [[ -z "${ro_off_hex:-}" || -z "${ro_addr_hex:-}" ]]; then
	echo "error: cannot locate .rodata" >&2
	exit 1
fi

ro_off=$((16#$ro_off_hex))
ro_addr=$((16#$ro_addr_hex))

tmp_syms="$(mktemp)"
trap 'rm -f "$tmp_syms"' EXIT

nm -n "$KO" | awk '$2 ~ /^[rR]$/ && $3 ~ /_field$/ { print $1"\t"$3 }' > "$tmp_syms"

if [[ ! -s "$tmp_syms" ]]; then
	echo "error: no *_field symbols found" >&2
	exit 1
fi

echo -e "symbol\tbyte_len\tentry\tfield\twidth\tword\tlsb"

mapfile -t lines < "$tmp_syms"
for ((i = 0; i < ${#lines[@]}; i++)); do
	addr_hex="${lines[$i]%%$'\t'*}"
	sym="${lines[$i]#*$'\t'}"

	if ! [[ "$sym" =~ $PATTERN ]]; then
		continue
	fi

	addr=$((16#$addr_hex))
	next_addr=0
	for ((k = i + 1; k < ${#lines[@]}; k++)); do
		next_addr_hex="${lines[$k]%%$'\t'*}"
		next_addr=$((16#$next_addr_hex))
		if (( next_addr > addr )); then
			break
		fi
		next_addr=0
	done
	if (( next_addr > addr )); then
		len=$((next_addr - addr))
	else
		# Unknown tail length for the last symbol in rodata; skip to stay safe.
		continue
	fi

	if (( len <= 0 || len > 512 )); then
		continue
	fi

	off=$((ro_off + addr - ro_addr))
	bytes=( $(dd if="$KO" bs=1 skip="$off" count="$len" 2>/dev/null | od -An -t u1 -v) )

	entry=0
	for ((j = 0; j + 3 < ${#bytes[@]}; j += 4)); do
		field="${bytes[$j]}"
		width="${bytes[$((j + 1))]}"
		word="${bytes[$((j + 2))]}"
		lsb="${bytes[$((j + 3))]}"

		# Basic sanity filter for bitfield tuples.
		if (( width == 0 || width > 32 || word > 7 || lsb > 31 )); then
			continue
		fi

		echo -e "$sym\t$len\t$entry\t$field\t$width\t$word\t$lsb"
		entry=$((entry + 1))
	done
done
