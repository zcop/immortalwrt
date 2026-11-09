> Reference only: these tests used the Kuncy7 foundation, not the new qosmio branch.

The RAM-only/uncommitted state below is historical. Native Qosmio image and
automatic-startup results are in [current status](../NSS%20Offload%20status.md)
and [the execution report](../nss-qosmio-dsa-slices.md#current-acceptance-status).

# CR881x QinQ DSA/NSS routing acceptance, 2026-11-04

The QinQ port-ID transport works with wired IPv4 NAT acceleration on the
CR881x/YT9215S. It replaces the single-tag port/bridge mapping while keeping
exact physical-port identity in the outer tag and a separate inner VLAN.
It remains an experimental, uncommitted RAM-module integration; no full
firmware image was flashed and boot autostart was not enabled.

## What it replaces

```
CPU/NSS <-> YT9215S:
DA | SA | 88a8, SVID=physical port | 8100, CVID=LAN/customer VLAN | payload
```

The switch inserts the per-port label on ingress to a CPU conduit. Linux's
DSA tagger inserts it on software egress; the NSS rule inserts the same pair
on accelerated egress. A reserved switch ACL steers that outer SVID to the
exact destination port; normal CVID membership still filters the output.
LAN1/2/3 use SVID 3072/3073/3074 on CPU8/GMAC1; WAN uses3075 on CPU4/GMAC0.

This can replace the single-tag CPU transport and its shared port/bridge
identity. It does **not** replace the stmmac/NSS DMA-ownership handover,
NSS firmware driver, IPQ5018 clock/reset fixes or startup ordering. The
validated implementation still uses those parts of Kuncy7's integration,
with a QinQ-aware DSA tagger, switch rules, qca-dsa-nss and ECM. QinQ alone
does not make an unchanged QSDK/qosmio package own upstream stmmac hardware.
A service could perform initialization at boot; this test leaves it manual.

## Routing results

Linux remained on LAN3, Windows/iperf3 port 5201 on WAN. External links were
1G and the LAN CPU link 2.5G. Both host Wi-Fi PHYs stayed loaded; Wi-Fi NSS
was not enabled. No nft flowtable was used. These are short functional and
throughput checks, not a soak or complete feature acceptance.

| Test | LAN -> WAN | WAN -> LAN | Router aggregate CPU idle |
| --- | ---: | ---: | ---: |
| Host QinQ baseline, 5s | 497.7 Mbit/s | not repeated | not sampled |
| QinQ NSS TCP, 15s each | 936.46 Mbit/s | 936.04 Mbit/s | 93.84 / 93.73% |
| QinQ NSS UDP, 900M offered, 8s each | 899.62 Mbit/s | 898.68 Mbit/s | 92.93 / 92.43% |
| VLAN-aware PVID1 NSS TCP, 10s each | 936.45 Mbit/s | 934.79 Mbit/s | 93.65 / 93.84% |

After Windows SharedAccess was restarted, the original DHCP WAN obtained
192.168.137.166. A third NSS start in the same boot used that DHCP interface:
five-second forward/reverse TCP checks reached 936.16/930.05 Mbit/s, with
438,668/419,482 added firmware hash hits. The earlier temporary static WAN
address was only a test fixture; saved WAN configuration stayed DHCP.

UDP receiver loss was 0.0301% forward and 0.0235% reverse. Forward TCP reported
zero retransmissions in both bridge modes; reverse sender retransmissions
were not supplied by this iperf3 result, so no zero-loss claim is made there.

All six runs had active ECM accelerated rules and positive NSS hash-hit
deltas: 1,272,800 / 1,257,001 (default TCP), 750,117 / 749,293 (UDP), and
866,155 / 839,316 (VLAN-aware TCP). NSS accelerated RX/TX packet counters
also increased. Rule activity, firmware hits and low CPU load establish
hardware acceleration independently of the speed figure.

## Port and VLAN identity

The default unaware LAN bridge has three separate roots and leaves:

```
lan1: SVID 3072 -> inner 3088
lan2: SVID 3073 -> inner 3088
lan3: SVID 3074 -> inner 3088
wan:  SVID 3075 -> inner 3075
```

Eight firmware nodes were created, with zero allocation failures and no
pending retries. Enabling bridge VLAN filtering changed each LAN leaf to
inner VID 1 while leaving each outer SVID unchanged. NSS stopped and restarted
successfully in the same boot; both directions then accelerated on that
customer VLAN. The normal bridge mode was restored afterwards.

The earlier two-host switch test also used the same destination MAC with
different outer SVIDs and proved LAN1 versus LAN3 steering, rejection of
unknown/wrong-conduit labels, and a negative inner-VLAN membership test.
Ordinary learned LAN-to-LAN switching stayed in silicon. Those results
are recorded in [the transport design and hardware evidence](nss-qinq-port-id-design.md).

## Source and build changes

- ECM patch 0062 models QinQ ports as Ethernet leaves, resolves the default
  inner VID for an unaware bridge or standalone port, and uses the firmware
  leaf as next hop for ordinary routed bridge traffic. Aware ports retain
  the bidirectional customer-VID checks. Non-ported flows remain on the
  host path; real VLAN-upper restrictions remain unchanged.
- The manual controller detects either transport, passes and validates
  `port_id_tpid=34984` for QinQ, and uses its module-directory override for
  both the DSA glue and ECM. The stop/reset/arm ordering stays the same.
- Both ECM patches are retained under `tools/nss/patches/qca-nss-ecm/`,
  installed into the pinned feed by the explicit overlay helper.
- Full kernel compilation and normal NSS glue/ECM package builds passed
  against 6.18.55. The kernel rebuild regenerated its complete symbol data;
  direct linking against the earlier partial symbol set was not accepted.
- Qoder drafted the bounded ECM change and independently reviewed the
  canonical patch. Primary corrected the inclusive bridge-number limit and
  patch context, checked source, built and performed all device operations.

The controller's17 mode/readiness tests, driver transport/fault harness
(including 60 ACL commit write-failure points), and host-restore cases
(9 with DSA, 1 without) pass. Static tests model software and register intent;
real MDIO-failure recovery was not injected on hardware.

## Limits and resource costs

The transport consumes the outer VLAN layer: transparent customer QinQ is
unsupported, and direct VLAN uppers on unaware/standalone QinQ ports remain
restricted. It reserves one 8-slot switch ACL block (4 slots used), leaving
376 slots for normal TC rules. The default four-port arrangement uses 8 NSS
VLAN nodes; this implementation caps its node table at 64.

Not established here: Wi-Fi NSS, IPv6 traffic acceleration, tagged customer
trunks, multi-VLAN reconfiguration under load, full STP/spoof/multicast/QoS
behavior, cold boot/autostart, or long-duration reliability. Preserving
port identity is proven; complete DSA feature parity is not yet certified.

Raw evidence is in `logs/qinq-ecm-integration-20261104/`: package/kernel
build logs, applied patch records, module hashes, node status, per-run
iperf JSON, CPU/ECM samples, firmware snapshots, `verified-benchmarks.json`,
serial logs and cleanup records. Earlier failed build attempts are retained
and are not counted as passing checks.

## Final device state

NSS was stopped after the tests and the QinQ host path retained. LAN bridge
VLAN filtering was restored to 0; WAN was restored to DHCP and obtained a
lease after the Windows SharedAccess restart. All five saved router config
hashes match the initial values, and no pending UCI changes remain. Boot ID
is unchanged. No flash, reboot, commit or push occurred; stock modules in
/lib/modules remain untouched.

A final host-only TCP check after cleanup received 500.02 Mbit/s, confirming
ordinary routing resumed after the acceleration tests.
