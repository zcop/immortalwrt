# Native NSS/QinQ DSA implementation slices

2026-11-05, branch `nss-qosmio-qinq`, base `9cca0ec1e74d`.
This records execution of [the feature-audit plan](nss-qosmio-dsa-feature-audit.md).
The original Kuncy reference remains unchanged; its 30 saved source hashes match.
Source commits, image identity and device validation are recorded per slice.
The latest accepted device image is build21; see Slice16 below.

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


## Physical-loop attempts before build13: not accepted

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

Source review also identified a separate RMA mapping error: bits5:0 are
CPU_CODE, while the driver called them FWD_MASK. The newer RB SDK preserves
these bits for every action, but that behavior must not be substituted for the
target stock ABI. The target-module check below supersedes that initial proposal.

## Slice 08: RMA ABI and BPDU transport

The stock YT9215S module was extracted alone from the saved squashfs and its
`fal_tiger_rma_action_set/get` disassembled. Its SHA256 is
`90ae5c0dd6eb2fa5b66b91d391f02a1fa7b6095bff698b7569f4fdfd4929bb93`.
Unlike the newer RB source, target stock sets CPU_CODE to63 for TRAP/COPY,
and preserves the prior code for FORWARD/DROP. The correction in both Linux
API helpers now follows that target behavior. Invalid actions leave the input
unchanged; bypass flags and unrelated bits survive. The actual-C harness covers
all64 prior codes, existing action bits, both helpers, wrapper read/write
failures, bypass changes and invalid inputs. It and the existing transport test
pass; kernel compilation also passes. At that stage the correction was not installed; it is included in build12
below. It is not established as the cause of the BPDU transport fault.

No-loop tests on running build11 narrowed the fault further:

| Test | Result |
| --- | --- |
| Enable external CPU port8 without proprietary RX tagging (`0x8000c=0x4008`) | Marked inferior BPDUs reach eth1 with SVID3074/CVID3088 and decode to LAN3; the original disabled CPU block delivered none |
| Host BPDU TX through ordinary QinQ | Seen on eth1-out; returns on eth1-in while CPU mode is enabled, with no BPDU seen on the workstation wire |
| Hardware ingress mirror of CPU8 to LAN3 | Host sees a router BPDU: NSS does physically transmit it to the switch |
| Padding comparison: CPU frames60/68/128bytes | No group-address BPDU exits; the identical LLC payload with unicast DA passes at every size. Padding is not the cause |
| RMA FORWARD/TRAP/DROP encodings, VLAN-edit bypass, ACL control-bypass bit/full-block commits | None establishes working group BPDU TX; these temporary settings are not source fixes |
| Hardware loop detector temporarily disabled with no loop cable | Same failure; exact guard value restored by2-second foreground/five-second independent watchdog |
| Existing eight-byte CPU forwarding tag for control TX | With external CPU mode enabled and TAG_EN clear, all9 marked BPDUs (three each at60/68/128bytes) reach physical LAN3, with the private header removed; unicast controls also pass |
| Native routed NSS with that CPU mode enabled |936.69Mbps forward /936.60Mbps reverse,98.56/98.40% idle, two active ECM connections,587724/585752 firmware IPv4 TX increments; baseline936.50Mbps |

The working control-TX frame is
`DA | SA | 9988 | 0000 | 0080 | 8004 | original length/EtherType | payload`
for LAN3 (hardware port2). Linux inserts the eight-byte switch command and the
switch removes it on egress. This is a host control-packet exception, not an
NSS flow using the proprietary tag. Ordinary accelerated data retains QinQ.
These raw-register tests preceded the build12 installation recorded below.

All register experiments restore the saved mode, RMA, mirror and steering state.
Loop protection remains enabled except the explicitly bounded no-loop control
above. LAN1/LAN2 are back up without carrier, the spare loop cable is removed,
and management stayed on LAN3. Configuration hashes and boot identity are
unchanged. The raw injection probes establish the candidate transport; they do
not establish automatic kernel BPDU TX, STP convergence, VLAN-aware blocked-port
BPDU reception or WAN control reception across the two CPU conduits.

Evidence: original ignored `logs/nss-slices-20261105/slice08-rma/`, especially
`cpu-padding-trace/`, `cpu-proprietary-retry-trace/` and `cpu-enabled-nss/`.
An external stock ACL review returned unsupported field-to-action inferences;
those were not accepted as register or source changes. The bounded TX framing draft was delegated; primary integration added the
required secondary-conduit dispatch and corrected the test scaffolding before
acceptance, as recorded below.


### Build12: installed control TX exception, 2026-11-06

The tagger uses the proprietary eight-byte forwarding command only for
01:80:c2:00:00:00..0f host control TX. Data keeps its existing QinQ framing and
NSS resolution. QinQ CPU setup now enables external CPU8 without TAG_EN, on
both CPU-port setup paths. Single-tag mode retains its earlier configuration.
Both RMA API helpers use the verified target-stock CPU_CODE semantics above.

Hardware tests also showed that the forwarding command does not work through
CPU4. WAN control TX therefore clones the original skb and sends the framed
clone through CPU8 with the exact WAN destination mask. The normal DSA caller
still releases its original skb. Primary-conduit control TX follows normal DSA
enqueue; data is never redirected. The driver already restricts this transport
to the CR881x CPU8/CPU4 mapping. One existing customer C-tag is preserved;
stacked/unsupported control tags are rejected.

The helper's initial harness omitted substitution markers/a mock constant and
modeled DSA tags as a prepend ahead of DA/SA. Those results were not accepted.
The primary corrected the mocks and expected wire layout, added actual callback
coverage of primary/secondary dispatch, unchanged data forwarding, allocation
failure and clone cleanup. All112 actual-C checks pass with address/undefined
behavior sanitizers, including LeakSanitizer outside the sandbox (its ptrace
restriction prevented the sandboxed run). RMA/transport/actual ECM resolver
checks and the complete firmware build pass. The initial ECM integration check
ran while its prepared-package fixture was absent during the full build; its
post-build rerun passed.

Build12 sysupgrade SHA256:
`1f2a05c4dbfcc33c2fea5b819387e21c10c4031b6499f13b977da721596075e2`.
It was installed once in slot1/mtd19. The checked, unchanged build11 remains
in slot0/mtd18, with a verified private full-slot backup and configuration
archive. Boot ID: `ee2f991c-a3bd-4fb7-8531-43e9c0a2601c`.
The final read-only check shows STP enabled, LAN3/WAN up, LAN1/LAN2 up
without carrier, CPU links2500/1000Mbps, and no UCI changes.
Six installed modules match the candidate image; all five critical prepared
kernel source files matched the frozen source manifest. Configuration hashes
are preserved, UCI changes are empty, and eight QinQ nodes start automatically.

| Installed build12 check | Result |
| --- | --- |
| Marked host BPDUs injected through lan3, allowing the actual DSA tagger to frame them |9/9 at physical LAN3; CPU8 capture shows9988/0000/0080/8004; private header removed before host wire |
| Marked host BPDUs injected through wan |9/9 observed by WAN hardware egress mirror; CPU8 shows9988/0000/0080/8008; zero group control packets on CPU4 |
| External LAN3 BPDUs decoded to Linux during those runs |24 and23 seen on lan3-in;24 ARP replies in each run |
| Passive capture of actual bridge-generated BPDUs, without probe injection |3 untagged BPDUs in6.5seconds, at2.00/2.08second intervals; automatic STP TX works |
| Fresh routed NSS forward |936.66Mbps,98.31% CPU idle,761034 firmware IPv4 TX increment, two active ECM connections |
| Fresh routed NSS reverse |936.38Mbps,98.67% CPU idle,755933 firmware IPv4 TX increment, two active ECM connections |
| Protection and restoration |Loop detector enabled throughout; WAN egress mirror returned to0; persistent configuration unchanged |

