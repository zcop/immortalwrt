# PPPoE over the CR881x QinQ dataplane

Build46 hardware checks on2026-11-11 confirm routed IPv4 NAT/TCP and routed
IPv6 TCP/bidirectional UDP through one PPPoE session on plain WAN and `wan.35`.
The device is CR8812DX/IPQ5018 +
YT9215S, Linux6.18.60, NSS firmware12.2 and the256MiB allocation profile.
This is bounded PPPoE acceptance; it does not certify DHCPv6-PD, authentication,
multiple concurrent sessions, sustained soak or firmware-destroy error recovery.

## Mapping and lifetime

ECM overlay `0110-qinq-admit-single-pppoe-session.patch` permits a single
innermost PPPoE interface above the existing exact Ethernet/bridge/VLAN underlay.
The device must be point-to-point `ARPHRD_PPP`, with a live NSS interface number
matching ECM's cached acceleration-engine identifier. Missing registrations,
stale identifiers, nested PPP and unsupported hierarchy types are rejected.
Existing QinQ membership, leaf ownership and retry checks still apply.

The ordinary IPv4/IPv6 frontends continue to own PPPoE session/peer fields.
The QinQ resolver supplies the physical conduit, private port SVID, inner VID
and next-hop leaf. No guessed PPPoE manager `base_if_num` override is added.
The shared IPv6 source path passes the same-image data checks below.

Clients overlay `0101-pppoe-hold-channel-device-through-setup.patch` keeps the
held lower-netdev reference returned by channel lookup until session setup
or failure unwind completes. All exits after successful lookup release it.
The previous helper released the reference before its caller used `opt.dev`.

## Tags and configuration

Configure the usual OpenWrt PPPoE WAN on `wan`, or create a normal802.1Q upper
and use `wan.35`. Session creation, ECM rules and QinQ nodes are automatic.
There is no manual NSS arm step. Normal netifd PPPoE handling selects MTU1492
with a1500-byte Ethernet lower device.

```text
CPU conduit, native: [DA][SA][88a8 SVID=3075][8100 transport VID=3075][8864 PPPoE][PPP][IP]
CPU conduit, VID35:  [DA][SA][88a8 SVID=3075][8100 customer VID=35]   [8864 PPPoE][PPP][IP]
WAN wire, native:   [DA][SA]                                    [8864 PPPoE][PPP][IP]
WAN wire, VID35:    [DA][SA]               [8100 customer VID=35] [8864 PPPoE][PPP][IP]
```

The CPU-side tag format follows the existing QinQ transport. YT9215S handles
ingress transport tagging and port steering/removal; the firmware generates
the configured tags on accelerated egress. Linux installs the interfaces and
rules rather than tagging each accelerated packet in software. The VLAN35
WAN capture verifies the external layout and absence of the private S-tag;
CPU-side PPPoE frames were not separately captured in this slice.

## Hardware results

Eight-second TCP runs used a MACVLAN LAN client and a PPPoE access concentrator
on the workstation's Ethernet NIC. A spare LAN1-to-WAN cable provided the
physical WAN path. Windows was disconnected during these tests; Linux stayed
on LAN3 with its original Wi-Fi and default route. The TCP destination was
the AC's PPP address198.18.45.1, with NAT replies to the router's PPP address,
so the test required routed PPPoE rather than a local Ethernet shortcut.
WAN stayed outside `br-lan`; loop protection remained enabled as a safeguard.

| Gate | Result |
| --- | --- |
| Plain PPPoE, MTU1492 |847.1Mbps LAN-to-AC /931.3Mbps reverse |
| PPPoE on WAN VLAN35 |846.7/931.3Mbps |
| Reconnect with reused SID201 and peer MAC |848.0/931.3Mbps; new PPP netdev and NSS interface |
| ECM and firmware evidence | Accelerated rules and increasing firmware counters in both directions for all three runs |
| Session withdrawal | Old NSS PPP registration absent after disconnect; reused-SID registration changed from interface29 to32 and netdev12 to13 |
| MTU boundary on VLAN35 |1492-byte DF IP packet delivered;1493 rejected with ICMP fragmentation-needed reporting1492 |
| VLAN35 WAN wire |500PPPoE frames; only customer VLAN35, no private S-tag leak; maximum observed IP packet1492 bytes |
| Native DHCP regression before PPP fixture |936.5/933.5Mbps with ECM/firmware evidence |

ECM dumps name the actual LAN client, PPP destination, customer VLAN35, SID201
and new PPP acceleration interface32. PPP base drop and malformed-header
counters remained zero at the final disconnect. These short runs do not
establish a long-term loss bound or explain the forward/reverse speed difference.
No PPPoE CPU-idle measurement was taken.

Two fixture setup errors were corrected before accepted runs: setting the
netifd WAN MTU option reduced the Ethernet lower device as well, yielding PPP
MTU1484; disabling the default route also required an explicit route to the AC
peer. Those initial failures are not recorded as firmware regressions.

## IPv6 and dual-stack acceptance

