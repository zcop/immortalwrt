# Qosmio NSS features confirmed on CR881x

Device checks updated: 2026-11-11. Device: CR8812DX/IPQ5018 + QCN6122 +
YT9215S, native Qosmio/QinQ profile. Installed test image: Build46 in slot1,
Linux6.18.60, NSS firmware12.2, LOW memory and the corrected256MiB Wi-Fi
allocation profile. Build42 is the preserved slot0 fallback. Source remains
under development. The upstream table comparison below dates from2026-11-10.

The [Qosmio support matrix](https://github.com/qosmio/openwrt-ipq/blob/main-nss/README.md#nss-support-matrix)
was checked against the live upstream page. Its columns cover IPQ807x/IPQ60xx,
not IPQ5018. Its upstream status is reference information, not acceptance for
this device. The selected feed remains pinned to
`0d970dbf0185e3f53709bd803e8a466598023c57`; platform reference is
`92a2d104145c8d265851c4b388a41bd8e9c21cd9` with local IPQ50xx adaptations.

A confirmed NSS data path needs matching traffic, ECM rules and firmware packet
counters. A compiled flag, loaded module, successful ping or YT9215S hardware
switching does not establish that claim. Results below retain their test-image
scope; earlier accepted features have not all been re-run on Build46.

## Core forwarding, beyond the upstream feature labels

| Function | Device evidence | Scope |
| --- | --- | --- |
| IPv4 NAT/routed TCP | Confirmed NSS: latest Build46 DHCP WAN936.2/931.5Mbps after the DHCPv6-PD fixture was restored, with ECM and firmware counters | One1Gbps endpoint fixture; earlier Build42 measured98.25%CPU idle; no new CPU-idle measurement |
| IPv4 routed UDP | Confirmed NSS with per-flow ECM mode2 and firmware counters | Default classifier policy1 waits for bidirectional qualification; controlled one-way tests temporarily used policy0 and restored it |
| IPv6 routed TCP/UDP | Confirmed NSS in earlier native30/31 tests | TCP923.71/923.67Mbps; UDP6 neighbour recovery at50Mbps with accelerated rules and TX growth. No NAT66 test; no fresh Build42 IPv6 load test |
| Automatic startup | Confirmed: native core/dataplane/ECM/QinQ load and nodes provision automatically | Build46 startup checked with eight QinQ nodes and both256MiB Wi-Fi profiles; no manual arm service |
| CPU-link backpressure | Confirmed Build42 PAUSE on/off/on removes switch deficit at200/300Mbps | Same buffers and firmware; multiport head-of-line effects remain untested |

## Every row of Qosmio's feature table

The upstream column summarizes Qosmio's IPQ807x/IPQ60xx table, not this device.
“Disabled” means not selected in our current build, not proven impossible.

| Qosmio feature | Upstream table | CR881x device result |
| --- | --- | --- |
| TUNIPIP6 | Supported | Disabled; no tunnel data-path validation; QinQ tunnel hierarchy mapping is not implemented |
| PPPOE | Supported | Confirmed Build46 routed IPv4 NAT/TCP and routed IPv6 TCP/bidirectional UDP with one session on plain WAN and WAN VLAN35. IPv4 reaches about847–849/931Mbps, IPv6 about831–833/918Mbps. MTU1492, IPv6 Packet Too Big and dual-stack session-ID reuse pass. Automatic DHCPv6-PD `/56` to test-LAN `/64`, renewal during TCP, prefix replacement and WAN35 reacquisition also pass with ECM/firmware proof. Authentication and multiple sessions remain unvalidated; see [PPPoE validation](nss-pppoe.md) |
| L2TPV2 | Supported | Disabled; no device acceptance; tunnel mapping gap applies |
| BRIDGE | Supported | NSS L2 bridging is not implemented by the QinQ resolver, which requires routed connections. Ordinary DSA/Linux bridging and YT9215S hardware switching pass their own tests |
| VLAN | Supported | Confirmed NSS for routed traffic using supported single-customer802.1Q paths: aware/unaware bridge VLAN uppers and direct port uppers, including runtime VID35/4094 checks.64-node NSS capacity; full customer S+C tags use separate standard-DSA host forwarding |
| MAP_T | Supported | Disabled; untested; no MAP-T acceptance |
| TUN6RD | Supported | Disabled; untested; no6rd acceptance |
| GRE | Supported | Disabled; untested; tunnel mapping gap applies |
| PPTP | Supported | Disabled; untested; tunnel mapping gap applies |
| IGS | Supported | NSS ingress shaping, not IGMP snooping. IGS and NSS shaper/qdisc are disabled; no NSS shaping acceptance. YT9215S policing/shaping tests are a different engine |
| VXLAN | Supported | Disabled; untested; tunnel mapping gap applies |
| MATCH | Supported | NSS MATCH manager disabled and untested. YT9215S ingress ACL matches pass separately |
| RMNET | FW11.4 on807x; absent on60xx | Disabled and untested; no IPQ5018 support claim |
| MIRROR | Supported | NSS MIRROR manager disabled and untested. YT9215S hardware ingress/egress mirroring passes separately, including established NSS traffic |
| WIFI (AP/STA) | Supported | Partial: QCN6122 5GHz STA routed IPv4 is confirmed NSS, including Build41 cold first association/data and Build42 forwarded TCP/UDP. Build42 internal2.4GHz STA IPv4 also has ECM/wifili proof, but performance acceptance fails at roughly0.5–0.9Mbps TCP in the current fixture. AP mode,2.4GHz cold association and Wi-Fi IPv6 remain unvalidated |
| WIFI (WDS) | Requires FW11.4 | Unvalidated with our FW12.2 profile; no WDS acceptance |
| WIFI (MESH) | Requires FW11.4 | Mesh support/manager disabled; unvalidated with FW12.2 |
| WIFI (AP VLAN) | ath11k driver warning | Untested on this device; no AP VLAN or Wi-Fi/customer-VLAN-combination acceptance. Preserve upstream warning without assuming it is a reproduced local failure |
| IPSEC | Marked unavailable in FW11.4–12.5 | No NSS IPsec acceptance; crypto/IPsec support disabled in this profile |
| PVXLAN | Marked unavailable in FW11.4–12.5 | Disabled; no device acceptance |
| CLMAP | Marked unavailable in FW11.4–12.5 | Disabled; no device acceptance |
| TLS | Marked unavailable in FW11.4–12.5 | No NSS TLS acceptance; ordinary host TLS/HTTPS is a separate function |
| CAPWAP | Marked unavailable in FW11.4–12.5 | No device NSS acceptance |
| DTLS | Marked unavailable in FW11.4–12.5 | No device NSS acceptance |

The important boundary is the current QinQ ECM resolver: its allowed interface
hierarchies are Ethernet, bridge and VLAN, optionally ending in one registered
PPPoE interface. It rejects non-routed flows. The custom mapping is wired into
the ported IPv4/IPv6 frontends. PPPoE hardware acceptance covers IPv4 NAT/TCP
and routed IPv6 TCP/bidirectional UDP. The later DHCPv6-PD gate obtains a real
lease from the controlled AC and advertises its assigned `/64` only on test
VLAN46; the management LAN retains its original ULA and RA policy. Tunnels,
L2 bridging and NSS multicast forwarding must not be inferred from those results.
YT9215S MDB/snooping/discovery tests remain valid switch results.

Both radio firmware instances initialize NSS. Separate traffic/counter tests
now demonstrate both5GHz and internal2.4GHz STA data paths, but only the former
has confirmed cold-association/data coverage. Build42 5GHz TCP download is545.78Mbps with one
stream and666.14Mbps with three, about97%CPU idle. These are5GHz STA fixture
measurements, not AP throughput guarantees. Small residual end-to-end UDP loss
remains despite zero measured switch deficit with PAUSE enabled. The later
2.4GHz test has severe throughput limits and loss on both routed software and
accelerated paths; both still share Wi-Fi NSS reception. It is a functional
offload result, not a performance/reliability pass. A subsequent same-image
Wi-Fi-NSS-disabled control also receives only0.13–0.49Mbps TCP, with FCS and
FrameOOR growth despite about97%CPU idle. This rules out Wi-Fi NSS as the sole
cause of the severe TCP limitation, but does not prove RF interference or
exclude an NSS contribution to loss. The UDP comparison remains confounded
by endpoint buffer errors and measurement intervals. See the2.4GHz section of
[Wi-Fi validation](nss-qosmio-wifi.md).

## Queueing and policy boundary

Our defaults retain NSS, normal queue initialization and matched CPU-link PAUSE;
user shaping/SQM/per-device bandwidth limits remain opt-in. The current build
has NSS IGS, SHAPER and NSS qdisc packages disabled. Tested YT9215S hardware TBF,
PCP queue selection and ingress ACL/policer behavior are distinct from NSS QoS.
Strict-priority/weighted contention acceptance still fails and remains deferred.
Linux bonding/LAG controls do not certify NSS acceleration over a bond; real
two-member LACP/load-sharing also lacks a suitable peer.

## Evidence and freshness

- Current profile flags: `.config`, `configs/xiaomi_cr881x_nss_qosmio.config`.
- Live read-only Build42 inventory, modules, DHCP WAN/no PPPoE session and radio
  initialization: `logs/cr8812dx-nss-feature-matrix-20261110/live-inventory.txt`.
- QinQ resolver restrictions: `tools/nss/patches/qosmio/qca-nss-ecm/0102-yt921x-qinq-native-port-and-vlan-rules.patch`
  and `0106-exact-standalone-8021q-upper-mapping.patch`.
- VLAN and IPv6 gates: [implementation slices](nss-qosmio-dsa-slices.md), especially
  installed Build17 results, later bridge-upper fixes and Slice25.
- UDP6 and movement gates: [daily traffic evidence](nss-qinq-daily-traffic.md).
- Wi-Fi initialization/cold traffic and memory: [Wi-Fi validation](nss-qosmio-wifi.md),
  `logs/cr8812dx-wifi-nss-256m-20261110/REPORT.md`.
- Latest IPv4/Wi-Fi/PAUSE: `logs/cr8812dx-nss-pause-20261110/REPORT.md`.
- Later internal2.4GHz STA test and restoration: `logs/cr8812dx-wifi24-nss-20261110/`.
- Same-image2.4GHz Wi-Fi NSS disabled control and restoration: `logs/cr8812dx-wifi24-control-20261110/`.
- Build46 PPPoE, VLAN35 wire tags, MTU and session replacement:
  [PPPoE validation](nss-pppoe.md), `logs/cr881x-pppoe-nss-20261111/`.
- Same-image IPv6 PPPoE and dual-stack recovery:
  `logs/cr881x-pppoe-ipv6-20261111/`, especially `qualified-evidence.json`.
- Automatic PPPoE DHCPv6-PD, renewal and renumbering:
  `logs/cr881x-pppoe-pd-20261111/`, especially `qualified-evidence.json` and
  the per-phase prefix/RA/client status records. Its `final-dhcp/` and
  `cleanup-final.txt` confirm original configuration and normal NSS restoration.

The original comparison used read-only checks; the later2.4GHz test above had
guarded temporary settings restored. Missing confirmation in other rows is
not a reproduced failure.