These are packet tests through the installed DSA callback, not a physical-loop
STP convergence test. WAN egress is proven by the switch's egress mirror; no
Windows packet capture is claimed. WAN ingress control copies across CPU
conduits, VLAN-aware listening/blocking BPDU reception, and physical-loop
convergence remain open. The RX tagger has not been changed in this slice.
No new source commit, push or release was made.

Evidence: original ignored `logs/nss-slices-20261105/slice08-rma/build12-accept/`,
`build12-artifacts.json`, the once-only upgrade records, and active-worktree
`logs/qosmio-native/build-12-control-{kernel,image}.log`.


### Build13: control RX and safe checkpoint, 2026-11-06

Build12 physical LAN1-to-WAN probes exposed two RX requirements: RMA control
copies from either user port reach CPU8 with their exact outer source-port VID,
but an inner private unaware VID4095. The old RX policy rejected them. Ordinary
unicast still followed its natural CPU conduit and decoded normally. A separate
RMA VLAN-leaky-bit experiment lacked its ordinary-data positive control in one
direction and is inconclusive; no source change is based on it.

The RX tagger now permits this CPU8 exception only for recognized destination
and protocol pairs: LLC42:42:03 BPDUs, slow protocols, EAPOL, and LLDP. The exact
outer switch/port VID remains mandatory, and CPU8 must belong to the same
switch. A destination MAC alone cannot admit IPv4, wrong LLC, PAUSE, or other
ordinary traffic. Recognized control can discard the private CVID4095, reach
its correct Linux user port across the CPU conduits, and bypass the ordinary
LEARNING/FORWARDING state requirement. Native PVID control tags are removed;
admitted non-PVID customer tags are retained. Ordinary data retains the
existing conduit, VLAN-membership and STP-state checks. This adds no ACL rules.

A bounded helper drafted the RX decision harness; primary review corrected its
endianness, DSA VID namespace and protocol-negative coverage. All74 actual-C RX
decision checks pass with address/undefined/leak sanitizers, and the112 TX
regressions pass. Kernel and full image builds pass. These harnesses do not
replace the installed callback and physical packet checks below.

Build13 sysupgrade SHA256:
`e8d4c9d49492b90f988863c9425efcd61af41477cbd50159e72a3c554f13c954`.
Installed once in slot0/mtd18; build12 remains in slot1/mtd19 with a verified
private full-slot backup. Six installed modules match the image and the five
critical prepared kernel files match the frozen source manifest. Boot ID:
`f67f834d-a9d3-488f-b547-1e77c60ab3db`.

| Installed build13 check | Result |
| --- | --- |
| Normal routed NSS before physical probes |936.57Mbps forward /936.29Mbps reverse,98.56/98.91% CPU idle;780811/755933 firmware IPv4 TX increments, two ECM connections |
| Standalone physical LAN1-to-WAN and reverse |BPDU, slow and EAPOL3/3 each reach the correct child in both directions; ordinary unicast3/3 each also passes |
| Protocol-negative probes |BPDU destination with IPv4 or wrong LLC is seen on CPU8 but never admitted as control on the child |
| VLAN-aware LAN1, PVID100, forwarding |Controls delivered untagged; ordinary data retains customer VID100 |
| VLAN-aware LAN1, PVID100, LISTENING |Controls3/3 each; ordinary data0; Linux root ID changes to the injected superior BPDU root |
| VLAN-aware LAN1, no PVID, LISTENING |Controls3/3 each; ordinary data0; Linux root ID changes to the injected superior root |
| VLAN-aware LAN1, non-PVID customer VID200, LISTENING |BPDU/slow/EAPOL3/3 each preserve C-tag200 on the child; CPU8 has outer3072/inner200; wrong-protocol controls with that same valid customer tag are rejected |
| Safeguards |Loop protection disabled only for1.06–1.11seconds per short probe, then restored; independent five-second restore and port shutdown safeguards |

The first listening attempt used15 timer ticks rather than1500 and had already
advanced to LEARNING. It is not accepted as held-LISTENING evidence. The rerun
reads back forward_delay1500, and state1 before and after injection. The tagged
VID200 test proves preservation, not tagged BPDU consumption by kernel STP;
root-learning proof is from the native and no-PVID tests. LLDP was absent even
on the parent in the before/after physical runs; its established hardware-path
gap remains open. At this checkpoint, actual physical-loop STP convergence had not been tested.

**Historical user-requested safe stop:** no physical-loop test had been started. The spare cable
remains connected LAN1-to-WAN, Windows is disconnected from WAN, both test ports
are administratively down and detached, and the temporary test bridge is gone.
LAN3 management is reachable; loop protection reads0x03266624. No test jobs are
running. Persistent network/wireless/firewall hashes are unchanged and UCI
changes are empty. WAN service is intentionally down in this isolated test
state. Do not bring these ports up, restore normal routing, or reboot until the
spare cable is removed. On resume, either prepare the bounded isolated STP
convergence test, or remove the spare cable, return Windows to WAN, restore
LAN1 to br-lan, explicitly bring WAN up and restart its DHCP interface. Perform
fresh routed NSS checks after normal restoration. No new commit, push or
release was made; original reference files remain unchanged.

Evidence: original ignored `logs/nss-slices-20261106/slice09-rx/`, especially
`build12-physical-baseline-r2/`, `build13-physical-standalone/`,
`build13-aware-{forward,listening-fd15,no-pvid,tagged}/`, `build13-accept/`,
`build13-artifacts.json`, and `SAFE-CHECKPOINT.json`. Source/build logs are in
the active worktree under `logs/qosmio-native/build-13-*`.


### Build13 resumed: physical STP loop accepted at100Mbps

The user confirmed the spare cable directly connected LAN1 to WAN, with Linux
management on LAN3 and Windows disconnected. A guarded six-second link wait
failed before STP could be tested. A standalone negotiation trace then showed
repeated1Gbps link loss, eventually downshifting to100Mbps; the loop detector
flag stayed0. This does not establish the cause of the PHY instability or prove
that hardware loop protection cut the link. No driver change was based on it.

For the control-plane acceptance, only LAN1/WAN advertised100Mbps full duplex
with autonegotiation. The temporary change had independent recovery, and both
ports' original advertised modes and pause settings were verified identical
after restoration. The100Mbps standalone link stayed up for the final eight
samples. No other user or CPU port changed its PHY configuration.

A dedicated VLAN-aware bridge joined LAN1 and WAN in PVID100 with no management
interface. Kernel STP ran with forward_delay1500 ticks (15seconds), hello100,
max_age2000, and an explicit bridge MAC. Hardware loop protection stayed on for
the complete test; no protection-off window was used. Independent initial and
hard shutdowns bounded the test, and loss of the blocking role would immediately
close both ports.

