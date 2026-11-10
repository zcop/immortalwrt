# QinQ packet delivery and recovery acceptance

This acceptance work takes priority over further scheduler experiments. Core
wired NSS/QinQ forwarding has passed throughput, VLAN, isolation and lifecycle
checks. The initial unknown-destination, broadcast and priority-tag packet
matrix below passes on native31. Independent hardware/software FDB cases also
pass, and a live neighbour-MAC change recovers with measured transition loss.
IPv6 neighbour deletion and an announced MAC change also have bounded packet
results below. Slice29 adds DHCP/RA/DAD, malformed-tag and longer traffic
evidence. Its initial silent hardware-only MAC-move and discovery failures have corrective
source and bounded follow-up evidence in Slice30 below. Build34 automatically programs the peer-isolation correction, and the
bounded active NSS move/return test now passes.

Source baseline for Slice30: `cb7a87d635c` plus the separately committed FDB,
bridge-isolation and hardware-learning fixes and recorded Build34 fingerprints.
During those tests, Build34 ran in slot0 with Build33 retained and hash-verified
in slot1. The current image is recorded in [NSS status](NSS%20Offload%20status.md).
Earlier native31 results below
remain historical evidence. Slice27's strict/weighted scheduler gate remains
open and is separate from these delivery tests.

## Slice30 corrective validation (Build34 installed)

Build32 fixes a register-width defect in existing FDB entry updates:
`yt921x_fdb_add_index_in12()` accepted 16-bit values for two 32-bit words,
truncating status, FID and destination ports on a second MDB join or partial
leave. The control words are now `u32`; the index remains `u16`.
An atomic `fdb lookup <MAC> <hardware FID>` diagnostic reads the command and
all result words under one register lock, including multicast entries that
normal FDB dumps omit.

| Gate | New device evidence | Limit |
| --- | --- | --- |
| Discovery forwarding | mDNS4, SSDP4, mDNS6 and SSDP6 each deliver 3/3 exact frames in both directions, with no duplicate; programmed groups show FID3088 and LAN1/LAN3 bitmap `0x005` | Low-rate numbered probes, not service-daemon discovery or every VLAN variant |
| MDB membership | Both first/second join orders produce masks `1 → 5 → 4 → absent` and `4 → 5 → 1 → absent`; removed receiver gets 0/3, remaining receiver 3/3 | One additional controlled group |
| Silent host MAC move | Builds33/34 move existing Linux dynamic FDB from LAN3 to LAN1 after hardware-only learning; bridge replies reach new port 3/3 and old port 0/3 while all links stay up | Native unaware bridge, one test MAC; measured observation bound includes receiver/SSH overhead |
| Avoid steady CPU copies | Normal learning mode0 remains in use; 0/3 moved-source and 0/3 later steady frames appear on CPU ingress | `AUTO_AND_COPY` was rejected after it copied every tested steady frame |
| Active NSS move | Build34 invalidates old accelerated rule2 before any new-location CPU-directed source; rule3 accelerates on LAN1 with 13,348 additional flow bytes, 46 received UDP frames, 0 old-port frames and 0 duplicate IDs | One low-rate IPv4 UDP flow, with all client egress blocked during silent movement; a legitimate opposite-direction packet from the new location then permits reacceleration |
| Return to original port | The same flow returns to LAN3, accelerates again, delivers 23 observed return-phase packets and completes the iperf session with exit0 | This steering fixture deliberately keeps the client at its old wire location while another receiver represents the new location; it is not a loss benchmark |
| Bridge separation | Source and live masks identify a missing peer-isolation step when a port leaves a bridge in tag_8021q mode; exact peer-bit correction restores a reachable fixture | Build34 sets both peer bits automatically; the full client/move fixture remains reachable and passes without manual register writes |

MAC learning events come from read-to-clear interrupt status `0x080044`,
bits18 (MOVE) and19 (CREATE), observed with interrupt mask0. There is no wired
switch IRQ consumer on this board. The driver polls status every100ms in QinQ
mode and scans at most32 rows per pass when an event requests reconciliation.
A busy table can take several passes; 100ms is not a guaranteed move deadline.
Only existing dynamic Linux entries are moved through normal bridge learning,
which also invokes ECM invalidation and preserves ordinary Linux aging.
Static/local/sticky/locked entries, disabled learning, disallowed VLAN/MST
states, multicast, CPU destinations and ambiguous multi-port rows are skipped.
VLAN-aware global SVL is also skipped because a shared FID cannot identify the
customer VID. Future interrupt consumers must coordinate status ownership;
an independent raw status read can consume a learning event.

