# Native NSS/QinQ DSA implementation slices

2026-11-05, branch `nss-qosmio-qinq`, base `9cca0ec1e74d`.
This records execution of [the feature-audit plan](nss-qosmio-dsa-feature-audit.md).
The original Kuncy reference remains unchanged; its 30 saved source hashes match.
No source commit, push or release is part of these tests.

## Slice 01: control packages, MST and policy validation

The profile adds `ip-bridge`, `tc-tiny`, `devlink`, `kmod-sched-core`,
`kmod-sched-flower`, `kmod-sched-act-police`, `kmod-sched-mqprio` and
`kmod-sched-ets`. `make defconfig` and the full image build passed.

Build09 sysupgrade SHA256:
`3ce34f4ae8089eee36c0377fe9d1ba4290552d38abf8e8f3ca63766abfa3d3a2`.
It was installed once in slot0; tested build08 remains in slot1. The first
preflight stopped before flashing because available RAM was below the guard.
Cache reclamation allowed the same guard to pass; no threshold was lowered.
Configuration hashes were preserved. All five checked driver/module files
matched the built image. Boot ID: `33829b99-c620-4d79-bc40-9076ea4b060f`.

The MST change binds `port_vlan_fast_age`, maps VID to the existing FID policy,
and flushes dynamic entries for that port/FID under `reg_lock`, returning errors.
The actual-C harness covers IVL/SVL/default FID, invalid inputs, write/wait/recovery
failures and locking. Existing transport and ECM resolver tests also passed.

| Device check | Result |
| --- | --- |
| Automatic startup | Eight QinQ nodes, zero allocation failures; no manual NSS start |
| Native routed TCP | 936.56 Mbps, zero retransmits, 98.07% CPU idle, two active ECM flows, 754523 additional firmware IPv4 TX packets |
| LAN CPU link | 2500 Mbps |
| Hardware MST | VLAN100 forward/block/forward: 3/0/3 replies; MST1 port bits 3/2/3 while MST2 stayed 3 |
| Exact FID ageing | Blocking VLAN100 removed its dynamic hardware entry; VLAN200's dynamic entry and VLAN100's static entry survived |
| Hardware WAN shaper on established NSS flow | About 936 -> 94 -> 936 Mbps with a 100 Mbps limit; register readback confirmed the rate |
| Hardware LAN shaper | About 942 -> 94 -> 942 Mbps; switching traffic does not depend on Linux TX qdisc execution |
| WAN ingress flower drop on established NSS flow | `skip_sw`, `in_hw`, 3744906 hardware-counted bytes; throughput stopped and recovered after removal |
| LAN ingress flower drop | 3/0/3 replies; hardware counter increased |
| Egress port mirror | Three reflected ingress copies observed only with the mirror enabled; host outgoing packets excluded |
| Hardware egress flower drop | FAILED: `in_hw` reported, but bulk stayed about 936 Mbps and the counter increased only 320 bytes |
| Original ingress policer programming | FAILED rate/burst validation; configured 100 Mbps delivered 9-19 Mbps in paced UDP tests |

For exact-FID testing, ordinary PVID reconfiguration flushed the old hardware
entry before both VLANs could coexist. A separate guarded test temporarily changed
only LAN1's hardware CVID field, learned both FIDs, restored the CVID, then changed
MST1. Hardware FDB dumps proved the required preservation. Registers, bridge
membership and persistent configuration were restored afterwards.

### Egress ACL correction

The failed egress test is explained by existing driver translation, not by proof
of a new NSS-specific regression. The vendor `fal_tiger_struct.c` names key word1
bit9 `RULE_LOGIC_NOT`; `yt_acl.h` describes the argument as reversing rule logic.
The driver treated that bit as an egress selector while retaining an ingress
source-port mask. This cannot select traffic leaving that port.

The driver now rejects egress flower adds before parsing, allocations or register
writes and removes this misuse of `KEYb_REVERSE`. The actual callback C harness
proves rejection without work and preserves the ingress add/error-cleanup path.
This correction is verified on running build10: `skip_sw` install fails with the
explicit extack and no `in_hw` rule remains. Hardware egress port mirroring
uses its own documented egress source mask and passed; it is a separate path.

