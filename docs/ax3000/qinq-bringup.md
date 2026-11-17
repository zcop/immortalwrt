# QCA8337 QinQ DSA transport

Upstream qca8k changes are maintained as OpenWrt kernel patches. The ordinary
AX3000 profile retains its QCA tagger; `redmi_ax3000_qinq` opts into `qca-qinq`
with `qca8k.experimental_qinq=1` and MDIO-only switch management.

## Wiring and packet format

On Redmi AX3000, CPU port0 connects to `eth1`, CPU port5 to `eth0`, switch
ports1–3 are LAN1–3, and port4 is WAN.

```text
DA | SA | 88a8: exact port SVID | 8100: inner domain VID | EtherType | payload
```

The switch inserts the ingress SVID. The Linux tagger inserts it on software
transmit; NSS VLAN interfaces generate both tags for accelerated transmit. The
SVID is
`dsa_tag_8021q_standalone_vid(port)`; the inner VID identifies the standalone
port or VLAN-unaware bridge; on a VLAN-aware bridge it carries the customer
VID. RX requires the exact port SVID, owning conduit and current VLAN membership. Linux replicates bridge TX per port; bridge aliases
are not accepted as physical port identities. FDB/MDB operations use the internal domain VID in unaware mode and customer
VID in aware mode. Proprietary QCA CPU headers are disabled.

## Switch programming

Use qca8k's regmap API to preserve MDIO locking and its cached page selector.
ACL entries use the indirect function window: command `0x400`, data
`0x404..0x414`. Direct SRAM action writes corrupted mask words on QCA8337.
Complete-entry writes hold `reg_mutex`, invalidate first, publish the mask last
and verify values, actions and masks after committing.

| ACL slots | Purpose |
| --- | --- |
|89| Reject external S-tags on user ports |
|90–93| Match port SVID, native C-VID and owning CPU; select one user port |
|94, then free entries 0–88| Exact customer C-VID/port/conduit steering; one entry per port/VID |
|95| Empty destination for unmatched CPU traffic |

Slots 89–95 must be free before activation. Customer admission supports up to
56 port/VID rows and claims additional free entries without replacing other
ACL owners. No VLAN-translation entries are used. The CPU default must not use `DP_ACT=DROP`: hardware ORs forwarding
commands across matching rules, which would also drop valid steering.
Headerless forwarding enables eligible flood destinations; VLAN and port
membership constrain them.

ACL destination overrides can bypass egress VLAN membership. Customer-VLAN
steering therefore requires an exact SVID/CVID/conduit match, published only
after user and CPU VTU membership succeeds. Every native rule also matches its
inner domain VID. Removing either customer membership invalidates its steering
rule before VTU deletion; removing a native domain invalidates that port's native rule first.
An update/readback failure invalidates the rule and holds the affected port
down until switch reinitialization. Retries return `EIO` while quarantined;
hardware cleanup remains best effort if subsequent MDIO operations fail.

## Supported scope and limits

Native standalone ports and VLAN-unaware bridges are implemented. Native
identity, MAC moves, full-MTU traffic and cold boot are verified on LAN1–3.
Standalone 802.1Q uppers and VLAN-aware bridges use independent port/VID rows.
Normal bridge VLAN membership, tagged/untagged egress and PVIDs program the
VTU and per-port C-VID without changing the exact port S-VID. CPU membership
is shared by VID and conduit, independently of user-port membership. Bridge
VLAN objects are cached while filtering is disabled and replayed when enabled;
customer grants are withdrawn before returning to native mode.

Hardware checks cover access/tagged VLAN35 switching between LAN1 and LAN2,
multiple VIDs on a port, full-MTU traffic, customer membership withdrawal and
re-add, and filtering disable/re-enable. Known-unicast VLAN35 TCP reaches
about 938 Mbps in both directions, with no matching bulk frames on the CPU
conduit and no NSS routed-packet increase. Unknown customer VIDs, wrong-port
SVIDs and deleted membership are rejected. These do not certify LAG, all
control protocols, or arbitrary live conduit changes.
The PHY-linked CPU5 lacks carrier with the current host Ethernet stack,
including ordinary DSA mode. The native NSS profile brings up both CPU links
at 1 Gbps and forwards native IPv4/IPv6 routed traffic through WAN.

NSS is disabled in the host QinQ profile. `redmi_ax3000_nss` uses native
NSS-DP with upstream phylink and admits QCA native standalone/unaware-bridge
paths through the shared QinQ adapter and ECM. Routed VLAN-aware bridge uppers
are admitted only when the upper is currently published and the selected port
is a current VID member. Unaware customer bridging, tunnels and non-bridge
master paths remain excluded for QCA. Firmware VLAN-node creation
and IPv4 NAT/TCP, bidirectional IPv4 UDP, and routed IPv6 TCP/UDP acceleration
are verified with live ECM state and NSS packet counters. IPv4 UDP qualifies
under the default bidirectional policy. Controlled one-way IPv6 UDP tests use
`accel_delay_pkts=0`, then restore the default `1`; default-policy non-PPPoE
IPv6 UDP with an external bidirectional peer remains unverified. Full-MTU traffic,
software forwarding, IPv6 neighbour rediscovery, Packet Too Big handling and
accelerated TCP at MTU1280 pass. Automatic NSS startup passes warm and cold
boot. This does not certify NSS L2 bridging, multicast or tunnels. Wi-Fi
validation is described below.