The LAN1--LAN2 physical fixture exposed a separate forwarding defect before
its iperf flow began. LAN2 had left br-lan, but former peers still allowed p1
in their isolation masks. A broadcast storm interrupted SSH while UART worked;
holding both ports down restored SSH. VLAN rows were distinct (3088/3104),
but peer masks were stale (`p0=0x6f9`, `p2=0x6fc`). Correcting only bit1 gives
`0x6fb`/`0x6fe` and keeps the client fixture reachable. The source correction
calls `yt921x_isolate()` after successful tag_8021q standalone setup, before
re-enabling later bridge policy. Loop protection was never disabled.

The initial NSS-flow attempt used the wrong timestamp field for duplicate
detection. Re-decoding its capture identifies 47 unique packet IDs100–146;
the corrected Build34 test also verifies old/new-port exclusion and return
delivery. Earlier fixture/setup failures remain preserved, including a
read-only iperf temporary-directory error and evidence-parser corrections.

Normal cabling is restored (Linux LAN3, Windows WAN/VLAN0, spare removed).
Build34 routes at936.51/936.71Mbps before an ordinary warm reboot and
934.56/936.56Mbps afterward, with98–99% sampled CPU idle and growing
firmware counters. Eight NSS interfaces return automatically. The installed
driver hash, untouched Build33 fallback slot, configuration hashes, workstation
addresses/MTU/MAC and task-resource cleanup are checked separately. No new
physical cold-boot, all-VLAN silent-movement or long-term reliability claim is
implied by these runs.

Actual-C host tests cover the FDB words, event walker/bridge policy and bridge
leave. Fixed sources and sanitizers pass; the pre-fix word and isolation
sources compile and fail their runtime oracles. Event mutations for lost
read-error retry, unsafe final index and static-entry overwrite also fail.
The source review's claimed MST signature error was rejected against the
actual kernel header; the unlock-before-notifier recommendation was adopted.
A later isolation review timed out and supplies no accepted verdict.

Evidence, failed fixtures, parser corrections, source fingerprints, private
recovery metadata and build manifests are under ignored
`logs/nss-slices-20261109/slice30-learning-multicast-fix/`.
The fixes and tests are committed separately on `nss-experiment`; no Slice30
push was performed.

## Required packet behavior

- A device sending an ordinary untagged frame does not supply the internal
  port ID. The switch classifies its physical ingress port and emits that
  port's SVID when sending the frame toward CPU/NSS.
- An unknown destination MAC does not make the ingress port unknown. Normal
  switch flooding remains bounded by the applicable forwarding domain,
  forwarding state, isolation and configured flood policy.
- CPU-originated bridge broadcasts and unknown-unicast copies are selected by
  Linux per eligible output port; each data copy receives that port's SVID.
  Ordinary LAN hardware switching retains its separate forwarding path.
- Missing or invalid transport tags on the internal CPU link are rejected.
  Do not replace this validation with MAC-based source-port guessing or
  unrestricted flooding to make a positive test pass.
- A valid frame unsupported by acceleration must retain its applicable host
  path. Packet delivery, acceleration refusal and malformed transport are
  different outcomes and must be recorded separately.

Implementation anchors:

- `target/linux/generic/files/drivers/net/dsa/yt921x_8021q.c`:
  `yt921x_qinq_user_port_enter()` and `yt921x_qinq_cpu_port_enter()`.
- `target/linux/generic/files/net/dsa/tag_yt921x_qinq.c`:
  `yt921x_qinq_rcv()`, `yt921x_qinq_find_dp()` and `yt921x_qinq_xmit()`.
- `target/linux/generic/files/drivers/net/dsa/yt921x_core.c`:
  `yt921x_refresh_flood_masks_locked()`.

## Source audit and current observation

The native31 ECM build includes `ECM_DB_XREF_ENABLE`,
`ECM_BAND_STEERING_ENABLE` and `ECM_BRIDGE_VLAN_FILTERING_ENABLE` in the actual
`ecm_interface.o` compile command. The NSS frontend's feature map includes
bridge support. Prepared kernel and ECM sources show these paths:

