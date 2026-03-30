#!/usr/bin/env bash
set -euo pipefail

ROUTER="${ROUTER:-root@192.168.2.1}"
YT_CMD="/sys/kernel/debug/yt921x_cmd"
PULSE_SEC=2
MODE="set"   # set|clear
ROW_PORT=""
BIT_IDX=""
DO_SWEEP=0
START_BIT=0
END_BIT=10
DURING_CMD=""

SSH_OPTS=(-F /dev/null -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/codex_known_hosts)

usage() {
  cat <<USAGE
Usage:
  $0 --row <port> --bit <idx> [--mode set|clear] [--pulse-sec N] [--during-cmd 'cmd']
  $0 --row <port> --sweep [--start-bit N] [--end-bit N] [--mode set|clear] [--pulse-sec N] [--during-cmd 'cmd']

Notes:
  - This only pulses one row of PORTn_ISOLATION: 0x180294 + 4*port
  - Auto-restores original register value on exit/error (best effort)
  - MIB snapshot reads selected counters for ports 0..10 before/after each pulse
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --row) ROW_PORT="$2"; shift 2 ;;
    --bit) BIT_IDX="$2"; shift 2 ;;
    --mode) MODE="$2"; shift 2 ;;
    --pulse-sec) PULSE_SEC="$2"; shift 2 ;;
    --during-cmd) DURING_CMD="$2"; shift 2 ;;
    --sweep) DO_SWEEP=1; shift ;;
    --start-bit) START_BIT="$2"; shift 2 ;;
    --end-bit) END_BIT="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ -z "$ROW_PORT" ]]; then
  echo "--row is required" >&2
  usage
  exit 1
fi

if [[ "$DO_SWEEP" -eq 0 && -z "$BIT_IDX" ]]; then
  echo "--bit is required unless --sweep is used" >&2
  usage
  exit 1
fi

if [[ "$MODE" != "set" && "$MODE" != "clear" ]]; then
  echo "--mode must be set|clear" >&2
  exit 1
fi

ssh_run() {
  ssh "${SSH_OPTS[@]}" "$ROUTER" "$@"
}

reg_read() {
  local addr_hex="$1"
  local out
  out=$(ssh_run "echo 'reg read ${addr_hex}' > ${YT_CMD}; cat ${YT_CMD}")
  local val
  val=$(printf '%s\n' "$out" | sed -n 's/.*= \(0x[0-9A-Fa-f]\+\).*/\1/p')
  if [[ -z "$val" ]]; then
    echo "failed to parse reg_read output for ${addr_hex}: ${out}" >&2
    return 1
  fi
  echo "$val"
}

reg_write() {
  local addr_hex="$1"
  local val_hex="$2"
  ssh_run "echo 'reg write ${addr_hex} ${val_hex}' > ${YT_CMD}; cat ${YT_CMD}" >/dev/null
}

# Selected MIB offsets (from driver map)
# rx_good_bytes_lo(0x3c), rx_bad_bytes_lo(0x44), rx_dropped(0x50),
# tx_broadcast(0x54), tx_multicast(0x5c), tx_pkt(0x9c)
MIB_OFFSETS=(0x3c 0x44 0x50 0x54 0x5c 0x9c)

mib_addr_hex() {
  local port="$1"
  local off="$2"
  local base=$((0x0c0100 + 0x100 * port))
  printf '0x%06x' $((base + off))
}

mib_snapshot() {
  local out_file="$1"
  : > "$out_file"
  for p in $(seq 0 10); do
    for off in "${MIB_OFFSETS[@]}"; do
      local a
      a=$(mib_addr_hex "$p" "$off")
      local v
      v=$(reg_read "$a")
      echo "p${p},${off},${v}" >> "$out_file"
    done
  done
}

hex_to_dec() {
  local h="$1"
  echo $((h))
}

print_deltas() {
  local before="$1"
  local after="$2"
  awk -F, '
    NR==FNR {k=$1","$2; b[k]=$3; next}
    {
      k=$1","$2;
      cmd = "printf \"%d\" " b[k]; cmd | getline bv; close(cmd);
      cmd = "printf \"%d\" " $3;   cmd | getline av; close(cmd);
      d = av - bv;
      if (d != 0) printf "%s,%s,delta=%d\n", $1, $2, d;
    }
  ' "$before" "$after"
}

row_addr_dec=$((0x180294 + 4 * ROW_PORT))
row_addr_hex=$(printf '0x%06x' "$row_addr_dec")
orig_hex=$(reg_read "$row_addr_hex")
if [[ ! "$orig_hex" =~ ^0x[0-9A-Fa-f]+$ ]]; then
  echo "invalid original register value for ${row_addr_hex}: ${orig_hex}" >&2
  exit 1
fi
orig_dec=$((orig_hex))

restore() {
  reg_write "$row_addr_hex" "$(printf '0x%08x' "$orig_dec")" || true
}
trap restore EXIT

pulse_one() {
  local bit="$1"
  local mask=$((1 << bit))
  local trial_dec

  if [[ "$MODE" == "set" ]]; then
    trial_dec=$((orig_dec | mask))
  else
    trial_dec=$((orig_dec & ~mask))
  fi

  local trial_hex
  trial_hex=$(printf '0x%08x' "$trial_dec")

  local before_file after_file
  before_file=$(mktemp)
  after_file=$(mktemp)

  mib_snapshot "$before_file"
  reg_write "$row_addr_hex" "$trial_hex"

  if [[ -n "$DURING_CMD" ]]; then
    bash -lc "$DURING_CMD" >/dev/null 2>&1 || true
  else
    sleep "$PULSE_SEC"
  fi

  reg_write "$row_addr_hex" "$(printf '0x%08x' "$orig_dec")"
  mib_snapshot "$after_file"

  echo "row=p${ROW_PORT} addr=${row_addr_hex} bit=${bit} mode=${MODE} orig=$(printf '0x%08x' "$orig_dec") trial=${trial_hex}"
  print_deltas "$before_file" "$after_file" || true

  rm -f "$before_file" "$after_file"
}

if [[ "$DO_SWEEP" -eq 1 ]]; then
  for b in $(seq "$START_BIT" "$END_BIT"); do
    pulse_one "$b"
  done
else
  pulse_one "$BIT_IDX"
fi