General hardware egress flower policy remains unresolved. CPU QinQ traffic can
be identified by actual source conduit plus destination SVID, but user-to-user
switching does not carry that private S-tag at ingress. A partial CPU-only rule
must not be advertised as a general egress policy. Software fallback alone also
does not guarantee policy on NSS or switch-bypassed traffic.

### Ingress policer finding and measured correction

`yt921x_ingress_meter_profile_apply()` programs CIR but ignores the requested burst
and leaves CBS zero. It writes only the final two words of the three-word meter
entry. A single-word CBS experiment did not persist and is not evidence against
the burst hypothesis. A complete table write persisted, verified by readback.

At 100 Mbps with CBS representing 64 KiB, Windows UDP delivery improved but
remained burst-limited. With the requested 1 MiB represented as 512 CBS tokens at
unit5, a 50 Mbps offer delivered49.82 Mbps with zero loss; a 150 Mbps offer
delivered98.06 Mbps. The restored baseline delivered149.78 Mbps with zero loss.
This supports retaining CIR conversion and fixing burst programming/full-entry
writes. It does not establish Windows packet timing directly.

The bounded source correction and test were delegated in an isolated worktree,
reviewed by the primary, and included in build10. The profile now accepts the
requested burst, packs the complete16-bit CBS across word1/word2, and commits all
three words through `yt921x_reg96_write()`. The actual-C test covers field
boundaries, unchanged CIR conversion, invalid bursts and register failures.
The primary fixed test scaffolding before accepting it; unrun helper claims
were not treated as validation.

Build10 sysupgrade SHA256:
`f2729305683a32c4a2db64691a2028268ca1737333d351caec053aeefd144cd6`.
It runs in slot1, preserving tested build09 in slot0. Exact image modules and
persistent configuration hashes matched after boot. Boot ID:
`8b8a1a7a-0bb5-4b3b-9d26-b3a668f37f20`. Compilation passed; packaging required
normal socket access for fakeroot and then completed successfully.

Through the normal `tc matchall ... police rate100mbit burst1m` API, the fixed
driver delivered50.00 Mbps at a50 Mbps UDP offer with zero loss and97.81 Mbps at
a150 Mbps offer. Removing the rule restored149.68 Mbps with zero loss. No direct
register change was used for this acceptance run. Temporary qdiscs were removed.

Remaining gates for this slice: routed NSS policer/mirror coverage, PCP/queue
packet coverage and a physical-loop test. These are not marked complete.

## Later slices

Build10 also includes the QinQ-specific bridge eligibility change. It retains
the rejection for single-tag mode and per-port software replication for host
bridge TX. The actual callback C test covers QinQ acceptance, single-tag
rejection, same/different bridge cases and failure cleanup. Cross-conduit
hardware packet tests passed:885.22 Mbps, zero retransmits,99.42% CPU idle and
zero firmware IPv4 TX increment. ARP returned3/3 without duplicates. WAN used
bridge VID3088 while retaining phys_if0; hardware FDB entries learned the user
ports. Runtime VLAN100/200 membership, broadcast control and isolation tests
passed across the two conduits. Removing WAN from VLAN100 blocked delivery;
restoring membership restored it. PVID changes alone do not remove membership;
the initial contrary expectation was corrected in the recorded test. Multicast
receiver/leave tests are still pending.

Customer VID namespace, direct VLAN uppers,
explicit standard-DSA/customer-QinQ fallback, capture decoding and flow
regeneration remain to be implemented and validated in the order of the audit.
The cross-conduit source review is advisory: generic tag_8021q installs internal
VIDs on both CPU ports. An outer port-identity SVID and a VTU entry used for the
inner forwarding domain are distinct roles even when their numeric IDs match.
Changing only the standalone restore loop would not change generic ownership;
no such change is accepted without a complete lifecycle analysis.

Evidence: original repository's ignored `logs/nss-slices-20261105/slice01/`.
Third-party work is source-only; the primary performed builds and device tests.


## Slice 04: high customer VID namespace

A shared driver/tagger/NSS predicate permits customer VIDs1-4094 while reserving
all local user-port standalone IDs and all seven possible unaware-bridge IDs,
even before those bridges exist. On CR881x this reserves11 customer IDs:
3072-3075,3088,3104,3120,3584,3600,3616,3632. Thus4083 customer IDs are usable.
Single-tag mode keeps its original blanket transport reservation. This does not
claim4094 usable customer IDs or remove the smaller NSS node-pool limit.

