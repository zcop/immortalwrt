# NSS + DSA features: CR881x and AX3000

Reviewed: 2026-11-23. Hardware evidence covers Xiaomi CR8812DX with YT9215S
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
| Concurrent PPPoE on one native WAN | Confirmed: eight concurrent IPv4 NAT/TCP sessions; all seven surviving streams completed across one reconnect and ECM recreated their acceleration rules. | Confirmed: eight concurrent IPv4 NAT/TCP sessions. The seven surviving TCP streams completed across one session reconnect, with ECM rule recreation. |
| Concurrent PPPoE across physical WAN ports | Confirmed: four IPv4 NAT/TCP sessions on WAN and four on LAN3 with separate peers. All seven surviving streams completed across one LAN3 session reconnect, with ECM rule recreation. | Confirmed: four IPv4 NAT/TCP sessions on WAN and four on LAN3 with separate peers. The seven surviving TCP streams completed across one session reconnect, with ECM rule recreation. |
| PPPoE over one customer VLAN | Confirmed: VLAN35 | Confirmed: VLAN35/4094; tagged TCP checks paced at 100 Mbps |
| PPPoE PAP/CHAP, wrong-password rejection and reconnect | Confirmed with a controlled access concentrator | Confirmed with a controlled access concentrator |
| PPPoE IPv4/IPv6 bidirectional UDP | Confirmed | Confirmed on native WAN and VLAN35/4094 under the default classifier policy |
| PPPoE DHCPv6-PD, renewal and renumbering | Confirmed | Confirmed, including installed-image traffic checks |
| MTU boundaries and IPv6 Packet Too Big | Confirmed | Confirmed |
| 5 GHz Wi-Fi STA/client routed IPv4 | Confirmed, including cold first association/data and live traffic. Peak throughput and sustained-load reliability unqualified | Confirmed, including cold first association/data; peak capacity unvalidated |
| 2.4 GHz Wi-Fi STA/client routed IPv4 | Partial: offload demonstrated; severe throughput limits in the test fixture | Partial: TCP/UDP offload demonstrated; weak-link performance remains unresolved |
| Wi-Fi AP client traffic | Confirmed: bounded simultaneous routed IPv4 TCP downloads on 2.4 GHz and 5 GHz with one client per band. Sustained dual-radio load and simultaneous uploads remain open. | Confirmed: simultaneous routed IPv4 TCP on 2.4 GHz and 5 GHz with one client per band. Routed IPv6 TCP confirmed with one 5 GHz client. |
| Wi-Fi IPv6 | Unvalidated | Confirmed: 5 GHz AP routed TCP; STA, 2.4 GHz and UDP coverage remain open |
| Wi-Fi WDS | Four-address peer interface registered. Forwarding acceleration remains unvalidated. | The shared 5 GHz AP and WDS station triggered a Wi-Fi firmware fault. Station-only WDS association passed. |
| 5 GHz Wi-Fi mesh (802.11s) | Partial. WPA3-SAE peering, rejoin and routed IPv4 TCP NSS offload with a 5 GHz AP on the same channel | Partial. WPA3-SAE peering and rejoin with a 5 GHz AP on the same channel. The routed flow from AX3000 was accelerated by CR881x. |
| Wi-Fi fast roaming (802.11r) | FT-PSK AP setup verified; client handoff unvalidated | FT-PSK AP setup verified; client handoff unvalidated |
| Wi-Fi AP VLAN | Unvalidated | Unvalidated |

Live CR881x 5 GHz STA confirmation uses active ECM rules and increasing NSS
IPv4 and wifili RX/TX counters during ordinary traffic. Peak throughput and
sustained-load reliability require controlled testing. FCS/REO reception errors
were observed, and their cause remains unisolated.