For standalone customer traffic, configure `wan.35` through normal netifd WAN
settings. Its registered upper grants a firmware leaf below the port's SVID
root. RX retains its customer tag for Linux VLAN demultiplexing; native RX
still strips the synthetic inner domain. TX retains one customer tag, including
PCP/DEI, and adds the private S-tag. Reserved transport VIDs and customer S-tags
are rejected. Removing the upper withdraws its NSS leaf and hardware steering;
a stale firmware leaf does not grant host admission. A CPU-conduit change with
surviving customer references returns `EBUSY`; bridge joins are also refused
while those references remain. DSA may withdraw and replay VLAN objects during
a live conduit change, allowing it past that guard. This transition remains
unverified: remove the upper explicitly before moving or bridging the port.
VLAN35/4094 on WAN and VLAN35 on standalone LAN2 pass IPv4 NAT/TCP acceleration
and full-MTU traffic with an external peer. LAN2 checks also confirm exact
port/C-VID steering, unknown-VID and wrong-port rejection, and withdrawal/recovery
across upper deletion and recreation. Routed IPv4/IPv6 TCP through VLAN-aware
bridge uppers also passes with live ECM acceleration and NSS packet counters,
about 904–930 Mbps and 98–99% CPU idle. Cold-boot validation of an aware bridge
configuration remains open.

Standalone PPPoE is admitted on a native port or one customer 802.1Q upper,
such as `wan.35`, with `kmod-qca-nss-drv-pppoe` selected.
The shared ECM resolver requires one live, currently registered PPP session;
bridged PPP underlays, stacked customer VLANs and nested PPP remain excluded
for QCA. Tagged PPP uses the existing exact port/C-VID steering and firmware
leaf; it adds no switch rule format or additional transport tag. The customer
upper must still be published and its firmware leaf ready. Stale PPP or VLAN
registrations do not authorize acceleration.

Native PPPoE checks cover IPv4 NAT and routed IPv6 acceleration, PAP/CHAP
success and wrong-password rejection, session teardown/recreation with a
reused session ID, and the
1492-byte MTU boundary. Oversized packets receive IPv4
fragmentation-needed or IPv6 Packet Too Big replies. These are local
access-concentrator checks, not validation against an ISP.

An external access concentrator also confirms IPv4 NAT/TCP and routed IPv6
TCP through VLAN35/4094 in both directions, with ECM mode2 and firmware PPP/IP
packet counters. Reconnect with a reused session ID, the VLAN35 IPv4 MTU1492
boundary, PAP on VLAN35 and CHAP-MD5 on VLAN4094 pass; wrong passwords leave
no live PPP registration. Traffic was paced at 100 Mbps, so this does not
establish peak tagged-PPPoE throughput.

Bidirectional UDP echo also passes on native PPPoE and VLAN35/4094 with the
normal classifier policy `accel_delay_pkts=1`. IPv4 and IPv6 payloads of 64,
1200 and full-MTU size (1464/1444 bytes respectively) have exact payload and
reply-count checks, per-flow ECM mode2 and firmware IP/PPP counter proof.
No endpoint loss, corruption or measured drop/header/VID/checksum errors occur
in these low-rate checks; burst behavior and UDP capacity remain unvalidated.

DHCPv6-PD over VLAN35/4094 also passes prefix acquisition, renewal during TCP,
prefix replacement without reconnecting PPP, and acquisition after a PPP
reconnect. A delegated `/56` supplies the existing LAN's configured `/60`;
Windows obtains a preferred `/64` SLAAC address and routed IPv6 TCP from that
address accelerates in both directions. Old SLAAC addresses become deprecated;
DHCPv6 address leases retain their separate preferred/valid lifetimes. The
external test uses the normal explicit WAN6 client with `fakeroutes=0` and
`defaultroute=0` to keep default routes off the test computers. Normal PPP
automatic-client prefix acquisition also succeeds. This is a single-LAN,
paced 100 Mbps check. Installed-image tests after a cold boot also confirm
VLAN35 IPv4 NAT/TCP, default-policy IPv4/IPv6 UDP, and delegated-prefix IPv6
TCP in both directions, including renewal during traffic. ISP interoperability
remains unvalidated.

Use an external peer to validate non-NAT routing. A peer namespace on the same
router, cabled back through another NSS-managed switch port, lets an unchanged
flow tuple re-enter NSS with different port VIDs. This fixture produces rule
flushes and ingress-VID mismatches for non-NAT IPv4/IPv6; NAT can hide the
collision. Do not infer a driver regression from that loop alone.
A same-switch cable fixture can also flood host-bound unknown unicast back
into WAN before PPPoE encapsulation. Eliminate that duplicate path before
interpreting NSS ingress-VID mismatches as a PPPoE failure.

