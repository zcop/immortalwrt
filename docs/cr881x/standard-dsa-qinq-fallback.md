# Standard DSA fallback for customer QinQ

Updated: 2026-11-09. Standard builds23/25 on Linux6.18.60 have accepted
customer-QinQ packet, isolation, raw-conduit, capture and bounded host-load/MTU
results in Slices19–22. The original build23 LAN3 exchange includes1500-byte
IP packets and preserved S/C PCP5/3; Slice20 adds multiport separation and
additional TPID/VID/PCP/DEI combinations. NSS is absent in this mode.

This is a separately built profile, not the installed native31 transport or
its on-device native30 fallback. Verified standard-image backups are retained.
Full external2.5G host saturation and sustained jumbo pressure remain
unmeasured; the available endpoint links are1G. See
[current acceptance status](nss-qosmio-dsa-slices.md#current-acceptance-status).

`configs/xiaomi_cr881x_standard_dsa.config` selects the ordinary CR881x DTS,
stmmac and the original YT921x DSA tagger. Both experimental transport options
and the NSS dataplane packages are disabled. It keeps the optimized profile's
application packages. The native `xiaomi_cr881x_nss_qosmio.config` is unchanged.

This is a boot-time choice. The transport parameters are read-only and latched
at switch probe; the driver does not implement runtime tag-protocol switching.
Changing a writable overlay module option does not change the early boot mode.

## Packet ownership

On the primary CPU link, Linux's legacy tagger adds an eight-byte private tag:

```text
CPU to switch: DA | SA | 9988 + private port mask | 88a8 + customer S-TCI | 8100 + customer C-TCI | EtherType | payload
Switch to CPU: DA | SA | 9988 + private source port | 88a8 + customer S-TCI | 8100 + customer C-TCI | EtherType | payload
External port: DA | SA | 88a8 + customer S-TCI | 8100 + customer C-TCI | EtherType | payload
```

The private header supplies port identity. Customer S-tag and C-tag fields stay
available to the customer. TX materializes any VLAN metadata before adding the
private header, preserving TPID and all TCI bits. This prevents MAC VLAN offload
from inserting the customer tag outside the private header. The tagger uses the
non-consuming insertion helper so DSA retains skb ownership on an error.

The secondary CPU link stays raw. Its single WAN user port supplies the receive
identity; TX leaves VLAN metadata for normal MAC handling. This does not promise
multiple indistinguishable raw ports on one conduit.

This standard path does not use NSS acceleration. The existing NSS path instead
uses the outer S-tag for private port identity and supports one customer C-tag.
It does not provide triple-tag firmware acceleration for customer S+C plus the
private identity tag. No PCP or DEI bits are repurposed in either change here.

## Initial frame-limit and source checks

These are the original source checks. Subsequent installed MTU and capture
results are summarized under [later acceptance](#later-standard-mode-acceptance).

At L3 MTU1500, the switch allowance including FCS is1526 bytes on user ports and
the raw CPU port, and1534 bytes on the primary CPU port (customer S+C plus the
private header). Standard mode's conservative maximum L3 MTU is9182 against
the9216-byte hardware limit. The existing experimental 802.1Q/QinQ arithmetic
is unchanged: QinQ still programs1522 on user ports and1526 on CPU ports.

The actual-C fake-register test covers all three modes,1500/9000/maximum MTUs,
register-bit preservation, errors and locking. The actual legacy TX C code passes
an independent wire-byte oracle for inline S+C, VLAN metadata, priority/DEI,
port selection, raw TX and error ownership under ASan/UBSan. Old MTU/tagger
source and a missing protocol update are rejected by runtime regression controls.
Both changed production C files compile against the prepared Linux6.18.55
AArch64 kernel. Expanded defconfig selects the ordinary DSA device and no NSS
packages; the current native `.config` hash remains unchanged. These checks do
not prove switch delivery or MTU acceptance on a standard-mode image.

## Lifecycle prerequisite and original device tests

Every native-to-standard transition requires stopping the old NSS core through
a validated lifecycle path before the new kernel reuses its memory. NSS
firmware can survive a warm reboot; the ordinary DTS lacks the NSS
reservation/probe. Do not pulse shared GCC resets or assume that a normal
reboot stops it.

A bounded build21 unload/reload test removed ECM, QinQ, PPPoE and the NSS core.
Core removal returned success; the raw WAN host path answered two pings. The
initial LAN ping was inconclusive: the workstation policy route sent replies
over Wi-Fi. Slice18 corrects this with Ethernet-bound probes. Reload recreated
all eight nodes and SSH, but emitted duplicate stats-sysctl and panic-notifier
registration warnings.
Source inspection found no matching module-exit cleanup for those two
registrations. This was a lifecycle defect, not a customer-QinQ hardware
limitation; Slice18's correction and accepted tests follow below.

The primary saved the UART/dmesg evidence and rebooted the same build21. The
clean boot has no new WARN taint, all eight nodes and unchanged configuration
hashes. Fresh routed NSS tests reached936.42/936.24 Mbps forward/reverse,
95.92%/97.72% CPU idle, with increasing firmware counters and active ECM rules.
No new firmware was flashed in slice17 and endpoint settings were unchanged.

Slice18 installs native build22 with matching Linux6.18.60 modules. Patch0106
unregisters the stats sysctl and panic notifier, cancels coredump work, drains
frequency work before unmapping, and releases connection tables after hardware
and IRQ teardown. Two core removals/reloads complete on the same boot without
duplicate registrations or kernel WARNs. With the core absent, Ethernet-bound
LAN ping, TCP and SSH work, and the router reaches Windows through the WAN
host path. The corrected second cycle restores automatically before its
45-second safeguard; all eight nodes return. Fresh native traffic then reaches
936.54/936.51Mbps with97.82%/98.55% CPU idle and increasing firmware counters.
Configuration hashes and the verified build21 recovery slot remain unchanged.
These results establish the unload prerequisite, not the standard-mode boot or
full customer-QinQ delivery. See [slice18](nss-qosmio-dsa-slices.md#slice18-nss-core-lifecycle-cleanup).

## Installed standard-mode validation

This subsection is the original build23 record, followed by the later results.

Slice19 builds the unchanged standard profile after adding an explicit disabled
default for `NF_CONNTRACK_DSCPREMARK_EXT` in the qualcommax kernel configuration.
Without that default, the non-NSS build stops at an interactive kernel question.
The native ECM package still overrides the setting to enabled. The saved native
`.config` is restored byte-for-byte after the standard build.

Build23 sysupgrade SHA256:
`c79086f4d10539dfb70d58ed8b260de14c0d899e95e9d61497ccaa5f3b13382d`.
The primary stops the tested build22 core before sysupgrade to slot0/mtd18.
UART records core removal before the upgrade handoff. Tested build22 remains
in slot1/mtd19, with a verified private raw/config backup. Standard boot ID:
`e502511a-f1c5-44fa-b115-5c060e582f2b`. stmmac and `yt921x-enhanced` bind,
NSS modules are absent, and the CPU8 link remains2.5Gbps.

Linux stays on LAN3, Windows stays on WAN/VLAN0. An isolated Docker macvlan
endpoint on the workstation's Ethernet interface supplies S3500/C35. Temporary
router VLAN uppers attach to `lan3`; a narrowly scoped temporary input rule
permits their test ICMP. No persistent UCI or workstation route changes are made.

| Traffic | ICMP data bytes | Result |
| --- | --- | --- |
| Endpoint to router |56 /1472 |3/3 replies at each size |
| Router to endpoint |56 /1472 |3/3 replies at each size |
| Wrong C VID36 |56 |0/2 replies |
| Wrong S VID3501 |56 |0/2 replies |

Both endpoint and CPU captures contain24 matching ICMP frames. The byte parser
requires TPIDs88a8/8100, SVID3500/CVID35, PCP5/3 and zero IP fragment bits.
Maximum external length is1522 bytes without FCS; the CPU length is1530 with
the private eight-byte header. CPU ingress identifies physical port2/LAN3;
CPU egress explicitly selects mask0x4. The endpoint sees no private header.
This proves the tested primary-conduit exchange; it does not prove delivery
or isolation with multiple connected customer-QinQ ports. DEI1 and other
S/C combinations are not tested here. Legacy CPU capture still uses
DLT_DEBUG_ONLY and is decoded manually; ordinary tcpdump filters are not
established by this parser.

Earlier probe attempts exposed a router ping interval incompatibility and a
capture filename collision. Their captures/results are retained; they are not
treated as hardware failures. The final probe uses verified whole-second ping
intervals and distinct filenames. Its fixtures, input rule and Docker network
are removed, configuration hashes match, and the workstation route is unchanged.

The primary returns to verified native build22 using the platform's ordinary
dualboot flag sequence, preserving standard build23 in slot0. Return boot ID:
`1c7709bd-9c88-4508-a854-2bf2d080045d`. NSS starts automatically with eight
nodes; exact module hashes, configuration hashes and the standard slot's raw
hash match. Fresh routed traffic reaches936.50/936.68Mbps,98.67%/98.30% CPU
idle, with active ECM connections and more than750000 additional firmware
IPv4 TX packets per direction. Loop protection stays enabled.

At the Slice19 checkpoint, multiport isolation, other tag combinations,
the raw conduit and host-load coverage were still pending. Later slices below
close the bounded packet/load gates; sustained2.5Gbps capacity remains open.
This mode uses host forwarding; triple-tag NSS acceleration is not provided.

Evidence is in original ignored
`logs/nss-slices-20261107/slice17-standard-fallback/`, including the profile
expansion, regression controls, AArch64 objects, UART capture, reload diagnostics,
clean reboot and recovery traffic results.

Build23 and packet evidence are in original ignored
`logs/nss-slices-20261107/slice19-standard-hardware/`, especially
`build23-artifacts.json`, `standard-identity.json`, `native-dscp-selection.json`,
`qinq-lan3-3500-35-final/` and `decode_qinq_pcaps.py`. UART for the transition
continues in slice18's `minicom.log` under the `SLICE19_` markers.

## Later standard-mode acceptance

Slice20 accepts exact customer-tag bytes in both conduit directions and
LAN3↔LAN1 bridge forwarding/isolation. Cases include S+C, C+C, S+S, C+S,
VID4094, outer/inner DEI1 and zero TCIs. Negative isolation and separate-bridge
controls reject the tested frames without duplicates. The secondary conduit
stays raw; it does not acquire a private port header.

Slice21 measures bounded router-local CPU pressure and fixes standard MAC MTU
ordering. Standard25 validates the installed correction; the stmmac conduit
limit makes8992 the tested effective user MTU, not a full9000-byte user path.
Native24's separate LAN9000-byte echo result must not be attributed to the
standard profile or to bidirectional routed9K NSS traffic.

Slice22 accepts installed-library capture and the packaged utility from RAM
on standard25, including enhanced-header normalization, live/offline filters
and VLAN auxiliary metadata. Raw DEBUG_ONLY capture limitations in the
original build23 parser record are not the current capture-tool status.

These tests restore normal native operation, configuration and physical
topology after completion. Their evidence is in the corresponding Slice20,
21 and22 sections of the [execution report](nss-qosmio-dsa-slices.md). The
report retains the image transitions and rejected fixtures; no claim of
runtime tag-protocol switching or complete host-capacity qualification follows.