| Physical loop check | Accepted result |
| --- | --- |
| Linux roles |WAN remained BLOCKING in all32 samples; LAN1 advanced LISTENING -> LEARNING -> FORWARDING |
| Switch MST0 state |LAN1 hardware2 ->1 ->3; WAN remained hardware2 (blocked) |
| Converged state |Four consecutive samples with Linux3/4 and hardware3/2, spanning3.08seconds after the normal STP timers |
| Actual received bridge BPDUs |35 on CPU8/eth1:34 with source-port SVID3075 and one with SVID3072; both use private inner VID4095, and advertised bridge/port IDs match the test bridge |
| Secondary CPU conduit |Zero test-bridge BPDUs on eth0, confirming the CPU8 RX exception is exercised |
| Protection and load |Guard0x03266624 throughout; loop flag0; CPU96.26% idle over the bounded test |

This closes the physical STP convergence gate at100Mbps for the tested
VLAN-aware cross-conduit bridge. It does not establish stable1Gbps negotiation
on this spare loop cable. Full-duplex1Gbps WAN negotiation recovered normally
when Windows was returned to WAN. The cause of the spare-link1Gbps instability
remains unconfirmed; no source fix or cable-fault diagnosis is claimed.

Both test ports were shut down before removing the temporary bridge. The user
removed the spare cable and returned Windows to WAN. LAN1 was restored to
br-lan, the original PHY advertisements restored, and WAN explicitly brought up.
DHCP Discover packets were observed on WAN/eth0 with no replies; Windows ICS
restart then restored its DHCP lease. Persistent configuration hashes and UCI
state stayed unchanged. Fresh routed NSS restoration results are recorded in
the final acceptance note below.

Evidence: original ignored `logs/nss-slices-20261106/slice09-rx/`, especially
`build13-link-negotiation/`, `build13-link-negotiation-100m/`,
`build13-100m-wrapper/`, `build13-stp-convergence-100m/acceptance.json`,
`build13-stp-convergence-100m/packet-summary.json` and `build13-restored/`.


#### Build13 final restored acceptance

Windows ICS restart restored WAN DHCP at192.168.137.65/24. Final fresh routed
NSS tests delivered936.49Mbps forward and936.64Mbps reverse, with99.03/98.31%
CPU idle,760247/753765 firmware IPv4 TX increments and two active ECM
connections in each run. Eight QinQ nodes are present, with zero allocation
failures. WAN is1Gbps and the LAN CPU link remains2.5Gbps. Original loop guard,
CPU mode, BPDU RMA and disabled mirror values match the baseline. Network,
wireless and firewall hashes are unchanged and UCI changes are empty.

The spare loop cable is removed, Windows is back on WAN, LAN1 is back in br-lan,
and management remains on LAN3. No reboot, flash, driver-source change, new
commit, push or release occurred during the resumed physical-STP test. At that checkpoint, build12
and13 source changes were uncommitted; the separate source commits are listed
below. No claim of complete DSA feature parity is made. Direct VLAN uppers, full customer-QinQ fallback,
flow regeneration/node-pool stress and the remaining packet policy tests keep
their separate acceptance gates.

Evidence: `logs/nss-slices-20261106/slice09-rx/build13-final-smoke/results.json`
in the original repository.


### Source commits after build13 acceptance

At the user's request, the validated changes were committed separately:

- `27e4076277`: correct the RMA CPU reason-code ABI and debug readout.
- `012b027c4f`: enable external CPU control mode and host link-local TX through CPU8.
- `43e47afb48`: receive recognized QinQ control copies across CPU conduits while preserving ordinary VLAN/data checks.

The split TX commit uses the exact frozen build12 tagger, verified against the
build12 artifact manifest. The RX commit then adds the build13 changes. The RMA
header comment was clarified to describe TRAP/COPY versus FORWARD/DROP; register
encodings are unchanged from the tested image. Focused RMA,112-check TX,
74-check RX, transport and actual ECM resolver tests pass before committing.
This does not include a push or release. The original Kuncy reference worktree
and its30 saved source hashes remain unchanged. Direct VLAN interfaces are the
next planned source slice; the normal router configuration remains restored.

## Slice10: direct customer VLAN interfaces on standalone QinQ ports

This slice adds explicit `wan.N` / `lanN.N` interfaces on standalone user
ports. Their wire pair is the existing per-port SVID plus customer CVID;
the base untagged interface keeps its native PVID and separate firmware leaf.
The switch programs tagged membership for the user port and its actual CPU
conduit. A customer VID has one owner across direct interfaces and bridge
domains because the hardware customer VLAN table is global. Conflicting
ownership, reserved transport VIDs and bridge/LAG joins with direct uppers
are rejected. Direct hardware/NSS membership requires a standalone non-LAG
base port. Remove the uppers before changing that role.

Tagger TX/RX, the NSS node collector and ECM share the registered direct-upper
check. Base leaves remain explicit when additional leaves exist; stale,
stacked, S-tagged or upper-master mappings do not authorize acceleration.
Kernel patch764 removes user membership if later host registration fails.
Partial driver I/O failure retains the VID reservation and closes that port.
Transparent tagged VLAN-unaware bridges and full customer QinQ remain separate
unsupported cases.

### Build14/15 hardware checks and the bridge transition correction

Build14 booted and passed normal routed NSS, but creating `wan.35` did not
program the VTU: standalone `rx-vlan-filter` was off, so Linux never invoked
the VLAN registration hooks. Linux upper creation and a firmware node alone
were therefore insufficient evidence. No Windows VLAN setting was changed.

Build15 enables DSA standalone VLAN filtering for QinQ only, admits direct
uppers only with that hardware feature enabled, and clears private customer
bridge awareness on bridge leave. It was installed once into slot0 and its
six module hashes matched the frozen image; slot1's build14 fallback and the
persistent configuration were verified unchanged.

| Installed build15 gate | Result |
| --- | --- |
| Native routed NSS, forward/reverse |936.57 /936.56 Mbps,98.91 /98.92% CPU idle; firmware counters advanced |
| `wan.35` switch membership | Port3 + actual CPU4, both tagged; native PVID3075 unchanged |
| Additional `wan.36` leaf and removal | Exact SVID3075/CVID36 leaf added and removed independently |
| Reserved VID, bridge-owned VID, direct upper on bridged port | Rejected without a surviving interface |
| Bridge join with direct upper; bridge VID colliding with direct owner | Rejected |
| Base WAN while `wan.35` also exists |936.56 /936.38 Mbps; firmware counters advanced in both directions |
| Direct `lan2.36` after detaching idle LAN2 | Port1 + actual CPU8 tagged; SVID3073/CVID36 firmware leaf; native PVID3073 retained |
| Same direct CVID on two standalone ports | Rejected |
| Cleanup | Temporary uppers removed, eight nodes, VTU35 empty, normal config and loop guard retained |

The idle LAN2 aware-bridge transition then exposed a second defect. Linux
reported VLAN100/PVID100, but hardware retained PVID3104 and VLAN100 contained
CPU8 only. DSA skips `port_vlan_filtering()` when its logical state remains
true from standalone mode to an aware bridge, while the driver's private
customer-awareness state still needed updating. The test restored LAN2 to
`br-lan`; management and normal WAN remained reachable.

The next candidate synchronizes that private state in the bridge-join callback.
The actual callback regression fails with build15's source and passes with the
correction, including aware/unaware joins and the direct-ownership guard.

Build16 was then installed once into slot1. Only `yt921x_l2.c` and the switch
module differ from build15; the other five frozen modules, ECM source and
build configuration match. The installed modules and preserved build15 slot0
were verified. Fresh native routed NSS delivered936.64Mbps in both directions,
with97.49/98.67% CPU idle and762951/753515 firmware IPv4 TX increments.

