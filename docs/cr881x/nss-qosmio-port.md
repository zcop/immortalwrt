# Qosmio native NSS / YT921x QinQ port

Updated: 2026-11-09. Native CR881x DT, phylink dataplane adapter, automatic QinQ
node provisioning and routed IPv4/IPv6 ECM mapping are implemented on
Linux6.18.60. Installed native31 is in slot1; tested native30 remains in slot0.
Restored IPv4 TCP reaches about936Mbps in both directions with firmware counters
advancing and about98% sampled CPU idle. Native30 also passes routed IPv6,
one physical cold boot and one ordinary warm reboot. NSS/QinQ starts without
a manual arm/controller; Wi-Fi NSS remains disabled.

Build18 repairs shared-conduit reload; build19 accepts existing TCP flow
reacceleration. Subsequent slices accept customer VLANs, standard customer
QinQ fallback, capture, bounded node exhaustion/churn and the specified daily
delivery/FDB cases. Advanced SP/weighted scheduling and broader daily-use
recovery remain open. See [current acceptance status](nss-qosmio-dsa-slices.md#current-acceptance-status),
the [daily delivery matrix](nss-qinq-daily-traffic.md) and
[initial migration tests](nss-qosmio-device-test.md). Older build logs and
artifacts below retain their original scope.

## Sources and branch ownership

- Development branch: `nss-experiment`; `master` is the release/build branch.
  The local development worktree retains the directory name
  `logs/worktrees/nss-qosmio-qinq`; that is no longer a branch name.
- Qosmio feed: `qosmio/nss-packages`, branch `NSS-12.5-K6.x`, pinned to
  `0d970dbf0185e3f53709bd803e8a466598023c57` in `feeds.conf.default`.
- Native platform reference: `qosmio/openwrt-ipq`, `main-nss`,
  `92a2d104145c8d265851c4b388a41bd8e9c21cd9`. Its current platform patches
  target kernel6.12 at that revision; this ImmortalWrt target uses6.18.60.
- Kuncy7 reference: the former `nss-qinq-experiment` source was based on
  `9cca0ec1e74d5d3de53fd30a02999447249dd501`. Its full dirty
  source/diff/config/feed overlay snapshots are preserved under
  `logs/qosmio-migration-20261104/kuncy-reference/`.
- Kuncy7 tree/feed donor revisions remain recorded in the archived
  [reference notes](reference/nss-kuncy-port.md). Use these for a specific
  dependency or measured behavior; they are no longer the active feed.

The migration removes the Kuncy7 backend introduced by
`dabf634cc4836867383fe7f91cefd0f1f63cf0a8` in this new worktree. The later
2.5G SerDes commit and independent YT921x transport foundation remain.
Removal, QinQ transport, native integration and individual driver fixes are
recorded in separate commits. Development has since been merged into and
pushed on `master`; the preserved Kuncy7 snapshots remain historical inputs.

## Removed active foundation

The Kuncy7 feed pin, `qca-dwmac-nss` package and manual `cr881x-nss`
controller, `qca-dsa-nss` package, old bootstrap/transport configs, local
Kuncy7 core-halt overlay, stmmac claim/lifetime patches, and the imported
Kuncy7 NSS kernel/ECM patch set were removed here. Shared kernel helpers
needed by qosmio were then restored individually with explicit provenance;
the stmmac takeover remains removed. The old NSS memory include is no longer
enabled in the CR881x DTS. General upstream fixes that happen to credit
Kuncy7 as author/tester remain normal platform code.

## Retained independent work

The existing YT921x driver/DONE feature baseline, single-tag compatibility
mode, QinQ tagger and registration, outer SVID steering, inner CVID handling,
reserved ACL block and transport/fault checks remain. The CPU8/LAN and
CPU4/WAN board policy remains unchanged. QinQ still reserves eight ACL
slots, uses four steering rules and leaves 376 normal TC slots.

The QinQ transport is the intended DSA/NSS wire format. Those host/switch
components do not depend on Kuncy7's stmmac ownership API. The old adapter
and ECM patches remain references. The native branch now provides its own
`qca-nss-qinq` node adapter and scoped ECM patches for the pinned qosmio APIs.
The ordinary IPv4 routed path is hardware-validated below. Later native
device tests cover customer VLAN membership and high-VID bridge uppers;
their exact scope and remaining gates are recorded in the slice report.

## Qosmio profile and native dependency

`configs/xiaomi_cr881x_nss_qosmio.config` extends the normal optimized
profile and selects `qca-ssdk`, `qca-nss-dp`, qosmio's core/ECM/PPPoE
packages, the core VLAN API, firmware 12.2-156 for IPQ5018, LOW memory
settings for this 256MB board, and the early QinQ switch-module parameter. Host ath11k remains;
Wi-Fi NSS is not selected.

Qosmio's `qca-nss-drv/Makefile` already has `SOC=ipq50xx_64` and depends on
`qca-nss-dp`; that dataplane in turn depends on SSDK. This tree previously
had only local SSDK patches, without its package Makefile. The native SSDK
package definition is restored from the pinned qosmio platform tree;
existing local SSDK patches and the IPQ50xx FAL guard remain for review.
SSDK's presence does not replace the YT921x DSA driver.

## Native integration and remaining validation

The separate `xiaomi_cr881x_nss` image uses `ipq5018-cr881x-nss.dts`.
It includes the normal board description and overrides the binding on its
existing two MAC nodes to `qcom,nss-dp`. The normal `xiaomi_cr881x` image
retains stmmac. Both variants keep the board compatible `xiaomi,cr881x`.
The native variant reserves 16 MiB at `0x40000000` for NSS within the
board's 256 MiB RAM and keeps the existing Wi-Fi reservations.

The native DP's opt-in `qcom,upstream-phylink` helper manages MAC clocks,
PHY/UNIPHY and link callbacks through phylink. It retains the 2.5G LAN
PCS and 1G WAN link. SSDK supplies required link symbols but is explicitly
passive on this variant; its switch initialization and global hardware
notifier do not run. The two MACs are not shared with stmmac. DT MAC IDs
1/2 map to NSS physical interfaces 0/1, matching the native driver's
zero-based firmware interface definitions.

Selected, source-reviewed IPQ5018 boot prerequisites are carried separately:
core-local clamp before firmware replacement and remove, core clock enabled
before reset release, write-combined meminfo request mapping, and the single
NSS core clock kept across unused-clock gating. No shared NSS GCC block
reset is pulsed. Native dataplane registration/removal holds RTNL for
phylink; host restore clears the old DMA initialization flags and rebuilds
host rings. Native sysupgrade and1G/2.5G links are device-proven. Slice18
accepts two core unload/reload cycles and both host Ethernet paths. Slice25
accepts one physical cold boot and one ordinary warm reboot on native30;
repeated power-cycle qualification remains open.

`qca-nss-qinq` autoloads and waits/retries for physical NSS interfaces. It
creates a root for each physical port's outer SVID and a leaf for each inner
VID. ECM lookups see only configured nodes with a ready matching parent.
Topology events block new lookups immediately, and resync invalidates ECM
connections before rebuilding stale nodes. Module teardown cancels work and
withdraws lookup readiness before unregistering nodes.
Build19 distinguishes a retryable resolver outage during rebuilding from hard
lookup failures. ECM keeps IPv4/IPv6 acceleration attempts decelerated during
that window, releases both interface lists and the message, and retries on a
later packet. Terminal exhaustion and mixed hard errors remain failures; a
generation check prevents an older give-up from closing a new retry window.
IPv4 retry behavior is device-tested. IPv6 has source/cross-compilation checks
and subsequent native30 routed-traffic/boot acceptance; that is not a separate
IPv6 existing-flow retry-transition acceptance claim.

A recoverable sysupgrade boot, automatic startup, link inspection and
bidirectional routed IPv4 flows with firmware counters have passed. The
slice report adds cross-conduit switching/isolation, high customer VIDs,
MST state/FID flushing and hardware ACL/mirror/rate-control evidence.
Build17 includes the libpcap correction, and physical-loop/STP control delivery
passed in build13. Native and standalone tagged WAN/LAN NSS, including
customer VID4094, have packet-level evidence. Later slices add transparent
customer C-tags on unaware bridges, standard customer QinQ, native host restore,
routed IPv6 and the bounded boot checks above. Remaining gates are tracked in
the current acceptance status and daily delivery matrix.
Compilation or a rule ACK alone does not prove packet fate. Wi-Fi NSS remains
disabled; no radio acceleration is claimed.

## ECM scope and packet ownership

The new ECM overlay changes only the selected qosmio 12.5 sources. It does
not substitute Kuncy7 ECM 14 files. Normal netdev-wide NSS lookups reject
QinQ ports: a single netdev can have multiple firmware leaves. The ported
IPv4/IPv6 frontends resolve each routed side's exact conduit, outer port
SVID, inner VID and leaf before constructing a rule. An explicit VLAN upper
of the bridge gives its exact side-specific VID; without one, a VLAN-aware
port must currently have exactly one VLAN membership. A stale firmware node
cache cannot resolve an ambiguous port. Missing membership/node/parent
rejects the acceleration attempt.

Initial native mapping covers ordinary routed Ethernet/bridge/VLAN
hierarchies. The subsequent [direct-VLAN slice](nss-qosmio-dsa-slices.md#slice10-direct-customer-vlan-interfaces-on-standalone-qinq-ports)
adds exact mappings for registered standalone `wan.N`/`lanN.N` interfaces.
Build16 passes hardware membership, ownership, bridge lifecycle, native
coexistence and bidirectional routed NSS on tagged `wan.35`, including hardware
mirror confirmation of customer tags. A900Mbps-paced test measured900Mbps
forward and591Mbps reverse at about99% CPU idle. Subsequent hardware isolation
identified missing customer-tag receive headroom on user ports: increasing
WAN's frame limit from1518 to1522 bytes restores936.6/936.3Mbps unpaced. The
source correction and actual-callback regression are validated, including an
AArch64 compilation with the kernel module flags. Build16 still has the
original limit. Build17 installed the correction and passed real tagged WAN35,
LAN1.35 and LAN1.4094 endpoint traffic in both directions, as recorded in the
slice report. Bridged, stacked and S-tagged direct
interfaces are not admitted to that transport.
The later [PPPoE slice](nss-pppoe.md) admits one innermost PPPoE interface
whose live NSS registration matches ECM's cached interface identity. Existing
frontend PPPoE fields are preserved alongside the exact QinQ underlay. Build46
passes routed IPv4 NAT/TCP on plain WAN and WAN VLAN35, MTU1492 and session-ID
reuse after teardown. Subsequent tests on the same image also pass routed IPv6
TCP/bidirectional UDP, IPv6 Packet Too Big and dual-stack session-ID reuse on
both underlays. A further same-image DHCPv6-PD gate passes automatic `/56`
delegation to test VLAN46's `/64`, renewal during traffic, prefix replacement
without restarting PPP and reacquisition over WAN VLAN35. The delegated test
client has matching ECM/NSS and WAN-wire evidence; the management LAN retains
its existing ULA and RA policy. Local PAP/plain-WAN and CHAP-MD5/WAN35 tests
also pass credential rejection, correct login and same-SID reconnect, followed
by accelerated IPv4/IPv6 traffic. Authentication uses normal netifd credentials
and Linux pppd; no extra NSS startup or authentication module is needed.
Nested PPP, tunnel hierarchies,
L2/multicast/non-ported acceleration and ambiguous customer VLANs remain outside
this mapping. YT921x hardware switching and its established DONE baseline remain
separate from these NSS limitations.

Wire format remains:

```text
[DA][SA][88a8 | SVID=3072+physical_port][8100 | inner_VID][payload]
```

- LAN ingress: YT921x adds the outer/inner tags on CPU8; native `eth1`
  represents NSS physical interface 1. The inner VID is the customer VID
  for an aware bridge, or the DSA bridge/standalone transport VID otherwise.
- Routed egress: Linux/ECM programs the pair and the exact leaf in the
  firmware rule. NSS must generate that pair on the selected physical
  conduit. YT921x's reserved steering ACL selects the port by outer SVID,
  strips the transport tag, and applies customer VLAN access/trunk policy.
- Host exceptions pass through the retained QinQ DSA tagger to the exact
  Linux port. Host TX uses that tagger to insert the tags. Linux sets up
  nodes/rules; it does not tag every accelerated packet in software.

WAN is physical switch port 3 over CPU4/native `eth0`/NSS interface 0.
Its standalone pair is SVID 3075 / inner VID 3075. Native routed IPv4
throughput and firmware packet counters confirm the LAN3/WAN path. Slice22
also accepts native `eth1` capture with exact S3074/C3088 framing; capture
and packet counters have different visibility of accelerated bulk traffic.

## Local metadata checks

Non-NSS feed checkouts are reused as read-only symlink inputs for local
metadata generation; indexes, package links and `.config` live in this
worktree. The qosmio NSS feed itself is a separate clone, not the Kuncy7
feed. This worktree does not share target staging or kernel build outputs
with the reference. The full source pin record is `logs/source-pins.json`.

### Dependency overlay and validation boundary

Qosmio gates `NSS_DRV_VLAN_ENABLE` to IPQ807x/IPQ60xx. Run
`python3 tools/nss/apply-qosmio-overlay.py` before indexing/generating config.
The guarded, idempotent overlay admits IPQ50xx to the **core VLAN API**
needed for QinQ firmware nodes. It checks the exact feed revision and the
complete original/post-edit file before changing the option. It does not
enable the generic SSDK/PPE VLAN-manager package on IPQ50xx; that package
is deliberately absent from this profile. The QinQ-specific node adapter
is now supplied by `package/kernel/qca-nss-qinq`. The old measured firmware capability is a reference
for this scoped dependency change, not validation of qosmio's implementation.

Feed indexing/install and `make defconfig` complete and the resulting config
selects native DP/SSDK/core/ECM, firmware 12.2, LOW memory, core VLAN and QinQ.
Kconfig still prints feed dependency cycles (Qt5, fwknop, squeezelite, and
a native NSS PPPoE/TLS chain through other packages). Three families also
appear in the earlier baseline logs. These metadata warnings remain; the
full selected image nevertheless builds successfully. Logs are retained
under this worktree's `logs/qosmio-build/`.

The retained transport/fault harness passes, including60 ACL commit-write
failure points. At migration, all30 saved Kuncy7 dirty-source hashes were
checked unchanged. Those snapshots remain preserved; no old reference branch
is required for the active implementation.

## Linux 6.18 dependency fixes

The selected packages now compile and pass module symbol resolution:

- Shared core bridge/VLAN, PPPoE and DSCP helpers use the already working
  6.18 contexts, including the bridge helper RCU fix. Qosmio's macvlan
  helper patch is imported separately. These expose shared kernel APIs;
  none restores the old stmmac claim API or manual NSS controller.
- SSDK uses the 6.18 PHY-driver array registration API and the moved
  `phydev->eee_cfg.eee_enabled` field.
- Native `qca-nss-dp` retains its HAL includes while receiving SSDK flags,
  indexes the new log-sized RPS flow table with `hash_32`, and uses the
  current platform remove callback.
- The guarded feed overlay propagates package feature/include flags through
  current Kbuild and preserves the packages' warning policy. Client
  `compat.h` is included by the source Makefile after kernel configuration,
  rather than through an earlier preinclude.
- NSS core allocates/frees a dummy NAPI netdev with the current API and
  terminates its OF match table for modpost.
- ECM uses the current timer API and the configured conntrack notifier
  chain, including failure-path unregister. The absent legacy TCP
  no-window-check extension is treated as disabled; global and per-flow
  `BE_LIBERAL` checks remain.
- ECM's PPTP/L2TP and bonding code now follows the corresponding NSS backend
  selection. The PPPoE client also gates LAG lookups on the NSS LAG backend.
  IPQ50xx exposes no LAG option in this feed, and this profile disables
  PPTP/L2TP. PPPoE and Linux bonding remain available; NSS acceleration over
  bonding is not enabled. Optional protocol acceleration is not certified.

Package source fixes live in `tools/nss/patches/qosmio/` and are installed
into the separate feed checkout by `apply-qosmio-overlay.py`. The installer
checks the pinned feed revision and exact base/final recipes, rejects
conflicting edits, and is idempotent. Do not maintain fixes only inside
ignored `feeds/` or `build_dir/` directories.

Bounded Qoder reviews checked the shared API surface, SSDK fixes, and ECM
notifier/client dependency changes. An initial notifier finding used an
incomplete excerpt and was retracted after reviewing the complete source.
The corrected review found no blocker in its supplied scope. Primary
acceptance is the full build and artifact inspection, not a runtime claim.

## Earlier compile-foundation artifact record

Build command: `make -j12 V=s`, exit 0; log:
`logs/qosmio-build/build-13.log`. Output directory:
`bin/targets/qualcommax/ipq50xx/` in this isolated worktree.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `immortalwrt-qualcommax-ipq50xx-xiaomi_cr881x-squashfs-sysupgrade.bin` | 18483475 | `7a5083d9ed2e57741346f8fe9295bbd97d4c4e99975601fda05c2ab912c0b1fb` |
| `immortalwrt-qualcommax-ipq50xx-xiaomi_cr881x-initramfs-uImage.itb` | 18399260 | `43a4de43f959d98e07a79913ddf8a854bc4868d1877ad5f00c083a82f1573ae4` |
| `immortalwrt-qualcommax-ipq50xx-xiaomi_cr881x-squashfs-factory.ubi` | 19529728 | `ff377466e465571a30084c924de50cbe530cd599e5c50b9e60495a14d5a40f6c` |

Inspection of the sysupgrade rootfs confirms native DP, SSDK, NSS core,
ECM, PPPoE, the YT921x driver and QinQ tagger, all with 6.18.55 vermagic.
The embedded firmware identifies as `NSS.FW.12.2-156-MP.R`. Early module
options select `yt921x_driver experimental_qinq=1`. The old takeover
packages/controller are absent. The FIT's DTB matches the built board DTB;
both Ethernet nodes still select stmmac and there is no native NSS node.
That confirms the remaining DT work, rather than proving acceleration.

The transport/fault harness, patch syntax checks, overlay idempotence and
tracked diff whitespace checks pass. All 30 original reference source
hashes still match. No device operation, commit or push was performed.
Detailed evidence: `logs/qosmio-build/RESULT.md` and
`logs/qosmio-build/artifact-inspection/verification.json`.

## Initial native image result: builds05–06

This is the original6.18.55 artifact record. Installed native31 and its
6.18.60 validation are identified in the current status above.

The active native image builds with `make -j12 V=s`, exit 0. Final log:
`logs/qosmio-native/build-05.log` records the pre-device implementation;
`build-06-sysupgrade-board-name.log` is the image actually flashed. Images are named
`immortalwrt-qualcommax-ipq50xx-xiaomi_cr881x_nss-*` under
`bin/targets/qualcommax/ipq50xx/`. The earlier compile-foundation artifacts
were copied to `logs/qosmio-native/foundation-artifacts/` before this work.

The final sysupgrade inspection confirms two native MAC bindings, a 2.5G
LAN fixed link, 256 MiB RAM with the 16 MiB NSS reservation, firmware
`NSS.FW.12.2-156-MP.R`, early `experimental_qinq=1`, and autoload of
`qca-nss-qinq`. Old takeover packages/controller are absent. All inspected
modules have Linux 6.18.55 vermagic.

Validation commands after building:

```sh
python3 -B tools/nss/test_qosmio_qinq.py
python3 -B tools/yt921x/test_8021q_transport.py
python3 -B tools/nss/apply-qosmio-overlay.py
git diff --check
```

The native harness executes the actual C node lookup/resync and prepared
ECM side resolver with mocked kernel APIs. It tests conduit/port/VID identity,
ready parent matching, missing/ambiguous nodes, boot retry with an empty
wanted set, standalone/unaware/aware bridge resolution, explicit L3 VLANs,
new membership preceding node resync, and unsupported path rejection.
These are source logic checks, not firmware or switch packet tests.

Full result, hashes, primary review dispositions and hardware gates:
`logs/qosmio-native/RESULT.md` and [device test details](nss-qosmio-device-test.md).
The original reference remains preserved. One sysupgrade was performed;
subsequent module tests use RAM only. No commit, push or release was performed.

## Native sysupgrade and startup fixes (2026-11-05)

The NSS image variant retains `BOARD_NAME := xiaomi_cr881x`, so its tar
contains `sysupgrade-xiaomi_cr881x/CONTROL`. The running CR881x NAND
checker rejected the initial variant-named directory; the corrected image
passed `sysupgrade -T` and was flashed once without force.

Hardware exposed two startup defects. Core patch 0105 moves core PLL
configuration ahead of shared bus rate changes, completing reference patch
0138. ECM patch 0104 provides a dedicated GPL-exported invalidator because
Linux 6.18 `symbol_get()` rejects the old non-GPL export. The QinQ adapter
resolves that wrapper; the original ECM export remains unchanged.

A resync test then exposed a TCP transition problem: with the host's global
liberal flag at zero, the old accelerated transfer stalled after rule
removal, although new flows still reached 936 Mbps. Temporarily setting
the global flag to one prevented the stall; it was restored afterwards.
ECM patch 0105 instead follows Linux flowtable's per-connection approach,
setting both TCP direction flags under the existing conntrack lock when
creating an NSS TCP rule. IPv4 and IPv6 use the same handling. See the
device report for the result with the global flag left at zero.

That initial TCP transition fix did not establish automatic regeneration or
renewed acceleration of an already retired connection. The build08 resync
test kept TCP alive but moved the old transfer to CPU forwarding at about
650–665 Mbps; its strict near-line-rate resync gate failed. New flows were
checked separately with packet counters. Retaining/rebuilding acceleration
for surviving connections was still open at that checkpoint. Build19 later
adds retryable resolver handling and accepts reacceleration on the same TCP
session; Slice23 adds bounded topology churn. The original failed build08
result remains in the migration report.

The [original DSA audit](nss-qosmio-dsa-feature-audit.md) explains the initial
gaps; use [current acceptance status](nss-qosmio-dsa-slices.md#current-acceptance-status)
for implemented work and remaining tests.