The driver arrays cover4096 entries; QinQ tagger checks and NSS leaf collection
use the same predicate. Outer identity and unaware-domain matching are unchanged.
Actual header C tests exhaust both the complete local VID space and a shifted
switch/port topology. The primary replaced the helper's mirrored expected-value
logic with explicit independent reserved sets and corrected a shifted-port
expectation. The real transport C test also checks high-VID filtering transitions
and cleanup, exact collision rejection, and unchanged single-tag restrictions.

Build11 SHA256:
`22d64a10cb6d432cfddae278efab87412396858256160c2a364540b15ddbe7be`.
It runs in slot0; tested build10 remains in slot1. Exact module hashes matched.
Boot ID: `bbe7640e-69a6-4743-a14d-d2bba1e1abd7`.

Hardware tests across LAN3 and WAN returned3/3 for3071,3076,3500,4094 and0/3
when WAN membership was removed. NSS leaf interfaces for each tested VID appeared
on LAN3/phys_if1 and WAN/phys_if0 with their respective roots. All eleven reserved
IDs and4095 were rejected. VLAN3500 retained3/3 after a filtering off/on cycle.
These checks prove forwarding and firmware-node configuration, not routed NSS
acceleration using every high VID; that L3 test remains pending.

After restoring all temporary VLAN/bridge state, fresh routed TCP achieved
936.54 Mbps with0 retransmits,98.32% CPU idle, two active ECM flows and775936
additional firmware IPv4 TX packets. Persistent configuration hashes are unchanged.

Build10 policy follow-ups: WAN hardware mirroring copied15236 matching TCP
packets while enabled and zero before/after; NSS counters increased in every
phase. The WAN policer allowed49.95 Mbps at a50 Mbps TCP offer while two ECM
flows remained active; an above-limit TCP offer slowed through policing loss,
and removing the rule restored149.85 Mbps. UDP rate checks passed, but their
firmware counters did not establish UDP acceleration; no such claim is made.

Evidence for Slice04: original ignored `logs/nss-slices-20261105/slice04/`.


## Physical-loop gate: not accepted; protection state needs separation

A spare cable joined LAN1 andLAN2 in a dedicated VLAN-unaware STP bridge;
LAN3 management and WAN were outside it. Both ports reached listening/learning,
but management SSH stopped responding before convergence could be accepted.
Removing the cable restored access without reboot. The test bridge was removed,
LAN1/LAN2 were restored up in br-lan, configuration hashes matched and no UCI
changes remained. A fresh flow then reached936.57 Mbps with0 retransmits,
98.55% CPU idle and762599 additional firmware IPv4 TX packets.

This run did not establish end-to-end STP convergence. The established driver
README marks loop prevention/enforcement DONE through corrected STP hardware
state mapping and a physical-loop test; that baseline is preserved. Hardware
loop detection was enabled, and its effect was not isolated from this test.
Port cuts therefore cannot alone establish a new STP or NSS regression.
Repeated own-source-address messages and port flaps were recorded; they do not
prove an NSS crash, firmware defect, or specific RMA failure. A review hypothesis
about the tagger's VLAN-aware STP check was rejected: this bridge was unaware.
DSA also reruns eth_type_trans after decapsulation, so broken LLC classification
is not established merely by reading the tagger. The next diagnostic must use
separate one-port bridges, not repeat the physical forwarding loop.

## Slice 05 candidate topology: tested bridge VLAN upper

On build11 a netifd-managed, single-port VLAN-aware bridge containing WAN,
with VLAN3500 as its untagged/PVID and a bridge VLAN upper as the WAN L3 device,
ran routed NAT at935.26 Mbps,0 retransmits,97.23% CPU idle, two active ECM flows
and757821 additional firmware IPv4 TX packets. This also validates routed NSS
using a high customer VID. An initial manually created-device attempt did not
become available in netifd and never reached packet testing; explicit temporary
netifd device declarations fixed setup.

The test used uncommitted UCI deltas and a verified rollback watchdog. The
original WAN device/DHCP, exact configuration hashes and empty UCI changes were
restored. Direct wan.N/lanN.N support remains a separate implementation, and
transparent tagged unaware bridges remain unsupported.

## Slice 07 capture portion: source fix and RAM validation

