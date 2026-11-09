# QinQ packet delivery and recovery acceptance

This acceptance work takes priority over further scheduler experiments. Core
wired NSS/QinQ forwarding has passed throughput, VLAN, isolation and lifecycle
checks. The initial unknown-destination, broadcast and priority-tag packet
matrix below passes on native31. Independent hardware/software FDB cases also
pass, and a live neighbour-MAC change recovers with measured transition loss.
IPv6 neighbour deletion and an announced MAC change also have bounded packet
results below. Physical port movement and broader recovery tests stay open.

Source baseline: `c5ad88b702dc`. The installed native31 source predates the
commit-title rewrite but has identical file contents. The router retains
native30 in the other slot. Slice27's strict/weighted scheduler gate remains
open and is separate from these delivery tests.

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
| Device cable moves from one LAN socket to another | Old output stops receiving after the controlled transition; traffic and acceleration resume on the new port | Cable-move gate OPEN; carrier events must be recorded |
| Same MAC moves while old and new links both remain up | New source observation updates the destination; stale NSS steering cannot persist | Independent source fixture needed; OPEN |
| ARP/ND mapping changes or an address is reused | Old MAC stops receiving application data; new owner receives it after resolution | Announced IPv4/IPv6 MAC changes recover with0.0749%/0.03984% UDP loss; other reuse/NUD cases OPEN |
| Discovery and IPv6 control | ARP, DHCP broadcast/unicast, IPv6 ND/RA/DAD and mDNS/SSDP follow their actual delivery policies | Actual NS/NA resolution/deletion and announced IPv6 MAC update pass bounded gates; full RA/DAD/DHCP/discovery matrix OPEN |
| Valid uncommon L2/priority-tagged traffic | Supported host forwarding works; unsupported customer encapsulation is explicitly recorded | External VID0/PCP0/5 ARP to CPU and LAN peer PASS on native bridge; other L2 protocols and host-generated priority TX OPEN |
| Corrupt/missing internal S-tag, wrong port/conduit, or forged external S-tag | Rejected without cross-port/cross-VLAN leakage | Source guards present; controlled negative packet gate OPEN |
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
