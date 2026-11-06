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
commit, push or release occurred during the resumed physical-STP test. Build12
and13 source changes remain uncommitted as recorded above; no claim of complete
DSA feature parity is made. Direct VLAN uppers, full customer-QinQ fallback,
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