| Event | Source-established handling | Live evidence / remaining boundary |
| --- | --- | --- |
| Linux bridge learns the same MAC on another port | Bridge move notifier calls ECM invalidation by MAC | Observe the actual callback effect and packet recovery |
| Linux FDB entry is deleted or expires | `fdb_delete()` sends the delete event; ECM invalidates nonlocal MAC connections | Exact test-MAC deletion with hardware entry retained passes with0 UDP loss; natural Linux expiry remains open |
| Device goes down or loses carrier | Generic ECM netdev notifier invalidates connections using the device | Measure recovery and re-acceleration after carrier returns |
| Neighbour MAC changes | ECM invalidates connections using the old MAC | Announced IPv4/IPv6 MAC replacement recovers with0.0749%/0.03984% transition loss; other reuse/ND cases remain open |
| VLAN/bridge topology changes | QinQ blocks new lookups immediately; coalesced resync invalidates flows and reconciles nodes | Slice23 accepts bounded surviving-TCP topology churn; broader combined recovery remains open |

QinQ resync has a100ms coalescing delay plus workqueue latency. Suppressing
new lookups does not itself synchronously remove every existing firmware rule.
Node creation/destruction counters need not change for a link-only event when
the node keys remain identical. Retry timing is not a fixed recovery bound.

The YT921x driver exposes hardware FDB operations, but this audit finds no
driver-generated switchdev FDB learning notification. A MAC learned or moved
only in hardware cannot be assumed to trigger the patched Linux bridge hooks.
Whether traffic subsequently causes a software observation must be measured.
An entry inserted through Linux `extern_learn` is not a substitute for a
hardware-only event because it already passes through Linux notification paths.

A read-only snapshot before the live tests on2026-11-09 found unknown-unicast actions set to
flood and filter mask0x400, which does not block the board's user ports0-3 or
CPU ports4/8. Eight QinQ nodes are present. This is configuration evidence,
not a packet-fate test by itself. No network or hardware setting was changed
by that snapshot; the subsequent controlled live tests are recorded below.

A bounded Qoder DeepSeek-Flash/Max review corroborates the notification paths.
The primary rejects its initial neighbour-comparison bug claim: the helper
named `ecm_mac_addr_equal()` actually returns `!ether_addr_equal()`, so the
caller's early-return condition is correct. The corrected handoff is retained.
No defect fix or hardware pass is inferred from the review.

## Slice28 initial packet results on native31

Windows moves to LAN1/VLAN0 while Linux stays on LAN3. Initial probes to the old
LAN lease192.168.2.169 receive no replies. Windows readback confirms its actual
address192.168.137.1 and VLAN0. Probing that address with sender addresses in
192.168.137.0/24 restores the positive control without changing any IP setting.
Earlier failed probes and captures are retained rather than counted as driver
failures. The updated probe also passes an independent3/3 router control.

Each phase sends three ARP requests with distinct sender protocol addresses.
Replies must match the Windows source MAC, sender/target addresses and expected
tags; every admitted probe must receive exactly one reply. Outgoing socket
copies are excluded, and reflected requests on LAN3 are counted separately.
This establishes ARP endpoint delivery, not arbitrary application throughput.

| Controlled condition | Native LAN replies | Customer VLAN35 replies |
| --- | --- | --- |
| Ordinary broadcast |3/3 |3/3 |
| Ordinary unicast |3/3 |3/3 |
| LAN1 learning disabled; Windows absent from hardware FDB; unknown-unicast flood on |3/3 |3/3 |
| Same absent hardware entry; unknown-unicast flood off |0/3 |0/3 |
| Broadcast while unknown-unicast flooding is off |3/3 |3/3 |
| Broadcast flooding also disabled |0/3 |0/3 |
| Flood controls restored while the hardware entry remains absent |3/3 |3/3 |
| Learning restored and Windows relearned |3/3 |3/3 |

The learning-off operation is confined to LAN1 and flushes that port's dynamic
hardware entries. `bridge fdb show` hardware `self` entries prove Windows is
absent before and after unknown-unicast probes; the final controls prove it is
relearned. Linux software entries are recorded separately. This is a controlled
unknown-destination fixture; it does not establish natural aging or recovery of
an already accelerated flow.

No admitted phase has duplicate replies or reflected requests. Broadcast
phases show exactly three LAN1 hardware broadcast transmissions and zero LAN3
outbound broadcast transmissions for those probes. An additional VLAN36
negative control receives0/3 replies for both unicast and broadcast while35 is
admitted. Management VLAN1 remains on LAN3. Exact learning/aging/flood registers,
private VTU roots, customer35 entry, lookup/filter selectors, PVIDs, bridge flags
and persistent configuration restore after the customer matrix. Hardware MAC
entries naturally relearn; their entire dynamic table is not replayed.

