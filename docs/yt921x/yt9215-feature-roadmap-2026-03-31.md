# YT9215S Feature Roadmap (CR881x, 2026-03-31)

## Scope
- Platform: Xiaomi CR881x (`ipq5018` + `YT9215S`)
- Tree: `backport-6.12` driver path
- Goal: production-ready home appliance behavior, with clear path for optional advanced features

## Current Status

### Done (Production-Ready)
- DSA core switching:
  - VLAN add/del/filtering
  - FDB add/del/dump
  - MDB add/del
  - bridge join/leave/flags
  - STP + MST hooks
  - LAG join/leave
- Port and conduit handling:
  - primary/secondary CPU conduit model
  - tested stable on CR881x LAN/WAN topology
- Mirror offload:
  - mirror path wired
  - mirror QoS priority-map control wired
- QoS offload (home target complete):
  - `mqprio` queue map offload
  - `ets` SP/DWRR scheduler offload
  - `tbf` offload at port level
  - `tbf` offload at queue level
  - ingress policer offload
  - DSCP/prio mapping via DCB hooks
  - `tc flower` DSCP classification (`ingress`, exact DSCP + `skbedit priority`)
- QoS telemetry:
  - `ethtool -S <port>` exposes `qos_*` fields
  - includes queue map, scheduler state, shaper state, and policer state

### Done (Documented / Reverse-Engineered)
- Large register map coverage for YT9215S control path
- Stock module table-id and field decode for major QoS/mirror/remark blocks
- Port identity and isolation matrix behavior confirmed on CR881x board wiring

## Next (High Value, Reasonable Risk)
- Expand `flower` coverage:
  - add more safe match/action combinations
  - preserve strict reject behavior for unsupported rules
- Improve QoS runtime observability:
  - add additional hardware counters if stable monotonic queue/drop counters are found
  - keep `ethtool -S` as primary user-visible surface
- Egress remark policy hardening:
  - finalize safe policy defaults for DSCP/PCP rewrite paths
  - expose only mappings with deterministic behavior
- CI/regression matrix:
  - add repeatable `tc` test script set for `mqprio`/`ets`/`tbf`/policer/flower

## Optional (Low Priority for Home Appliance)
- Full stock parity for all QoS subfields in `0xe9/0xea` and related scheduler internals
- Deep tuning of unknown QoS-adjacent islands (example: `0x355000..0x355028`)
- Enterprise feature set expansion (complex trust/remark policy stacks)

## Not Feasible or Not Worthwhile Now
- NSS fastpath parity for this DSA topology:
  - current evidence indicates practical limits with this hardware/firmware combination
  - not a blocker for home QoS target because switch hardware offload already covers core data path needs
- Unsafe write exploration of unknown critical registers:
  - avoid on production branch unless protected by isolated bring-up branch and UART recovery path

## Home Appliance Verdict
- Mark YT9215S integration as **feature-complete for home deployment**.
- Remaining work is mostly observability depth and optional advanced policy expansion, not baseline functionality.
