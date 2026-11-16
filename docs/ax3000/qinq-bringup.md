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
`accel_delay_pkts=0`, then restore the default `1`; default-policy IPv6 UDP
with an external bidirectional peer remains unverified. Full-MTU traffic,
software forwarding, IPv6 neighbour rediscovery, Packet Too Big handling and
accelerated TCP at MTU1280 pass. Automatic NSS startup passes warm and cold
boot. This does not certify NSS L2 bridging, multicast, tunnels or Wi-Fi.

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
establish peak tagged-PPPoE throughput. Tagged UDP, DHCPv6-PD, cold boot and
ISP interoperability remain unvalidated.

Use an external peer to validate non-NAT routing. A peer namespace on the same
router, cabled back through another NSS-managed switch port, lets an unchanged
flow tuple re-enter NSS with different port VIDs. This fixture produces rule
flushes and ingress-VID mismatches for non-NAT IPv4/IPv6; NAT can hide the
collision. Do not infer a driver regression from that loop alone.
A same-switch cable fixture can also flood host-bound unknown unicast back
into WAN before PPPoE encapsulation. Eliminate that duplicate path before
interpreting NSS ingress-VID mismatches as a PPPoE failure.

## Build and checks

Use `configs/redmi_ax3000_qinq.config` for QinQ or
`configs/redmi_ax3000_dsa.config` for the standard DSA comparison;
`configs/redmi_ax3000_nss.config` enables wired NSS with Wi-Fi NSS disabled.
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

When testing a tagged Linux endpoint, distinguish its NIC transmit failures
from switch drops. The test workstation's r8169 NIC reproducibly stopped
completing TX when tagged TCP began; ICMP still passed before the stall.
Reopening that NIC restored management. Disabling only VLAN insertion offload
was insufficient; software TX checksums allowed sustained tagged TCP. Snapshot
and restore endpoint offload settings around this control; no router workaround
is implied.
