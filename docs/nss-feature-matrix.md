# NSS + DSA features: CR881x and AX3000

Reviewed: 2026-11-21. Hardware evidence covers Xiaomi CR8812DX with YT9215S
and Redmi AX3000 with QCA8337. Both use IPQ5018/QCN6122, native Qosmio NSS,
firmware 12.2, LOW NSS memory and the 256 MiB ath11k allocation profiles.

This table describes this tree's implementation and recorded device tests.
Results span several images; they are not a complete retest of the latest
commit or a long-term reliability qualification.

- **Confirmed:** device traffic, ECM rules and NSS packet counters demonstrate
  acceleration. For switch rows, packet/register checks demonstrate switch offload.
- **Partial:** operation is demonstrated, with the stated coverage or performance gap.
- **Unvalidated:** no sufficient device evidence; this does not establish a failure.
- **Excluded:** the current adapter does not admit the interface hierarchy or flow.
- **Disabled:** the NSS manager is not selected in the provided profiles.

## Routing and wireless

| Feature | CR881x / YT9215S | AX3000 / QCA8337 |
| --- | --- | --- |
| Automatic NSS/ECM/QinQ startup | Confirmed | Confirmed |
| Exact DSA port identity through private QinQ tags | Confirmed | Confirmed |
| IPv4 NAT/TCP | Confirmed | Confirmed |
| IPv4 routed UDP | Confirmed, including bidirectional qualification | Confirmed, including bidirectional qualification |
| IPv6 routed TCP | Confirmed | Confirmed |
| IPv6 routed UDP | Confirmed in bounded tests | Partial: acceleration confirmed; ordinary non-PPPoE default-policy external-peer coverage remains open |
| Routing through native VLAN-unaware bridges | Confirmed | Confirmed |
| Standalone customer VLAN uppers, such as `wan.35` | Confirmed, including VID35/4094 cases | Confirmed: WAN VID35/4094 and LAN2 VID35 |
| VLAN-aware bridge VLAN uppers | Confirmed | Confirmed; cold boot with this configuration remains open |
| Customer VLAN uppers on VLAN-unaware bridges | Confirmed | Excluded by the QCA resolver |
| Single-session PPPoE on native WAN | Confirmed: IPv4 NAT and routed IPv6 | Confirmed: IPv4 NAT and routed IPv6 |
| PPPoE over one customer VLAN | Confirmed: VLAN35 | Confirmed: VLAN35/4094; tagged TCP checks paced at 100 Mbps |
| PPPoE PAP/CHAP, wrong-password rejection and reconnect | Confirmed with a controlled access concentrator | Confirmed with a controlled access concentrator |
| PPPoE IPv4/IPv6 bidirectional UDP | Confirmed | Confirmed on native WAN and VLAN35/4094 under the default classifier policy |
| PPPoE DHCPv6-PD, renewal and renumbering | Confirmed | Confirmed, including installed-image traffic checks |
| MTU boundaries and IPv6 Packet Too Big | Confirmed | Confirmed |
| 5 GHz Wi-Fi STA/client routed IPv4 | Confirmed, including cold first association/data and live traffic. Peak throughput and sustained-load reliability unqualified | Confirmed, including cold first association/data; peak capacity unvalidated |
| 2.4 GHz Wi-Fi STA/client routed IPv4 | Partial: offload demonstrated; severe throughput limits in the test fixture | Partial: TCP/UDP offload demonstrated; weak-link performance remains unresolved |
| Wi-Fi AP client traffic | Unvalidated; AP startup checked | Confirmed: 5 GHz AP client routed IPv4 NAT/TCP and IPv6 TCP in both directions; bounded single-client checks |
| Wi-Fi IPv6 | Unvalidated | Confirmed: 5 GHz AP routed TCP; STA, 2.4 GHz and UDP coverage remain open |
| Wi-Fi WDS | Unvalidated with firmware 12.2 | Unvalidated with firmware 12.2 |
| Wi-Fi mesh / roaming | Unvalidated; mesh manager disabled | Unvalidated |
| Wi-Fi AP VLAN | Unvalidated | Unvalidated |

Live CR881x 5 GHz STA confirmation uses active ECM rules and increasing NSS
IPv4 and wifili RX/TX counters during ordinary traffic. Peak throughput and
sustained-load reliability require controlled testing. FCS/REO reception errors
were observed, and their cause remains unisolated.

AX3000 AP checks use a separate Wi-Fi subnet and an external wired peer.
Completed payload transfers, exact ECM rules in accelerated mode and increasing
NSS IPv4/IPv6/wifili counters establish routed offload. These paced transfers
do not measure peak throughput or qualify AP cold boot, multiple clients or
long-term reliability. Bridged AP traffic is covered separately below.

The private outer S-tag identifies the physical DSA port. The inner tag carries
the native forwarding domain or one customer VLAN. Customer S+C QinQ would
require an additional tag and is not an admitted NSS path. CR881x has a separate
[standard DSA customer-QinQ profile](cr881x/standard-dsa-qinq-fallback.md) using
host forwarding.

UDP qualification depends on the tracker and classifier policy. Sparse or
one-way traffic staying in software is not sufficient evidence of a dataplane
failure. PPPoE tests use local access concentrators; ISP interoperability,
multiple sessions and maximum tagged-PPPoE throughput are not established.

## Other NSS engines

