# PPPoE over the CR881x QinQ dataplane

Build46 hardware checks on2026-11-11 confirm routed IPv4 NAT/TCP through one
PPPoE session on plain WAN and on `wan.35`. The device is CR8812DX/IPQ5018 +
YT9215S, Linux6.18.60, NSS firmware12.2 and the256MiB allocation profile.
This is bounded PPPoE acceptance; it does not certify PPPoE IPv6, authentication,
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
The shared IPv6 source path is present, but its PPPoE data path remains untested.

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
`mtu46-result.json` and `vlan35-wire-proof.json`. Test-port/cable restoration
and the final post-restoration DHCP regression remain a separate pending gate;
the pre-test DHCP result above does not stand in for that check.
