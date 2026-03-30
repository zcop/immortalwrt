#!/usr/bin/env bash
set -euo pipefail

ROUTER="${ROUTER:-root@192.168.2.1}"
YT_CMD="${YT_CMD:-/sys/kernel/debug/yt921x_cmd}"
SSH_OPTS=(-F /dev/null -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/codex_known_hosts)

# Comma-separated range list: start_hex:length_hex
RANGES="${RANGES:-0x040000:0x200,0x100000:0x200,0x200000:0x200,0x2c0000:0x200}"
IDLE_SEC="${IDLE_SEC:-1}"
TRAFFIC_CMD="${TRAFFIC_CMD:-ping -I wan -s 200 -c 200 172.16.9.199 >/dev/null 2>&1 || true}"
TOP_N="${TOP_N:-60}"

usage() {
  cat <<'USAGE'
Usage:
  rmon_hunt_scan.sh [--ranges LIST] [--idle-sec N] [--traffic-cmd CMD] [--top N]

Examples:
  rmon_hunt_scan.sh
  rmon_hunt_scan.sh --ranges '0x040000:0x400,0x100000:0x400'
  rmon_hunt_scan.sh --traffic-cmd 'ping -I wan -s 200 -c 1000 172.16.9.199 >/dev/null 2>&1 || true'

Output columns:
  addr, d_idle_u32, d_trial_u32, score=(d_trial-d_idle), v0, v1, v2

Heuristic:
  high score with low idle drift is a better RMON-counter candidate.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ranges) RANGES="$2"; shift 2 ;;
    --idle-sec) IDLE_SEC="$2"; shift 2 ;;
    --traffic-cmd) TRAFFIC_CMD="$2"; shift 2 ;;
    --top) TOP_N="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; usage; exit 1 ;;
  esac
done

ssh_run() {
  ssh "${SSH_OPTS[@]}" "$ROUTER" "$@"
}

snapshot_remote() {
  local out_file="$1"
  ssh_run "set -eu; \
    YT_CMD='${YT_CMD}'; RANGES='${RANGES}'; \
    IFS=','; \
    for r in \$RANGES; do \
      start=\${r%:*}; len=\${r#*:}; \
      off=0; \
      while [ \$off -lt \$((len)) ]; do \
        addr=\$(printf '0x%06x' \$((start + off))); \
        echo \"reg read \${addr}\" > \"\${YT_CMD}\"; \
        line=\$(cat \"\${YT_CMD}\"); \
        val=\$(printf '%s\n' \"\$line\" | sed -n 's/.*= \\(0x[0-9A-Fa-f]\\+\\).*/\\1/p' | tail -n1); \
        [ -n \"\$val\" ] || val=0x00000000; \
        printf '%s %s\n' \"\$addr\" \"\$val\"; \
        off=\$((off + 4)); \
      done; \
    done" > "${out_file}"
}

run_traffic() {
  ssh_run "set -e; ${TRAFFIC_CMD}"
}

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT
s0="${tmp_dir}/snap0.txt"
s1="${tmp_dir}/snap1.txt"
s2="${tmp_dir}/snap2.txt"

echo "snapshot0: ranges=${RANGES}"
snapshot_remote "$s0"
sleep "$IDLE_SEC"
echo "snapshot1: idle window=${IDLE_SEC}s"
snapshot_remote "$s1"
echo "trial: ${TRAFFIC_CMD}"
run_traffic
echo "snapshot2: post-trial"
snapshot_remote "$s2"

awk -v topn="$TOP_N" '
function hex2dec(h,    i,c,v,n) {
  sub(/^0x/, "", h);
  h = toupper(h);
  n = 0;
  for (i = 1; i <= length(h); i++) {
    c = substr(h, i, 1);
    v = index("0123456789ABCDEF", c) - 1;
    if (v < 0) return 0;
    n = (n * 16) + v;
  }
  return n;
}
function u32delta(a, b) {
  if (b >= a) return b - a;
  return b + 4294967296 - a;
}
FILENAME==ARGV[1] {
  v0[$1] = hex2dec($2);
  h0[$1] = $2;
  next;
}
FILENAME==ARGV[2] {
  v1[$1] = hex2dec($2);
  h1[$1] = $2;
  next;
}
FILENAME==ARGV[3] {
  v2[$1] = hex2dec($2);
  h2[$1] = $2;
}
END {
  print "addr,d_idle_u32,d_trial_u32,score_u32,v0,v1,v2";
  for (a in v0) {
    if (!(a in v1) || !(a in v2)) continue;
    d01 = u32delta(v0[a], v1[a]);
    d12 = u32delta(v1[a], v2[a]);
    score = d12 - d01;
    printf "%s,%u,%u,%d,%s,%s,%s\n", a, d01, d12, score, h0[a], h1[a], h2[a];
  }
}
' "$s0" "$s1" "$s2" \
| awk -F',' 'NR==1{print;next} {print | "sort -t, -k4,4nr"}' \
| awk -F',' -v topn="$TOP_N" 'NR==1{print;next} NR<=topn+1 {print}'

echo
echo "raw snapshots:"
echo "  $s0"
echo "  $s1"
echo "  $s2"
