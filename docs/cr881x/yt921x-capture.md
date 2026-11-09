# Capturing YT921x traffic

`yt921x-capture` writes a paired packet view and per-record metadata without
changing the router's network configuration. Both CR881x build profiles select
the package, which installs `/usr/bin/yt921x-capture`. The original Slice22
acceptance used the packaged executable from RAM on native24 and standard25,
whose images predate that profile selection. Current image and acceptance
boundaries are in [the execution status](nss-qosmio-dsa-slices.md#current-acceptance-status).

Choose the mode from the actual conduit role:

| Interface on the tested CR881x | Mode | Captured framing |
| --- | --- | --- |
| Native NSS primary `eth1` | `qinq` | Ethernet with transport S-tag and inner C-tag |
| Standard DSA primary `eth1` | `enhanced` | Ethernet addresses, private8-byte9988 header, original EtherType |
| Native NSS secondary `eth0` | `qinq` | Ethernet with WAN transport S-tag and inner C-tag |
| Standard DSA secondary `eth0` | `raw` | Ordinary Ethernet, possibly with customer VLANs |
| Logical user port, such as `lan3` | `raw` | Ethernet after the DSA receive tagger |

In standard mode, the secondary conduit's DSA sysfs attribute can report the
tree's enhanced tagger despite carrying raw Ethernet. The utility requires an
explicit mode; it does not infer framing from sysfs or mistake an ordinary9988
EtherType for a private header in raw mode.

For a bounded native LAN3 probe capture:

```sh
yt921x-capture --mode qinq --iface eth1 --prefix /tmp/lan3-probe \
  --filter 'vlan 3074 and vlan 3088 and icmp' --count 6 --timeout-ms 5000
```

For the standard primary conduit, ordinary protocol filters operate on the
normalized Ethernet frame:

```sh
yt921x-capture --mode enhanced --iface eth1 --prefix /tmp/legacy-probe \
  --filter 'arp or icmp' --timeout-ms 5000
```

The prefix must be new. The three outputs are created exclusively with mode0600:

- `.raw.pcap`: the socket packet with a stripped VLAN tag reconstructed from
  actual `PACKET_AUXDATA`, when provided. Enhanced mode uses `DLT_DEBUG_ONLY303`
  and retains the complete private header; other modes use Ethernet/EN10MB.
- `.pcap`: normalized Ethernet/EN10MB. Enhanced mode removes only the private
  header. Customer VLAN tags, including PCP/DEI, remain. Raw and QinQ modes
  preserve the Ethernet framing, including native transport tags.
- `.jsonl`: one row per output record, with a shared zero-based index,
  direction, timestamp provenance, lengths and auxiliary VLAN fields. Enhanced
  records also retain the full private header and decode RX-valid/source-port,
  priority/code and TX-valid/destination-mask fields. A decoded port value is
  meaningful only when its valid bit is set.

Live timestamps are taken in userspace after receive; they are not hardware or
kernel receive timestamps. The raw output is a reconstructed packet-socket
view, not a physical wire analyzer: it cannot recover an absent FCS, packets
never delivered to the CPU, or metadata the kernel did not supply.

VLAN reconstruction inserts the auxiliary tag after the private header at
offset20 in enhanced mode, and at offset12 for raw/QinQ. A valid zero TCI is
retained. Filters compile against the normalized Ethernet view and execute in
userspace, so nested VLAN filters still need to name both visible tags. The
utility neither enables promiscuous mode nor installs a mirror or kernel BPF
filter. Keep captures bounded and inspect the reported drop count; filtering
the output does not remove packet-socket receive load.

The default duration is5000ms. With a positive `--count`, failure to collect
that many matches returns nonzero. With count0, reaching the duration is a
normal bounded completion and reports `timeout:true`. Malformed input,
capture drops, unavailable statistics, and output errors return nonzero.
Retrieve files after capture ends to avoid measuring their SSH transfer.

Existing saved captures can be normalized offline:

```sh
yt921x-capture --mode enhanced --read /tmp/old-private.pcap \
  --prefix /tmp/decoded --filter 'ip or arp'
tcpdump -nn -e -r /tmp/decoded.pcap
```

Offline mode requires DEBUG_ONLY303 for enhanced input and EN10MB for raw/QinQ.
It cannot reconstruct a VLAN tag already omitted from an older savefile. On
the tested libpcap1.10.7, native `yt921x-qinq` captures already support ordinary
Ethernet and VLAN filters; standard enhanced DEBUG_ONLY captures need explicit
byte-offset filters or this normalized view. The system host's libpcap1.10.5
cannot write DEBUG_ONLY303; use the target's pinned libpcap1.10.7 for the host
offline/sanitizer checks.

CPU packet capture shows host traffic and exceptions. Switch forwarding and
NSS acceleration can bypass it. Slice22 captured all six control echo packets
but only96 TCP packets during a934Mbps routed transfer, while the firmware's
global IPv4 TX counter increased764525. The global counter is not an exact
per-flow denominator. The result establishes visibility limits, not packet
loss; use appropriate firmware/switch counters or a separately configured
mirror when diagnosing the fast path.

Implementation checks are `tools/nss/test_yt921x_capture.py --binary PATH` and
`tools/nss/test_yt921x_capture_aux.py --pcap-source PATH --pcap-build PATH
--sanitize`. The latter compiles the production C functions and verifies
byte-exact metadata reconstruction and ancillary-message rejection.