The same Build46 image passes IPv6CP negotiation and routed IPv6 without a
source fix or rebuild. Netifd's `ipv6=1` enables IPv6CP without automatically
starting a DHCPv6 client. The AC and router negotiated `fe80::1`/`fe80::2`;
controlled global-scope ULA addresses and return routes were then assigned
to the PPP endpoints. The LAN namespace used the router's existing LAN prefix,
so no new LAN prefix or workstation address/default route was configured.
This verifies the routed dataplane, not DHCPv6 prefix delegation or ISP setup.

| Gate | Plain WAN, SID301 | WAN VLAN35, SID401 | VLAN35 after SID401 reuse |
| --- | --- | --- | --- |
| IPv6 TCP LAN-to-AC / reverse |832.7/918.1Mbps |831.2/918.3Mbps |831.7/918.4Mbps |
| IPv4 NAT/TCP LAN-to-AC / reverse |848.3/931.1Mbps |848.5/931.1Mbps |848.6/931.1Mbps |
| IPv6 UDP echo at50Mbps in each direction |31250 sent/returned |31250 sent/returned |31250 sent/returned |
| IPv6 packet size1492 | Delivered | Delivered | Delivered |
| IPv6 packet size1493 | Packet Too Big, MTU1492 | Packet Too Big, MTU1492 | Packet Too Big, MTU1492 |

TCP runs lasted eight seconds. Each UDP run lasted six seconds and used one
bidirectional tuple, preserving the default ECM classifier policy. All IPv6
TCP/UDP dumps identify the actual test addresses, `accel_mode=2`, expected
session ID and current PPP NSS registration, with increasing firmware counters.
Ingress VID mismatch/missing, VLAN marking mismatch, interface mismatch,
missing PPP session and IPv6 rule-drop counters stayed zero in the captured
midflow snapshots. These are short functional tests, not long-term loss bounds.

Each phase's500-frame WAN capture contains IPv6 PPPoE payload, with no VLAN on
plain WAN or exactly customer VLAN35 on the tagged path. No private S-tag is
visible externally; maximum observed IPv6 packet size is1492 bytes. After
disconnect, the old PPP registration disappeared. Reusing SID401 and the peer
MAC created a new netdev17-to19 and NSS interface32-to33; both address families
then accelerated again. No firmware ABI, memory allocation or switch-register
changes were made for this acceptance step.

Docker's IPv4-only MACVLAN network initially disabled IPv6 on the test Ethernet
endpoint. The LAN namespace was recreated using the per-endpoint IPv6 sysctl;
this fixture correction was separate from router firmware. A guard expiry
while preparing the fixture also held test ports down as designed; accepted
runs used a freshly armed guard and completed before expiry.

Evidence is under `logs/cr881x-pppoe-ipv6-20261111/`: `RESULT.json`,
`qualified-evidence.json`, per-phase `wire-proof.json`, `mtu6.json`, ECM dumps,
session negotiation logs and reconnect-before/down/after captures. The source
review is static evidence; acceptance rests on the recorded traffic, firmware
counters, exact ECM hierarchy and WAN captures.

## Source checks and image identity

`tools/nss/test_qosmio_qinq.py` runs extracted production resolver functions
for native/VLAN PPPoE and stale, missing, malformed and nested registrations,
alongside the existing collector/notifier/retry gates.
`tools/nss/test_pppoe_device_lifetime.py` runs production session lookup/setup
with address/undefined-behaviour sanitizers, checking success and eleven
failure paths for balanced lower-device references and resource unwind.

Build45 was rejected because mac80211 reused a source tree prepared for the
standard profile, omitting NSS patches despite the selected configuration.
The separate mac80211 fix adds `CONFIG_ATH11K_NSS_SUPPORT` to
`PKG_PREPARED_DEPENDS`; Build46 reapplied the conditional patches and verified
NSS parameters,256MiB Wi-Fi flags, firmware ABI and unchanged reservations.

Accepted Build46 sysupgrade SHA256:
`759265f07859e1947719b489ce66a0b8281e9238d516ccec35b7e0fb27b5c63f`.
The preserved Build42 fallback remains in slot0; Build46 is in slot1.
Raw results and temporary fixture restoration state are retained locally in
`logs/cr881x-pppoe-nss-20261111/`, including `CURRENT.md`, `traffic-native1492/`,
`traffic-vlan35/`, `traffic-reused-sid201/`, `same-session-reconnect.json`,
`mtu46-result.json` and `vlan35-wire-proof.json`.

After the IPv6 tests, spare-cable removal and Windows reconnection to WAN were
confirmed. The exact original network/wireless/firewall/DHCP hashes were
restored, with no pending UCI changes. Both address families had zero accelerated
flows and no PPP registration after disconnect. The owned Docker namespaces,
network and temporary router boot guard were removed. Fresh DHCP WAN/NSS traffic
then passed at936.3/934.1Mbps, with increasing firmware counters, eight normal
QinQ nodes and no kernel warnings. UART closed without a reset. Final evidence:
`logs/cr881x-pppoe-ipv6-20261111/final-dhcp/`, `cleanup-final.txt` and `RESULT.json`.