The same idle LAN2 lifecycle test now passes: direct CVID36 programs tagged
port1/CPU8 membership and retains PVID3073; aware bridge join programs
PVID100 and user+CPU8 membership, with only the user port untagged. Bridge
leave removes VLAN100 and restores PVID3073. Returning to normal `br-lan`
restores PVID3088, empty test VLANs and eight firmware nodes. Configuration
hashes are unchanged, UCI changes empty and loop protection stays enabled.
These are hardware membership/lifecycle checks on an unplugged port, not a
tagged LAN endpoint traffic test. The subsequent tagged WAN test is recorded
below; tagged endpoint coverage on direct LAN uppers remains separate.

The final negative test also distinguishes Linux interface creation from
transport admission. After a role transition, an unaware-bridge port has
`rx-vlan-filter: off [fixed]`; Linux can then create a VLAN upper without
calling the driver's registration hook. That is not a supported direct-VLAN
data path. The tagger/controller/ECM checks must still refuse it. The build15
initial-boot rejection above must not be generalized into a guarantee that
every such Linux interface creation fails. Build16's repeated check confirmed
that the created `lan2.36` has neither a VLAN36 hardware entry nor an NSS leaf;
the temporary interface was then removed.

The final build16 coexistence run also passed: `wan.35` programs port3/CPU4
tagged membership without changing native PVID3075, ownership conflicts and
bridge joins are rejected, and additional `wan.36` adds/removes independently.
Base routed NSS while `wan.35` exists delivers936.58/936.32Mbps with
98.30/98.43% CPU idle and766852/755960 firmware TX increments. Cleanup leaves
eight nodes, empty VLAN35, normal WAN up and unchanged persistent config.

Evidence: original ignored `logs/nss-slices-20261106/slice10-direct-vlan/`,
especially `build15-accept/`, `direct-api-coexistence-build15/`,
`idle-port-lifecycle-build15/`, `build16-accept/` and
`idle-port-lifecycle-build16/` and `direct-api-coexistence-build16-r2/`.
The earlier `direct-api-coexistence-build16/` aborted on the invalid assumption
that bridged upper creation must always fail; it cleaned up and is not counted
as a full pass. Source snapshots and frozen artifact manifests
distinguish each installed candidate from subsequent source corrections.

### Build16: real VLAN35 WAN traffic and restored native acceptance

Windows Ethernet was temporarily set to customer VLAN35. Initially it had an
APIPA address instead of192.168.137.1, and the WAN hardware mirror showed
correctly tagged DHCP/ARP requests without replies. Tagged ARP/ping later
worked, and a static router address was used briefly for diagnosis. The first
unpaced TCP attempt advanced firmware counters but timed out returning the
iperf result; it is not an accepted throughput result. The independent watchdog
restored that temporary setup later; it did not cause the earlier timeout.

Windows DHCP subsequently recovered. A fresh bounded run obtained
192.168.137.45 on `wan.35` from192.168.137.1 and retained DHCP for the accepted
tests. There were nine firmware nodes, including the exact standalone WAN
SVID3075/CVID35 leaf. No driver or firmware source changed during endpoint
diagnosis. One intermediate harness attempt stopped when DHCP recovered
between its checks; that aborted attempt cleaned up and is not counted as a
traffic pass.

Both routed TCP directions then completed with a requested900Mbps pace,
one-second omission and eight measured seconds:

| VLAN35 routed NSS measurement | LAN → Windows WAN | Windows WAN → LAN |
| --- | --- | --- |
| Receiver throughput |899.96Mbps |591.42Mbps |
| Router CPU idle |99.03% |98.79% |
| Firmware IPv4 TX increment |737389 |1145840 |
| Active ECM connections |2 |2 |
| VLAN/interface mismatch increments |0 /0 |0 /0 |

The lower reverse rate was unresolved at this checkpoint. The subsequent MTU
investigation below identifies the user-port receive-frame limit as its cause.
The original unpaced result timeout was not independently reproduced. These
earlier measurements alone did not establish performance parity.

The WAN hardware mirror was tested with `matchall skip_sw` rules confirmed
`in_hw`, using paced100Mbps TCP and an AF_PACKET capture on LAN3. AUXDATA was
included because the workstation NIC strips VLAN headers into metadata.

| WAN physical-port mirror gate | No mirror | Mirror enabled | Mirror removed |
| --- | --- | --- | --- |
| Egress matching TCP packets |0 |24056 |0 |
| Ingress matching TCP packets |0 |25491 |0 |

Every matching enabled-capture frame had a single customer `0x8100 / VID35`
tag at that mirror point, with the expected router/Windows source MAC. Each
phase also had positive NSS firmware packet increments and two accelerated
ECM connections. This observes customer tags at the user-port mirror point;
it is not a native CPU-link capture of the private outer S-tag.

The bounded runner reverted its owned temporary UCI device/interface options
and removed mirrors on completion. Windows was then restored to VLAN ID0 and
SharedAccess restarted. Final native WAN DHCP and fresh routed NSS passed:
936.68/936.35Mbps,98.79/98.92% CPU idle,766529/755945 firmware TX increments,
and two accelerated ECM connections in each direction. Eight nodes remain;
`wan.35` and its temporary netifd section are absent, UCI changes are empty,
and network/wireless/firewall hashes match the saved baseline. Installed
module hashes and the preserved build15 fallback slot also match. Loop guard,
CPU control mode, BPDU RMA, disabled hardware mirror and the2.5Gbps LAN CPU
link match their expected values. Linux remains on LAN3 and Windows on WAN.

Evidence in the original ignored slice10 directory: `tagged-wan-build16/`,
`vlan35-wire-diagnostic/`, `vlan35-nss-wire-r2/`,
`bounded16-r2-results.json` and `build16-restored-accept/`. Source revisions and deployment status are recorded below.

### Tagged reverse asymmetry: user-port MTU headroom confirmed

Matched native controls first ruled out pacing alone:900Mbps-paced TCP reached
900.02/900.07Mbps, while unpaced reverse reached936.74Mbps. A second control
kept Windows at VLAN0 but temporarily made WAN an access port for customer
VLAN35. Linux still used `wan.35` and the same NSS SVID3075/CVID35 mapping;
only WAN's default CVID and VLAN35 untagged-member bit were changed. That
control reached899.94/900.11Mbps paced and936.51Mbps unpaced reverse. Exact
register values and normal routing were restored afterwards. Thus the direct
VLAN35 firmware mapping can run at native throughput.

Source and live registers then showed the missing allowance:

- `yt921x_dsa_port_change_mtu()` budgets MTU +14-byte Ethernet header +4-byte
  FCS on user ports:1518 bytes for MTU1500.
- CPU ports add eight bytes, correctly allowing the internal S+C pair:1526.
- A user-port customer C-tag also consumes four bytes. Its full MTU1500 frame
  is1522 bytes including FCS, exceeding the programmed1518-byte RX limit.

The live WAN MAC3 register is `0x84008`; its frame-size field occupies bits21:8.
The experiment changed only that field from1518 to1522, preserving all other
register bits. Windows stayed on VLAN35 and the same `wan.35` DHCP topology
remained active throughout this comparison.

| Receive-limit check |1518-byte limit |1522-byte limit |1518 restored |
| --- | --- | --- | --- |
| ICMP payload1468: IP1496, tagged wire1518 including FCS |2/2 |2/2 |2/2 |
| ICMP payload1472: IP1500, tagged wire1522 including FCS |0/2 |2/2 |0/2 |

Each rejected full-size pair incremented the switch's `rx_crc_errors` counter
by2, rather than the oversize counter. Increasing the receive limit accepted
the same frame size; restoring it reproduced rejection. This counter labeling
must therefore not be used to dismiss the frame-length defect as cable noise.