External VID0 priority-tagged ARP also passes3/3 with PCP0 and PCP5, both to the
router and to Windows on LAN1. Each positive has exactly one response per
probe and no reflection. Ordinary untagged controls pass before the priority
probes. This resolves the external-ingress concern for the tested native path;
it does not establish arbitrary host-generated VID0 TX or every bridge mode.

For CPU-originated broadcasts, a two-second RAM-only raw-ARP sender on br-lan
emits three requests using distinct source protocol addresses. Windows replies
once to each. An independent raw receiver on the Linux Ethernet wire sees
exactly one untagged request per probe. The CPU-conduit capture contains six
matching requests: one outerSVID3072/innerCVID3088 copy for LAN1 and one
outerSVID3074/innerCVID3088 copy for LAN3 for each probe. No additional matching
request copy appears on that captured conduit. This verifies Linux per-port
replication on the tested native bridge. The capture reports zero kernel drops;
the temporary sender is removed and configuration remains unchanged.

The two learning/flood fixtures use independently armed local restoration
guards and UART coverage. Both complete normally; exact restoration checks
pass and no UART recovery is required. Their completed RAM guards are removed
after verification. No production driver or firmware change is needed for
these passed cases.

Windows is returned to WAN/VLAN0, SharedAccess is restarted and the router
obtains192.168.137.65 by DHCP. Final routed NSS delivers936.56/936.61Mbps with
two active ECM rules,761396/753437 additional firmware TX packets and
98.91/98.09% sampled CPU idle. Eight nodes remain automatic; original
configuration, bridge/VLAN/flood/learning settings and loop protection are
verified. No kernel warning, reboot or flash occurs. UART is closed without
reset after the checks.

## Independent FDB and neighbour recovery results

A temporary macvlan container on the existing Ethernet wire supplies endpoint
192.168.2.253/MAC02:28:29:00:00:fd. The address has no lease, neighbour entry or
ARP respondent before creation. Linux management and physical cables stay
unchanged; Windows remains the WAN iperf server. Each accepted case uses one
40-second reverse UDP session at50Mbps with1200-byte payloads. Actual receiver
loss is recorded, with software and hardware FDB observations kept separate.

ECM initially denies acceleration for the observed one-way reverse UDP flow.
The default/DSCP classifier source confirms the bidirectional packet condition.
One valid opposite-direction UDP packet on the same tuple permits acceleration;
the policy is not changed. Each case verifies UDP accel_mode2 before its state
transition and firmware TX increments. Software delivery before priming is not
presented as NSS acceleration.

| State transition | Independent evidence | Receiver result |
| --- | --- | --- |
| Natural hardware FDB expiry | Hardware entry disappears while Linux entry remains across15 consecutive samples spanning about24s; UDP stays accelerated |208279 packets,0 lost;49.99Mbps |
| Delete only the Linux FDB entry | Hardware entry remains across four samples while Linux entry is absent; flow decelerates; an endpoint packet restores learning and acceleration |208320 packets,0 lost;50.00Mbps |
| Keep the IP and change endpoint MAC to02:28:29:00:00:fe | ARP announcement updates neighbour state; a new accelerated ECM connection names the new MAC |208282 packets,156 lost;49.95Mbps |

Natural hardware expiry is observed without changing either aging interval or
FDB state. Register0x180440 reads1 and Linux bridge aging reads30000; neither is
modified. Firmware TX increases202002 packets; the minimum complete receive
interval is49.42Mbps. The software-deletion case targets only the test MAC with
the normal bridge master operation. No matching UDP flow remains accelerated
during its observed software-entry absence. Relearning restores acceleration
on the same application session; firmware TX increases185655 packets and the
minimum complete interval is49.30Mbps.

The MAC-change case changes only the container interface's MAC while up, then
immediately sends a valid ARP request to the gateway. IP, UDP port and process
are retained. Another opposite-direction packet satisfies the classifier for
the rebuilt connection. New-MAC acceleration is observed367ms after observing
the updated neighbour. That interval includes polling/command latency and is
not an exact hardware recovery duration. All156 lost packets occur in one
receive interval near the change; subsequent intervals report no further loss.
The minimum complete interval is48.25Mbps; total loss is0.0749%. The test's
bounded-recovery gate is re-acceleration within2s of neighbour observation and
loss below0.1%. It passes that gate, and is explicitly not lossless. No ordinary
DSA comparison attributes this loss specifically to NSS or QinQ.

