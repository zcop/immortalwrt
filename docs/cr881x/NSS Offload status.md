# CR881x NSS Offload Status

## Current stmmac integration, 2026-11-02

Wired NSS routing now works in both directions on CR881x with kernel 6.18.55,
the upstream stmmac driver and the existing YT921x DSA driver in its opt-in
single-tag 802.1Q mode. Both CPU conduits remain in use. No `qca-nss-dp` or
`qca-ssdk` is used by this integration.

| Single TCP stream, 30 seconds | Host 802.1Q | NSS + ECM | NSS CPU idle |
| --- | ---: | ---: | ---: |
| LAN -> WAN | 543.1 Mbit/s | 939.0 Mbit/s | 98.98% |
| WAN -> LAN | 536.4 Mbit/s | 938.8 Mbit/s | 99.00% |

Firmware special IPv4 RX/TX counters increased by 2,786,776 and 2,717,331
packets respectively during the two NSS runs. ECM showed active rules, and
no nft flowtable was enabled. The matched 400 Mbit/s reverse UDP test received
399.7 Mbit/s with zero reported loss; the earlier original-transport host
baseline received 344.0 Mbit/s with 13.95% reported loss.

NSS startup is still manual. This validates routed unicast on the connected
LAN3 and WAN ports; QinQ, Wi-Fi acceleration and the wider VLAN/control-plane
feature matrix remain unvalidated. The normal host ath11k code is unchanged. After a clean reboot, keeping both
host Wi-Fi PHYs loaded also passed 938.8/936.7 Mbit/s ten-second TCP checks.
The manual controller now supports stop/restart without a router reboot:
the glue restores prior DSA user-port states, and the IPQ50xx HAL halts its core
before removal. Two same-boot restarts passed with both host PHYs retained.
The separate ath11k hot-reload problem is avoided by keeping its drivers loaded;
Wi-Fi NSS and boot autostart remain disabled.
See [current porting notes and limits](nss-kuncy-port.md) for exact sources,
image identity, mapping, startup ordering and evidence paths.

The remainder records the older qca-nss-dp integration and its former
asymmetry; it does not describe this new result.

Date: 2026-04-23

## Historical status

- Platform: IPQ50xx + YT9215S (DSA)
- NSS stack loads and runs (`qca-nss-drv`, `qca-nss-dp`, `ecm`).
- Best observed behavior is asymmetric with current DSA integration:
  - Downstream (WAN -> LAN): offload works well, near line-rate.
  - Upstream (LAN -> WAN): offload is weaker/partial and often falls back.

## Measured routing results (historical)

Test path: LAN client `10.1.0.178` <-> WAN host `192.168.137.1` via router `192.168.2.1`.

- NSS ON, flow offload OFF:
  - Downstream TCP (`iperf3 -R`, 20s): ~941 Mbps
- NSS OFF, flow offload ON:
  - Downstream TCP (`iperf3 -R`, 20s): ~399 Mbps
  - Upstream TCP (`iperf3`, 20s): ~578 Mbps

## Historical configuration notes

- Do not run NSS ECM and OpenWrt flow offload together on this device.
- Use one fast path at a time:
  - NSS mode: ECM ON, flow offload OFF
  - Software mode: ECM OFF, flow offload ON
- `nf_conntrack_tcp_be_liberal=1` is important to reduce ECM flush/thrash on this kernel line.

## Historical DSA vs NSS tradeoff

- Keep DSA:
  - Pros: modern OpenWrt switch model, standard DSA/switchdev workflow, better upstream maintainability.
  - Cons: NSS acceleration remains asymmetric on this platform (mainly downstream gain).

- Drop DSA (move to non-DSA switch path to favor NSS parser compatibility):
  - Pros: higher chance of symmetric NSS acceleration.
  - Cons: lose DSA-era features/ergonomics (per-port DSA workflow, bridge-vlan model, switchdev consistency), higher maintenance burden.

Historical decision for CR881x:
- Keep DSA as the default architecture.
- Current NSS benefit is mostly download speed; this does not justify dropping many YT921x switch offloads and DSA control-plane features.