With the1522-byte limit, unpaced routed NSS completed at936.64Mbps forward
and936.25Mbps reverse,98.43/99.04% CPU idle,765717/755039 firmware TX increments,
and two accelerated ECM connections per run. VLAN/interface mismatch increments
were zero. No CRC, drop, overflow or NSS error counters increased during these
corrected transfers; normal PAUSE traffic was counted during reverse load.

The direction follows the receive path: Windows-to-router data enters the
1518-byte user-port filter carrying the customer tag. Router-to-Windows data
leaves that port, and its small return ACKs fit the filter. Dropping full-size
reverse packets explains the stalled start and lower TCP result without a
firmware or per-port identity change.

The test restored the original frame limit and native WAN, and Windows was
returned to VLAN0. Fresh native NSS reached936.68/936.64Mbps with about99% CPU
idle, zero VLAN/interface mismatch increments and unchanged persistent config.
Build16 remains installed. The source correction now reserves one customer
C-tag on QinQ user ports while retaining the CPU's existing total allowance,
legacy-mode behavior and maximum MTU. The actual-callback regression passes
with this correction and fails on the original code. It also checks jumbo
bounds, non-frame register bits and I/O error propagation. The complete source
file cross-compiles using the existing kernel module's AArch64 compiler flags.
These checks produced a separate object only; a new image must be built and
installed before this correction persists on the router. Source revisions are
listed below.

Evidence: original ignored `logs/nss-asymmetry-20261106/`, especially
`native-matched/`, `access35-matrix-r2/`, `frame-limits-live.json`,
`mtu35-boundary-r1/{ping-probes,verdict,results}.json` and `native-restored/`.
The first access-control attempt stopped on an unsupported optional ethtool
flag before data traffic and restored its setup; it is not a throughput pass.

### Committed standalone-VLAN and MTU changes

The source changes are committed separately on `nss-qosmio-qinq`:

| Commit | Change |
| --- | --- |
| `b2e0cb82f564` | Roll back user VLAN membership after failed host registration |
| `0ad4d2a6367f` | Support standalone QinQ VLAN uppers across driver, tagger, NSS and ECM |
| `fa5afef6586c` | Reserve customer-tag receive headroom on QinQ user ports |

Focused kernel rollback, driver transport, tagger TX/RX, NSS/ECM and MTU
regressions pass before committing. The MTU cross-compilation result matches
the committed source bytes. The installed image remains build16; a new image
build and installation are required to make its MTU correction persistent.
No push or release is included.

### Build17: installed MTU correction and tagged LAN acceptance

Build17 was built from committed HEAD `e88a0992a9dc` with the existing Qosmio
profile and unchanged `.config` hash. The twelve build jobs came from `nproc`.
Its frozen sysupgrade SHA256 is
`6faa572dff6196548052eba36512d98dfaf80d8e5c1f9696c12c323e7eccccaf`.
The one-time sysupgrade installed slot0, preserving and hash-checking build16
in slot1/mtd19. The new boot ID is
`0d103c66-d194-4356-bd26-3ae77af7be19`; installed module hashes match the
frozen image. Only `yt921x_driver.ko` differs from build16's six recorded
NSS/DSA modules.

All four user MACs now receive1522-byte frames automatically; CPU4/CPU8 retain
1526 bytes. The2.5Gbps LAN CPU link and normal protections remain enabled.
The following tests did not change raw frame-limit registers. Each tagged
case passed three full-size ICMP probes with1472-byte payloads.

| Build17 routed TCP, unpaced | Forward Mbps | Reverse Mbps | CPU idle forward/reverse | Firmware TX increments forward/reverse |
| --- | --- | --- | --- | --- |
| Native WAN after boot |936.62 |936.38 |95.94 /98.80% |773019 /754873 |
| Real tagged `wan.35` |936.54 |936.64 |98.21 /98.68% |770971 /753338 |
| Real tagged `lan1.35` |879.67 |938.71 |98.67 /98.07% |752823 /757804 |
| Real tagged `lan1.4094` |882.23 |939.00 |98.43 /98.07% |765568 /755532 |
| Native WAN after restoration |936.60 |936.35 |98.55 /98.55% |763228 /757018 |

Every run recorded two active ECM connections. Tagged runs had zero VLAN and
interface mismatch increments. Linux stayed on LAN3. Windows used a real
customer tag on the physical endpoint port; LAN1 tests exercised
SVID3072/CVID35 or4094 on `phys_if=1`, with tagged user0/CPU8 VTU membership
`0x101`. WAN35 used `phys_if=0`. The LAN forward rate remains below the WAN
result; these short runs establish functional offload, not full throughput
parity between the topologies.

WAN35 obtained DHCP. The LAN tests used temporary static192.168.137.254 on
the router after DHCP did not complete. Windows remained192.168.137.1.
All temporary devices/interface settings were removed, the three saved config
hashes match, and UCI changes are empty. Windows is restored to WAN/VLAN0;
normal WAN DHCP obtained192.168.137.134. Eight firmware nodes remain with no
allocation failures. No new unfinished-feature guards were added.

### Shared-conduit reload failure: reproduced and traced

The first LAN preparation attempts lost management during netifd reload.
Serial confirmed the kernel remained alive. A later explicit `ifup lan`
restored management; accepted LAN packet measurements required that recovery
and do not establish automatic lifecycle correctness.

A bounded idle-port test reproduced both triggers without cable changes:
remove LAN1 from the unaware bridge, then create a standalone `lan1.35`
interface. Temporary matching-build udebug tools captured netifd's existing
DEVICE/SYSTEM ring without restarting the daemon. Netlink events and sysfs
flags were recorded alongside netifd device status.

The detach releases LAN1's netifd parent claim, reducing `eth1`'s active count
to zero and calling `system_if_down(eth1)`. Kernel DSA closes all user ports
sharing that CPU conduit. LAN2/LAN3 then lack `IFF_UP` while netifd still
reports `up=true`. Creating the VLAN upper creates/reopens the conduit and
LAN1; LAN2/LAN3 remain down until explicitly reapplied. This is an
administrative lifecycle failure, distinct from the successful tagged-frame
and firmware-forwarding checks above. A focused source correction is being
prepared; it is not installed or accepted yet.

The bounded worker restored the saved network, LAN/WAN carrier and eight
firmware nodes. Temporary tracing was stopped, persistent config hashes match,
and loop protection stayed enabled throughout. Transparent tagged unaware
bridges and the broader policy/recovery matrix remain open.

Evidence: original ignored
`logs/nss-slices-20261106/slice11-mtu-image/`, including frozen build17 source
and artifact manifests, `build17-accept/`, `tagged-wan-build17/`,
`direct-lan1-build17/`, `high-vid-lan1-build17/`,
`build17-restored-accept/` and `lifecycle-trace/`. Failed preparation attempts
are retained separately and are not counted as automatic recovery passes.

### Build18: shared-conduit lifecycle correction accepted

The netifd patch
`package/network/config/netifd/patches/100-dsa-keep-conduit-lifecycle-with-kernel.patch`
excludes DSA user ports from automatic parent-device ownership on kernels
where the existing DSA-conduit query is available. Kernel DSA already opens
the conduit when opening a user port. Ordinary parent discovery and the
pre-6.1 path retain their existing behavior. This fixes the administrative
lifetime problem without changing switch registers, firmware tags or the
NSS/DSA modules.

