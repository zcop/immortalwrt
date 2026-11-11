# Legacy DSA RMA reason 0x3f

Source investigation: 2026-11-11, base `97043f4a33f8` on `nss-experiment`.
Issue: <https://github.com/zcop/immortalwrt/issues/8>.
The decoder change has host checks and local CR881x hardware validation on the
standard, non-NSS profile. Local testing also reproduced a separate raw-WAN
control-TX reflection. Neither result is a reproduction on the reporter's
device. The native NSS image is retained in the other firmware slot.

## Where the reason comes from

The standard profile uses the proprietary eight-byte YT tag on CPU8/eth1
and a raw secondary CPU4/eth0 conduit for WAN port3. Linux's WAN/LAN bridge
membership and hardware user-port forwarding are separate from this CPU-link
selection.

`yt921x_rma_ctrl_action_set()` programs reason63 (`0x3f`) for RMA TRAP/COPY.
Commit `27e40762772f9eeb58fb02500ccce29e9cb3d051` introduced this correction
against the target stock module. `yt_nic.h` in the vendor source independently
names reason63 `CPU_CODE_RMA`. Do not substitute the newer RB setter's
hard-coded0x37 for the target-stock ABI.

The configured CPU-bound path is:

```text
RMA classification on a user port
  -> RMA reason0x3f, with configured VLAN/isolation bypass
  -> CPU_COPY 0x180690 selects the external CPU destination
  -> EXT_CPU_PORT 0x08000c selects primary port8
  -> proprietary tag enabled in legacy mode
  -> GMAC1 / eth1
  -> source port recovered from the tag
  -> Linux user interface, including wan for source port3
```

The legacy register values implied by setup are `CPU_COPY=0x1` and
`EXT_CPU_PORT=0xc008`. These are source-derived settings, not new live
register reads. CPU_COPY selects switch-to-CPU delivery; it does not mean
the frame was originally generated or offloaded by Linux.

## Packet evidence from the saved standard-mode fixture

The issue's exact log text is `net eth1: Unknown tag code 0x3f`. Refreshed
issue contents still contain no raw packet or decoded source port.

Locally retained Slices19/20 captures contain83,191frames across34files.
Forty-six frames in five standard-mode CPU captures have RMA reason0x3f,
source port3, destination `01:80:c2:00:00:00` and BPDU LLC `42:42:03`.
Their source address is the fixture's temporary LAN1 MAC
`fe:20:fc:00:00:01`, explicitly set by `run_multiport.py`.

In the `multiport-bridge-groups-1794095901` capture, all16 such received
frames match preceding CPU TX BPDUs addressed to LAN1, with private TX
mask0x1. The test had a spare physical cable between LAN1 and WAN. Thus
these packets leave LAN1, cross the cable, enter physical WAN port3 and
are trapped to CPU8. The first matched delay is about0.255ms. Matching
removes the private tag and Ethernet padding beyond the declared LLC
length; repeated identical BPDUs are paired with the nearest preceding TX.
This is evidence for external re-ingress, not an internal CPU-port leak.

The captured RX word is `0x98fe`, independently decoded as:

| Field | Value |
| --- | --- |
| Source-port-valid | 1 |
| Source port | 3, WAN |
| Priority | 0 |
| Reason-valid | 1 |
| Reason | 0x3f, RMA |

The original tagger already resolves this source to `wan` before logging the
unknown reason against physical conduit `eth1`. It then strips the private
tag and returns the packet. The warning alone establishes neither packet
loss nor a failure of hardware bridge forwarding.

Captures use this tree's DLT_DEBUG_ONLY303 raw Ethernet/private-tag format;
an older workstation tcpdump cannot decode that link type. The local parser
was corrected to handle this format after inspecting its definition and
frame bytes. Unsupported formats are never counted as zero matching packets.

## Decoder correction and checks

The decoder adds reason0x3f to the legacy tagger's reason enum and
the existing CPU-directed trap/copy cases. It removes the false unknown-code
warning without adding a hardware-forwarded mark or changing source-port,
payload, VLAN, malformed-tag, CPU-source or raw-conduit handling.

`tools/yt921x/test_legacy_control_rx.py` extracts and executes the production
RX function. It covers all four user ports, WAN received on the primary CPU
conduit, priority and customer-VLAN preservation, existing forwarded/trapped
classes, genuinely unknown reasons, missing validity bits, short/invalid
headers, CPU source ports4/8 and ambiguous raw-conduit mappings.

Validation performed:

- The RX harness passes ASan/UBSan outside the ptrace sandbox. LeakSanitizer
  cannot run under that sandbox; its first diagnostic failure is retained.
- The unpatched source compiles and fails the RMA no-warning runtime oracle.
- An additional offline harness feeds an actual saved68-byte RMA BPDU to
  the production RX function: it reaches `wan` without warning or an offload
  forwarding mark, preserving the normalized frame bytes.