| Feature | CR881x / YT9215S | AX3000 / QCA8337 |
| --- | --- | --- |
| NSS L2 bridge acceleration | Unvalidated: native Wi-Fi/QinQ pair mapping implemented; no device acceptance | Partial: 5 GHz AP ↔ native QinQ port in one VLAN-unaware bridge; IPv4/IPv6 TCP on WAN and LAN conduits, plus IPv4/IPv6 UDP on WAN, confirmed in both directions |
| NSS multicast acceleration | Unvalidated | Unvalidated |
| NSS IGS / shaper / qdiscs | Disabled; no NSS shaping acceptance | Disabled; no NSS shaping acceptance |
| TUNIPIP6 / TUN6RD / GRE / L2TPv2 / PPTP / VXLAN | Excluded by current hierarchy mapping; no device acceptance | Excluded by current hierarchy mapping; no device acceptance |
| MAP-T | Disabled, unvalidated | Disabled, unvalidated |
| NSS MATCH / MIRROR managers | Disabled, unvalidated | Disabled, unvalidated |
| RMNET / PVXLAN / CLMAP | No device acceptance | No device acceptance |
| NSS IPsec / TLS / DTLS / CAPWAP | No device acceptance | No device acceptance |
| Bond/LAG acceleration | Unvalidated; no two-member peer test | Unvalidated |

Ordinary Linux VPN, bridge or TLS operation does not establish acceleration by
the corresponding NSS engine. Switch ACL, mirror and multicast tests below
also use a different engine from the NSS managers above.

The initial QinQ L2 path admits known unicast between one Wi-Fi port and one
DSA port in the same VLAN-unaware bridge. Both MACs must currently resolve to
the selected forwarding ports. FDB changes and bridge-port lifecycle/policy
events invalidate rules. Customer-tagged or VLAN-aware bridging, hairpin,
isolated/locked ports and unsupported hierarchies retain host forwarding;
same-switch LAN traffic retains switch forwarding. Broadcast and discovery
remain on their existing bridge paths. UDP acceptance uses paced, unreliable
WebRTC data channels with small and 1000-byte application messages under the
default classifier policy; it does not establish raw full-MTU or peak UDP
performance. UDP on the LAN conduit and multi-client L2 acceptance remain open.

## Switch offload and driver controls

| Feature | CR881x / YT9215S | AX3000 / QCA8337 |
| --- | --- | --- |
| LAN-to-LAN hardware switching | Confirmed | Confirmed, including customer VLAN35 switching near 1 Gbps |
| Customer VLAN membership, PVID and tagged/untagged egress | Confirmed in the tested matrix | Confirmed, including withdrawal/re-add and filtering transitions |
| FDB/MDB forwarding domains | Confirmed in the tested matrix | Confirmed after the bridge-database isolation fix, including two separate bridges |
| STP/MST under the QinQ profile | Confirmed in the tested matrix | Unvalidated |
| Hardware ingress ACL/policing, mirroring and TBF shaping | Selected paths confirmed | Comparable QinQ control coverage unvalidated; transport ACLs alone do not establish it |
| Strict-priority / weighted contention | Acceptance incomplete | Unvalidated |

The full [YT921x driver inventory](../README.yt921x.md) covers additional
switch features. Its DONE baseline does not imply every NSS/QinQ combination
has been retested. Hardware switching can bypass both Linux and NSS; CPU packet
captures alone cannot measure all offloaded traffic.

## Remaining qualification and references

Sustained AP/multiple-client traffic, simultaneous dual-radio load, roaming,
repeated boot cycles and long-duration memory testing remain open. QCA
bridge-domain checks cover bounded packet/routing cases; cold-boot and
endurance qualification remain incomplete. A common feature table does not imply identical
peak rates, advanced switch controls or complete recovery coverage.

- CR881x: [NSS feature details](cr881x/nss-feature-matrix.md),
  [Wi-Fi coverage](cr881x/nss-qosmio-wifi.md),
  [PPPoE coverage](cr881x/nss-pppoe.md) and
  [daily packet delivery](cr881x/nss-qinq-daily-traffic.md).
- AX3000: [QCA8337 QinQ implementation and validation limits](ax3000/qinq-bringup.md).

## Building NSS images

From a fresh checkout of this repository, use the profile for your device.
The example uses the [CR881x profile](../configs/xiaomi_cr881x_nss_qosmio.config).
For AX3000, use the [AX3000 profile](../configs/redmi_ax3000_nss.config).
The feed update applies the repository's NSS patches automatically.

```sh
./scripts/feeds update -a &&
./scripts/feeds install -a &&
cp configs/xiaomi_cr881x_nss_qosmio.config .config &&
make defconfig &&
grep -x 'CONFIG_NSS_FIRMWARE_VERSION_12_2=y' .config &&
make
```

IPQ50xx builds that enable NSS default to the tested firmware **12.2**.
Both profiles also select LOW NSS memory, QinQ and the ath11k 256 MiB
profiles with small buffers. If you keep an older
`.config`, select firmware 12.2 in `make menuconfig` before building.
`make defconfig` preserves an explicit firmware choice. The build check
rejects firmware 11.4 on IPQ50xx when the NSS driver is selected because
its driver and patch directory lack Linux 6.18 compatibility.

Binary package repositories default to ImmortalWrt snapshots. Custom kernel
modules require the repository produced for the matching firmware build;
`CONFIG_VERSION_REPO` selects a self-hosted build repository. Optional extra
URLs belong in `/etc/apk/repositories.d/customfeeds.list`.