Rejected fixtures remain recorded: the first container lacks writable temporary
storage required by iperf, and a later parser stops on a duplicate `serial`
field. Neither is packet acceptance. The final container has a small RAM /tmp
and read-only root. The attempted standard DSA `self` delete leaves the learned
hardware entry present, so it is not used as hardware-only deletion evidence.
Cleanup instead creates a tracked bridge static entry for each exact test MAC,
deletes it through the master path and proves software/hardware absence. No
raw FDB engine write, management-table flush or global aging change is used.

All temporary containers/networks, task neighbour/FDB entries and ECM state
nodes are removed. Configuration and boot ID are preserved. Final normal
routed NSS reaches936.60/936.51Mbps, with778833/754723 additional firmware TX
packets, two active ECM rules and98.80/98.43% sampled CPU idle. Eight nodes
remain automatic and no kernel warning appears.

A final read-only audit independently confirms that the task containers,
networks, both test-MAC FDB entries, neighbour entry and ECM character nodes
are absent. Original management IP/MAC, boot ID, configuration hashes and
bridge flags are preserved. Loop protection remains enabled and the normal
Linux-LAN3/Windows-WAN topology is restored. No test remains active.

These results accept the specified IPv4 ARP/UDP cases. The IPv6 follow-up is
recorded below; physical and silent hardware-only port movement, broader
discovery protocols and long-term reliability remain open.

## IPv6 neighbour deletion and MAC replacement

On unchanged native31, an IPv6-only macvlan endpoint on LAN3 uses
`fd42:28:1::253`, with Windows on WAN at `fd42:28:2::1`. Each case retains one
reverse UDP6 socket for40seconds at50Mbps with1200-byte payloads. Valid
opposite-direction UDP primers satisfy the existing ECM classifier condition.
Both cases show an initial gateway NS/NA pair in the actual br-lan capture.
The independent decoder verifies ND code, checksum, hop limit255, target
address and link-layer options; captures report zero kernel drops.

| Case | Receiver result | Firmware evidence |
| --- | --- | --- |
| Delete only the test IPv6 neighbour; send a router probe to trigger real NS/NA resolution |208328 packets,0 lost;50.00Mbps | UDP6 accelerated before/after; TX+201931, hash hits+201932 |
| Retain IPv6 address/socket; change MAC from02:28:30:00:00:fd to02:28:30:00:00:fe and announce it |208338 packets,83 lost,0.03984%;49.98Mbps | New-MAC UDP6 connection accelerated; TX+211135, hash hits+211136 |

The deletion test captures three router-to-endpoint NS packets and three
solicited NA responses. A new neighbour can appear immediately, so no extended
cache-absence window is inferred from the delete command. In the MAC test,
one captured unsolicited unicast NA has Override=1, Solicited=0, Router=0 and
the correct new target MAC. The retained global address is checked after the
change. NSS uses the new MAC and the old-MAC UDP connection is no longer
accelerated. Acceleration is observed339ms after neighbour observation,
including polling/command latency. All83 losses occur in one transition
interval; the minimum complete interval is49.02Mbps. This passes the same
2s/<0.1% bounded gate as IPv4; it is not lossless acceptance.

The earlier MAC test is retained as FAILED:328/208327 lost,0.1574%. Its
sender opens/binds the packet socket after changing MAC, creating a measured
67.54ms post-command announcement gap. That gap confounds attribution to
router recovery. The corrected sender pre-opens the socket, as in the IPv4
test; its measured gap is0.023ms. One new run uses the unchanged rate,
duration and loss gate, plus an announcement-gap prerequisite below5ms.
Neither the failed result nor this correction establishes an NSS-specific
regression without an ordinary-DSA comparison.

A ping-summary parser mismatch stops the first fixture before UDP and is
retained separately. The first completed test also exposes incomplete fixture
cleanup: a temporary router /64 is advertised, producing extra endpoint ND
entries and a deprecated workstation SLAAC address. Those exact task entries
are removed and the user removes the test-derived workstation address; its
saved address baseline is verified. The corrected fixture uses router /128
addresses with exact host routes and cleans every ND entry belonging to its
two task MACs. Its workstation address sets match before/after. No persistent
RA setting, management route, firmware or driver is changed.

