# Current qosmio migration status

The `nss-qosmio-qinq` branch now boots by normal sysupgrade and starts native
NSS plus the QinQ node adapter automatically on Linux 6.18.55. YT921x keeps
DSA ownership. Routed IPv4 TCP measured 935–936 Mbps in both directions with
about 98.5–98.8% router CPU idle; 900 Mbps UDP had zero loss (2026-11-05).
These are new native qosmio measurements, not the Kuncy7 reference results.

A topology resync can retire an existing TCP connection's NSS rules and
leave that session on CPU forwarding; fresh connections accelerate again.
The persistent-image near-line-rate resync gate did not pass. TCP continuity
is fixed, but automatic reacceleration of surviving sessions remains work.

Ambiguous VLAN, direct port VLAN-upper, PPP/tunnel and L2 NSS paths remain
on the host in this initial native port. Wi-Fi NSS is disabled. Customer
VLAN/multiport membership, cross-conduit switching, high customer VIDs,
MST state/FID flushing and hardware rate/mirror/drop policies have subsequent
device evidence. Physical-loop/STP control delivery, IPv6 traffic, driver
restore and cold/warm boot remain validation gates. The libpcap capture fix
is RAM-tested and awaits inclusion in a new image. Startup and TCP
resync fixes and their flashed/RAM-only status are recorded in
[device test details](nss-qosmio-device-test.md) and
[source selection and implementation](nss-qosmio-port.md); the current
build11 results and open issues are in [execution slices](nss-qosmio-dsa-slices.md).

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