The actual-C regression exercises the patched parent-discovery function with
6.18 and5.15 paths; the original source fails the required DSA-parent check.
The full netifd package cross-compilation and image build passed, and prepared
source bytes match the reviewed patch applied to the pinned pristine source.
Source changes remain uncommitted at this checkpoint.

Build18's frozen sysupgrade SHA256 is
`d26fac777e93dd06cbd61a76fc7071f3933de3e528b9dd3734842d0657d47c8c`.
A one-time sysupgrade installed slot1 and preserved the verified build17
slot0/mtd18. Boot ID is `6c4a9387-b1e1-409e-b3dc-02d089e594c4`.
The installed `sbin/netifd` SHA256 matches
`6ce26d2fad14d6031e4712a97682d8eaefe9cca81ee1765e3aa41756140e419d`;
all six recorded DSA/NSS module hashes are identical to build17. The profile,
config hashes and automatic1522/1526-byte frame limits are preserved.
Native NSS reached933.01/936.66Mbps,98.30/98.21% CPU idle,764000/752907
firmware TX increments and two active ECM connections per direction.

The complete formerly failing sequence then passed without a manual LAN
restart: detach idle LAN1 from `br-lan`, create a configured standalone
`lan1.35` interface, remove it, and rejoin LAN1. In all five sampled phases,
`eth1`, LAN2 and LAN3 retain flags `0x1303`; the continuous netlink capture
records zero administrative-down events for those devices. Firmware nodes
transition8→9→8, with no allocation failures. Fresh routed NSS traffic while
the upper exists reaches927.25Mbps/99.04% idle with767962 firmware TX
increments; after restoration it reaches936.56Mbps/97.30% idle with756699
increments. Each fresh transfer has two active ECM connections.

The separate existing-flow regression is not accepted: a TCP connection
started before the LAN1 detach has two active ECM rules before the change,
then none three seconds after it. Host forwarding continues as its rate
falls toward450Mbps. A subsequent35-second diagnostic confirms zero active
ECM rules at both3 and13 seconds after the change, with unchanged firmware
packet counts between those samples. The replacement TCP connection's ECM
state reports `can_accel=1` and `accel_mode=-2` (`FAIL_RULE`) at both times.
Host forwarding continues; the675.75Mbps whole-run average includes the
initial accelerated period and is not a software-path benchmark. Fresh flows
after the same topology changes accelerate normally. A bounded source review
supports separating transient QinQ lookup failures from permanent bad-rule
classification; its implementation and candidate validation remain pending.
The netifd lifecycle fix and existing-flow reacceleration are therefore
tracked as separate results.

All temporary interface settings are restored, UCI changes are empty, original
config hashes match, and Linux remains LAN3 with Windows WAN/VLAN0. Loop
protection remains enabled. No new feature guards, commit, push or release
are included.

Evidence: original ignored
`logs/nss-slices-20261106/slice12-dsa-conduit/`, including exact source and
artifact manifests, `build18-accept/`, `reload18-accept/` (combined check
failed on existing-flow acceleration) and `port-reload18-accept/` (complete
port-lifecycle and fresh-flow checks passed).


### Build19: existing-flow NSS reacceleration accepted

Build18's surviving TCP flow remained in `FAIL_RULE` after topology changes.
Build19 distinguishes temporary QinQ resolver unavailability (`-EAGAIN`) from
invalid/missing/ambiguous mappings. Only the ECM resolver receives this new
result; the root and lookup APIs retain their old negative-result contract.
A terminal retry flag keeps allocation give-up and shutdown hard, while a
lock-protected generation prevents stale give-up from closing a fresh retry
window. The new `0107-retry-transient-qinq-resolution.patch` preserves exact
`-EAGAIN` through ECM and leaves IPv4/IPv6 attempts decelerated for a later
packet. Both acquired interface lists, the message and pending acceleration
state are released exactly once. Mixed hard errors retain the bad-rule path.
Firmware tags, port maps and the NSS command ABI are unchanged.

The actual-C controller, side resolver, collector and IPv4/IPv6 cleanup tests
pass. Semantic negative controls restore the old controller result, old ECM
normalization, or omit a list release; each fails its required C assertion.
The new patch applies with zero fuzz. Both kernel packages and the full image
cross-compile, with prepared ECM source matching the reviewed bytes. The same
profile/config and twelve jobs from `nproc` are used.

Frozen sysupgrade SHA256:
`31be739699cbf03f9dd3e0adce877100bd62b5b8a26bfb8fb64f7bd2d4fa8213`.
The once-only upgrade installed slot0/mtd18. Build18 remains in slot1/mtd19,
verified against its pre-upgrade raw hash and host backup. Build19 boot ID is
`4d455fc6-455d-4615-b7df-2b5cfe381a42`. Installed hashes match the image;
only `qca-nss-qinq.ko` and `ecm.ko` differ among the six recorded modules.
Netifd and the other NSS/DSA modules match build18. Configuration, guards,
2.5Gbps LAN conduit and automatic1522/1526-byte frame limits are preserved.

Fresh unpaced NSS traffic reaches936.65/936.31Mbps forward/reverse,
98.55/98.92% CPU idle,759147/757170 firmware TX increments and two active
ECM connections per direction.

A single55-second TCP test then spans LAN1 detach, creation of configured
`lan1.35`, removal of that upper, and LAN1 rejoin. At each sample three seconds
after the reload, the data flow is accelerated (`accel_mode=2`), without
`FAIL_RULE`. Firmware TX increments between adjacent samples are:

| Transition | Firmware TX increment | Firmware nodes |
| --- | --- | --- |
| LAN1 detached |368670 |8 |
| VLAN upper created |371726 |9 |
| VLAN upper removed |337750 |8 |
| LAN1 rejoined |361322 |8 |

The same TCP session completes at926.66Mbps over the entire test, including
the transitions. This establishes recovery at the sampled times, not zero
transient software forwarding. `eth1`, LAN2 and LAN3 retain flags0x1303;
continuous netlink capture records zero administrative-down events for them.
No manual LAN restart or iperf reconnection is used. Allocation failures stay
zero. Saved config hashes match, UCI changes are empty, and temporary devices
are removed. Linux stays LAN3; Windows stays WAN/VLAN0. No new feature guard,
commit, push or release is included.

Remaining gates include transparent customer tags on VLAN-unaware bridges,
the broader NSS policy/recovery matrix, cold/warm boot, native host restore
and live IPv6 traffic. IPv6 retry paths have source and build coverage only.
Evidence: original ignored
`logs/nss-slices-20261106/slice13-qinq-retry/`, including source/negative-control
logs, exact source/artifact manifests, `build19-accept/` and
`flow-reload19-accept/`.


## Slice14: tagged unaware-bridge hardware prerequisites

The next slice first separates customer-tag preservation from hardware
MAC-learning domain selection. Build19 remains installed. A bounded experiment
uses only otherwise-unused customer VID35 and three marked frames per case;
normal LAN/WAN configuration and loop protection stay in place.

The vendor field called `L2_VLAN_TBL_SVL_EN` is bit35 of the VTU entry. The
Linux header had named that bit `SVLAN_EN`. The hardware probe establishes its
shared-learning function rather than inferring it from the name:

| VID35 entry | Observed external-source hardware FID |
| --- | --- |
| No VTU membership | No learned entry |
| LAN3/CPU8 members, FID35, bit35 clear |35 |
| Same members, FID3088, bit35 clear |35 |
| Same members, FID3088, bit35 set |3088 |
| Restored FID35, bit35 clear |35 |