Router/container resources, task ND/FDB entries, guards and state nodes are
removed; boot and configuration hashes remain unchanged. The user confirms
Windows temporary address/route removal. A final read-only audit verifies no
test IPv6 addresses/routes/neighbours on the router/workstation, no task
resources, eight automatic nodes, loop protection enabled and no kernel
warning. Restored native IPv4 TCP reaches936.47/936.28Mbps with770286/754899
firmware TX increments and97.86%/98.07% sampled CPU idle. Evidence is under ignored
`logs/nss-slices-20261109/slice28-ipv6-recovery/`. RA/DAD policy, DHCPv6,
broader multicast/discovery, silent/cable moves and longer reliability tests
remain open.

## Slice29 DHCP, discovery and silent-move tests

Historical pre-fix results; see Slice30 above for corrective evidence.

These native31 tests use the existing image and persistent configuration. Linux
remains on LAN3. An isolated LAN1-to-WAN cable replaces Windows for the L2
matrix: LAN1 stays in `br-lan`, WAN stays unbridged, and WAN DHCP is stopped.
Loop protection stays enabled. A router-local deadline and controller cleanup
hold both test ports down between runs. This is a native VLAN-unaware bridge
fixture; it does not repeat every case on a customer VLAN.

| Gate | Observed result | Acceptance boundary |
| --- | --- | --- |
| DHCPv4 | Real client acquires192.168.2.143/24, renews twice, releases; Discover/Offer/Request/ACK/Release decoded | PASS for acquisition/renew/release on native LAN |
| DHCPv6 | Acquisition plus multicast Renew, Rebind, Information-request and Release receive valid replies; task bindings released | PASS for these exchanges; old BusyBox unicast retry defect retained separately |
| IPv6 RA/SLAAC | Valid hop255/checksum, managed/other flags, native ULA PIO, RDNSS and actual kernel RA address | PASS for observed native policy; lifetime0 matches absence of an upstream IPv6 default |
| Stable-owner DAD |3/3 unicast ND,3/3 solicited-node multicast ND and3/3 source-unspecified DAD probes receive valid NAs; kernel duplicate address enters `dadfailed` | PASS for stable-owner duplicate detection; rapid remove/reuse timing case remains open |
| Ordinary L2 unicast |3/3 exact numbered frames in each direction, no duplicates | PASS on the isolated physical path |
| Discovery multicast | IPv4 mDNS3/3 both directions; SSDPv4, mDNSv6 and SSDPv6 have missing deliveries despite offloaded MDB rows for both LAN ports | FAIL/open investigation; packet forwarding probes do not establish service-daemon discovery |
| Invalid transport tags | Missing tags, S-only/C-only, unknown/aliased SVID, wrong conduit and wrong outer TPID:3 sent,0 delivered on observed path per case; externally forged port/WAN SVID also0/3 | PASS for specified vectors, with valid CPU4/CPU8/ordinary external controls; not every possible malformed encapsulation |
| Silent hardware-only MAC move | Hardware moves test MAC from LAN3 to LAN1 while Linux `master` FDB remains LAN3; CPU bridge TX reaches old LAN3(3/3) and misses new LAN1(0/3) | FAIL; CPU-visible source packet moves Linux FDB and restores new-port delivery3/3 with0 old-port copies |
| Longer wired traffic | TCP600s each direction at936.54/935.00Mbps; UDP100Mbps300s each direction loses45/3124953 and0/3125166 packets |1800s of completed phase coverage with growing firmware counters and no observed link transition/new kernel warning; not multi-day qualification |

The initial DHCPv6 client sends unicast Renew/Release. The server returns the
valid `UseMulticast` status5; BusyBox1.37 misleadingly prints `NAK (0)` and
reacquires with a new IAID. Captured bytes establish status5. Correct multicast
transactions succeed without changing server policy, and all test bindings are
released. Do not count the old client's behavior as a switch drop.

Two rapid address-reuse DAD attempts fail their fixed2s state assertion. They
are preserved. The successful diagnostic first verifies the router owns the
address without tentative/duplicate flags, then captures host wire, CPU conduit
and bridge traffic. Valid DAD NS/NA exchange and the actual endpoint's
`dadfailed` state establish the stable-owner case, without closing the earlier
readiness/timing case.

