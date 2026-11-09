# Current native NSS status

Updated: 2026-11-09. Development uses `nss-experiment`; `master` is the
release/build branch. The installed experimental image is native31,
Linux6.18.60, in slot1, with tested native30 preserved in slot0. Qosmio's
native dataplane/core/ECM and `qca-nss-qinq` start automatically. YT921x
retains DSA port identity through the internal S-tag/C-tag transport.

Latest restored IPv4 TCP is936.47/936.28Mbps LAN→WAN/WAN→LAN, with
97.86%/98.07% sampled CPU idle, active ECM rules and770286/754899 additional
firmware TX packets. These are bounded runs on1Gbps endpoint links.

| Area | Accepted evidence and limits |
| --- | --- |
| DSA/VLAN integration | Customer VLAN membership/isolation, high VIDs, direct port and unaware-bridge VLAN uppers, cross-conduit switching and STP/MST have packet evidence |
| Lifecycle and topology | Core unload/reload restores both host paths; existing-flow reacceleration,64-node capacity/recovery and bounded bridge/VLAN churn pass |
| Standard customer QinQ | Multiport separation, secondary raw conduit and additional S/C/PCP/DEI cases pass in the separate standard DSA profile; this mode uses host forwarding |
| IPv6 and boot | Native30 routes IPv6 at about923–924Mbps with firmware counters; one physical cold boot, one ordinary warm reboot and guarded sysupgrade pass |
| Capture | Installed-library native/standard capture and the capture utility pass their bounded matrix; CPU capture cannot see all NSS/switch traffic |
| Daily packet delivery | Native/VLAN35 unknown-unicast and broadcast policy, external VID0 priority tags and CPU broadcast replication pass without duplicate replies |
| FDB and neighbour recovery | Natural hardware expiry and exact Linux FDB deletion during UDP each lose0 packets; IPv4 endpoint MAC replacement recovers with156/208282 packets lost,0.0749%, in one transition interval |
| IPv6 neighbour recovery | NS/NA resolution and exact neighbour deletion during UDP6 pass with0 loss; announced MAC replacement recovers with83/208338 lost,0.03984%, in one interval; the earlier delayed-announcement failure remains recorded |

Remaining work is explicit in the [current slice status](nss-qosmio-dsa-slices.md#current-acceptance-status)
and [daily delivery matrix](nss-qinq-daily-traffic.md). Physical cable moves,
silent hardware-only MAC moves, broader IPv6/discovery/empty-state coverage,
malformed internal-tag tests and long-term reliability remain open. The MAC
replacement result is bounded recovery, not lossless delivery.

Strict-priority/weighted ETS contention acceptance still fails; software storm
containment, bridged802.1X and general hardware egress flower remain limited.
PCP queue selection, TBF, ingress ACL/policer and egress mirror/shaper evidence
remain separate. Full external2.5G saturation, end-to-end9K and two-member LACP
need suitable peers. Wi-Fi NSS is disabled; PPP/tunnel and L2/multicast NSS
acceleration are outside this adapter's accepted scope. Customer S+C QinQ
requires the [standard DSA profile](standard-dsa-qinq-fallback.md).

See [implementation and source pins](nss-qosmio-port.md),
[initial migration tests](nss-qosmio-device-test.md) and
[the original feature audit](nss-qosmio-dsa-feature-audit.md) for provenance.
The old manual `cr881x-nss` controller belongs to the archived Kuncy7 path.

## Historical offload report: 2026-04-23

The following measurements and conclusions describe the earlier integration.
Its asymmetric forwarding and proposed DSA tradeoff do not describe the
native Qosmio/QinQ path above. Its sysctl advice is also historical; the native
implementation uses per-connection TCP liberal handling.

### Status at that time

- Platform: IPQ50xx + YT9215S (DSA)
- NSS stack loads and runs (`qca-nss-drv`, `qca-nss-dp`, `ecm`).
- Best observed behavior is asymmetric with current DSA integration:
  - Downstream (WAN -> LAN): offload works well, near line-rate.
  - Upstream (LAN -> WAN): offload is weaker/partial and often falls back.

### Measured routing results at that time

Test path: LAN client `10.1.0.178` <-> WAN host `192.168.137.1` via router `192.168.2.1`.

- NSS ON, flow offload OFF:
  - Downstream TCP (`iperf3 -R`, 20s): ~941 Mbps
- NSS OFF, flow offload ON:
  - Downstream TCP (`iperf3 -R`, 20s): ~399 Mbps
  - Upstream TCP (`iperf3`, 20s): ~578 Mbps

### Configuration notes at that time

- Do not run NSS ECM and OpenWrt flow offload together on this device.
- Use one fast path at a time:
  - NSS mode: ECM ON, flow offload OFF
  - Software mode: ECM OFF, flow offload ON
- `nf_conntrack_tcp_be_liberal=1` is important to reduce ECM flush/thrash on this kernel line.

### Conclusion at that time: DSA vs NSS tradeoff

- Keep DSA:
  - Pros: modern OpenWrt switch model, standard DSA/switchdev workflow, better upstream maintainability.
  - Cons: NSS acceleration remains asymmetric on this platform (mainly downstream gain).

- Drop DSA (move to non-DSA switch path to favor NSS parser compatibility):
  - Pros: higher chance of symmetric NSS acceleration.
  - Cons: lose DSA-era features/ergonomics (per-port DSA workflow, bridge-vlan model, switchdev consistency), higher maintenance burden.

Final decision for CR881x:
- Keep DSA as the default architecture.
- Current NSS benefit is mostly download speed; this does not justify dropping many YT921x switch offloads and DSA control-plane features.
