# CR881x native qosmio NSS / QinQ device tests

Date: 2026-11-05. This report covers the native qosmio branch, not the
historical Kuncy7 measurements. Primary performed all device operations.

## Topology and packet mapping

```text
Linux 192.168.2.100/enp2s0 -- LAN3 -- YT9215S CPU8
  -- eth1 / native NSS physical 1 -- routed NAT -- native NSS physical 0 / eth0
  -- YT9215S CPU4 -- WAN -- Windows 192.168.137.1:5201
```

LAN conduit: 2.5 Gbit/s. WAN conduit and both endpoint Ethernet links:
1 Gbit/s. Linux route to the Windows server goes via 192.168.2.1/enp2s0;
no workstation routes were changed during these tests. SSH uses the existing
Ethernet-bound proxy because ordinary management routing selects Wi-Fi.

LAN3 uses outer SVID3074 and inner bridge transport VID3088. WAN uses
outer SVID3075 and inner VID3075. TPIDs are 0x88a8 outside and 0x8100 inside.
The switch/tagger and DSA port netdevs remain under the YT921x driver.
QinQ creates four per-port roots and four inner-VID leaves. Firmware interface
numbers are allocated dynamically; identity is conduit plus SVID plus VID.

## Images, sysupgrade and recovery

Initial native build06 was installed once by normal `sysupgrade`, without
force, into slot0/mtd18. SHA256:
`811dc91a658ba53a6f5fae37c2284463aefd95645e93787d41918c27f8d3be95`.
It booted Linux6.18.55, firmware `NSS.FW.12.2-156-MP.R`, and automatically
loaded native DP/core/ECM plus `qca-nss-qinq`, with eight configured nodes.
Both 1G WAN and 2.5G LAN conduits linked. No manual arm/start was used.

The initial tar directory had the image variant name; the installed NAND
checker rejected it. `BOARD_NAME := xiaomi_cr881x` corrected the archive to
`sysupgrade-xiaomi_cr881x/CONTROL`. The rejected image was never flashed.
Both native builds selected for installation passed `sysupgrade -T`
before their single attempts.

The old Kuncy7 slot1/mtd19 image was backed up in full before migration.
Its SHA256 is
`81aef87c78fc39fc942aff28a7a0521ed383dc9eadd4b81e0f76e90855acd6d1`.
After native slot0 passed forwarding and resync tests, corrected build08
was installed into slot1/mtd19 using normal sysupgrade. Its SHA256 is
`961e403e6a85fb3685ae271606ac662e2dba1990931e797ad72c61377ff18a1c`.
Native slot0 remains the tested on-device fallback; its full partition and
current configuration were also backed up before the second upgrade.

Private backups remain in the original repository's ignored device log
directory with restricted permissions. Bootloader code was not flashed.
Sysupgrade and normal boot update the dual-slot environment; no claim is
made that raw environment bytes were unchanged.

## Initial native traffic evidence

Tests were single 12-second iperf3 runs. CPU idle is the aggregate router
`/proc/stat` delta from immediately before traffic to the active snapshot.
These short runs establish the path, not sustained benchmark limits.

| Test | Receiver rate | Router idle | Transport result |
| --- | ---: | ---: | --- |
| LAN3 to WAN TCP | 936.41 Mbps | 98.54% | 0 sender retransmits |
| WAN to LAN3 TCP | 935.25 Mbps | 98.84% | Reverse sender retransmits unavailable |
| LAN3 to WAN UDP, requested900 Mbps, payload1472 | 899.92 Mbps | 98.64% | 0/917216 packets lost |

Active ECM count was2 during each transfer, covering the iperf control/data
flows. The first transfer advanced the firmware's IPv4 accelerated TX count
from0 to1,033,261. Subsequent reverse TCP and UDP raised it to2,042,246 and
2,959,455. Invalid-interface create count stayed0; node count stayed8,
with no allocation failure or retry. This is packet-counter evidence in
addition to throughput and configured firmware nodes.

## Device-found defects and fixes

1. Boot printed two `ubi0_core_clk_src` RCG update warnings. Core patch0105
   completes the reference clock ordering: select and enable the core PLL
   before changing shared bus rates. Keep existing core-local reset clamps;
   do not pulse shared GCC resets.
2. Linux6.18 rejected `symbol_get(ecm_db_connection_defunct_all)` because
   that existing API is not GPL-exported. ECM0104 adds a dedicated GPL
   wrapper; the QinQ module resolves it. RAM reload recreated all eight
   nodes without another lookup warning.
3. With that invalidator working and global TCP liberal0, resync deleted
   two rules but stalled the old transfer after about4 seconds. A fresh
   flow still reached936.32 Mbps. A temporary global-liberal1 A/B test
   sustained935.57 Mbps through resync; the global value was restored0.
   ECM0105 follows Linux flowtable's per-connection TCP handling under the
   existing conntrack lock, for IPv4 and IPv6, without a global sysctl change.

