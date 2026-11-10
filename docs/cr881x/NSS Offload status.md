# Current native NSS status

Updated: 2026-11-10. Development uses `nss-experiment`; `master` is the
release/build branch. The installed experimental image is Build42,
Linux6.18.60, in slot0, with Build41 preserved byte-for-byte in slot1. It retains
Build34 bridge/FDB fixes, Build35–38 memory changes and Build41 experimental
Wi-Fi NSS support with the corrected256MiB allocation profile. Build42 adds
matched RX/TX PAUSE on the GMAC1/switch CPU8 fixed link. See [Wi-Fi NSS validation](nss-qosmio-wifi.md) and
[the memory budget](nss-memory-budget.md). The ordinary warm-reboot gate also passes, with
eight NSS interfaces created automatically and no worker/kernel errors. Qosmio's
native dataplane/core/ECM and `qca-nss-qinq` start automatically. YT921x
retains DSA port identity through the internal S-tag/C-tag transport.

Build42 wired IPv4 TCP after Wi-Fi cleanup is936.69Mbps LAN→WAN with98.25%
CPU idle. Its bounded on/off/on PAUSE test removes the measured switch packet
deficit at200/300Mbps; Wi-Fi TCP download reaches545.78/666.14Mbps with1/3streams
and about97%CPU idle in the current5GHz STA fixture. Residual end-to-end loss,
multiport head-of-line effects and Build42 cold-boot acceptance remain open.
Earlier Build40 wired IPv4 TCP after Wi-Fi cleanup is936.60Mbps LAN→WAN with98.66%
CPU idle and759003 additional NSS TX packets. Earlier Build34 IPv4 TCP after
an ordinary warm reboot was934.56/936.56Mbps
LAN→WAN/WAN→LAN, with98.02%/98.93% sampled CPU idle, active ECM rules and
514230/501812 additional firmware TX packets. These are bounded runs on1Gbps endpoint links.

## Default forwarding policy

The native NSS profile uses the following defaults:

| Feature | Default |
| --- | --- |
| NSS core/dataplane, ECM and QinQ port mapping | Enabled automatically |
| Matched CPU-link RX/TX PAUSE | Enabled on both GMAC1 and switch CPU8 |
| Normal switch queue/scheduler initialization and host queue disciplines | Retained |
| User bandwidth shaping, SQM and per-device rate limits | Opt-in; disabled by default |

PAUSE supplies link backpressure for the validated2.5Gbps-to1Gbps burst case.
User shaping is a separate policy for bandwidth limits or congestion latency;
its interaction with NSS must be validated for the selected configuration.
Including `tc` and scheduler modules for control and diagnostics does not enable
a bandwidth shaper. Normal `fq_codel` host queues likewise do not imply that
SQM is configured or that accelerated traffic traverses those queues.

The successful Build42 throughput tests used these defaults. A live read-only
check found no enabled QoS/SQM service, no HTB/CAKE shaper on the tested netdevs,
and no LAN3 hardware port/queue shaper or ingress policer. The stock ROM's
reported QoS speedup is not a reason to enable user shaping in this profile:
its shipped paths also change forwarding/acceleration eligibility. Static stock
evidence: `logs/cr8812dx-stock-qos-20261110/REPORT.md`; live queue check:
`logs/cr8812dx-stock-qos-20261110/default-qos-check.txt`.

## Accepted evidence and limits

The [Qosmio feature comparison](nss-feature-matrix.md) covers every upstream
matrix row and separates confirmed NSS acceleration, YT9215S switch offload,
disabled features and adapter gaps on this IPQ5018 profile.

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
and [daily delivery matrix](nss-qinq-daily-traffic.md). Slice29 adds native DHCPv4,
DHCPv6, RA/SLAAC, stable-owner DAD and specified malformed-tag gates, plus30min
of completed traffic phases. Slice30 fixes the truncated MDB update words:
specified discovery groups and both MDB membership orders now pass. Event-based
learning reconciliation passes silent host-MAC movement without CPU data copies;
an active NSS rule is invalidated and rebuilt for the new port. The bounded
Build34 packet-ID/return-path test passes, including completion of the same
iperf session. A separate stale peer-isolation mask
found by the fixture is corrected in the installed Build34; its automatic
peer-mask validation passes. One TCP session
survives LAN3→LAN1→LAN3 with reacceleration and measured stalls; rapid
address-reuse timing, broader customer-VLAN cases and long-term reliability
retain their own gates. Announced MAC replacement is bounded
recovery, not lossless delivery.

Strict-priority/weighted ETS contention acceptance still fails; software storm
containment, bridged802.1X and general hardware egress flower remain limited.
PCP queue selection, TBF, ingress ACL/policer and egress mirror/shaper evidence
remain separate. Full external2.5G saturation, end-to-end9K and two-member LACP
need suitable peers. Wi-Fi NSS is experimental:5GHz STA routed traffic is
accelerated and the measured switch burst-loss deficit is removed with PAUSE.
Internal2.4GHz STA routed IPv4 also has actual ECM/wifili evidence, but its
current test fixture fails performance acceptance at roughly0.5–0.9Mbps TCP.
The same-image Wi-Fi-NSS-disabled control also performs poorly at0.13–0.49Mbps,
with radio errors and roughly97%CPU idle. The severe TCP limitation persists
without Wi-Fi NSS; interference and the precise loss cause remain unproven.
Residual loss, broader performance/asymmetry,2.4GHz cold recovery and AP/Wi-Fi
IPv6 coverage remain open.
PPP/tunnel and L2/multicast NSS acceleration are outside this adapter's accepted scope. Customer S+C QinQ
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