- The existing standard-tag TX wire-oracle tests pass.
- Full standard-profile images were built against Linux6.18.60. Build43 has
  reason recognition only; Build44 additionally contains the control-TX fix.
  Their kernel payloads and expanded configurations are byte-identical. The
  temporary Build44 overlay did not replace the running module: the tagger
  loads from `/etc/modules-boot.d` before `mount_root` mounts the overlay.
  Full-image installation is required for hardware acceptance.
- Build43 receives three externally injected LAN3 BPDUs as source port2,
  reason63, with correct child-interface delivery and no unknown-code warning.
  The same positive control passes on the full Build44 image. A module-file
  checksum is not proof of loaded code: the discarded overlay-only attempt
  still ran Build43. The accepted run additionally matches loaded symbol
  offsets against the candidate module and confirms the saved NSS-slot hash.

## Separate raw-WAN control-TX defect

The before-fix cable fixture connects isolated, standalone LAN1 and WAN.
Their isolation registers block ordinary forwarding in both directions;
hardware loop protection stays enabled throughout. Captures start after both
links have carrier, with a bounded port-down guard. Marked BPDUs distinguish
direction and sequence without relying on timestamps alone.

With Build43, all three untagged and all three VLAN100 BPDUs sent through LAN1
leave CPU8 with destination mask1, cross the cable, enter physical WAN port3,
and reach Linux `wan` with reason63. In the opposite direction, raw WAN TX
produces three CPU8 RX frames per case with source CPU4/reason63 and no LAN1
delivery. These CPU-source frames are dropped before reason decoding. The
reflection is a distinct TX defect, not the direct cause of the issue's
unknown-reason warning.

The fix sends raw-conduit link-local control traffic through the unique tagged
CPU, retaining the original user-port destination in the private TX mask.
Ordinary raw WAN data remains raw. The helper clones the skb, reuses the
existing VLAN/checksum/tag construction, and queues only the clone; DSA owns
the original. Missing or ambiguous tagged CPUs fail closed. No CPU indices
are hard-coded, and conduit assignment or switch register policy is unchanged.

`test_standard_tags.py` checks destination selection, original-skb ownership,
clone/framing failures, missing/ambiguous CPU selection, VLAN metadata order
and the existing raw-data wire oracle under ASan/UBSan. These checks do not
establish hardware egress. The accepted full-image cable run provides that
evidence:

| Marked BPDU case | Before fix, Build43 | After fix, full Build44 |
| --- | --- | --- |
| LAN1 to WAN, untagged | 3/3 received as source port3 | 3/3 received as source port3 |
| WAN to LAN1, untagged | 0/3; three CPU4 reflections | 3/3 received as source port0 |
| LAN1 to WAN, VLAN100 | 3/3 received as source port3 | 3/3 received as source port3 |
| WAN to LAN1, VLAN100 | 0/3; three CPU4 reflections | 3/3 received as source port0 |

All twelve after-fix BPDUs match byte-for-byte between normalized CPU TX,
CPU RX and the receiving Linux child interface. WAN control TX uses CPU8
destination mask8; LAN1 uses mask1. There are no CPU4/CPU8-source reflections,
unknown-reason warnings, malformed captures or capture drops. Three ordinary
WAN unicast TX frames still appear on raw `eth0`; the isolated fixture does
not prove their peer delivery. Both test ports are held down after the run.

The earlier no-carrier run captured no marked traffic and is inconclusive.

After testing, the original NSS Build42 slot0 was restored with its flash
hash and all four saved network/Wi-Fi/firewall/DHCP configuration hashes
unchanged. The temporary module overlay and boot guard were removed. Native
NSS routing reached936.4Mbps forward and933.4Mbps reverse in short TCP runs,
with active ECM acceleration and increasing NSS IPv4 counters in each run.
These are restoration checks, not legacy-profile throughput measurements.

## Limits

The reporter's exact WAN-uplink bridge/VLAN configuration still needs a matching
decoded packet or a test on that device. The isolated fixture does not establish
ordinary unicast delivery or general bridge performance. Do not retarget WAN,
disable RMA traps or relax isolation based only on the warning.

Both profiles include `yt921x-capture`. For a bounded read-only diagnostic on
an installed standard build, its enhanced mode records the private header's
`rx_port`, `code`, `code_en` and packet direction alongside a normalized capture:

```sh
yt921x-capture --mode enhanced --iface eth1 --prefix /tmp/issue8-eth1 \
  --count 20 --timeout-ms 10000 --filter 'ether dst 01:80:c2:00:00:00'
```

This captures BPDU-destination traffic only. A CPU capture alone does not prove
physical egress or an internal reflection; retain that distinction when
comparing the original reporter's WAN and LAN uplinks.

Local evidence is retained under ignored
`logs/issue8-rma-investigation-20261111/` and
`logs/issue8-rma-hardware-20261111/`. External patch-review attempts timed out
or failed without an accepted verdict; the primary owns the stated checks.
