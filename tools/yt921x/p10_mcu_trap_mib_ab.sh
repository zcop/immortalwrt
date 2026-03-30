#!/usr/bin/env bash
set -euo pipefail

ROUTER="${ROUTER:-root@192.168.2.1}"
YT_CMD="${YT_CMD:-/sys/kernel/debug/yt921x_cmd}"
SSH_OPTS=(-F /dev/null -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/codex_known_hosts)

TARGET_REG="${TARGET_REG:-0x180690}"
RUN_MATRIX=0
BIT_IDX=10
MODE="auto"      # auto|set|clear|flip
PULSE_SEC=3

MIB_START="${MIB_START:-0x0c0100}"
MIB_END="${MIB_END:-0x0c01fc}"
MIB_STEP=4

DURING_CMD=""
TAG=""

declare -A ORIG_VALS=()
declare -a TOUCHED_REGS=()

usage() {
  cat <<'USAGE'
Usage:
  p10_mcu_trap_mib_ab.sh [options]

Options:
  --target <hex>       target register (default: 0x180690)
  --matrix             run bit10 A/B on 0x180690, 0x180510, 0x180514
  --bit <n>            bit index (default: 10)
  --mode <m>           auto|set|clear|flip (default: auto)
  --pulse-sec <n>      pulse duration seconds (default: 3)
  --mib-start <hex>    MIB window start (default: 0x0c0100)
  --mib-end <hex>      MIB window end (default: 0x0c01fc)
  --mib-step <n>       MIB stride bytes (default: 4)
  --during-cmd <cmd>   local command to run during pulse (default: sleep pulse-sec)
  --tag <name>         label printed in output
  -h, --help           show this help

Notes:
  - Uses /sys/kernel/debug/yt921x_cmd through SSH.
  - Captures MIB window before and after pulse, printing only changed words.
  - Always restores original register values on exit/error (best effort).
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --target) TARGET_REG="$2"; shift 2 ;;
    --matrix) RUN_MATRIX=1; shift ;;
    --bit) BIT_IDX="$2"; shift 2 ;;
    --mode) MODE="$2"; shift 2 ;;
    --pulse-sec) PULSE_SEC="$2"; shift 2 ;;
    --mib-start) MIB_START="$2"; shift 2 ;;
    --mib-end) MIB_END="$2"; shift 2 ;;
    --mib-step) MIB_STEP="$2"; shift 2 ;;
    --during-cmd) DURING_CMD="$2"; shift 2 ;;
    --tag) TAG="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ "$MODE" != "auto" && "$MODE" != "set" && "$MODE" != "clear" && "$MODE" != "flip" ]]; then
  echo "--mode must be auto|set|clear|flip" >&2
  exit 1
fi

if ! [[ "$BIT_IDX" =~ ^[0-9]+$ ]]; then
  echo "--bit must be integer" >&2
  exit 1
fi

if (( BIT_IDX < 0 || BIT_IDX > 31 )); then
  echo "--bit must be in range 0..31" >&2
  exit 1
fi

if ! [[ "$MIB_STEP" =~ ^[0-9]+$ ]] || (( MIB_STEP <= 0 )); then
  echo "--mib-step must be a positive integer" >&2
  exit 1
fi

ssh_run() {
  ssh "${SSH_OPTS[@]}" "$ROUTER" "$@"
}

reg_read() {
  local addr_hex="$1"
  local out val
  out=$(ssh_run "echo 'reg read ${addr_hex}' > ${YT_CMD}; cat ${YT_CMD}")
  val=$(printf '%s\n' "$out" | sed -n 's/.*= \(0x[0-9A-Fa-f]\+\).*/\1/p' | tail -n 1)
  if [[ -z "$val" ]]; then
    echo "failed to parse reg_read output for ${addr_hex}: ${out}" >&2
    return 1
  fi
  printf '0x%08x\n' "$((val))"
}