A libpcap patch recognizes only yt921x-qinq as standard Ethernet framing;
DA/SA,0x88a8 outer VLAN,0x8100 inner VLAN and original EtherType remain intact.
Other DSA encodings keep their existing opaque handling. The helper's suggested
extra alignment change was rejected: ordinary Ethernet already uses offset2,
as does the existing DSA branch. Falling through the normal Ethernet path also
selects the normal VLAN AUXDATA reconstruction path.

A rebuilt library loaded from router RAM produced EN10MB pcaps retaining exactly
SVID3074/CVID3088 for known probes. Ethernet-source, outerTPID, outerVID and nested
VLAN+ARP filters each selected3/3 probes live and offline. Plain arp correctly
selected0 because VLAN headers precede ARP. The installed library checksum still
matches build11; the source patch is not yet in the running firmware. RX VLAN
and S-tag hardware parsing are fixed off on this native conduit, so an enabled
hardware-AUXDATA mode was not claimed as tested. Flow regeneration and resource
stress portions remain pending.


The user highlighted the existing loop-port cutoff. Current all-port inspection
shows LAN1/LAN2 administratively down with PHY BMCR0x1940 (power-down bit set)
by diagnostic cleanup, while LAN3/WAN remain1G and CPU links1G/2.5G. Those
post-cleanup down states are not evidence of autonomous loop cutoff. Loop-detect
`en=1` and software storm-guard `en=0` were read back; hardware action/status
causality remains to be established. A follow-up BPDU attempt had no carrier
and started user-port capture before the ports were up, so it is inconclusive.
It must not be recorded as a BPDU delivery failure.


The vendor `yt_loopdetect.h` defines generate-way0 as hardware and1 as software.
Thus en1/gen_way0 includes hardware-generated probes; it does not mean generation
is disabled. Future guard-off diagnostics are limited to a5-second on-device
watchdog, with a shorter foreground window and restoration of the exact saved
register before dropping the test ports, so a netdev-down operation cannot
delay re-enabling protection. The previous180-second watchdog
was too long and is not to be reused for protection-off experiments. The
short-window script is a prepared diagnostic, not an automatic startup service.

After the user confirmed cable removal, LAN1/LAN2 were restored administratively
up in br-lan. Loop detection remained en1 at the exact saved0x03266624 and the
boot ID was unchanged. The prepared diagnostic now waits for watchdog readiness
before disabling protection, uses a two-second foreground observation, and lets
the independent five-second fallback finish rather than cancelling it after a
write. Its shell syntax passed. A separate RAM-only timer/marker experiment on
the router verified survival of the parent shell's HUP; the protection-off
diagnostic itself was not rerun.

With the physical loop absent and protection enabled throughout, Linux on LAN3
sent three marked802.3 LLC BPDUs plus controls. Neither CPU conduit nor the
LAN3 user capture contained the marked BPDUs. All three unicast copies of the
same LLC payload and all three ARP controls appeared on eth1 and LAN3, retaining
the expected private VLAN pair; none appeared on eth0. The bridge remained
root at7000.ccd843541c79, with its original STP state and configuration hashes.
The first attempt stopped at a preflight assumption about STP being disabled;
it sent no packets. The completed test used inferior-priorityf000 BPDUs.
This narrows the remaining diagnostic to destination-specific control handling;
it does not yet distinguish switch RMA delivery from NSS handling before host
capture, and does not establish physical-loop convergence. Evidence:
original ignored logs/nss-slices-20261105/slice04/bpdu-no-loop-results.json.

A bounded follow-up preserved external CPU-copy destination8 with both EN and
TAG_EN still zero. Readback confirmed8 during the experiment and0 afterwards.
It produced the same0 BPDU /3 unicast LLC /3 ARP result on eth1/LAN3; eth0 stayed
empty. Thus preserving NUM alone is not a proven fix. The first setup aborted
before changing the register because this router's sleep rejects fractional
arguments; both diagnostic scripts now use integer router-side sleeps. The
two-second foreground restore and independent five-second fallback completed
with protection en1 throughout. See bpdu-cpu-target-results.json in the same log
directory. No physical loop or firmware change was involved.

Source review also identified a separate RMA mapping error: vendor bits5:0 are
CPU_CODE, while the current driver calls them FWD_MASK and overwrites them in
both action helpers. The vendor action API preserves these bits. This is a
source-backed correction candidate, not an established cause of BPDU loss;
no guessed CPU reason code or permanent RMA change was applied in this run.