Untagged baseline traffic learns in bridge FID3088. Therefore writing only the
FID field does not remap VID35; bit35 must select shared VLAN learning.
The test uses paired VTU writes through the driver's table API. An earlier
single-word write/read attempt did not commit the entry and supplies no FID
remapping result. The earlier workstation ping check followed its Wi-Fi policy
route; subsequent readiness checks use the pinned Ethernet SSH path without
changing workstation routing.

With VID35 mapped to FID3088 and only LAN3/CPU8 as tagged members, packet
captures then show three frames in each direction:

- LAN3 ingress reaches the CPU conduit as outer0x88a8/SVID3074 followed by
  inner0x8100/CVID35. The marked external source learns in FID3088.
- Explicit CPU-conduit TX with that pair reaches the Linux endpoint on LAN3
  with only the customer0x8100/CVID35 tag. The internal S-tag is removed.

These are switch wire-path checks using ordinary marked Ethernet frames;
they do not establish Linux tagger admission, automatic configuration, or NSS
acceleration for tagged unaware bridges. An extra script assertion expected a
synthetic CPU-originated source to appear in the hardware FDB; it did not.
That expectation is not required for these packet observations, and the reason
for the absent CPU-source learning remains unestablished. CPU8's inspected
learning register is zero, so no learning-disable-bit explanation is claimed.

All synthetic FDB entries, temporary bridge VLAN metadata and the VID35 VTU
entry were removed. Saved config hashes match, UCI changes are empty, eight
NSS nodes remain without allocation failures, and protection stayed enabled.
Fresh routed native NSS after restoration reaches936.57Mbps,98.91% CPU idle,
775254 firmware TX increments and two active ECM connections.

The source prerequisite now corrects bit35 naming/programming in the existing
SVL control paths. Nonzero-VID adds and mode application set or clear the bit
with FID; last-member deletion clears both. VID0, partial-member removal and
unrelated VTU fields retain their existing behavior. The actual-C regression
passes, and restoring the old add, delete or mode-apply function separately
fails the emitted-bit checks. Existing transport and MST fast-age regressions
pass. Both changed driver objects cross-compile with the real AArch64 kernel
module flags. This source change is committed separately and is not installed; build19
remains on the router. Full tagged unaware-bridge support still needs coherent
VTU ownership, tagger admission and exact NSS/ECM leaves. One VTU entry per
customer VID cannot simultaneously hold different FID mappings for separate
bridges. This experiment does not resolve that ownership constraint or enable
an arbitrary transparent VLAN trunk.

Evidence: original ignored
`logs/nss-slices-20261106/slice14-unaware-tags/`, especially
`fid-probe-v3/result.json`, `wire-probe/observations.json`,
`wire-probe/primary-acceptance.json`, `wire-probe/restored.txt` and
`restored-nss-result.json`. Earlier stopped harness attempts are retained
separately and are not counted as complete passes.

The alternate private-SVID lookup experiment leaves customer VID35 absent from
the VTU and selects each port's transport SVID for lookup. The single-port
wire check preserves the customer tag in both directions and learns the
external source in FID3088. Earlier two-port LAN1/LAN3 probes received no ARP
replies while the Windows address was changing. LAN1's egress mirror captured
three requests and its TX broadcast counter increased by three. That
diagnostic harness's generic `replies` field counts captured requests; it is
not evidence of replies.

A fresh Windows readback subsequently confirms192.168.137.1/24 Preferred,
superseding the earlier link-local address169.254.206.204/16. Retesting that
current address gives three of three actual ARP replies in both the broadcast
learning and learned-unicast phases. All replies carry one0x8100/CVID35 tag;
Windows's MAC learns in hardware FID3088. The simultaneous CPU-conduit capture
contains zero marked unicast requests. Customer VID35 remains absent from the
VTU throughout. Two further three-request phases also pass with the per-source
C-tag transparency overrides disabled and restored, so this test does not
establish that those overrides are required. This accepts the bounded two-port
switch packet test, not automatic Linux/NSS support for tagged unaware bridges.

Direct LAN1 register inspection finds PHY BMSR0x796d with link and
autonegotiation complete, MAC status0x1fa at0x80200 (link bit8, full duplex,
1Gbps), RX/TX enabled, STP forwarding, learning enabled and LAN3 permitted by
the isolation mask. Neither loop protection nor 802.1X blocks it. This also
exposes a debug display bug: the status reader used control-register bit9
instead of status-register bit8 for link. The source now uses a separate
`YT921X_PORT_STATUS_LINK` mask without changing control-register bit9. This
display fix alone is committed as28d529d178. The changed debug object
cross-compiles for AArch64; it is not installed. Live
readback confirms link up, mirror register zero, no temporary clsact and no
UCI changes after restoration. Loop protection stays on. The final retest
restores all register/table snapshots, leaves eight NSS nodes with no
allocation failures, and preserves configuration hashes. Evidence:
`svid-lan-switch-current-ip/result.json`, `learned-fdb.txt` and `cpu.pcap`.

## Slice15: automatic private-SVID switch domains

The QinQ switch backend now programs private-SVID learning domains during
normal bridge joins, leaves and VLAN filtering changes. Unaware bridge ports
use their per-port SVID for lookup; their private VTU entries share the bridge
FID and contain only the bridge's eligible user ports plus the CPU ports.
Standalone/direct and aware ports retain C-VLAN lookup and egress membership
filtering. CPU ingress membership filtering is bypassed after setup; the four
existing rules still require the exact CPU conduit and destination SVID, and
unmatched CPU traffic remains denied. No additional ACL slots are consumed.

The shared update closes user ingress while rewriting roots, lookup selectors
and egress filters, flushes dynamic FDB entries, then restores the saved gates.
A partial I/O failure latches affected ports closed using the existing fault
path. Customer VID entries, native PVIDs and unrelated VTU fields are preserved.
The actual-C harness checks bridge peer expansion/shrink, independent FIDs,
aware/direct behavior, unrelated fields and all18 write,22 read and4 flush
failure points. Existing FID and MST regressions pass. Replacing synchronization
with a no-op, or removing shared fault closure, causes runtime assertion failures.
The changed driver objects also cross-compile with the actual AArch64 flags.

Build20 completes with the existing configuration and12 jobs reported by
`nproc`. Its sysupgrade SHA256 is
`28ad08eae6e95c029cf97efb171c1ac8abff75a0f24003c1c900461c60f658cb`.
Only `yt921x_driver.ko` differs among the compared modules; NSS, ECM, the tagger
and netifd match build19. The unstripped driver contains the new implementation
and reproduces the exact packaged bytes after normal OpenWrt stripping.
One sysupgrade installs build20 in slot1/mtd19, preserving verified build19 in
slot0/mtd18 and a private host backup. New boot ID:
`d557a8db-e94f-4609-9818-309821fca738`.

With Linux on LAN3 and Windows on LAN1/VLAN35 at192.168.137.1, the installed
driver passes these checks without manual hardware register writes:

| Normal Linux configuration | Actual ARP replies to three probes |
| --- | --- |
| Same unaware bridge, broadcast |3 |
| Same unaware bridge, learned unicast |3 |
| Separate unaware bridges, broadcast |0 |
| Separate unaware bridges, unicast |0 |
| LAN1 in a separate aware bridge admitting VLAN35 |0 |
| LAN1 restored to the original unaware bridge |3 |

