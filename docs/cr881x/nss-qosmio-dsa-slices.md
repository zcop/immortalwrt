# Native NSS/QinQ DSA implementation slices

2026-11-05, branch `nss-qosmio-qinq`, base `9cca0ec1e74d`.
This records execution of [the feature-audit plan](nss-qosmio-dsa-feature-audit.md).
The original Kuncy reference remains unchanged; its 30 saved source hashes match.
Source commits, image identity and device validation are recorded per slice.
Development now uses `nss-experiment`. The current experimental device image is
build31 on Linux6.18.60, with validated native30 preserved in the other slot and earlier
native/standard images retained as verified private raw backups. Slice22 accepts
installed-library capture on both images and a userspace capture package tested
from RAM. Slice24 records multicast/queue fixes and remaining scheduler/policy
limits; Slice25 records actual IPv6 acceleration and boot acceptance below.

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


## Slice17: standard DSA customer-QinQ fallback prerequisites

The new [fallback profile and source report](standard-dsa-qinq-fallback.md) select
the ordinary stmmac/legacy DSA path without NSS or private VLAN transport. Its
MTU arithmetic reserves customer S+C plus the primary CPU header; legacy TX
materializes customer metadata inside that header and preserves raw secondary
TX. Both actual-C harnesses, adversarial regression controls, expanded defconfig
and AArch64 production-object checks pass. The default NSS profile is unchanged.
No standard-mode image has been built, flashed or hardware-validated yet.

A bounded build21 lifecycle test exposed missing cleanup for the stats sysctl
and panic notifier on core reload. The restore safeguard recovered NSS/SSH;
the primary saved evidence and rebooted the same image to clear the warning
state. Native routed NSS then reached936.42/936.24Mbps,95.92%/97.72% CPU idle,
with active ECM and increasing firmware counters. Configuration hashes remain
unchanged. Full host-path restoration, lifecycle repair and a safe fallback
transition remain prerequisites for customer double-tag hardware acceptance.

Evidence: original ignored `logs/nss-slices-20261107/slice17-standard-fallback/`.

## Slice18: NSS core lifecycle cleanup

Build22 validates native NSS on the merged Linux6.18.60 tree. The full build
also catches the6.18.57 socket-bind API backport: ECM patch0109 selects
`sockaddr_unsized` and uses the real netlink address size, retaining the older
callback type for older kernels. No feed revision or firmware ABI changes.

Core patch0106 tracks common/platform ownership, unwinds late init failures,
unregisters stats sysctl and panic notifier registrations, stops coredump
producers and cancels their delayed work, drains IPQ50xx frequency work before
unmapping, and destroys its queues/debugfs. DMA-visible connection tables are
freed only after platform removal stops the hardware and IRQ/NAPI producers.
Symbol-only loads without an NSS common node have no cleanup side effects.
Core-local clamp/reset and clock sequencing are unchanged; GCC resets are not
pulsed. This is acceptance of the selected IPQ5018 profile, not exhaustive
teardown coverage for optional clients or other SoCs.

The actual production C passes84 checks under ASan/UBSan, four AArch64 object
compiles and a full image build. The baseline and deliberately omitted cleanup
steps fail at runtime. Prepared package sources reproduce all six reviewed
source hashes; packaged modules match the installed modules. LeakSanitizer is
disabled because it cannot run under the restricted runner's ptrace.

One ordinary sysupgrade installs build22 in slot1/mtd19, preserving verified
build21 in slot0/mtd18 and its private raw/config backup. Image SHA256:
`2d27d296b1de2363998019e43d0454d7eb25a0e19f5105e6c66493451125a268`.
Boot ID: `3ef2eb27-4ac4-4b30-8817-6ec6af568d8f`. The enhanced MDIO driver binds,
the LAN CPU link stays2.5Gbps, and eight native nodes appear automatically.

Two UART-controlled cycles remove ECM, the QinQ adapter, PPPoE and the core,
then reload them. With the core absent, the stats sysctl and NSS debugfs are
absent too. WAN host ping succeeds. LAN tests bind explicitly to
`enp2s0/192.168.2.100`:57 successful core-off ping/TCP/SSH samples in cycle1,
and22 in cycle2. One additional cycle2 sample overlaps the transition and
does not pass every probe. The old unbound ping is not a valid negative test:
the workstation routes192.168.2.0/24 replies over Wi-Fi through policy table200.
No workstation route or endpoint setting changes are made.

Cycle1 exposes a test-orchestration defect: the restore timer expires before
the UART unload starts and leaves a premature completion marker. The primary
restores NSS through UART and fixes the test to arm/verify its independent
45-second timer immediately before removal, rejecting stale markers. Cycle2
then completes its own reload in about21 seconds; its safeguard reports
`WATCHDOG_NOT_NEEDED`. Both reloads recreate eight nodes with zero allocation
failures/retries. The boot ID and taint4096 stay unchanged; no duplicate stats
entry, notifier warning, kernel WARN, Oops or refcount fault appears. The
existing dataplane-close-not-acknowledged messages occur during clamped-core
removal; host forwarding and reload still pass.

| Installed build22 native traffic | Forward/reverse Mbps | CPU idle forward/reverse |
| --- | --- | --- |
| Before removal |936.48 /936.48 |98.91% /98.07% |
| After both reloads |936.54 /936.51 |97.82% /98.55% |

Each direction has an active ECM connection and adds more than700000 firmware
IPv4 TX packets. These are short individual runs. Persistent network, wireless
and firewall hashes match; loop protection stays enabled. Full customer QinQ
and a standard-DSA boot remain separate gates at this point. The device tests
do not commit, push or create a release.

Evidence: original ignored `logs/nss-slices-20261107/slice18-nss-lifecycle/`,
including `integration.json`, `lifecycle-test-results.json`, `arm-core-results.json`,
`final-build.json`, `build22-artifacts.json`, both native acceptance directories,
`cycle1/`, `cycle2/`, `post-lifecycle-state.txt` and the UART capture.

## Slice19: standard DSA customer-QinQ hardware path