The first discovery fixture repeatedly joins/leaves groups; it misses reverse
SSDP. Persistent group holders remove that churn, and Linux MDB readback shows
both LAN1 and LAN3 offloaded memberships for SSDP and the IPv6 groups. Loss
persists: the final matrix misses LAN3-to-LAN1 SSDPv4, mDNSv6 and SSDPv6 while
the reverse probes pass. IPv4 mDNS reaches both wire receivers and the CPU
conduit. Missing known-group frames do not appear at the intended wire
receiver. The precise ASIC table/parser cause is not established; the software
MDB display alone does not prove hardware egress programming. No forwarding
policy is relaxed to make these tests pass.

The silent-move test uses MAC02:29:36:00:00:fd and ordinary EtherType0x88b5.
A source frame addressed to the router first teaches Linux and hardware LAN3.
A later frame from the same MAC, sent through WAN's physical cable into LAN1
and addressed to the known LAN3 host, teaches hardware LAN1 without changing
Linux's entry. The subsequent bridge-originated frames reach the old LAN3
wire receiver.
Linux's port selection implies LAN3's SVID through the tagger; the CPU-link
tag itself is not captured in this particular move test.
A frame addressed to the CPU from the new location repairs the Linux entry.
Both physical links remain up. This confirms the source-audited learning
notification gap for host bridge steering; it does not measure an existing
accelerated NSS connection's transition. A repair must reconcile real hardware
moves with Linux learning and flow invalidation, then repeat both host and
accelerated-flow tests.

A separate physical cable-move test keeps one180s TCP session running while
the user moves Linux LAN3→LAN1→LAN3, with Windows on WAN and UART available.
Both switch and Linux FDB follow the workstation. Each carrier-loss event
reduces the accelerated-rule count to0; firmware counters subsequently grow
on the new port. The one-second samples first exceed a10000-packet growth
threshold about9s/8s after the respective carrier-up observations. This is an
observed recovery interval, not a fixed driver guarantee. TCP records zero-byte
intervals during both moves, survives without reconnecting, and averages
800.41Mbps over the whole180s including the stalls, with181 retransmissions.
This accepts session survival and reacceleration for these two physical moves;
it is not lossless or instantaneous recovery, nor an ordinary-DSA A/B result.

The original traffic harness stops its last reverse-UDP phase when firmware
counters fail to grow. The one-way flow has no opposite-direction packet for
classification. An explicit valid counter-direction primer allows the remaining
300s phase to accelerate and pass. The aborted phase remains separate: these
are30min of accepted phase coverage, not an uninterrupted pass of the original
aggregate harness. No reset, flash or persistent network change is used.

Raw probes, captures, failed runs, client corrections, helper disposition and
restoration evidence are under ignored
`logs/nss-slices-20261109/slice29-daily-acceptance/`. The bounded negative source
review is checked by the primary; a subsequent multicast source-review request
times out without a verdict and is not an accepted review.

Final restoration verifies the original host address/MTU/MAC set, configuration
hashes and boot ID, eight automatic nodes, loop protection and no kernel
warning. Task containers, networks, ND/FDB entries, addresses, RAM tools and
guards are absent. The spare cable is removed, Linux is back on LAN3 and
Windows is on WAN/VLAN0; WAN DHCP obtains192.168.137.10. Fresh native TCP reaches
936.56/936.65Mbps, firmware TX+506211/+503066 and sampled CPU idle98.69%/99.19%.
The serial console is closed without reset. The two failed gates above remain
open; restoring throughput does not close them.

## Packet test matrix

Run native untagged traffic first, then the same relevant cases in an isolated
customer VLAN. Use independently numbered probes and distinct test MACs.
Keep management outside every modified test domain.

