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