The [standard-mode report](standard-dsa-qinq-fallback.md#installed-standard-mode-validation)
now records an installed Linux6.18.60 image and actual customer S+C delivery.
An explicit disabled DSCP-extension kernel default fixes the non-NSS build;
native ECM retains its enabled override. The native build configuration stays
unchanged. UART stops build22's core before the standard sysupgrade can reuse
its memory. Build23 occupies slot0 and preserves tested native build22 in slot1.

LAN3 exchanges S3500/C35 in both directions with3/3 replies at ICMP data sizes
56 and1472. Two mismatched VID probes each receive0/2 replies. Independent
endpoint and CPU byte checks find24 matching frames each: both customer tags
and PCP5/3 survive,1500-byte IP datagrams are unfragmented, private RX port2
and TX mask0x4 identify LAN3, and the external frame has no private header.
No NSS acceleration is used in standard mode. These are one-port, low-rate
packet tests; multiport customer-QinQ isolation, DEI1, other tag combinations,
the secondary conduit and controlled host throughput remain separate gates.

Temporary router links, the exact input rule and isolated Docker fixtures are
removed; configuration hashes and workstation routing match. Earlier harness
errors are retained separately. The device tests do not commit, push or create
a release; the subsequent source commits are recorded separately.

The ordinary dualboot selection returns to native build22 without reflashing
it; standard build23 remains verified in slot0. Automatic NSS startup restores
eight nodes on boot `1c7709bd-9c88-4508-a854-2bf2d080045d`. Fresh routed TCP
reaches936.50/936.68Mbps with98.67%/98.30% CPU idle, active ECM connections
and increasing firmware counters. Configuration/module hashes match and loop
protection remains enabled. See `native-return-accept/` for this final check.

Evidence: original ignored `logs/nss-slices-20261107/slice19-standard-hardware/`.

## Remaining DSA/QinQ plan after Slice19

Six further slices group the remaining parity and acceptance work from the
original audit. This is a planning count; a failing test may require another
fix slice. Existing-flow reacceleration is already accepted in build19, and
routed policer/mirror checks already have device evidence. Their repetition
under stress belongs to the remaining matrix, not a new implementation claim.

| Slice | Remaining work | Acceptance gate |
| --- | --- | --- |
|20 | Complete standard-mode customer-QinQ packet coverage | Multiport separation and delivery, secondary raw conduit, DEI1 and additional S/C combinations; preserve customer tags and exact port identity |
|21 | Host-path load and MTU boundaries | Controlled2.5G host load, end-to-end9K where endpoints permit, correct oversize handling, bounded CPU pressure and clean recovery |
|22 | Capture integration accepted below | Installed-library native and legacy capture, live/offline filters and VLAN metadata; new package tested from RAM; CPU-bypassed visibility limits measured |
|23 | Bounded NSS node/topology stress accepted below |64-node capacity and promotion, real allocation retries and reuse, twelve bridge/VLAN waves with a surviving accelerated TCP session; failure cleanup checked with actual C |
|24 | Remaining policy and L2 parity | General egress-TC behavior, PCP queues,802.1X/storm policy during accelerated flows, multicast receive/leave, and two-member LACP/load distribution |
|25 | IPv6 and boot/recovery acceptance | Actual IPv6 acceleration and firmware counters, cold/warm boots, upgrade and native/standard transitions, preserved configuration and final restored traffic |

Wi-Fi NSS and adding PPP/tunnel acceleration are separate extensions. This
plan does not expand the vendor-private feature scope in `AGENTS.md` or retest
unrelated established DONE features solely to increase the slice count.

## Source commits after Slice19 acceptance

At the user's request, the accepted source changes are committed separately on
`nss-experiment`:

- `5c6f7307ebc`: adapt ECM's netlink bind callback to the Linux6.18.57 API.
- `48859b4a6ac`: unwind NSS core resources and add the actual-C lifecycle harness.
- `56a5448a00c`: default the platform's DSCP extension off without native ECM.

The production patch/config bytes match the hardware acceptance checkpoint.
Documentation corrects the stale existing-flow status using the already
accepted build19 result and records the six-slice remaining plan. Source
commits do not alter the installed build22 image. No push or release is part
of this commit request.

## Slice20: standard-mode customer QinQ packet matrix

The installed standard build23 (Linux6.18.60, primary `yt921x-enhanced`
private tag, secondary raw conduit) passed the customer-tag packet gates.
This validates standard-mode packet preservation; native NSS continues to
reserve its transport S-tag as documented earlier. No firmware rebuild or
new driver change was needed for this slice.

The primary operated a temporary LAN1↔WAN cable fixture at100Mbps/full, with
Linux on LAN3. Both cable ports were standalone for conduit tests. LAN1 then
joined `br-lan` for LAN3↔LAN1 delivery, isolation and bridge-group checks; WAN
remained a standalone physical receiver/sender. A userspace helper sent three
256-byte frames per direction, with unique cookies and sequence numbers,
custom MACs and a deterministic payload. Receivers rejected outgoing copies,
reconstructed VLAN metadata only when `PACKET_AUXDATA` reported it, and checked
the whole frame through a fixed deadline to detect duplicates.

| Outer TPID/TCI | Inner TPID/TCI | Both directions |
| --- | --- | --- |
|88a8/adac |8100/6023 |Passed: S3500/C35, PCP5/3, DEI0 |
|88a8/bdac |8100/6023 |Passed: outer DEI1 |
|88a8/adac |8100/7023 |Passed: inner DEI1 |
|88a8/fffe |8100/fffe |Passed: VID4094, PCP7 and DEI1 on both tags |
|8100/bdac |8100/7023 |Passed: C+C |
|88a8/0000 |8100/0000 |Passed: both zero TCIs |
|88a8/bdac |88a8/7023 |Passed: S+S |
|8100/bdac |88a8/7023 |Passed: C+S |

A separate Python oracle verified72 actual receive-file frames, including48
from the16 conduit-direction cases and24 from positive LAN forwarding cases.
The primary conduit capture shows LAN1 TX mask1 and RX source port0 while
preserving both customer tags. The raw conduit has no private9988 header.
Its legacy `DLT_DEBUG_ONLY` capture omits the outer tag removed into NIC VLAN
metadata on ingress; full-tag validation therefore uses the logical receiver's
actual auxiliary metadata rather than inferring that tag from expected input.
Capture/filter integration remains Slice22.

The LAN3↔LAN1 bridge matrix passed in both directions, using S3500/C35 with
PCP5/3 and DEI1 on both tags:

| Linux configuration | Received per direction |
| --- | --- |
|Same bridge |3/3, exact bytes |
|Both LAN ports isolated |0/3 |
|Isolation cleared |3/3, exact bytes |
|LAN1 detached from the LAN bridge |0/3 |
|LAN1 rejoined |3/3, exact bytes |

Four negative policy cases blocked all12 probes. This is functional packet
fate evidence; it does not independently establish which positive frames
bypassed the CPU entirely.

This artificial switch-to-itself fixture required recorded temporary policy.
Unknown destination MACs avoided collisions with the switch's shared FDB,
but normal CPU unknown-unicast filters also blocked them. Those CPU filter
bits were relaxed for the fixture and reapplied after normal bridge changes
recomputed the managed filters. User-port isolation remained enforced.
An initial attempt produced repeated IPv6 MLD frames and lost Ethernet
management before the next test packet was sent. UART stayed responsive;
the independent35-second watchdog held the test ports down. The hardware
loop detector stayed enabled throughout, with clear detection flags.
Temporary multicast bypass changes, distinct fixture MACs and disabled
IPv6 autoconfiguration on fixture interfaces kept later tests quiet. This
is not a completed multicast-policy or loop-detector acceptance test; those
remain with Slice24. Original policy, MACs, IPv6 settings, advertisements
and autonegotiation were restored before normal operation.

`tools/nss/qinq_packet_probe.c` contains the delegated and primary-reviewed
userspace fixture. Primary corrections cover final unpadded ancillary
messages and observed file length despite stale `fstat` metadata.
`python3 tools/nss/test_qinq_packet_probe.py --sanitize` passes576 independent
wire vectors,29 rejection controls and12 actual-C metadata/stale-size controls.
A NET_RAW-only isolated socket pilot also verified outgoing-copy rejection
and real VLAN auxiliary-data reconstruction for three stacked-tag profiles.
The tool and these checks do not configure the router or workstation network.

Evidence is retained under ignored
`logs/nss-slices-20261107/slice20-qinq-matrix/`: `packet-acceptance.json`,
the accepted per-direction results and receive files, conduit pcaps, command
audits, restoration logs and the separate fixture failures. The temporary
helper branch was removed after integration; its draft worktree remains
detached. Only `master` and `nss-experiment` remain as local branches.

Normal networking is restored with no UCI delta and unchanged persistent
network/wireless/firewall configuration hashes. The spare cable was removed
before normal LAN/WAN membership returned. The verified native build22 slot1
booted without reflashing on boot `efac1eab-5d41-4a32-9e86-772f780beed6`;
standard build23 remains verified in slot0. NSS and ECM started automatically
with eight nodes, no allocation failures and a2500Mbps CPU link. Fresh8-second
routed TCP tests reached936.66/936.28Mbps, with98.67%/98.44% CPU idle in the
sampled transfer windows, active ECM rules and769132/755983 additional firmware
TX packets. Module/binary hashes match; final kernel state has no WARN, Oops or
panic. See `native-return-accept/` and `native-final-state.json`.

Slice20 is accepted. Five planned slices remain:21 through25 in the table
above. This slice adds userspace test tooling and acceptance evidence; it does
not change driver/firmware source or create a release.

## Slice21: host load and runtime MTU handling

Native build22 and standard build23 were tested with Linux on LAN3 and
Windows on WAN, both external links at1Gbps. The CPU8 conduit remains at
2500Mbps. Temporary HTTP services, MTUs and packet captures are task-owned;
persistent network, wireless and firewall configuration hashes are unchanged.
This topology cannot establish sustained2.5Gbps host throughput.

A temporary LAN-only uhttpd/CGI workload transferred256MiB in each direction.
Native router-to-Linux measured174.34Mbps with37.65% average idle; the reverse
measured223.37Mbps with12.09% idle and a lowest sample of2.53%. Standard mode
measured271.18/224.51Mbps, with18.01/20.67% average idle. These include userspace
HTTP and CGI work and are CPU-pressure observations, not dataplane ceilings.
Native IPv4 forwarded counters did not rise during router-local transfers;
N2H queue drops and allocation failures had no increments.

A separate concurrent native test kept an already accelerated reverse flow
paced at200Mbps while uploading128MiB to the router. Upload measured220.13Mbps;
CPU idle averaged13.57% and reached4.46% in the lowest sample. Firmware TX
increased91622 packets during the upload. Fully overlapping one-second routed
intervals measured199.23–201.33Mbps, with199.93Mbps receiver average for the
whole routed test. N2H queue/allocation failure counters did not increase.
This proves the sampled paced flow survived host CPU pressure; it does not
prove line-rate host delivery or full2.5Gbps capacity.

### Native MTU receive bug and fix

The original native build accepted MTU9190 and9000, updated physical and VLAN
NSS interfaces synchronously, and programmed the expected switch frame limits.
That configuration success did not establish reception. Linux-originated IP
packets of2022 bytes passed and2023 bytes failed: Ethernet14 + internal S+C8 +
IP2022 + FCS4 is exactly2048 bytes. LAN3 and CPU8 counters showed switch
forwarding without CRC/drop increments. Failed sizes did not reach the logical
LAN capture, and neither NSS nor physical-interface error counters explained
them.

`nss_dp_ipq5018.c` set GMAC jumbo reception only from phylink's initial MAC
configuration. The native MTU handler updated the firmware and switch without
updating that bit. Patch105 calls the native MAC helper only after dataplane
MTU acceptance and the software MTU update. A per-port lock serializes MAC
register updates with phylink callbacks; speed, duplex, pause and RX/TX state
are preserved. Rejected dataplane requests leave MAC state unchanged. The
ordinary SSDK path remains unchanged.

Native build24, installed with normal sysupgrade to slot1 while standard23
remained in slot0, resolves the measured failure. A guarded sweep passed IP
lengths1500,2022,2023,2048,4000,8000 and9000. Packet decoding confirms both an
unfragmented request and reply at every sampled jumbo size. A separate test
passed three9000-byte echoes with switch RX/TX jumbo counters increasing by
three each and no CRC/drop increment. An attempted9001-byte DF packet at host
MTU9000 was rejected locally with `Message too long`. Both guards restored
host/router MTUs, switch frame limits, NSS node semantics and workstation
routes; no conduit restart or manual MAC register write was needed.

Windows advertised Jumbo9014 in its NIC settings but retained effective IPv4
MTU1500; it received router-originated9000-byte requests and replied as1500-byte
fragments. Switch WAN counters independently showed TX-jumbo3 and RX-jumbo0.
Those Windows exchanges are not counted as bidirectional jumbo or routed NSS
jumbo acceptance. Windows was restored to Jumbo Disabled/1514 and VLAN0.
Linux supplied the actual jumbo endpoint for the native LAN test. These are
bounded packet checks, not sustained-jumbo memory/load acceptance.

Native24's normal routed tests reached936.54/936.25Mbps with98.55/98.79% sampled
CPU idle, active ECM rules, and777426/756078 additional firmware TX packets.
Its packaged module hashes match the image; configuration and standard23
fallback hashes were verified. A private verified native24 slot backup was
saved before testing the separate standard-path correction.

### Standard MTU ordering correction and installed validation

The standard23 CPU conduit advertises maximum MTU9000, so a user MTU9000
request correctly fails because the private tag requires another8 bytes.
The tested effective user limit is8992. Requests at8992 were accepted but
showed the same IP2022/2023 receive cutoff, followed by complete restoration.
The upstream stmmac MTU handler reopened the MAC before storing the requested
MTU; core initialization therefore read the old value. Patch823 publishes
the new MTU before reopening and restores the previous value if reopening
fails. Validation and DMA-allocation failures keep the previous MTU.

`tools/nss/test_stmmac_mtu.py` extracts the actual handler and exercises XDP
rejection, FIFO rejection, DMA-allocation failure, reopening failure, live
increase, live decrease and an administratively down interface. The original
handler reproduces stale MTU visibility at MAC open; the patched handler uses
the requested value and preserves failure behavior. These seven scenarios
pass. Standard25 built successfully and was installed with a single guarded
sysupgrade to slot0 after NSS core shutdown. Native24 slot1 and its private
recovery backup were preserved.

On standard25 boot `7c863e63-9940-416e-b5d7-d7681fb4bb44`, the same sweep passed
IP lengths1500,2022,2023,2048,4000,8000 and8992. Captures confirm unfragmented
requests and replies at every sampled jumbo size. An8993-byte DF request at
host MTU8992 was rejected locally. Host/router MTUs, switch frame registers,
workstation routes and persistent configuration were restored. The CPU link
remained2500Mbps, loop protection stayed enabled, and no kernel WARN/Oops/panic
was found. Standard25's packaged files and preserved native24 partition hash
were verified; the standard partition hash is
`e62b9ca341f55bb3becc7f9cf530531e7f829db3fb34c34f4cf857c0120952ff`.

The native maximum was also measured rather than inferred from the API:
IP lengths9000,9001,9018,9100 and9190 passed with unfragmented request/reply
captures at user MTU9190. User MAC frame limits were9212 bytes and CPU8's limit
was9216. This was another bounded packet test with complete restoration.

Evidence: ignored `logs/nss-slices-20261108/slice21-host-mtu/`, including
size-probe command audits and pcaps, explicit restoration results, native24
artifact and recovery manifests, host-load records, focused C-harness output
and separately retained failed prerequisites/fixtures. The native link-restart
attempt closed DSA user ports and obtained no jumbo measurements; UART
restored those ports, and that attempt is not counted as a pass. External
review findings are accepted only after primary source/build checks.


Normal native operation was restored without reflashing after the standard25
test. Final boot `01faf66b-a54a-4910-bf29-87c75bc4b008` is native24 slot1;
standard25 remains verified in slot0. Fresh routed tests measured936.52 and
936.64Mbps, with98.55% and99.16% sampled CPU idle, active ECM rules and
777232/753786 additional firmware TX packets. Final checks confirm eight NSS
nodes with no pending retries or allocation failures, restored1500-byte user
and1508-byte conduit MTUs, original frame limits and configuration hashes,
normal ath11k module loading, loop protection enabled, and no kernel
WARN/Oops/panic. Windows remains WAN/VLAN0/Jumbo Disabled; Linux remains LAN3
at MTU1500. No temporary HTTP service or task MTU guard remains active.

The host-pressure and bounded MTU packet gates are accepted on this topology.
Sustained2.5Gbps host load, sustained jumbo-buffer pressure and true routed9K
acceleration remain unmeasured: the available external links are1Gbps and the
Windows endpoint cannot originate full-size jumbo replies. These limits are
not inferred as either pass or fail. The remaining functional plan is slices22
through25; those capacity checks are retained explicitly for suitable hardware.
Native24 predates the separate standard-kernel patch; standard25 validates that
patch. Neither image is a new release. The native top-level build configuration
is restored; the prepared kernel cache last built standard mode, so future
native builds must rebuild against the native configuration.

## Slice22: installed capture, normalization and metadata

Installed libpcap1.10.7/tcpdump4.99.7 were validated on native24 and standard25.
The installed libpcap hash on both images is
`c2a3e4daad6dbd7893215566ca8de526ff4b2b92919a44ad492390439c845947`.
Native primary captures use EN10MB and retain the transport tags. Standard
primary captures use DEBUG_ONLY303 with the private8-byte header; the standard
secondary conduit is raw Ethernet despite reporting the tree's enhanced tagger
in sysfs. Ordinary protocol filters cannot compile against the installed
DEBUG_ONLY link type. Logical user-port captures use ordinary Ethernet.

| Installed-library check | Result |
| --- | --- |
| Native primary, nested S3074/C3088+ICMP filter |6/6 request/reply packets, live and offline |
| Native source-MAC filter |3/3; wrong-port and plain untagged-ICMP controls matched0 |
| Native logical LAN3 |6/6 untagged request/reply packets |
| Standard primary, explicit byte-offset filters |6/6; source bytes selected3/3; RXport2/TXmask4 preserved |
| Standard logical LAN3 |6/6 with normal ICMP filters |
| Standard primary, customer S3500/C35 |3/3 exact264-byte frames including the private header; both TCIs preserved |

Accepted captures have zero kernel drops. An early immediate-mode capture used
an unnecessarily large snapshot and too few ring slots; another broad source-MAC
diagnostic captured its own SSH transfer. Their drop/missing-packet results
were rejected. Bounded, narrow-filter reruns with256-byte snapshots passed;
neither failed fixture establishes a network data-path defect.

`tools/nss/yt921x_capture.c` adds explicit raw, QinQ and enhanced modes. It
reconstructs VLAN tags only from actual auxiliary metadata, writes paired raw
and normalized pcaps, and retains full private bytes plus decoded port/mask
metadata in an aligned JSONL file. Enhanced reconstruction inserts the tag at
offset20; raw/QinQ uses12. Ordinary BPF runs against normalized Ethernet in
userspace. It preserves zero TCIs, PCP/DEI, unknown private codes and complete
captured bytes. It refuses output overwrite and reports malformed records,
capture drops and unmet positive counts as failure. It does not change network
configuration, promiscuous mode, acceleration or mirroring. Usage and limits
are in [yt921x-capture.md](yt921x-capture.md).

The utility was drafted by a bounded Qoder helper with DeepSeek-Flash/max
verified in its interactive UI. Primary review corrected JSON punctuation,
deadline handling, test fixture path collisions and an output-open cleanup
leak. The host offline suite and actual-production-C ancillary harness pass
ASan/UBSan/LSan against the target's pinned libpcap1.10.7. Six byte-exact AUXDATA
vectors cover raw/QinQ/enhanced reconstruction, including zero TCI and inner
tag preservation; malformed ancillary controls are rejected. The host's older
libpcap1.10.5 cannot write DEBUG_ONLY303 and is not an accepted substitute.

The new `yt921x-capture` OpenWrt package builds with warnings treated as errors;
both native and standard source profiles select it. APK SHA256:
`f6dcf053b295394d8108251800e793695321d1c03cf1bfc76e76f779073c51e7`.
Its extracted executable matches the package staging binary, SHA256:
`caca63337793297d1f3d938677111ad0aa5f97f23cc5e85b9692cc0726c3507c`.
The package executable was copied to RAM for live acceptance; no APK was
installed and no firmware was flashed for this userspace-only change. The two
existing images contain the validated libpcap patch, not this new utility.

On standard25, the package executable captured three double-tagged customer
frames through the enhanced primary and six ordinary ICMP frames through the
raw secondary, without malformed frames or capture drops. Separate AArch64
offline and live byte checks confirm exact private-header removal, both
customer TCIs and RXport2. With Windows temporarily on VLAN35, the raw WAN
test received three real RX auxiliary records with TPID8100/TCI35; both output
pcaps contain that actual reconstructed tag and report no drops. Enhanced
offset20 AUX reconstruction is covered by the actual-C harness; it is not
claimed as a newly observed hardware-AUX case. Full-tag primary packets also
exist, so legacy captures do not universally lose a tag. Older savefiles that
already omitted metadata cannot recover it through normalization.

Windows was restored to WAN/VLAN0 and SharedAccess restarted. Standard25
remained in slot0; a hash-verified slot transition returned to native24 slot1
without flashing. Final native boot:
`aab8d974-9ed0-4e1e-b904-5b161e3463a3`.

The package's native QinQ capture collected all six marked echo packets with
exact S3074/C3088 framing. During a separate13-second capture containing an
8-second routed transfer, all six positive-control echoes were visible but
only96 TCP packets were captured. The firmware's global IPv4 TX counter grew
by764525 packets, ECM had two active rules, receiver throughput was934.08Mbps
and sampled CPU idle was98.73%. A fresh reverse transfer reached936.64Mbps
with754573 additional firmware TX packets. Both captures report zero drops
and malformed frames. The duration-only capture's `timeout:true` is its normal
bounded completion. The global firmware count is not an exact per-flow count;
the evidence demonstrates that CPU packet sockets cannot observe the bulk NSS
fast path. Switch-only forwarding also needs its own counters or a configured
mirror for packet visibility.

Original configuration hashes, empty UCI changes, normal1500/1508 MTUs, eight
NSS nodes, normal ath11k loading and enabled loop protection were preserved;
no kernel WARN/Oops/panic was found. Evidence is retained under ignored
`logs/nss-slices-20261108/slice22-capture/`, including command audits, pcaps,
aligned metadata, package/source hashes and rejected fixtures. Slice22's
functional capture gates are accepted; Slices23-25 remain. Capacity limits
recorded under Slice21 still need suitable external hardware.

The additional bounded Agy source review timed out at180seconds without a
verdict and is not counted as an independent review pass. No source changes
or scope violations were detected in that review workspace. Primary source
inspection, sanitizer checks, package verification and device acceptance are
the evidence for this slice.

A late physical-link event occurred after the throughput checks: WAN went
down at uptime291.68seconds and returned at100Mbps/full at301.04seconds.
Read-only `ethtool wan` then showed local1000baseT advertisement intact but
partner advertisement limited to10/100. WAN ping and DHCP remain working;
LAN3 is1000Mbps and the CPU conduit remains2500Mbps. No port configuration
or advertisement was changed by these capture tests. The cause of the late
WAN renegotiation is unconfirmed. After the user requested another check, WAN
was again1000Mbps/full, with1000baseT partner advertisement and three ping
replies without loss. The log records its return to gigabit at uptime605.75s;
no router setting was changed to obtain that result. The934/937Mbps readings
precede the link event. Test RAM files were removed and UART closed without
reset.

## Slice23: bounded node capacity, allocation recovery and topology churn

Slice22 is committed on `nss-experiment` as `e2cc1209f68`. Slice23 uses the
unchanged installed native24 image on boot
`aab8d974-9ed0-4e1e-b904-5b161e3463a3`; no production driver change, firmware
flash or manual NSS restart is needed for these checks. Standard25 remains
in slot0. Linux stays on LAN3 and Windows on WAN/VLAN0. LAN1/LAN2 have no
cables; loop protection stays enabled. Each temporary topology has an
independent restoration watchdog, and persistent UCI configuration is untouched.

### Adapter capacity and promotion

The glue caps its table at64 nodes; this differs from the firmware's shared
dynamic-interface pool. With the normal eight nodes present,60 registered
customer VLAN uppers on standalone LAN1 request60 additional leaves. The
actual table admits56, reports its capacity warning and omits four. All live
interface IDs and `(SVID, VID, phys_if)` keys remain unique, every leaf has its
correct root, and the other ports' nodes retain their IDs and parents.

Deleting eight admitted leaves lets all52 remaining uppers obtain nodes,
including the four previously omitted VIDs. Removing all test uppers and
returning LAN1 to `br-lan` restores eight nodes. A second capacity run proves
native routed traffic still accelerates with the table full:936.56Mbps and
772074 additional firmware IPv4 TX packets at64 adapter nodes. Cleanup again
restores the original topology. No tagged endpoint is attached to the spare
port, so packet forwarding through an omitted customer leaf is not claimed
as tested by this table-capacity fixture.

### Real transient allocation failure and slot reuse

Sixteen cycles create and delete eight customer leaves on disconnected LAN1.
All128 requested leaf allocations complete across119 distinct interface IDs,
demonstrating nine reuses. At cycle14 the actual firmware-interface allocator
fails six times. Kernel logs show retries1-6 at the existing2000ms interval;
the desired nodes subsequently appear without a resync command or reload.
No give-up, deallocation failure or unregister-wait warning is observed.

The test restores eight nodes with `creates=208`, `destroys=200`,
`alloc_failures=6`, and `retries_pending=0`. The failure counter is cumulative
evidence of the exercised path, not six outstanding errors. Native management
and WAN ping remain reachable, and configuration hashes match. This is real
allocation-retry recovery, not injected firmware behavior; the precise reason
the firmware temporarily withheld those IDs is not established by its counter.

### Surviving TCP flow during bridge and VLAN churn

One60-second TCP connection spans twelve waves on the disconnected spare ports:
join an isolated VLAN-aware bridge with VIDs1100/1101, replace1100 with1102,
detach both ports to standalone state, and return them to `br-lan`; repeat
three times. The adapter alternates between ten and eight nodes. Each wave
checks uniqueness and parent identity, preserves LAN3/WAN node IDs, and
requires the original TCP client port35003 to be accelerated again with
increasing firmware TX counters.

All twelve waves pass. The same socket completes60.00seconds, averaging
890.86Mbps with median measured intervals936.01Mbps, minimum561.44Mbps and
1358 TCP retransmits. Firmware TX grows2341468 packets between the pre-test
snapshot and wave11; every adjacent wave adds more than10000 packets. This
establishes repeated reacceleration and TCP survival at the sampled times;
the retransmits and reduced average preclude a zero-loss or interruption-free
claim. No additional allocation failures occur during this sequence.

ECM can rebuild a connection from a return packet and reverse its displayed
source/destination orientation. The accepted oracle normalizes both directions
of the exact endpoint/port tuple; a direction-specific early matcher incorrectly
missed an already accelerated flow. Recorded initial and after-wave dumps
prove the same client port in both orientations. Fixture corrections also
cover an ECM device node initially placed under nodev `/tmp`, duplicate
`serial` construction, and iperf's `start.connected` JSON location. The completed
twelve-wave run was accepted from its saved ECM/counter/iperf records after
the last JSON-path correction, without repeating its traffic. Earlier fixture
stops and a temporarily unavailable Windows listener are retained separately
and are not treated as driver failures.

### Actual-C failure cleanup checks

`tools/nss/test_qinq_allocation.py` extracts the production create/destroy,
parent matching and resync functions. A bounded fake API ledger injects
allocation, registration, MAC, MTU and add-tag failures. It checks unpublished
readiness before configuration ACK, reference and registration balance,
preservation of an unrelated node, root-before-leaf creation, child-before-root
release, shortage recovery and idempotent resync. Independent fixture values
also check the firmware tag and next-hop arguments. The collector and scheduler
are mocked here; real capacity/retry behavior is covered by the live tests above.

Qoder drafted the single test file with DeepSeek-Flash/max verified in its
interactive UI. Primary review corrected an undeclared helper and an event-order
assumption, matched Linux Kbuild's `-fno-strict-overflow` semantics, and
strengthened registration and firmware-tag checks. ASan/UBSan/LSan pass. The
negative control removes only the extracted register-failure `dev_put()` and
fails the actual runtime reference ledger; production source is untouched.
The existing actual-C lifetime checks also pass. The old combined mapping test
passes its controller portion but cannot run its ECM portion because the
prepared ECM source cache is absent after the standard build; that full suite
is not counted as a new pass. No production change requires a firmware rebuild.

After the completed traffic and capacity checks, eight nodes remain with
`creates=306`, `destroys=298`, `alloc_failures=6`, `retries_pending=0`. These
bounded table, firmware reuse and reference-ledger checks found no stale nodes
or double release; they are not an unrestricted kernel memory-leak or concurrency
proof. Original configuration, MTUs and port membership are restored. Evidence:
ignored `logs/nss-slices-20261108/slice23-topology/`, including individual
node/counter/ECM snapshots, failure/retry logs, completed iperf JSON, watchdog
records, sanitizer output and rejected fixtures. The remaining functional
plan is Slices24-25; Slice21's external capacity limits remain separately open.

Final acceptance verifies the installed build24 module/binary hashes and original
configuration, bridge membership and1500/1508 MTUs. WAN/LAN3 are1000Mbps and the
CPU conduit is2500Mbps; ath11k and loop protection remain enabled. A fresh
reverse NSS transfer reaches936.64Mbps with98.67% sampled CPU idle, two active
ECM rules and753684 additional firmware TX packets. No kernel WARN/Oops/panic
or unregister/deallocation fault is found. Test guards have exited, exact
task-owned RAM files are removed, and UART is closed without reset. The helper
draft remains in a detached worktree; local branches are only `master` and
`nss-experiment`. Source changes in this slice are limited to the new host
harness and this acceptance record.


## Slice24: multicast, queue corrections and remaining policy limits

Slice23 is committed as `a1d16b94e97`. The following driver corrections on
`nss-experiment` have source and bounded hardware evidence:

- `660987c8cf12`: fix(yt921x): preserve known multicast when flooding is disabled
- `798a90c44c24`: fix(yt921x): correct priority-to-queue register field order
- `86c58a18b850`: fix(yt921x): separate packed queue and scheduler group indexes
- `2be9b8e271c8`: fix(yt921x): remove excess budget from single-rate queue TBF

 The current image is native30 on Linux6.18.60; packet acceptance for
each correction is identified below rather than inferred from a build pass.

### Known multicast groups survive unknown-flood-off

Windows joins239.24.0.1 through a real UDP socket on LAN1/192.168.2.169. The
LAN1 MDB entry reports offload, but native24's `mcast_flood off` suppresses both
the joined and unknown groups. Register A/B identifies the cause: the driver
puts the user-port bit in both unknown-multicast and all-multicast drop masks.
Clearing only the all-multicast user bit restores64/64 known-group egress while
the unjoined group stays blocked.

The correction strips DSA user-port base bits from the all-multicast mask,
retaining CPU/reserved bits and the separate storm mask. Unknown multicast,
router exclusions, broadcast, unknown unicast, write ordering and errors stay
unchanged. The actual production-C harness covers the masks and I/O faults with
ASan/UBSan/LSan; the old function compiles and fails semantic checks.

Installed native26 confirms joined-group delivery with flooding disabled. When
the user closes the actual socket, the MDB entry disappears: former-group
traffic has0/64 egress with flooding disabled and64/64 when re-enabled. Native29
retention uses Windows's existing SSDP membership:64 joined probes produce65
LAN1 multicast TX frames including one background frame, while64 unknown probes
produce0. Source ingress counters match the probes. Windows application
consumption and precise leave latency are not measured. The test socket is
closed and original flood flags are restored.

### Correct priority-map fields and queue indexes

Vendor metadata places UC priority0 at bits2:0 and priority5 at22:20; MC
priority0 is at1:0. The old shift macros reverse both layouts. The correction
uses UCshift4*priority and MCshift2*priority. An identity UC map becomes
0x76543210. The actual-C harness tests apply/readback, literal identity and
asymmetric expectations, reserved bits and I/O faults. Sanitizers pass; the old
header compiles and fails semantic checks.

On native29, valid customer-VID35 IPv4/UDP probes verify PCP0/5 queue selection
on physical LAN1/port0. Windows stays untagged on LAN1, and Linux stays on LAN3
with its untagged management membership present. A temporary static destination
FDB entry and unknown-unicast flood-off distinguish known delivery from
flooding. Every phase refreshes that entry, checks a64-frame1Mbps positive
control and verifies the complementary queue is disabled.

| Selected5Mbps queue | Matching PCP delivered /9765 offered at40Mbps | Other PCP delivered /9765 |
| --- | --- | --- |
|q0 |PCP0:1248 |PCP5:9765 |
|q5 |PCP5:1247 |PCP0:9765 |

Known-destination controls deliver64/64; a distinct unknown MAC delivers0/64.
CPU-port TX increments remain16–24 during the large probes rather than tracking
the9765 frames. The uncontrolled dynamic-FDB fixture could not separate queue
selection from flooding; no dynamic-learning defect is inferred from it. An
earlier native28 run failed its final unrestricted control and is not counted
as acceptance. Original VLAN/PVID, FDB, flood, qdisc and touched registers are
restored exactly. Final normal LAN TCP reaches941.21Mbps with zero retransmits.

Vendor `fal_tiger_qos_queue_id_get()` uses packed queue indexes: UCport*8+qid,
MC88+port*4+qid. The MC boundary follows the explicit11-port definition. The old
Linux shaper uses port*12. On native27, normal TC on LAN3/physicalport2 programs
index24 and leaves a40.10Mbps offer unrestricted; copying the same profile to
index16 reduces delivery to9.17Mbps, and restoring16 returns40.10Mbps.

Final source uses `YT921X_QOS_QUEUE_FLOW_INDEX` for shapers, shaper telemetry
and ETS flow weights. The separate legacy mode-table index is retained. Native29
normal `mqprio`/TBF, without manual profile copying, measures40.11Mbps unshaped,
4.46Mbps at requested5Mbps and9.17Mbps at10Mbps on LAN3q0. Readback identifies
index16. Exact restoration returns40.09Mbps. Multicast queue scheduling, every
modeled port/queue and general DWRR behavior are not established by this test.

### Single-rate queue TBF

Mirroring CIR/CBS into EIR/EBS admits about twice the requested rate. With a
controlled FDB and a low-rate positive control for each phase, the old5Mbps
profile delivers9.91Mbps. Keeping both enable gates and zeroing excess WORD1
reduces delivery to5.05Mbps; restoring the mirror returns9.93Mbps. Disabling
either gate instead bypasses the limit, so a single-gate change is incorrect.

The correction keeps CIR/CBS, both gates, conversion, write order and rollback,
but writes EIR/EBS0. The actual-C raw-helper harness checks that profile, queue
index, argument/conversion/read failures and every write-failure rollback.
Sanitizers pass; the old mirrored-rate substitution compiles and fails. The
native29 normal-TC5/10Mbps results above accept the installed single-rate
behavior for that TCP fixture.

### Final ETS index correction and open scheduler work

Queue flow/shaper tables and DWRR mode tables use different index spaces. The
first native28 shared-index draft was too broad. A later correction preserved
mqprio and mode telemetry, but final static review caught another shared ETS
local variable: its packed flow index was also reused for mode writes. The
primary confirmed the finding and split `flow_idx` from `mode_idx`. That review
timed out and is not a completed approval; its concrete finding is accepted
through independent source inspection and checks.

The index harness also compiles the actual ETS apply function with named fake
band/weight/register dependencies. Independent flow/mode address oracles cover
all modeled ports, weighted/strict/unassigned bands and each I/O-error prefix.
The pre-fix native29 source compiles and fails mode-address checks. The corrected
source passes ASan/UBSan/LSan. Existing SP-write semantics are deliberately
outside this oracle; it does not prove hardware scheduling.

Installed native30 ordinary `tc ets` on LAN3/port2 uses the packed flow range
16–23, updating weighted queues20–23, and mode entries24–31. Adjacent mode
entries16–23 and32–35 stay unchanged. Four strict bands and quanta
256/512/768/1000 exercise these writes. An initial100/200 expectation was
rejected because the real kernel clamps quanta to a minimum256. Exact
register/qdisc/config restoration passes after each run. The flow/mode-address
gate passes; complete ETS acceptance does not.

The separate SP assertion reads0xdeadbeef at0x300408. Vendor metadata identifies
0x300400 as the single-entry `flush_cfg`; the existing Linux macro instead
assumes a per-port SP bitmap there. Vendor queue priority uses C/E priority
fields in the packed flow table, and mode groups resolve by priority. A second
bounded external source investigation completes without scope violations and
corroborates this existing discrepancy, which the primary checks against the
original vendor files. This is a Linux mapping issue, not missing hardware.
The next scheduler fix must replace that SP assumption, resolve priority groups,
preserve vendor queue-update ordering and test actual strict/weighted contention.

### Image, review and restoration

Native29 SHA256:
`dd34d2d7343b008501911334014a5b98c9c51d5a1039b54a2fbda847d5a70ba9`.
Native30 SHA256:
`d5dea4a327c61b7402cb7c5c25e3b2a2635bb711f7b359472c501d3f12cbcc61`.
The final `make -j12 V=s` build passes;12 is the current `nproc` result. A normal
sysupgrade installs native30 in slot0, preserving native29's slot1 raw hash.
All six installed module hashes match. The production source files match the
frozen build; a later test-only scope clarification does not change the image.

Qoder supplied bounded multicast, qmap and index/rate drafts; the primary
reviewed every diff, corrected the shared-index scope, ran checks and owns
hardware acceptance. Agy supplied bounded source investigations. Helpers did
not operate the device. Detached drafts and original Kuncy reference remain
preserved; local branches remain `master` and `nss-experiment`.

A source-address-only LAN test initially followed the workstation's existing
Wi-Fi policy route. Binding iperf to enp2s0 fixes the fixture without a route
change. The first native26 upgrade wrapper also failed before writing either
slot because it removed the image after procd closed ubus. Unchanged raw hashes
and UART exposed that failure. The corrected wrapper execs normal sysupgrade
without post-handoff cleanup; subsequent upgrades record actual UBI writes.
All rejected controls, source manifests, private recovery hashes, UART and exact
restoration ledgers remain under ignored
`logs/nss-slices-20261108/slice24-policy-l2/`.

### Remaining policy and peer limits

On native24, existing WAN802.1X raw RX/TX gates each stop an established NSS
flow for two seconds and recover. Unblocked intervals reach at least934.28Mbps
for RX and901.78Mbps for TX. This accepts the raw gates; bridged EAP/auth
integration remains partial as recorded in AGENTS.md.

General hardware egress flower rejects `skip_sw` drop with EOPNOTSUPP and an
accurate extack. Software-only egress drop counts0 packets while NSS traffic
continues at least905.98Mbps. It therefore does not enforce this policy on the
bypassed bulk path. Ingress flower, egress mirrors and shapers retain their
separate accepted paths.

The software storm guard activates its source-port bit at a low100pps
threshold, but64/64 subsequent source-LAN3 multicast frames still leave LAN1.
Source containment is unproven by that destination-filter mask. Original guard
settings and four filter registers are restored; loop protection remains on.
This finding concerns the software approximation, not the established vendor
hardware storm-rate feature, and is not routed-NSS storm-policy acceptance.

Open gates are the scheduler correction above, software storm containment,
bridged802.1X, broader dynamic-learning/multicast scheduling, and real two-member
LACP/load distribution. The user has only the current one-NIC computers, so the
LACP peer is unavailable. Slice21's external2.5G/9K limits remain separate.

## Slice25: IPv6 acceleration, cold boot and ordinary reboot

The installed native30 image passes actual routed IPv6 in both directions.
An isolated IPv6-only macvlan endpoint on the Linux workstation uses
`fd42:25:1::253` through physical LAN3. The router temporarily receives
`fd42:25:1::1/64` on br-lan and `fd42:25:2::2/64` on WAN. Windows on WAN uses
`fd42:25:2::1/64` with a return route for the test LAN prefix. There is no NAT66
or firewall-rule change. The workstation's own addresses/routes stay intact.
The existing IPv4 Windows server and native QinQ topology remain present.

Initial IPv6 throughput is923.71/923.67Mbps for LAN→WAN/WAN→LAN, with two
active IPv6 ECM rules and98.56% sampled aggregate CPU idle. Firmware IPv6 TX
increments789629/753035 and hash-hit increments789631/753037 establish actual
firmware forwarding alongside the traffic result. The initial read-only
container could establish control but lacked writable temporary memory for
libiperf stream setup; that rejected fixture was restored. Adding an8MiB tmpfs
fixes the endpoint without a router-source change.

The user then physically powers the router off for10 seconds. After the cold
boot, the primary verifies a new boot ID, image/module hashes, slot0 boot-success
flags, unchanged native29 slot1 hash and original configuration. Eight QinQ
nodes start automatically, with zero allocation failures or pending retries.
No manual module/service startup is used. Both IPv4 and IPv6 traffic pass.

Next, an ordinary `reboot` is issued with NSS still loaded. This differs from
the guarded sysupgrade path, which unloads NSS before handoff. The ordinary
reboot again starts eight nodes automatically and passes both protocols. The
built IPQ50xx driver already asserts its core-local reset clamp and orders it
before copying firmware; this source evidence is corroborated by the measured
warm boot, rather than substituted for it.

| Boot | Protocol | LAN→WAN Mbps | WAN→LAN Mbps | Sampled CPU idle |
| --- | --- | --- | --- | --- |
|Physical cold boot |IPv4 |936.57 |936.24 |98.4–98.5% |
|Physical cold boot |IPv6 |923.69 |923.27 |97.7–98.2% |
|Ordinary warm reboot |IPv4 |936.65 |936.62 |98.9% |
|Ordinary warm reboot |IPv6 |923.67 |923.39 |97.9–98.7% |

Every row has two active ECM rules and more than750000 additional firmware TX
packets; each IPv6 run also has more than750000 additional firmware hash hits.
These are single bounded runs. Cold boot ID:
`c9b70101-0b0e-482a-87d8-dbb4e4b231ea`; subsequent ordinary reboot ID:
`a0d24f93-a0df-4a63-88c0-f867710cd731`. No kernel WARN/Oops/panic appears.
User-facing MTUs remain1500, conduits1508, WAN/LAN3 links1Gbps and CPU conduit
2.5Gbps. Persistent configuration and loop protection are preserved.

Each temporary IPv6 fixture has an independent router restoration guard and
removes its Docker endpoint/network and both router test addresses afterwards.
The user confirms removal of the temporary Windows address and return route.
Windows remains on WAN/VLAN0, Linux on LAN3; normal IPv4 DHCP/NSS operation is
restored. Evidence is in ignored `logs/nss-slices-20261108/slice25-ipv6/`,
including per-boot module/fallback/config audits, packet counters and fixture
restoration records.

This accepts native IPv6, one physical cold boot, one ordinary warm reboot and
the guarded native29→native30 sysupgrade with preserved configuration. It is
not a repeated power-cycle stress matrix or a newly forced fallback/recovery
boot. The earlier native/standard image transitions remain the evidence from
Slices21–22; a fresh native30-to-standard transition has not been performed.
Slice24's scheduler/policy and external-peer limits and Slice21's external
capacity gates therefore remain open. Wi-Fi and PPP/tunnel extensions remain
outside this plan's accepted traffic scope.

## Slice26 in progress: scheduler priority groups

The preceding corrections and Slice25 report are committed through
`b72fa9efff32`. This slice's scheduler source is committed as `fc662a4ed9a2`; its
strict/weighted packet acceptance is **not passed**.

### Source correction and checks

Vendor source establishes three distinct concepts: a global queue flush
command at0x300400, per-queue C/E priorities and weights in table0xe6, and
per-priority-group byte/packet units in tables0xe7/0xe8. The mode bit is0 for
bytes and1 for packets; it is not a DWRR enable. Equal priorities share DWRR,
while distinct priorities select strict precedence. The candidate removes the
invented per-port SP bitmap and maps these controls explicitly.

Plain mqprio and ETS teardown plan a fair priority0/unit-weight baseline for
the12 UC/MC flows. ETS preserves MC flow words, assigns weighted UC bands to
group0 and distinct priorities to strict bands, and proportionally scales
weights exceeding the10-bit field. Group addresses use the assigned priority.
The hardware convention that higher numeric priority wins remains unaccepted
by packet evidence; reversing the tested priorities did not reverse the winner.

A transaction snapshots flow, group-unit and enqueue words before writes,
disables enqueue, flushes changed priorities with a20ms polling bound, programs
the plan and restores enqueue. Error cleanup re-quiesces queues, attempts
flush/state/enqueue restoration and retains the first error. Multiple persistent
I/O failures can still prevent restoration; host mocks are not a hardware
recovery guarantee. The flush command is never replayed as saved register state.

Telemetry retains names/count/order, derives configured precedence and shared
UC/MC-group counts from real flow words and returns zero on unreadable,
invalid or split-priority state. These describe configured groups, not proof
that the hardware delivered the requested scheduling behavior.

Qoder supplied a bounded five-file draft in an isolated worktree, with
DeepSeek-Flash/Max verified before submission and individual edit approvals.
The primary corrected polling return/status handling, C/E comparison, rollback
ordering, shared-group telemetry, macro declaration order and harness defects.
The source extractor now skips call sites and preserves struct terminators.
The draft is preserved; helper tests or final approval are not inferred.

Five actual-C suites pass ASan/UBSan/LSan: scheduler, queue index, queue rate,
qmap and multicast masks. The scheduler suite checks literal register words,
group addresses, ordering, invalid plans, poll errors/timeouts and each single
I/O failure on the tested transaction. Generated source mutations for packet
units, queue-based group addresses, C-only priority comparison and live rollback
all compile and fail semantic assertions. No production files are mutated by
those negative controls. The poll mock checks arguments and condition handling;
elapsed hardware polling time is not established by it.

### Installed candidate and failed contention acceptance

The full12-job build passes. Native31 SHA256:
`7c8735a2163c71dab1505dcb50a6939491988517ff251696eb9b8353d3caac64`.
Normal sysupgrade installs slot1, boot
`7c69832b-eea5-43ee-9110-5e5eae2a5b86`, revision
`r0+43560-b72fa9efff3`. All six module hashes match; native30's slot0 raw hash
and original configuration are preserved. Eight QinQ nodes start automatically,
without allocation failures, pending retries or kernel warnings.

A bounded probe independently verifies the vendor flush command on unused
LAN3q7/packed23: enqueue-disable works, the first status read is0x40000017
with DONE set, and the exact original enqueue state is restored. No priority
or weight is changed by that command probe.

The contention fixture uses static known-unicast VID35 forwarding from LAN3
to Windows on LAN1, unknown-unicast flood-off, and PCP0/5 streams with distinct
384/1024-byte packet sizes. Separate source RX and destination TX size bins
count each stream; CPU copies remain small. Each stream passes a low-rate
positive control, and the unshaped20+20Mbps offer delivers all19532/7328 frames.

A20Mbps port-shaper fixture fails strict/weighted acceptance. The first cleanup
script stops on BusyBox's rejection of `sleep .02`; the primary repairs the
same guard/restore script with whole-second waits, executes it and verifies
all original registers, VLANs, qdisc and persistent configuration. The rejected
run and cleanup repair remain recorded; neither is hidden as a passing test.

The next fixture negotiates a real10Mbps/full LAN1 bottleneck while LAN3 stays
at1Gbps. Ordinary `tc ets` programs and reads back the intended priorities,
weights, group units and restored enqueue states, but packet behavior fails:

|10Mbps physical fixture |PCP0 Mbps |PCP5 Mbps |Required behavior |
| --- | --- | --- | --- |
|PCP0 configured strict-high |4.45 |5.22 |PCP0 should dominate |
|PCP5 configured strict-high |4.50 |5.20 |PCP5 should dominate |
|Configured weight1:3 |4.96 |4.70 |PCP5 should receive the larger share |

Queue classification is tested independently using the **same** packets:
TBFq0 delivers PCP0 at4.82Mbps and leaves PCP5 at19.85Mbps; TBFq5 leaves
PCP0 at19.82Mbps and limits PCP5 to5.00Mbps. This confirms the selected queues
and retained TBF behavior rather than attributing the failure to PCP parsing.

A final raw A/B follows the vendor's exact per-queue sequence: disable one
queue, flush/check DONE, write its desired word, re-enable it, then continue.
Its strict results remain approximately4.44/5.26 and4.53/5.14Mbps; weighted
sharing remains5.01/4.65Mbps. Batching is therefore not established as the cause.
Both physical-link fixtures restore exact touched words, original advertisement,
1Gbps link, VLAN/PVID, FDB/flood, qdisc and configuration. Normal LAN TCP after
the diagnostics reaches940.95Mbps with zero retransmits.

The unresolved cause could involve scheduler semantics or admission behavior;
buffer constants alone do not prove either explanation. No undocumented buffer
bits are changed and no hardware capability is declared missing. A bounded Agy
source review times out without a structured verdict and is not counted as
approval or evidence. Further work needs a discriminating measurement or a
standard-path comparison, not another claimed fix based only on readback.

Evidence, failed gates, source/image manifests, private recovery backups,
UART and restoration records are under ignored
`logs/nss-slices-20261108/slice26-scheduler/`. Native30 remains the validated
fallback; strict/weighted ETS on native31 remains experimental.

After returning Windows to WAN/VLAN0 and restarting SharedAccess, DHCP assigns
the router192.168.137.64. Final native31 routed NSS measures936.27/936.61Mbps,
with two active ECM rules,800044/754070 additional firmware TX packets and
97.60/98.91% sampled CPU idle. Original configuration, bridge membership,
VLAN-unaware mode,1500/1508 MTUs, qdiscs and link advertisements are restored;
loop protection remains enabled. No new kernel warning appears. Completed
task-owned RAM guards are removed and detached helper drafts are preserved.
Only local `master` and `nss-experiment` branches remain. No push or release is
performed. The committed experiment retains failed scheduler packet acceptance
as an open gate.