| Case | Required result | Existing evidence / next gate |
| --- | --- | --- |
| New host with empty neighbour and destination-FDB state | ARP/ND resolves; DHCP where applicable succeeds; first application packets arrive | Fresh IPv6 gateway NS/NA pair and subsequent UDP6 pass; full empty-state DHCP/first-application matrix OPEN |
| Real Ethernet broadcast, host-originated and LAN-originated | One delivery per eligible receiver, no reflection to ingress, no wrong VLAN/bridge delivery | Initial native CPU replication and native/VLAN35 LAN policy PASS; broader discovery protocol/domain coverage OPEN |
| Unknown unicast to a real receiver | Hardware FDB is demonstrably absent; enabled flood reaches eligible receiver, disabled flood obeys policy | Controlled learning-off ARP gate PASS on native LAN and VLAN35; general application/accelerated-state recovery OPEN |
| Hardware FDB expires or is removed while Linux/NSS still know the destination | Existing traffic recovers or continues correctly without reset; relearning is observed | Natural hardware expiry during reverse UDP PASS with0 loss; arbitrary deletion/other patterns OPEN |
| Linux FDB expires or is deleted while hardware still knows the MAC | Flow invalidation and fallback/rebuild preserve delivery and isolation | Exact test-MAC deletion/fallback/relearning PASS with0 loss; natural Linux expiry/other protocols OPEN |
| Device cable moves from one LAN socket to another | Traffic and acceleration resume on the new port | One TCP session survives LAN3→LAN1→LAN3; FDB follows and firmware traffic resumes with measured stalls. Silent movement and arbitrary repeated moves remain separate |
| Same MAC moves while old and new links both remain up | New source observation updates the destination; stale steering cannot persist | Initial FAIL superseded by Slice30 host-move pass; Build34 active NSS invalidation, new-port delivery, return delivery and same-session completion also pass for the one low-rate IPv4 UDP case |
| ARP/ND mapping changes or an address is reused | Old MAC stops receiving application data; new owner receives it after resolution | Announced IPv4/IPv6 MAC changes recover with0.0749%/0.03984% UDP loss; other reuse/NUD cases OPEN |
| Discovery and IPv6 control | ARP, DHCP broadcast/unicast, IPv6 ND/RA/DAD and mDNS/SSDP follow their actual delivery policies | Native DHCPv4/v6, RA/SLAAC and stable-owner DAD pass; Slice30 corrects the specified discovery-group packet failures. Rapid reuse and broader VLAN cases OPEN |
| Valid uncommon L2/priority-tagged traffic | Supported host forwarding works; unsupported customer encapsulation is explicitly recorded | External VID0/PCP0/5 ARP to CPU and LAN peer PASS on native bridge; other L2 protocols and host-generated priority TX OPEN |
| Corrupt/missing internal S-tag, wrong port/conduit, or forged external S-tag | Rejected without cross-port/cross-VLAN leakage | Specified native CPU/external negative vectors PASS with ordinary unicast controls; observed active paths only |
| NSS interface or rule temporarily unavailable | Valid traffic uses the intended host path and subsequently re-accelerates | Allocation/topology stress passed; combine with unknown-state gates above |

For the uncommon-L2 case, choose an EtherType that is not a reserved MAC-control
protocol. Pause and link-local control frames have separate intentional paths;
their handling cannot stand in for ordinary unknown-protocol forwarding.

## Evidence and fixture rules

For each case, record the switch's real FDB separately from the Linux bridge
FDB, neighbour table and NSS flow state. `bridge fdb show` can display both
`master` and hardware `self` entries; a display change alone does not establish
which table was altered. Verify the intended entry is absent before a test.

Use endpoint receive logs, sequence numbers and packet capture for delivery.
Switch MIBs and NSS counters provide supporting evidence, not application
delivery proof. When flooding is expected, count duplicates and check an
excluded receiver/domain as a negative control. Use the same TCP connection
across a transition where the fixture permits it, and record UDP loss separately.

The normal fixture is Linux on LAN3 and Windows on WAN/VLAN0. LAN delivery
matrices use Windows on LAN1 with its NIC/VLAN settings unchanged.
A routed cable-move test can retain Windows as WAN server while the Linux
client is moved between LAN sockets with UART management available. That test
includes link events and does not prove a silent hardware-only MAC move.
Such a move needs an independent two-source fixture; retain this limit instead
of using a Linux FDB insertion as an equivalent test.

Test-table changes must target test MACs and isolated domains. Do not flush the
management FDB/neighbour state, shorten the management bridge's aging time,
disable loop protection, or use a zero-rate shared resource to manufacture
backlog. Preserve normal CPU flood filters and VLAN/isolation controls.

Before any live mutation, save exact state, arm a locally executing restoration
guard and verify UART recovery for operations that can interrupt management.
Use bounded phases and stop after a failed positive control. Restore tables,
flags and endpoint topology, then verify normal DHCP/routed NSS. Source
inspection, fault injection and a guard being armed are not restoration proof.

Audit evidence, helper disposition, packet captures, per-probe results and
restoration records are under ignored
`logs/nss-slices-20261109/slice28-daily-traffic/`. The results accept the bounded
cases above; the full recovery matrix and long-term reliability remain open.
