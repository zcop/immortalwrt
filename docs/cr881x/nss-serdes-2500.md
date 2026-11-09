# CR881x 2.5G LAN CPU SerDes with NSS

Updated: 2026-11-09. The active native Qosmio/QinQ profile runs Linux6.18.60
with a2.5Gbps LAN CPU link and a1Gbps WAN CPU link. Installed native31 uses
slot1; tested native30 remains in slot0. NSS and the QinQ adapter start
automatically; the archived `cr881x-nss` manual controller is not part of
this profile.

| Physical path | Binding/path | Link rate |
| --- | --- | ---: |
| LAN switch port8 → GMAC1/eth1 | Native NSS DP, phylink, UNIPHY PCS, `2500base-x` |2500Mbps |
| WAN switch port4 → GMAC0/eth0 | Native NSS DP and existing1G PHY path |1000Mbps |

The board's LAN fixed links select `2500base-x`,2500Mbps/full duplex, with
`motorcomm,serdes-mode = "2500base-x"` on port8. The native NSS DTS overrides
the MAC bindings; the ordinary image retains stmmac. This uses the current
GMAC/UNIPHY description, not the old ESS `dp1`/`dp2` properties. The native
CPU transport carries an outer per-port SVID and an inner transport/customer
CVID; it is no longer the original single-tag NSS experiment.

Restored native31 routed IPv4 reaches about936Mbps in both directions with firmware
packet increments and about98% sampled CPU idle. Both endpoint links are1G,
so this verifies forwarding through the faster internal link, not aggregate
2.5Gbps saturation. Native30 also passes actual routed IPv6, one physical
cold boot and one ordinary warm reboot with NSS loaded. See
[current acceptance status](nss-qosmio-dsa-slices.md#current-acceptance-status)
for exact images, counters and open capacity/reliability gates. Wi-Fi NSS
remains disabled.

The [2026-11-02 SerDes report](reference/nss-serdes-2500.md) retains the
original image SHA,263 link samples over546seconds, six minutes of sustained
TCP, checked MAC counters, UDP and same-boot restart measurements. It used
the Kuncy7 foundation and manual controller. Its stop-before-reboot procedure
and original slot identities describe that historical image; use the
[native integration notes](nss-qosmio-port.md) for the active stack.