RAM tests of the corrected ECM at global liberal0 passed one forced resync
in each direction:

| Active transfer resynced | Receiver rate | Result |
| --- | ---: | --- |
| LAN3 to WAN TCP | 936.48 Mbps | All1-second intervals930–957 Mbps;5 retransmits |
| WAN to LAN3 TCP | 933.07 Mbps | All1-second intervals899–937 Mbps |

These are actual invalidation transitions, not simply repeats of a steady
transfer. They establish TCP continuity; throughput alone does not prove that
the same data flow was accelerated again. Node identities remain unchanged.

The stricter persistent-image transition check exposed that distinction:
the existing TCP data flow continued after resync but fell back to the CPU
at about647–665 Mbps, averaging762.80 Mbps over the whole run. NSS destroyed
two rules; only one new rule request appeared, and active acceleration fell
to0. The strict850 Mbps resync gate therefore FAILED. The invalidation API
retires connections, so automatic reacceleration of every surviving session
is not accepted as working. Fresh-flow acceptance is recorded separately;
this limitation must not be hidden by the earlier fast RAM runs.

## Final persistent-image validation

Build08 booted from slot1/rootfs_1 without a manual start or RAM module
replacement. Rootfs ECM, QinQ adapter and NSS core module hashes match the
inspected build08 image exactly. The full inactive native slot0 partition
hash still matches its pre-upgrade backup.

Both earlier NSS startup defects are absent from the new boot log: no RCG
configuration timeout and no rejected symbol_get. NSS firmware booted
successfully; the core clock reads1 GHz and AXI399999999 Hz. Native phylink
reports1G WAN and2.5G LAN. Eight QinQ nodes are ready, with zero allocation
failures/retries. The global TCP liberal value remains0.

Fresh flows are evaluated separately from the retired resync session:

| Fresh persistent-image test | Receiver rate | Router idle | Firmware TX packet increase |
| --- | ---: | ---: | ---: |
| LAN3 to WAN TCP | 935.48 Mbps | 98.79% | 1,031,246 |
| WAN to LAN3 TCP | 895.72 Mbps overall; about936 Mbps after first second | 99.28% | 963,132 |

Forward TCP had zero sender retransmits. Reverse TCP's first1-second
interval was446 Mbps; all subsequent intervals were936–937 Mbps. The
short-run average is retained rather than rerunning to obtain a nicer
number. Fresh-flow acceptance allows startup ramp-up and checks subsequent
intervals, total rate and firmware counters.

The final900 Mbps UDP run received899.85 Mbps at
98.91% router CPU idle. It lost60/917193 packets
(0.00654%), so the strict zero-loss gate FAILED. Firmware TX increased
917172 packets and invalid-interface requests stayed unchanged at0. The
functional routed fast path is validated; zero loss is not claimed for this
persistent-image run. No attribution of this small loss to the router or
the Windows endpoint has been established.

Combined evidence and distinct gate results:
`logs/qosmio-device-20261105/persistent-final-verification.json` in the
original repository. The strict transition/zero-loss failures were retained;
no throughput test was repeated to replace an inconvenient measurement.

Network settings gained only `network.globals.packet_steering=0` during
native startup. Wireless, firewall and DHCP config hashes match the original
backup; all four config hashes were unchanged across the second upgrade.
The temporary Wi-Fi shutdown used to free upgrade RAM was not persisted;
host ath11k loaded again on boot.

## Source checks and remaining scope

Full builds06,07,08 exited0. Build08 contains the packaging, PLL ordering,
GPL invalidation and TCP transition fixes. Artifact inspection verifies
native MAC ownership, both conduit rates, memory reservation, firmware,
module vermagic, early QinQ option and automatic module loading. The node/
ECM C harness and established YT921x transport/fault checks passed.

Qoder provided a bounded read-only source review. Primary rejected its
unsupported double-lock assertion against the actual nonlocking helper;
builds and hardware tests determine acceptance. No helper performed live
operations. All30 original reference source hashes remain unchanged.

Still pending on native qosmio: customer VLAN/multiport isolation and
reconfiguration, native captures showing both tags, routed IPv6, cold/warm
boot matrix, core-driver removal/host restore and unsupported-path fallback.
Wi-Fi remains host ath11k; Wi-Fi NSS is disabled. PPP/tunnel, direct port
VLAN uppers, ambiguous VLAN defaults, L2/multicast/non-ported NSS acceleration
remain outside the initial port. No commit, push or release was performed.

Raw evidence is under the original repository's
`logs/qosmio-device-20261105/`: minicom capture, guarded upgrade scripts,
preflight/hash records, iperf JSON, active firmware snapshots, RAM module
hashes and TCP fix results. Build logs and current artifact metadata are in
this worktree's `logs/qosmio-native/`.

DSA feature comparison, actual packet tests, current image gaps and ordered
fix plan: [native DSA feature audit](nss-qosmio-dsa-feature-audit.md).