AX3000 routed AP checks use test Wi-Fi subnets and an external wired peer.
The dual-radio IPv4 run sustained 12 and 64 Mbps of paced download for
15 minutes, followed by a minute at 6 and 24 Mbps upload for the 2.4 GHz and
5 GHz clients, respectively. The exact TCP flows were simultaneously
accelerated in both phases. Wi-Fi NSS counters increased on both radios and
available memory stayed above 28 MiB. See [AX3000 Wi-Fi NSS](ax3000/qinq-bringup.md#wi-fi-nss).

The CR881x 2.4 GHz AP check used a separate Wi-Fi subnet with one WPA2
station. A paced TCP echo crossed the wired LAN and AP via a scoped
destination NAT rule. The exact ECM rule and IPv4/internal-radio counters
confirmed offload with the existing 5 GHz STA uplink active.
See [CR881x Wi-Fi validation](cr881x/nss-qosmio-wifi.md#24-ghz-ap-routed-traffic).

The private outer S-tag identifies the physical DSA port. The inner tag carries
the native forwarding domain or one customer VLAN. Customer S+C QinQ would
require an additional tag and is not an admitted NSS path. CR881x has a separate
[standard DSA customer-QinQ profile](cr881x/standard-dsa-qinq-fallback.md) using
host forwarding.

UDP qualification depends on the tracker and classifier policy. Sparse or
one-way traffic staying in software is not sufficient evidence of a dataplane
failure. PPPoE tests use local access concentrators. ISP interoperability,
concurrent tagged PPPoE, duplicate session ID and peer MAC across physical
ports, and maximum tagged-PPPoE throughput remain unvalidated on hardware.

### 5 GHz mesh

The CR881x and AX3000 NSS profiles select `CONFIG_ATH11K_NSS_MESH_SUPPORT`
and include a mesh-capable wpad. The option selects the NSS mesh manager
and kernel mesh support on IPQ50xx with NSS firmware 12.2 or 12.5. Keep
`nss_offload=1` on ath11k and configure SAE mesh with the same channel
configuration on both nodes.
An AP sharing the radio with mesh needs a distinct interface MAC.

The 12.2 check used channel 149 at 20 MHz. Both QCN6122 radios kept their APs
active and registered mesh encapsulation and decapsulation interfaces with
NSS. Both mesh peers authorized, exchanged IPv4 packets and rejoined after
teardown. TCP streams from AX3000 over routed and bridged mesh backhaul to an
external wired peer reached ECM acceleration mode 2 on CR881x. The bridged
run also increased NSS encapsulation counters on CR881x and decapsulation
counters on AX3000. Satellite Wi-Fi client load, wider channels,
firmware-restart recovery and a live 802.11r handoff remain unvalidated.

To use host forwarding for 802.11s on QCN6122, add
`nss_offload_qcn6122=0` to the existing `ath11k` line in
`/etc/modules.d/ath11k` and reboot:

```text
ath11k nss_offload=1 frame_mode=2 nss_offload_qcn6122=0
```

The option selects the host datapath for every interface on QCN6122. The
IPQ5018 2.4 GHz radio and Ethernet keep their NSS settings. Use a mesh-capable
wpad package such as `wpad-mesh-mbedtls` or `wpad-openssl` for SAE.

The host datapath also passed SAE peering, rejoin, and routed and bridged
mesh backhaul checks at 20 MHz. A concurrent wired IPv4 NAT flow on CR881x
remained NSS-accelerated.

## Other NSS engines

| Feature | CR881x / YT9215S | AX3000 / QCA8337 |
| --- | --- | --- |
| NSS L2 bridge acceleration | Unvalidated: native Wi-Fi/QinQ pair mapping implemented; no device acceptance | Partial: 5 GHz AP ↔ native QinQ port in one VLAN-unaware bridge; IPv4/IPv6 TCP and UDP on WAN and LAN conduits confirmed in both directions |
| L2 bridge policy compatibility | Unvalidated | Confirmed on LAN conduit with IPv4/IPv6 TCP and ICMP: isolation, VLAN/PVID restrictions, host fallback and native NSS recovery; UDP fallback unvalidated |
| Routed IPv4 IPTV via NSS | QinQ uses software forwarding. YT9215S replication backend pending. | Partial. One wired receiver passed `igmpproxy` join, leave and rejoin under the default ECM policy. Automatic multi-port membership remains open. |
| NSS WAN QoS | Confirmed simultaneous upload and download shaping for accelerated IPv4 TCP on native WAN, WAN VLAN 35, and single-session PPPoE over VLAN 35. Native WAN loaded latency improved. | Confirmed simultaneous upload and download shaping for IPv4 TCP on native WAN, WAN VLAN 35, and single-session PPPoE over VLAN 35. Untagged PPPoE download also passed with NSS CoDel. |
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
performance. Multi-client L2 acceptance remains open.

On AX3000, `qca8k.qinq_multicast=1` enables the experimental IPv4 routed
path. Configure the IPTV upstream and downstream in the multicast routing
daemon. The downstream is one VLAN-unaware wired bridge with IGMPv2
snooping and a local querier. Linux supplies the receiver set and the
switch driver supplies the QinQ tags for one NSS output.

ECM resolves the upstream independently. It can use a native QinQ WAN
port or a separate, registered NSS Ethernet MAC, either untagged or with
one 802.1Q upper. The ingress and egress must use distinct NSS physical
interfaces. The separate-MAC path awaits device validation. A tagged
WAN upper on a DSA port, Wi-Fi bridge members and other unsupported
configurations use software forwarding.

With the default ECM setting `accel_delay_pkts=1`, a verified one-way IPv4
QinQ multicast path can qualify after two software-forwarded packets. With
bridge fast leave, the last receiver leaving withdrew the NSS rule. With
fast leave disabled, NSS retained the rule while Linux retained the
multicast route and withdrew it when Linux removed the route. Use an ECM
module built against the same kernel configuration as the image.

### Wired WAN QoS

The AX3000 and CR881x NSS profiles include `nss-wan-qos`. Shaper builds
reserve 1 MiB for the firmware pool at boot. Rate limits are disabled by
default. The service shapes upload at the WAN conduit and download on an NSS
IFB. The service uses NSS FQ-CoDel for upload and for downloads on non-PPPoE
WANs. When `network.wan.proto` is `pppoe`, download uses NSS CoDel. It
requires a routed wired WAN on a dedicated QinQ conduit and rejects existing
custom queues on the conduit. The YT9215S switch shaper has separate per-port
controls.

Set rates in kbit/s, with zero for an uncapped direction.

```sh
uci set nss_wan_qos.wan.enabled=1
uci set nss_wan_qos.wan.upload=100000
uci set nss_wan_qos.wan.download=300000
uci commit nss_wan_qos
/etc/init.d/nss-wan-qos restart
```

Use `tc -s qdisc show dev eth0` and `tc -s qdisc show dev ifb-nss-wan`
to inspect shaping on the default WAN conduit. Disable rate limits and
reload the service to remove its queues. AX3000 WAN VLAN 35 kept both
IPv4 TCP flows accelerated under simultaneous caps and returned to uncapped
rates after the queues were removed.

On AX3000, two alternating on/off comparisons used a lab WAN bottleneck of
100 Mbps in each direction. Under simultaneous accelerated IPv4 TCP upload
and download, 80 Mbps NSS FQ-CoDel caps reduced p95 UDP round-trip latency
from about 113 ms to 3–4 ms. Idle p95 was about 3 ms. This check used the
native WAN with a fixed upstream queue and a small forwarded UDP echo flow.

On CR881x, the same test used an 80 Mbps lab WAN bottleneck and 64 Mbps
NSS FQ-CoDel caps, with a 100 Mbps LAN client link. Across two runs, p95 UDP
round-trip latency fell from 76–112 ms to about 4.7 ms. Simultaneous shaped
TCP upload and download sustained about 60 Mbps each, with ECM acceleration
and activity on both NSS queues.

On CR881x with a gigabit LAN client, tagged WAN VLAN 35 yielded 92 Mbps
upload and 189 Mbps download at 100/200 Mbps caps using FQ-CoDel. One PPPoE
session over VLAN 35 yielded 83/256 Mbps at 100/300 Mbps caps, with FQ-CoDel
on upload and CoDel on download. Both TCP flows were accelerated together,
both queues carried traffic, and uncapped rates returned after removal.

On AX3000 with firmware 12.2, NSS FQ-CoDel on the download IFB counted
roughly twice the packets reported by the NSS IGS node for accelerated
PPPoE. At a 300 Mbps cap, routed IPv4 TCP download reached about 142 Mbps
with FQ-CoDel and 284 Mbps with CoDel on untagged PPPoE. The service selected
CoDel for PPPoE on VLAN 35, where simultaneous 100 Mbps upload and 300 Mbps
download caps yielded 90 and 257 Mbps with both flows still accelerated.
Concurrent PPPoE sessions under shaping and untagged PPPoE shaping on CR881x
remain unvalidated.

## Switch offload and driver controls

| Feature | CR881x / YT9215S | AX3000 / QCA8337 |
| --- | --- | --- |
| LAN-to-LAN hardware switching | Confirmed | Confirmed, including customer VLAN35 switching near 1 Gbps |
| Customer VLAN membership, PVID and tagged/untagged egress | Confirmed in the tested matrix | Confirmed, including withdrawal/re-add and filtering transitions |
| FDB/MDB forwarding domains | Confirmed in the tested matrix | Confirmed after the bridge-database isolation fix, including two separate bridges |
| Same-switch bridged IPTV from WAN to TV ports | Unvalidated | Unvalidated |
| Switch replication of CPU-injected QinQ multicast | Unvalidated | An exact ACL bitmap replicated QinQ frames from both host injection and NSS. Changing the bitmap changed the receivers under the same NSS rule. Ordinary MDB lookup did not forward the tested CPU frames. |
| STP/MST under the QinQ profile | Confirmed in the tested matrix | Unvalidated |
| Hardware ingress ACL/policing, mirroring and TBF shaping | Selected paths confirmed | Comparable QinQ control coverage unvalidated; transport ACLs alone do not establish it |
| Strict-priority / weighted contention | Acceptance incomplete | Unvalidated |

The full [YT921x driver inventory](../README.yt921x.md) covers additional
switch features. Its DONE baseline does not imply every NSS/QinQ combination
has been retested. Hardware switching can bypass both Linux and NSS; CPU packet
captures alone cannot measure all offloaded traffic.

## Remaining qualification and references

Sustained dual-radio AP load on CR881x, larger client counts on AX3000,
roaming, repeated boot cycles and long-duration memory testing remain open. QCA
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

For release builds, refresh the dashboard/theme feed after selecting the
profile and before compiling. This also regenerates dependencies and cleans
the two UI packages. The record pins their source revision and versions for
the release. A reused build tree needs this step for each new release.

```sh
python3 scripts/release-apps.py prepare --record tmp/release-apps.json &&
make &&
python3 scripts/release-apps.py check --record tmp/release-apps.json \
  --manifest bin/targets/qualcommax/ipq50xx/*.manifest
```

For both profiles in one release, retain the same app record and verify each
profile's manifest with its corresponding `.config`. The check rejects stale
or missing selected packages and feed changes after preparation. Commit-pinned
feeds continue to follow their configured revisions.