reg_write() {
  local addr_hex="$1"
  local val_hex="$2"
  ssh_run "echo 'reg write ${addr_hex} ${val_hex}' > ${YT_CMD}; cat ${YT_CMD}" >/dev/null
}

snapshot_mib() {
  local out_file="$1"
  local start_dec end_dec a
  start_dec=$((MIB_START))
  end_dec=$((MIB_END))
  : > "$out_file"
  for ((a = start_dec; a <= end_dec; a += MIB_STEP)); do
    local addr_hex val
    addr_hex=$(printf '0x%06x' "$a")
    val=$(reg_read "$addr_hex")
    printf '%s,%s\n' "$addr_hex" "$val" >> "$out_file"
  done
}

print_delta() {
  local before_file="$1"
  local after_file="$2"

  declare -A before_map=()
  while IFS=, read -r addr val; do
    before_map["$addr"]="$((val))"
  done < "$before_file"

  local changed=0
  while IFS=, read -r addr val; do
    local av bv d
    av=$((val))
    bv="${before_map[$addr]:-0}"
    d=$((av - bv))
    if (( d != 0 )); then
      changed=1
      printf '  %s delta=%d (%#010x -> %#010x)\n' "$addr" "$d" "$bv" "$av"
    fi
  done < "$after_file"

  if (( changed == 0 )); then
    echo "  (no MIB deltas in window)"
  fi
}

calc_trial() {
  local orig_dec="$1"
  local bit="$2"
  local mode="$3"
  local mask trial

  mask=$((1 << bit))
  case "$mode" in
    set) trial=$((orig_dec | mask)) ;;
    clear) trial=$((orig_dec & ~mask)) ;;
    flip) trial=$((orig_dec ^ mask)) ;;
    auto)
      if (( (orig_dec & mask) == 0 )); then
        trial=$((orig_dec | mask))
      else
        trial=$((orig_dec & ~mask))
      fi
      ;;
    *) echo "invalid mode: $mode" >&2; return 1 ;;
  esac
  printf '%d\n' "$trial"
}

remember_orig() {
  local reg="$1"
  local orig="$2"
  if [[ -z "${ORIG_VALS[$reg]:-}" ]]; then
    ORIG_VALS["$reg"]="$orig"
    TOUCHED_REGS+=("$reg")
  fi
}

restore_all() {
  local reg
  for reg in "${TOUCHED_REGS[@]}"; do
    local val="${ORIG_VALS[$reg]}"
    reg_write "$reg" "$(printf '0x%08x' "$val")" || true
  done
}
trap restore_all EXIT

run_one() {
  local reg="$1"
  local orig_hex orig_dec trial_dec trial_hex
  local before_file after_file

  orig_hex=$(reg_read "$reg")
  orig_dec=$((orig_hex))
  remember_orig "$reg" "$orig_dec"

  trial_dec=$(calc_trial "$orig_dec" "$BIT_IDX" "$MODE")
  trial_hex=$(printf '0x%08x' "$trial_dec")

  before_file=$(mktemp)
  after_file=$(mktemp)

  snapshot_mib "$before_file"
  reg_write "$reg" "$trial_hex"

  if [[ -n "$DURING_CMD" ]]; then
    bash -lc "$DURING_CMD" >/dev/null 2>&1 || true
  else
    sleep "$PULSE_SEC"
  fi

  reg_write "$reg" "$(printf '0x%08x' "$orig_dec")"
  snapshot_mib "$after_file"

  printf 'case=%s reg=%s bit=%d mode=%s orig=%#010x trial=%#010x' "${TAG:-default}" "$reg" "$BIT_IDX" "$MODE" "$orig_dec" "$trial_dec"
  if (( trial_dec == orig_dec )); then
    printf ' (no-op)\n'
  else
    printf '\n'
  fi
  print_delta "$before_file" "$after_file"

  rm -f "$before_file" "$after_file"
}

if (( RUN_MATRIX == 1 )); then
  run_one 0x180690
  run_one 0x180510
  run_one 0x180514
else
  run_one "$TARGET_REG"
fi
