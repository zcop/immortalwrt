> Reference only: these tests used the Kuncy7 foundation, not the new qosmio branch.

# NSS double-tag firmware capability test, 2026-11-02

The CR881x IPQ5018 NSS firmware accepted explicit two-VLAN rules and routed
the lab traffic with both tags generated correctly. Both `0x8100 + 0x8100`
and `0x88a8 + 0x8100` passed. This establishes the firmware capability needed
for the proposed outer-port-ID/inner-customer-VLAN transport. The production
DSA tagger and ECM rule mapping are drafted but have not yet been validated
on hardware.

The later [DSA/NSS routing acceptance](nss-qinq-routing-test.md) now validates
the integrated wired IPv4 path. The records below remain the separate
low-rate direct-rule firmware capability test.

## Tested path

Kernel 6.18.55, NSS firmware `NSS.FW.12.2-156-MP.R`, and the verified 2.5G
GMAC1/YT921x CPU8 link were used. Source checkpoint is `9cca0ec1e7`; the
additional lab module and sender are under `tools/nss/qinq-probe/`.

The Linux workstation remained on LAN3. A temporary hardware VLAN admitted
only LAN3/switch2 and CPU8; both ports retained tags. A RAM-loaded probe
created NSS VLAN nodes and one fixed bidirectional UDP forwarding rule on
physical interface 1. This bypassed ECM rule construction and used no NAT.
The workstation injected and captured raw Ethernet packets on enp2s0.

```text
Workstation -> YT921x -> GMAC1/NSS -> YT921x -> same workstation

Forward input:  [DA=router][SA=peer][outer 1002][inner 77][IPv4 TTL64][UDP]
Forward output:[DA=peer][SA=router][outer 1002][inner 78][IPv4 TTL63][UDP]

Return input:  [DA=router][SA=peer][outer 1002][inner 78][IPv4 TTL64][UDP]
Return output: [DA=peer][SA=router][outer 1002][inner 77][IPv4 TTL63][UDP]
```

In this test, **the workstation tags ingress**, the switch carries the tags
unchanged, **NSS generates the egress tag stack and rewrites the inner VID**,
and the switch carries both tags back to the workstation. This is a firmware
test, not the eventual access-port scheme where YT921x adds/removes a
repurposed outer tag. Its inner-VLAN rewrite proves controlled egress
generation as well as parsing and forwarding.

## Results

| Format | Firmware rule | Forward valid | Return valid | NSS IPv4 hash-hit delta |
| --- | --- | ---: | ---: | ---: |
| Single `8100`, VID1002 control | ACK | 200/200 | 200/200 | 400 |
| Double `8100/1002 + 8100/77,78` | ACK | 200/200 | 200/200 | 400 |
| Double `88a8/1002 + 8100/77,78` | ACK | 200/200 | 200/200 | 400 |

All returned frames had the expected MAC addresses, tag stack and inner VID,
TTL63, and valid IP/UDP checksums. Each case's VLAN-node operations completed
synchronously, and IPv4 creation and destruction received ACKs. The module
pins itself until its firmware state is cleared.

`special.ipv4_hash_hits` increased by exactly the injected packet count.
The `special.ipv4_rx_pkts/tx_pkts` counters used in earlier ECM benchmarks
did not increase for these directly installed lab rules. They are not used
as evidence here; the matching hash hits plus captured routed/tag-rewritten
packets establish the result. These were low-rate functional tests, not a
QinQ throughput, NAT or full DSA integration benchmark.

## Host restrictions found first

An initial attempt to use `eth1.1002.77` was blocked by Linux DSA's conduit
upper-interface check (`-EBUSY`) before traffic could reach the firmware.
The existing NSS VLAN manager also defaults both TPIDs to 0x8100; its
0x88a8 configuration interface is behind a PPE-specific compile gate.
Neither host restriction is a firmware rejection. The dedicated module
submitted the required VLAN-node and IPv4-rule messages directly, without
relaxing production DSA checks or changing the feed.

## Cleanup and limits

The test rules, firmware nodes and RAM module were removed. Every recorded
switch register was restored. Temporary Linux VLANs, source policy routes
and test firewall rules from the earlier attempt were removed. Saved
network/firewall/wireless/DHCP/system hashes and host policy rules match their
original values; the router remained in the same boot with both host Wi-Fi
PHYs and ordinary wired NSS active at 2.5G.

The first 88a8 cleanup restored a shared TPID profile before the test ports'
selectors, temporarily interrupting Ethernet management. UART restored the
recorded selectors first and then the profiles; no reboot or firmware write
was needed. The cleanup ordering is corrected in the local test runner.
Normal LAN/WAN TCP after cleanup measured 938.96/938.87 Mbit/s. No fatal
kernel signature appeared.

The source-only helper audit of the host path completed. A separate module
review could not read all its inputs under helper permissions and timed out;
it supplied no accepted verdict. Primary review, the actual module build,
the eight frame-codec/checksum checks, ACKs, counters, captures and cleanup
verification provide the evidence for this bounded result.

Raw records are in `logs/nss-qinq-probe-20261102/`: `verified-cases.json`,
each case's `frames/wire.pcap`, `frames/summary.json`, firmware counter
snapshots, create/destroy responses and cleanup records, plus
`final-state.json` and the UART captures. No full image was flashed for this
probe. Next is validating the drafted pieces on hardware: the port-to-SVID
mapping and switch access/trunk handling, the Linux tagger, and the ECM rule
mapping (feed patch 0061), including isolation and fallback tests. This
result alone does not implement those pieces.
