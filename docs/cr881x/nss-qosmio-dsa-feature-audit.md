# Native NSS/QinQ versus the YT921x DSA baseline

Historical audit: 2026-11-05, former branch `nss-qosmio-qinq`, build08,
kernel6.18.55. The comparison, restrictions and ordered plan below describe
that image. They are retained as evidence of the original failures and are
not a list of current defects.

As of 2026-11-09, development is on `nss-experiment`, installed native31 on
Linux6.18.60. Later slices implement and test MST/control delivery, high
customer VIDs, direct and unaware-bridge VLAN uppers, cross-conduit switching,
core reload cleanup, standard customer QinQ, capture and bounded node/churn
recovery. Native30 also passes routed IPv6 and cold/ordinary warm boot checks.
Slices20–25 now have scoped results. Their remaining acceptance boundaries
are listed in the current status linked below.

Current limitations include failed SP/weighted scheduler contention acceptance,
general hardware egress flower, software storm containment, bridged802.1X and
external-peer capacity/LACP gates. Daily packet delivery and FDB/neighbour
recovery now have bounded results, including measured loss during an IPv4
MAC replacement. Physical/silent port moves and broader reliability remain open.
Use [current acceptance status](nss-qosmio-dsa-slices.md#current-acceptance-status)
and the [daily delivery matrix](nss-qinq-daily-traffic.md) for today's status.

`README.yt921x.md` DONE entries remain the established driver baseline.
This audit checks the selected native image and QinQ integration against it.
A missing image package, a kernel integration requirement, and a limitation
of the new packet transport are different findings. The switch's hardware
capability is not inferred to be missing from any of them.

## Test setup and evidence

Linux192.168.2.100/enp2s0 stayed on LAN3. Windows moved to LAN1 for the LAN
tests and received192.168.2.169 by DHCP. It returned to WAN afterwards and
automatically resumed192.168.137.1/ICS. No endpoint addresses/routes were
changed by the tests. Crafted ARP probes used an otherwise unused source
address without assigning that address to the workstation.

The primary performed device operations. Qoder DeepSeek-Flash/max supplied
one bounded read-only source review (`20261105-100831-e8fb042b`), not hardware
acceptance. Its claim that `tx_fwd_offload=false` removes LAN hardware
switching was rejected: that flag concerns CPU-originated bridge TX
replication. Its shared-VID and QoS assertions were not accepted without
supporting source/data. The relevant restrictions were checked directly.

Raw evidence, scripts and packet captures are in the original repository's
ignored `logs/qosmio-dsa-feature-audit-20261105/`. Temporary utilities were
built from this worktree and loaded into router RAM. No package was installed
persistently, and no firmware, driver source, or UCI configuration was changed.

## Original build08 comparison and actual results

| Feature | Native image result | What this establishes |
| --- | --- | --- |
| Ordinary LAN switching | PASS:941.12 Mbps TCP,0 retransmits,98.93% router CPU idle | LAN switching is preserved; CPU TX replication is not the LAN forwarding path |
| MAC learning/FDB | PASS: peer MACs learned on their exact ports; hardware/self FDB entries present | Exact port identity and normal learning remain available |
| Single customer VLAN, access/trunk | PASS: VLAN100 and200 each returned3/3 tagged ARP replies through an access peer | Tagging and untagging work |
| VLAN isolation | PASS: unregistered VID200 and VID100 after moving the peer to PVID200 returned0/3; the matching VID returned3/3 | VLAN membership/PVID enforcement works |
| Port isolation | PASS: both ports isolated0/3, restored3/3 | Isolation survives this transport |
| Separate bridge groups | PASS: separated ports0/3, rejoined3/3 | Bridge-domain separation survives |
| Broadcast flood control | PASS: enabled3/3, disabled0/3 | Broadcast policy is enforced |
| Customer802.1ad/QinQ | RESTRICTED: service-tagged probes rejected; source explicitly drops user S-tags and third tags | This transport consumes the S-tag layer; full customer QinQ is not available through it |
| Customer VLAN3072–4094 | RESTRICTED: VLAN3500 programming returned EBUSY | This is a transport restriction, not a silicon VLAN limit |
| Direct port VLAN upper / tagged unaware bridge | RESTRICTED in source | The driver rejects direct port VLAN programming; the QinQ tagger rejects tagged TX without a VLAN-aware offloaded bridge |
| Bridge spanning LAN/WAN CPU conduits | Functional ARP PASS3/3, hardware offload declined | Linux software fallback works for the tested unaware bridge; full hardware bridge parity is absent |
| STP/MST | Hardware MST integration gap; ordinary blocking packet test not accepted | See the corrected test interpretation below |
| Static MDB and snooping state | Control PASS: static239.1.2.3 and dynamic IPv6 groups reported offload | Full multicast delivery/leave behavior still needs a receiver test |
| LAG | Control PASS after taking the member down before enslaving | Kernel accepted an802.3ad bond/member; two-link LACP/load distribution not tested |
| Jumbo MTU | Control PASS: LAN2 MTU9000, conduit MTU9008; restored1500/1508 | No end-to-end9K delivery claim; endpoint MTUs were unchanged |
| TC ACL/drop, mirror, shaping/policing | Cannot exercise fully in this image | Required utilities and several kernel action/scheduler packages are omitted |
| ACL capacity | Eight entries reserved, four steering entries active |376 of384 normal TC slots remain; the reservation does not grow with customer VLAN count |
| Linux bridge CPU TX replication | Per-port software replication | Extra CPU work for host-originated bridge floods; ordinary LAN hardware forwarding remains intact |
| NSS VLAN node capacity | Source limit64 root/leaf nodes | Acceleration capacity is smaller than the switch VLAN table; this is not a loss of ordinary switch VLAN capacity |
| Conduit packet capture | DEBUG_ONLY link type303; Ethernet filters fail | Raw capture works; user DSA ports report normal Ethernet. This is custom-tagger/libpcap integration, not evidence of missing traffic |

The VLAN/bridge tests use three requests per positive/negative condition;
they establish packet fate, not high-load or adversarial stress coverage.

## STP/MST: corrections and confirmed integration requirement

The first manual `bridge link ... state4` test was made with kernel STP
disabled. Linux's `br_set_port_state()` calls `br_port_state_selection()`,
which returned the port to forwarding. Readback showed forwarding and
hardware MST0 port bits3. Its three replies do not demonstrate a hardware
blocking bypass; that test was invalid as a blocking test.

Changing MST mode on the existing bridge also failed: Linux forbids changing
that mode while its ports have VLANs. A subsequent test therefore created
an empty dedicated MST bridge before adding LAN1 and VLAN100, leaving the
management bridge intact. Linux accepted software MST states, but the
hardware MST1 register stayed0 in all states and the CPU-injected forwarding
baseline returned0/3. The0/3 blocked result is not accepted as a pass when
the positive baseline also failed.

Source explains a concrete offload eligibility problem. Kernel
`net/dsa/port.c:dsa_port_supports_mst()` requires all of:

- `vlan_msti_set`
- `port_mst_state_set`
- `port_vlan_fast_age`
- configurable learning

YT921x registers the MST state and VLAN association callbacks and per-port
fast ageing, but not `port_vlan_fast_age`. DSA therefore cannot offload an
MST bridge in this image. Linux's user notifier converts EOPNOTSUPP to a
successful software bridge attachment, so successful `ip link`/MST commands
are insufficient evidence of hardware offload. QinQ's handling of VLAN-aware
software fallback also needs explicit coverage.

The successful LAG correction is recorded separately: a member must be down
before enslaving it. The initial EPERM with an up member is not a confirmed
LAG regression.

## Scope of the other restrictions in build08

The high-VID restriction is explicit in
`yt921x_8021q_bridge_vlan()`: tracker arrays have3072 entries and all customer
VIDs at/above3072 are rejected. The tagger additionally rejects
`vid_is_dsa_8021q()` customer IDs. Normal802.1Q permits1–4094; the present
mode therefore loses1023 usable customer IDs. IDs0/4095 are not ordinary
customer VLANs and must not be counted as lost capacity.

The bridge-join guard rejects hardware bridges across different CPU
conduits. The actual Linux attachment succeeds via software fallback. With
Windows on WAN at its verified137.1 address, crafted LAN3 probes received
3/3 replies. WAN's firmware leaf stayed standalone VID3075 rather than the
LAN bridge VID3088. No cross-conduit throughput figure is claimed. An earlier
probe used the old LAN address after Windows changed it and is inconclusive.

The compiled image lacks the installed `bridge`/`tc` commands and selects
neither `kmod-sched-core` nor the relevant optional scheduler/action packages.
Kernel config has no TBF, GACT or MIRRED. Temporary userspace tools alone
cannot fix that: root TBF and clsact creation failed with ENOENT, and the
flower/drop attempt could not be installed. This is a profile/packaging gap;
it does not prove that NSS bypasses the switch's hardware policer or shaper.

For capture, libpcap identifies `/sys/class/net/eth1/dsa/tagging` as
`yt921x-qinq`, which is absent from its protocol table. It intentionally
falls back to DLT_DEBUG_ONLY. Raw captures contain the actual Ethernet
S-tag/C-tag stack, while Ethernet-address BPF filters cannot compile for
that DLT. Accelerated NSS traffic also bypasses normal Linux capture paths;
fixing decoding alone will not make tcpdump observe every accelerated frame.

## Original ordered fix plan and acceptance gates

1. **Restore image test/control parity.** Add `ip-bridge`, `tc-tiny`,
   `devlink`, `kmod-sched-core`, `kmod-sched-flower`,
   `kmod-sched-act-police`, `kmod-sched-mqprio` and `kmod-sched-ets` to the
   feature-test profile. Build matched kmods/image. Confirm actual hardware
   ACL drop/counter behavior on ingress and CPU/NSS egress, mirror copies,
   policer limits and100 Mbps shaper A/B deltas. Test both LAN switching
   and routed NSS; do not substitute software qdisc effects for HW proof.
2. **Repair MST integration.** Add/bind `port_vlan_fast_age` using the
   existing VID-aware `yt921x_fdb_flush_raw()` under the driver lock. Keep
   other VLANs' learned/static entries intact. Re-run held MST forward/block/
   forward with3/0/3 packet results, confirm hardware register changes,
   check two VLANs with different MSTIs, and then run a physical-loop test.
   Explicitly test or reject unsupported QinQ software-fallback cases.
3. **Enable QinQ bridges across CPU conduits.** Keep the old guard for
   single-tag mode. For QinQ, use the outer per-port identity and actual
   conduit in forwarding, node ownership and VLAN membership. Validate
   hardware FDB/MDB and multicast copies on both CPU ports, no duplicate
   packets, VLAN isolation, and near-line-rate LAN↔WAN L2 switching with low
   CPU. Do not infer success from bridge attachment or ARP alone.
4. **Recover the customer VLAN namespace.** Remove the blanket high-VID
   exclusion only after auditing internal VID ownership, tracker sizes,
   generic tag_8021q helpers, tagger validation and ECM matching. Reserve
   actual internal IDs safely or allocate them from unused space; prevent
   collisions with standalone/unaware bridge domains. Gate on VLAN3071,
   3072,3500,4094 and collisions during bridge changes, with matching wire
   tags and zero cross-domain delivery. No blanket claim of all4094 IDs
   until internal-domain allocation is solved.
5. **Restore direct VLAN uppers and transparent-tag cases deliberately.**
   First specify ownership and fallback for `wan.N`/`lanN.N`, including
   driver VLAN registration and tagger TX/RX; extend ECM only afterwards.
   Test a one-port VLAN-aware WAN bridge plus bridge VLAN upper as a
   source-backed workaround candidate before documenting it as working.
   Tagged VLAN-unaware bridging needs a separate domain-isolation design.
6. **Handle the actual QinQ tradeoff explicitly.** A customer double tag plus
   the private port tag exceeds the firmware's two-tag mapping used here.
   Provide an explicit standard-DSA/non-NSS mode for workloads requiring
   full customer QinQ unless a separately validated host-only exception
   design can preserve those frames. Do not promise a firmware change or
   assume triple-tag NSS acceleration.
7. **Complete observability and lifecycle work.** Add proper libpcap/tcpdump
   support for this tagger and test live/offline filters plus VLAN metadata;
   do not blindly relabel captures as Ethernet. Rework topology invalidation
   so surviving supported flows can regenerate/reaccelerate, preserving
   safe fallback while nodes are unavailable. Stress the node pool and
   verify mirror/802.1X/storm/QoS policy on already accelerated traffic.

## Restoration and test limitations

Every temporary bridge, bond, VLAN, PVID, port isolation flag, STP/MST setting
and MTU was restored. Configuration hashes remain unchanged. Windows is back
on WAN; WAN DHCP recovered automatically. No flash, reboot, commit or push
was performed in this audit. The original Kuncy7 source reference is preserved.

Test-harness corrections are retained in the raw logs. The initial `nohup`
watchdogs could not launch because that command is absent; foreground cleanup
did restore their changes. Further tests used a HUP-ignoring watchdog whose
independent execution was verified. Conduit capture required raw unfiltered
mode, and one MST dump returned a valid JSON fragment followed by a diagnostic.
None of these aborted/inconclusive tests is relabeled as a hardware pass.

At the end of this original audit, the pending end-to-end tests were9K delivery,
two-member LACP/load balancing, multicast delivery/leave/MVR,802.1X policies,
mirror/policer/shaper/PCP queues, unaware tagged trunks and direct VLAN uppers.
The baseline DONE features remain the reference; these rows describe the
limits of build08 and the validation actually performed at that time. Later
results and the remaining boundaries are linked at the top of this document.

## Final post-audit check

Normal routed WAN operation was restored without Windows commands. A fresh
routed transfer reached936.44 Mbps,0 retransmits,99.03% router CPU idle,
with two active ECM flows and786183 additional firmware IPv4 TX packets
(one-second startup omission, eight-second measurement). All eight QinQ
nodes were present. Bridge STP/VLAN/MST flags, MTUs and configuration hashes
were restored; temporary interfaces were absent and the boot ID unchanged.
The machine-readable combined report is `SUMMARY.json` in the audit log
directory. The code remains uncommitted and no image was rebuilt/flashed
for this feature audit.