Replies retain the customer0x8100/CVID35 tag. VID35 is absent from the VTU in
the final unaware configuration, and private roots restore automatically.
The existing STP forward delay is observed before the rejoin packet check.
CPU8 remains at2.5Gbps, user/CPU frame limits remain1522/1526, loop protection
stays on, configuration hashes match, and eight NSS nodes remain without
allocation failures. The link-status display fix is confirmed on the installed
driver (`link=1`, speed1000).

After Windows returns to WAN, a hardware mirror first finds untagged DHCP
Discover packets answered by Windows with VLAN35-tagged Offers. Fresh Windows
VLAN0 readback resolves that endpoint mismatch; the initial DHCP timeout is
retained separately and is not a firmware throughput result. The temporary
mirror is removed, and the router's configuration is preserved.

Native routed NSS then reaches936.59/936.40Mbps forward/reverse with
98.55/98.33% CPU idle,774371/755974 firmware TX increments and two active ECM
connections in each direction. Installed module hashes, fallback build19,
configuration, CPU link speed, frame limits and protection settings pass.

One55s TCP session also completes through netifd-driven LAN1 detach, standalone
`lan1.35` creation/removal and bridge rejoin. Every post-change sample has
accel_mode2 and increasing firmware counters; the node count follows8/9/8/8
with no allocation failures. The conduit, LAN2 and LAN3 retain flags0x1303,
and continuous netlink capture records zero administrative-down events for
those interfaces. No manual LAN restart or TCP reconnection is used.

This run averages884.84Mbps and has temporary throughput dips during reload,
including two one-second intervals near98.5Mbps around upper removal, before
returning to roughly936Mbps. The older build19 run averaged926.66Mbps in the
same test. These are individual runs, so they do not establish the cause or
size of a performance regression. They establish recovery at the sampled
times; uninterrupted acceleration and reload performance remain separate
optimization work. Configuration hashes match after restoration, temporary
interfaces and mirrors are absent, eight NSS nodes remain healthy, and loop
protection stays enabled. Windows is left on WAN/VLAN0 and Linux on LAN3.

This slice changes switch programming only. Tagged unaware-bridge traffic
through Linux/NSS still needs tagger admission and exact customer VLAN leaf
ownership; the switch packet results do not establish that host/firmware path.

Evidence: original ignored
`logs/nss-slices-20261106/slice15-svid-domains/`, especially
`source-checks.json`, `arm-build-candidate/result.json`, `build20-artifacts.json`,
`driver-strip-check.json`, `build20-lan-accept/result.json`,
`wan-wire-readiness/result.json`, `build20-native-accept-v2/results.json`,
`flow-reload20-accept/results.json` and `restored-final.json`.

## Slice16: customer VLANs on unaware bridges

An ordinary `br-lan.35` or `br-lan.4094` interface now works with a
VLAN-unaware DSA bridge and routed NSS acceleration. The tagger retains a
customer C-tag while stripping the synthetic native bridge tag. Tagged TX
keeps the customer TCI and adds the physical port's outer SVID. Standalone
upper ownership, aware-bridge membership, control traffic and the reserved
VID namespace keep their existing policies. This adds no switch-driver or
ACL change; full customer QinQ remains outside this two-tag transport.

The NSS controller adds a customer leaf for each member port's SVID, bound
to the exact registered bridge VLAN upper for MAC, MTU and reference
lifetime. Native leaves remain explicit, even with several customer leaves.
ECM accepts a one-level 802.1Q upper only when its current parent is this
port's actual unaware bridge and the VLAN core still publishes that exact
device. Stale leaves cannot authorize removed uppers. Unregister events use
structural classification because Linux unpublishes the VLAN first, then
release its nodes synchronously. Port removal also destroys customer leaves
under that port's root before releasing the root, including leaves owned by
a surviving bridge upper.

Actual-C checks pass for tagger RX89/0 and TX163/0, exact collector/resolver
mapping, upper publication, bridge/port separation, capacity, notifier
ordering, node references and IPv4/IPv6 retry cleanup. Fault controls fail
at runtime when customer leaves, unaware ECM admission, live publication or
bridge-upper event handling are removed. The old owner-only teardown also
fails. The changed tagger, combined controller/header and ECM code compile
with the real AArch64 flags. The generated0108 overlay applies without fuzz
and reproduces the reviewed ECM source exactly; the checks also pass against
the package source prepared by the full build.

Build21 uses the existing configuration, source HEAD9dbe023c77 plus the
reviewed changes frozen in `integrated-sources.json`, and12 jobs from
`nproc`. Its sysupgrade SHA256 is
`2203ff8d8ad732845411adcb3ca14a7ebda5e27ffb925ab9f0dca1e0d445ef82`.
Only `tag_yt921x_qinq.ko`, `qca-nss-qinq.ko` and `ecm.ko` differ from build20;
the switch driver, core NSS modules and netifd match. One sysupgrade installs
it in slot0/mtd18, preserving verified build20 in slot1/mtd19 and a private
backup. Boot ID: `7b0c9c51-3082-46a4-a9e4-edc6644a09b0`.

Windows stays on WAN/VLAN0 at192.168.137.1; Linux management stays on LAN3.
Normal temporary netifd interfaces create the tested bridge VLAN upper and
an additional `br-lan.100`, with LAN-zone membership. An isolated Docker
macvlan endpoint on the workstation's tagged Ethernet link supplies the
customer traffic. Each of the three LAN SVIDs gets both customer leaves:
14 NSS nodes coexist with the native leaves. Each traffic check requires a
new, exact accelerated data connection with the expected LAN3 and VLAN-upper
hierarchy, plus increasing firmware IPv4 counters. No manual register writes
or NSS arming are used.

| Installed build21 traffic | Forward/reverse Mbps | CPU idle forward/reverse |
| --- | --- | --- |
| Native routed baseline |936.57 /936.47 |97.98% /98.44% |
| Unaware bridge upper VLAN35 |936.70 /936.12 |97.22% /97.47% |
| Unaware bridge upper VLAN4094 |936.51 /935.83 |97.55% /97.69% |

The customer runs each add more than753000 firmware IPv4 TX packets. These
are individual runs, not a long-duration benchmark. Native traffic also
remains accelerated at936.5Mbps with both customer uppers present. Its first
post-reload CPU samples are lower:84.0%,79.0% and78.8% idle across three
configurations. A follow-up after an eight-second pause reaches97.0% idle
at936.54Mbps with the same14 nodes. This records the initial-versus-settled
observation; it does not establish the cause or eliminate reload optimization
as separate work.

All three temporary configurations remove cleanly: eight nodes remain,
creates33/destroys25, zero allocation failures and no pending retries. No
kernel fault or unregister-wait warning appears. Configuration hashes match;
temporary VLAN devices, Docker endpoints, qdiscs and mirrors are absent.
Customer VTU entries35/100/4094 are empty again. CPU8 stays at2.5Gbps, frame
limits stay1522/1526, private SVID lookup/filter policy restores, and loop
protection stays enabled. UART is closed without resetting the device.

Runtime acceptance here covers IPv4 routing/NAT through these unaware bridge
uppers and native coexistence. IPv6 traffic and full customer QinQ are not
established by the retry harness or these IPv4 tests. No release is created
as part of this acceptance.

Evidence: original ignored
`logs/nss-slices-20261107/slice16-unaware-upper/`, especially
`source-checks.json`, `0108-application.json`, the three `arm-*` directories,
`build21-artifacts.json`, `build21-native-accept/results.json`,
`build21-unaware-35/`, `build21-unaware-4094/`,
`build21-native-coexistence/` and `restored-final.json`.
