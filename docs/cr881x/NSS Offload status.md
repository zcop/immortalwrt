# Current native NSS status

The `nss-experiment` branch starts native NSS and its QinQ node adapter
automatically on Linux6.18.60, with YT921x retaining DSA ownership. On2026-11-07,
restored build22 reaches936.50/936.68Mbps routed IPv4 TCP,98.67%/98.30% CPU
idle, active ECM connections and increasing firmware counters. Earlier native
tests measured zero loss at900Mbps UDP. These are short individual tests.

Build19 fixes existing-flow reacceleration after transient topology changes.
A single TCP session stays usable and is accelerated at each sampled point
after detach, VLAN-upper creation/removal and rejoin, averaging926.66Mbps
across the test. Resource exhaustion and repeated topology stress remain open.

Customer VLAN/multiport membership, direct port VLAN uppers, unaware bridge
VLAN uppers, cross-conduit switching, high customer VIDs, STP/MST and hardware
rate/mirror/drop policies have later slice evidence. Core unload/reload now
restores both Ethernet host paths without duplicate registrations or kernel
WARNs. A warm standard-DSA boot passes customer S3500/C35 delivery through
LAN3, including1500-byte IP packets and PCP5/3, then returns to automatic NSS
startup. Standard mode uses host forwarding. Multiport customer QinQ, the raw
secondary conduit and sustained host-path load remain open. Wi-Fi NSS is
disabled; IPv6 traffic, PPP/tunnel paths and full cold/warm boot coverage remain
separate acceptance work. The [remaining DSA/QinQ plan](nss-qosmio-dsa-slices.md#remaining-dsaqinq-plan-after-slice19)
groups the current parity work into six further slices.

Startup and TCP resync behavior and earlier validation details are recorded in
[device test details](nss-qosmio-device-test.md) and
[source selection and implementation](nss-qosmio-port.md). Current image and
packet evidence are in [execution slices](nss-qosmio-dsa-slices.md) and the
[standard-mode report](standard-dsa-qinq-fallback.md).

The older measurements below are historical; they do not validate this branch.

# CR881x NSS Offload Status

Date: 2026-04-23

## Current status

- Platform: IPQ50xx + YT9215S (DSA)
- NSS stack loads and runs (`qca-nss-drv`, `qca-nss-dp`, `ecm`).
- Best observed behavior is asymmetric with current DSA integration:
  - Downstream (WAN -> LAN): offload works well, near line-rate.
  - Upstream (LAN -> WAN): offload is weaker/partial and often falls back.

## Measured routing results (latest)

Test path: LAN client `10.1.0.178` <-> WAN host `192.168.137.1` via router `192.168.2.1`.

- NSS ON, flow offload OFF:
  - Downstream TCP (`iperf3 -R`, 20s): ~941 Mbps
- NSS OFF, flow offload ON:
  - Downstream TCP (`iperf3 -R`, 20s): ~399 Mbps
  - Upstream TCP (`iperf3`, 20s): ~578 Mbps

## Configuration notes

- Do not run NSS ECM and OpenWrt flow offload together on this device.
- Use one fast path at a time:
  - NSS mode: ECM ON, flow offload OFF
  - Software mode: ECM OFF, flow offload ON
- `nf_conntrack_tcp_be_liberal=1` is important to reduce ECM flush/thrash on this kernel line.

## Conclusion: DSA vs NSS tradeoff

- Keep DSA:
  - Pros: modern OpenWrt switch model, standard DSA/switchdev workflow, better upstream maintainability.
  - Cons: NSS acceleration remains asymmetric on this platform (mainly downstream gain).

- Drop DSA (move to non-DSA switch path to favor NSS parser compatibility):
  - Pros: higher chance of symmetric NSS acceleration.
  - Cons: lose DSA-era features/ergonomics (per-port DSA workflow, bridge-vlan model, switchdev consistency), higher maintenance burden.

Final decision for CR881x:
- Keep DSA as the default architecture.
- Current NSS benefit is mostly download speed; this does not justify dropping many YT921x switch offloads and DSA control-plane features.

DSA feature comparison, actual packet tests, current image gaps and ordered
fix plan: [native DSA feature audit](nss-qosmio-dsa-feature-audit.md).
