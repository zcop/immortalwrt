#!/usr/bin/env bash
set -euo pipefail

# Decode selected YT stock table-id mappings directly from yt_switch.ko.
# Default path targets CR881x stock rootfs in this workspace.

KO_DEFAULT="Collection-Data/cr881x/mtd22_rootfs/lib/modules/4.4.60/yt_switch.ko"
KO="${1:-$KO_DEFAULT}"

if [[ ! -f "$KO" ]]; then
	echo "error: module not found: $KO" >&2
	exit 1
fi

if ! command -v readelf >/dev/null 2>&1 || ! command -v nm >/dev/null 2>&1; then
	echo "error: readelf/nm required" >&2
	exit 1
fi

ro_hex="$(readelf -SW "$KO" | awk '/\.rodata[[:space:]]+PROGBITS/{print $6; exit}')"
sym_hex="$(nm -n "$KO" | awk '$3=="tbl_reg_list"{print $1; exit}')"

if [[ -z "${ro_hex:-}" || -z "${sym_hex:-}" ]]; then
	echo "error: failed to locate .rodata or tbl_reg_list" >&2
	exit 1
fi

ro_off=$((16#$ro_hex))
sym_off=$((16#$sym_hex))
base=$((ro_off + sym_off))
entry_size=24

echo "ko: $KO"
printf "rodata_off=0x%s tbl_reg_list=0x%s file_base=%#x\n" "$ro_hex" "$sym_hex" "$base"
echo

echo "ID   BASE_MMIO    W1         W2         W3         W4         W5"
echo "---- -----------  ---------  ---------  ---------  ---------  ---------"
for id in 0x0d 0x98 0x99 0xa3 0xad 0xae 0xc6 0xc9 0xcc; do
	id_dec=$((id))
	off=$((base + id_dec * entry_size))
	words="$(dd if="$KO" bs=1 skip="$off" count="$entry_size" 2>/dev/null | od -An -tx4 -N"$entry_size" | tr '\n' ' ')"
	set -- $words
	w0="${1:-}"
	w1="${2:-}"
	w2="${3:-}"
	w3="${4:-}"
	w4="${5:-}"
	w5="${6:-}"
	printf "0x%02x %-11s %-10s %-10s %-10s %-10s %-10s\n" \
		"$id_dec" "$w0" "$w1" "$w2" "$w3" "$w4" "$w5"
done

echo
echo "Hint:"
echo "  w0 is MMIO base for this table-id."
echo "  field-id usage must still be correlated with disassembly (fal_tiger_*)."
