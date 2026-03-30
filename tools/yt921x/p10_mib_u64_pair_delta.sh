#!/usr/bin/env bash
set -euo pipefail

ROUTER="${ROUTER:-root@192.168.2.1}"
YT_CMD="${YT_CMD:-/sys/kernel/debug/yt921x_cmd}"
SSH_OPTS=(-F /dev/null -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/codex_known_hosts)

# default p10 signal pairs (lo,hi)
PAIR_LIST=(
  "0x0c01b0,0x0c01b4"
  "0x0c01c0,0x0c01c4"
  "0x0c01d4,0x0c01d8"
  "0x0c01f0,0x0c01f4"
)

SLEEP_SEC=2

usage() {
  cat <<'USAGE'
Usage:
  p10_mib_u64_pair_delta.sh [--sleep-sec N]

Reads p10 candidate 64-bit pairs twice and prints wrap-safe delta as:
  d_hi (u32), d_lo (u32), and combined hex hi:lo

This avoids invalid math when lower 32-bit words wrap between reads.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --sleep-sec) SLEEP_SEC="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; usage; exit 1 ;;
  esac
done

ssh_run() {
  ssh "${SSH_OPTS[@]}" "$ROUTER" "$@"
}

reg_read() {
  local addr="$1"
  local out val
  out=$(ssh_run "echo 'reg read ${addr}' > ${YT_CMD}; cat ${YT_CMD}")
  val=$(printf '%s\n' "$out" | sed -n 's/.*= \(0x[0-9A-Fa-f]\+\).*/\1/p' | tail -n1)
  if [[ -z "$val" ]]; then
    echo "failed to parse read for ${addr}: ${out}" >&2
    return 1
  fi
  printf '%u\n' "$((val))"
}

# Stable free-running 64-bit read using Hi-Lo-Hi sequence.
# Prints: "hi,lo"
read_u64_pair_hilohi() {
  local lo="$1" hi="$2"
  local hi_a lo_v hi_b tries

  tries=0
  while :; do
    hi_a="$(reg_read "$hi")"
    lo_v="$(reg_read "$lo")"
    hi_b="$(reg_read "$hi")"

    if [[ "$hi_a" == "$hi_b" ]]; then
      printf '%u,%u\n' "$hi_b" "$lo_v"
      return 0
    fi

    tries=$((tries + 1))
    if (( tries >= 5 )); then
      # Fallback: use the later high word and re-read low once.
      lo_v="$(reg_read "$lo")"
      printf '%u,%u\n' "$hi_b" "$lo_v"
      return 0
    fi
  done
}

snapshot_pairs() {
  local out="$1"
  : > "$out"
  local p lo hi vlo vhi
  for p in "${PAIR_LIST[@]}"; do
    lo="${p%,*}"
    hi="${p#*,}"
    IFS=, read -r vhi vlo <<<"$(read_u64_pair_hilohi "$lo" "$hi")"
    printf '%s,%s,%u,%u\n' "$lo" "$hi" "$vlo" "$vhi" >> "$out"
  done
}

# returns d_hi,d_lo
u64_delta_split() {
  local lo1="$1" hi1="$2" lo2="$3" hi2="$4"
  local d_lo d_hi

  if (( lo2 >= lo1 )); then
    d_lo=$((lo2 - lo1))
    d_hi=$(((hi2 - hi1) & 0xffffffff))
  else
    d_lo=$((((lo2 + 0x100000000) - lo1) & 0xffffffff))
    d_hi=$(((hi2 - hi1 - 1) & 0xffffffff))
  fi
  printf '%u,%u\n' "$d_hi" "$d_lo"
}

before=$(mktemp)
after=$(mktemp)
trap 'rm -f "$before" "$after"' EXIT

snapshot_pairs "$before"
sleep "$SLEEP_SEC"
snapshot_pairs "$after"

echo "sleep_sec=${SLEEP_SEC}"
echo "lo,hi,d_hi_u32,d_lo_u32,d_hex_hi:d_hex_lo"

declare -A B_LO B_HI
while IFS=, read -r lo hi vlo vhi; do
  B_LO["$lo,$hi"]="$vlo"
  B_HI["$lo,$hi"]="$vhi"
done < "$before"

while IFS=, read -r lo hi vlo vhi; do
  key="$lo,$hi"
  lo1="${B_LO[$key]}"
  hi1="${B_HI[$key]}"
  IFS=, read -r d_hi d_lo <<<"$(u64_delta_split "$lo1" "$hi1" "$vlo" "$vhi")"
  printf '%s,%s,%u,%u,0x%08x:0x%08x\n' "$lo" "$hi" "$d_hi" "$d_lo" "$d_hi" "$d_lo"
done < "$after"