## Wi-Fi NSS

The NSS device node must have `qcom,wlan-dataplane-offload-enabled` before
ath11k registers either radio. This initializes wifili handlers and the
external-radio interface pool. The NSS profile selects ath11k offload with
`frame_mode=2`, the 256 MiB host and NSS memory profiles, and small buffers.
Both IPQ5018 and QCN6122 report 4096 TX descriptors, 2048 completions,
CE5 256/256 and private RX buffers; host TX rings are omitted while offloading.
No firmware carveout changes are required for this selection.

Forwarded IPv4 NAT/TCP through a QinQ LAN port and a station interface is
verified on both bands with bulk-flow ECM mode2 and matching firmware IP and
wifili counters, including the first WPA2 association and data after power
removal. UDP echo on both bands confirms small and full-MTU payload integrity
and default-policy acceleration. Bidirectional iperf UDP uses separate streams:
a stream whose tracker has an unknown side can stay in software under the
normal policy.
The current 2.4 GHz peer has a weak link with UDP timeouts and inconsistent
TCP downloads, so that check establishes operation rather than capacity.
Peak throughput, AP mode, Wi-Fi IPv6, roaming and sustained simultaneous
dual-radio load remain unvalidated. Check minimum
memory headroom during traffic as well as the idle value afterward.

The 256 MiB AX3000 also defaults `vm.percpu_pagelist_high_fraction` to `100`,
as CR881x does, to return cached free pages to the shared allocator sooner.
`95-ipq50xx-pcp-memory` installs `13-ax3000-pcp.conf` only when no administrator
setting exists, including an explicit zero. This leaves the 16 MiB emergency
watermark, DMA pools, NSS limits and firmware reservations unchanged.

IPQ50xx LOW-profile linear NSS receive refills use slab-backed SKB heads.
Long-lived firmware buffers can otherwise pin partly used 32 KiB fragment
pages while short-lived status buffers cycle through the allocator. Slabs
reuse freed head objects while other buffers remain live. This can cost more
than tightly packed fragments initially, but avoids the observed backing-page
growth after using both radios. `NET_SKB_PAD`, `SKB_ALLOC_RX`, atomic allocation
and DMA buffer lengths are preserved. Pool counts and watermarks are unchanged;
paged/jumbo refills and other memory profiles retain their existing allocators.

AX3000 requires `qcom,smp2p-feature-ssr-ack` on `wcss_smp2p_out` to restart
the shared Q6 after removing `ath11k_ahb`. Without it, restart times out and
the following attempt reports an unbalanced IRQ enable; the older non-NSS
image has the same failure. With it, repeated reloads pass after warm and cold
boot. After explicitly running `wifi down`, use `wifi up` once both radios
have registered; registration may complete after `modprobe` returns. Wired
NSS nodes remain active throughout these Wi-Fi reloads.

For cold-boot acceptance, test the first WPA2 association and forwarded data
after removing power. Warm registration alone is insufficient. Check each
radio's wifili TX/completion and REO counters alongside the actual ECM data
flow, and compare allocation/drop counters before and after traffic. REO error
events alone do not identify a switch-tag or buffer-allocation failure.

## Build and checks

Use `configs/redmi_ax3000_qinq.config` for QinQ or
`configs/redmi_ax3000_dsa.config` for the standard DSA comparison;
`configs/redmi_ax3000_nss.config` enables wired and Wi-Fi NSS with the 256 MiB
allocation profile.
Sysupgrade archives retain `BOARD_NAME=redmi_ax3000` for compatibility with
older upgraders.

Against the prepared kernel source:

```sh
python3 tools/qca8k/check-mdio.py "$LINUX_DIR/drivers/net/dsa/qca"
python3 tools/qca8k/check-qinq-setup.py "$LINUX_DIR/drivers/net/dsa/qca"
```

These cover MDIO transactions, ACL-window failures, active-rule updates,
shared CPU membership, bridge filtering/PVID transitions and readback
corruption. `check-qinq-tagger.py` checks frame construction and admission;
`tools/nss/test_qosmio_qinq.py` checks adapter/ECM policy. Host tests do not
establish packet forwarding or NSS firmware behavior.

For default-policy UDP checks, exchange traffic in both directions. The ECM
service sets `accel_delay_pkts=1` after loading the module; direct `insmod`
replacement bypasses that initialization. Restore policy `1` before comparing
results. Match the complete flow tuple in either orientation: ECM can create
the accelerated flow when it first sees the reply.

When testing a tagged Linux endpoint, distinguish its NIC transmit failures
from switch drops. The test workstation's r8169 NIC reproducibly stopped
completing TX when tagged TCP began; ICMP still passed before the stall.
Reopening that NIC restored management. Disabling only VLAN insertion offload
was insufficient; software TX checksums allowed sustained tagged TCP. Snapshot
and restore endpoint offload settings around this control; no router workaround
is implied.
