# Standard DSA fallback for customer QinQ

Status: source and configuration checks pass; no fallback image has been built
or booted. The running router remains on native NSS build21.

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

## Frame limits and source checks

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

## Device gate before a fallback boot

A clean native-to-standard transition is still required. NSS firmware can survive
a warm reboot; an ordinary DTS lacks the NSS reservation/probe. Stop the old core
through a validated lifecycle path before allowing the new kernel to reuse its
memory. Do not pulse shared GCC resets or assume that a normal reboot stops it.

A bounded build21 unload/reload test removed ECM, QinQ, PPPoE and the NSS core.
Core removal returned success; the raw WAN host path answered two pings. LAN
host-path ping did not succeed in the short window before the reload safeguard,
so full host restoration remains unproven. Reload recreated all eight nodes and
SSH, but emitted duplicate stats-sysctl and panic-notifier registration warnings.
Source inspection confirms that registration has no matching module-exit cleanup
for those two resources. This is a lifecycle defect to fix before further reload
or mode-transition tests, not proof of a customer-QinQ hardware limitation.

The primary saved the UART/dmesg evidence and rebooted the same build21. The
clean boot has no new WARN taint, all eight nodes and unchanged configuration
hashes. Fresh routed NSS tests reached936.42/936.24 Mbps forward/reverse,
95.92%/97.72% CPU idle, with increasing firmware counters and active ECM rules.
No new firmware was flashed and endpoint settings were unchanged.

Next validation after lifecycle repair: build and boot this explicit standard
profile with recovery coverage; prove customer S+C delivery, both directions,
1500-byte payload boundaries, exact port selection and isolation. Test the2.5G
host path under controlled load separately: previous CPU-saturation trouble is
not resolved by these source checks. Full customer QinQ remains unvalidated on
this candidate until those tests pass.

Evidence is in original ignored
`logs/nss-slices-20261107/slice17-standard-fallback/`, including the profile
expansion, regression controls, AArch64 objects, UART capture, reload diagnostics,
clean reboot and recovery traffic results.
