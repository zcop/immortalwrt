# Archived Kuncy7 integration reference

Historical foundation, inactive in the native Qosmio stack. The former
`nss-qinq-experiment` branch is no longer an active branch; its source/diff/
config/feed snapshots remain in ignored
`logs/qosmio-migration-20261104/kuncy-reference/`. Measurements and manual
controller procedures below apply only to that foundation. Use
[current native status](../NSS%20Offload%20status.md) for the active image,
automatic startup and remaining acceptance gates.

# CR881x NSS integration

The experiment keeps ImmortalWrt's upstream stmmac Ethernet stack and the
existing YT921x driver. The initial foundation provides the NSS kernel API,
driver, firmware, DSA glue, ECM and wired clients, without automatic takeover.
An opt-in, single-tag YT921x 802.1Q transport is implemented and has passed
wired LAN/WAN acceleration and manual stop/restart tests on CR881x. The image
with a 1G CPU trunk measured about 939 Mbit/s in each TCP direction with about
99% CPU idle.
Use `cr881x-nss start|stop|restart|status` for manual control. QinQ port-ID
transport now also passes wired IPv4 NAT with exact outer port identity:
about 936 Mbit/s both ways, including a VLAN-aware PVID1 check. Wi-Fi NSS,
boot autostart and broader feature acceptance remain deferred. The original
transport remains the default. See [QinQ routing acceptance](nss-qinq-routing-test.md).
Do not arm a GMAC while its CPU link still carries the proprietary YT tag.

The LAN CPU SerDes has subsequently been enabled at 2.5G using today's native
GMAC/UNIPHY PCS stack. Wired NSS, six minutes of sustained TCP, UDP and a
same-boot NSS restart passed with no carrier changes or checked MAC errors
during the traffic window. See [2.5G SerDes results and limits](nss-serdes-2500.md).

A separate direct-rule firmware probe has now passed double-tag parsing,
forwarding and egress generation for both `8100+8100` and `88a8+8100`, with
bidirectional inner-VID rewrites. The later DSA/ECM prototype now passes
wired routing; the direct probe remains distinct evidence for the firmware
capability. See [QinQ firmware evidence](nss-qinq-firmware-test.md).

The build also requires the preceding upstream compatibility fixes: the
CR881x fstools overlay patch follows the new overlay-name argument, and the
YT921x policer callback uses the current flow-action API. These are kept in
a separate prerequisite commit. Dated build sections below describe their
particular artifacts; current hardware results and remaining gates are in the
final validation and manual stop/restart sections.

## Pinned sources

- Tree: `kuncy7/openwrt-nss-edma`, `c3po-tag-8021q`,
  `13e8bc914709c4794b3ec7b0421d2038386e697e`.
- Feed: `kuncy7/nss-packages`, `ipq50xx-rebase`,
  `4fa071ab14de0694c341b0c6f1970ac94aca3e62`, pinned in `feeds.conf.default`.

The imported target patches retain their upstream authorship. They add the
stmmac ownership API and ring/IRQ lifetime handling, core-clock protection,
generic tag_8021q fixes and kernel helpers needed by ECM and NSS clients.
`package/kernel/qca-dwmac-nss` exports the NSS data-plane API for stmmac;
`package/kernel/qca-dsa-nss` maps generic tag_8021q domains into firmware VLAN
interfaces. The latter also selects its required NSS VLAN support here.

NSS reserved memory is declared by the opt-in `ipq5018-nss.dtsi` include,
instead of unconditionally reserving RAM on every IPQ5018 board. CR881x uses
8 MiB at 0x40000000 and `NSS_MEM_PROFILE_LOW`, following the donor's 256 MiB
layout. Initial firmware is 12.2 release 156, the line used for the donor's
documented small-memory measurements. Firmware and host structure definitions
must agree when changing versions.

## Foundation build

Use `configs/xiaomi_cr881x_nss_bootstrap.config`. It extends the normal optimized
profile and leaves that original profile intact. Update/install only the pinned
NSS feed when the other feeds are already prepared:

```sh
./scripts/feeds update nss
python3 tools/nss/apply-local-patches.py
./scripts/feeds install -a -p nss
cp configs/xiaomi_cr881x_nss_bootstrap.config .config
make defconfig
make -j12 V=s
```

The bootstrap selects NSS modules, firmware, ECM, VLAN and PPPoE clients. It
does not select `nss-tools-dwmac`, `qca-nss-dp`, `qca-ssdk` or ath11k NSS offload.
The donor core and ECM packages have no module autoload; ECM's init script has
no START entry. The core's separate init script only manages IRQ/RPS policy.
Existing host ath11k and its firmware remain unchanged at this stage.

Compilation is not device validation. A successful foundation build establishes
module/kernel compatibility; it does not prove NSS firmware startup or packet
acceleration. Keep the flashed host baseline and its rollback slot available
until a separate transport test has been reviewed.

### Verified foundation build (2026-11-01)

The full bootstrap build passed on kernel 6.18.55. The generated sysupgrade
archive was unpacked and checked: NSS core/glue, ECM, VLAN and PPPoE modules
are present alongside the original YT921x driver/tagger and host ath11k. The
firmware identifies itself as `NSS.FW.12.2-156-MP.R`. Its embedded board DT
reserves 8 MiB for NSS and retains 256 MiB board RAM.

There are no NSS module autoload entries, no takeover service, and no ECM boot
service. The S94 core init entry only configures IRQ/RPS policy; the glue's
zero mask and deferred-probe gate prevent automatic firmware takeover. The
netdev notifier only acts on an already overridden port and does not arm one.
These are image/source checks, not a new hardware test.

Artifacts are under `bin/targets/qualcommax/ipq50xx/nss-bootstrap-20261101/`.
The sysupgrade SHA256 is
`8d5adfe4f010cb7c24f965d38313d855dda9dbf4540f182b5b6452a0bc179384`.
The known-good host images were retained separately under
`logs/nss-integration-20261101/baseline-images/`. Build logs and detailed
acceptance evidence are in `logs/nss-integration-20261101/RESULT.md`.
The bootstrap has not been flashed.

## Wiring and next gates

Preserve the two physical CPU links: switch port 8 -> GMAC1 is the primary LAN
conduit; port 4 -> GMAC0 is secondary, with user mask 0x8 selecting WAN. Both
are switch CPU links. Do not treat WAN as an independent native PHY or infer
the NSS physical-interface index from a Linux interface name alone.

The single-tag transport and dual-conduit policy are implemented below.
Host-only device validation and NSS wired validation have passed on LAN3/WAN.
The outer-port-ID/inner-customer-VLAN extension remains future work.
The imported DSA glue still uses shared bridge/VLAN identity; it
does not implement that QinQ scheme or preserve every original YT trap field.
Test firmware parsing, bidirectional accelerated counters, and generated egress
tags separately. Wi-Fi offload, its 256 MiB allocations and runtime ordering
follow after the wired foundation. Preserve the existing driver DONE features
and require explicit fallback for unsupported traffic.

## Experimental YT921x transport

The switch driver accepts the read-only module parameter
`experimental_8021q=1` at load time. Its default is false. With the parameter
unset, the original primary YT tag and secondary no-tag path are retained.
The experiment requires the CR881x DT mapping; it does not offer a runtime
protocol swap. The bootstrap profile keeps the original mode. Build with
`configs/xiaomi_cr881x_nss_transport.config` to enable the transport: its
`CONFIG_DSA_YT921X_EXPERIMENTAL_8021Q=y` option bakes the module argument into
the image. NSS takeover remains manual.

Observed boot order on CR881x: the switch loads from `/etc/modules-boot.d` at
about3.3 seconds; the writable overlay mounts at about11.3 seconds. Editing
`/etc/modules.d/42-dsa-yt921x` in the overlay therefore cannot select the mode
for that early load. Use the separate transport image, retaining a verified
default-mode image in the other slot. Do not unload a live DSA switch to
change this setting remotely.

The tagger and switch programming use the YT VLAN implementation from
`michioxd/openwrt-wf810df` at
`63fc98585ad49fac9b43c9c2a4c30415a22b8edd` as a reference, adapted into the
existing split driver. No donor driver replacement is used. The vendor
`fal_tiger_vlan_port_egrTagMode_set()` also maps entry-based tagging to hardware
mode 5; `fal_tiger_struct.c` places the C-tag mode at bits 14:12.

| Front port | Switch port | CPU port | Conduit | NSS physical interface |
| --- | ---: | ---: | --- | ---: |
| lan1 | 0 | 8 | eth1 / GMAC1 | 1 |
| lan2 | 1 | 8 | eth1 / GMAC1 | 1 |
| lan3 | 2 | 8 | eth1 / GMAC1 | 1 |
| wan | 3 | 4 | eth0 / GMAC0 | 0 |

Both CPU ports use `yt921x-8021q` in the experiment. The external proprietary
CPU block is disabled; TPID profile 0 becomes 0x8100, CPU VLAN membership is
tagged, and front-port egress uses each VTU entry's untagged bitmap. CPU ports
block each other. Their destination masks retain the board's LAN/WAN split,
and a user port can select only its intended conduit. Netifd's initial move
from DSA's default CPU assignment remains allowed.

The generic tag_8021q framework installs transport VLANs on both CPU ports.
Hardware user/CPU isolation restricts the usable path; the RX tagger also
checks the conduit. NSS glue keys its nodes by `(VID, physical interface)`.
After netifd has finished and transport has passed host-only tests, the
required explicit mapping is `ifmap=0:eth0,1:eth1`, with no firmware arm during
mapping setup. The donor's default `phys_if 1 -> eth0` is wrong for this board.
No automatic arm service has been added.

### Frame ownership in this first mode

For an untagged LAN bridge, the wire to the CPU is:

```text
LAN device -> YT switch -> eth1 -> host or NSS
[DA][SA][EtherType][payload]
             YT adds: [DA][SA][0x8100 | bridge transport VID][EtherType][payload]

host or NSS -> eth1 -> YT switch -> LAN device
[DA][SA][0x8100 | bridge transport VID][EtherType][payload]
             YT removes the transport tag on an access-port egress
```

Linux TX inserts the tag in the new DSA tagger on the host path. On the NSS
path, qca-dsa-nss asks the firmware to generate the configured VLAN tag. The
wired unicast path has passed packet-capture and accelerated-counter checks
on LAN3/WAN; the wider VLAN/control-plane matrix remains unvalidated.
The switch forwards to a learned MAC
or floods within the allowed VLAN and isolation masks. It does not probe
ports until it finds the recipient.

Standalone ports use their framework port VID, and an unaware bridge uses a
shared bridge VID. For an aware bridge, the tag carries the real customer VID
instead. There is no simultaneous outer port ID plus inner customer VLAN in
this milestone. Shared bridge/VID traffic therefore identifies the bridge or
VLAN domain, rather than the precise ingress jack. PCP retains normal traffic
class use; it is not borrowed for port identity.

### Explicit limits and validation

- Customer bridge VLANs are limited to 1..3071; 3072..4095 belong to DSA.
- Direct port VLAN uppers such as `wan.35` are rejected. Use an aware bridge
  and its VLAN configuration for this first experiment.
- Reuse of one VID in different active bridge domains and hardware bridge
  offload across the CPU conduits are rejected. Linux may retain a bridge
  port in software after an offload rejection; inspect bridge offload state.
- An unaware bridge accepts untagged access traffic in this mode. Transparent
  carriage of arbitrary customer tags needs a different transport; use an
  aware bridge for configured VLAN traffic.
- FDB dumps may display the internal transport VID for an unaware bridge;
  a Linux VID-0 presentation layer is not implemented in this experiment.
- Proprietary trap/copy reason metadata is absent. Control traffic, mirroring,
  STP/802.1X behavior, and precise per-port accounting need separate hardware
  tests. DONE features remain the established baseline for the original mode;
  they are not newly certified for this experiment.
- The experiment clears the legacy IGMP VLAN-leaky, isolation-bypass and
  ingress-filter-bypass settings. The original mode keeps its current defaults.
- A failed VLAN programming transaction latches the affected port closed and
  prevents normal link-up from reopening it until reboot. The close writes
  are best effort if MDIO itself has failed; this is not a physical isolation
  guarantee during a bus failure.

Run `python3 -B tools/yt921x/test_8021q_transport.py` for the source-backed fake
register tests. They compile the actual transport and common VLAN functions
with AddressSanitizer/UBSan and cover conduit masks, PVID transitions,
reserved/direct VLAN rejection, conflicting bridge VIDs and injected writes.
They establish software state and register intent, not silicon packet fate.

Clean boot, ARP/DHCP and bidirectional routed unicast on LAN3/WAN have now
passed with this transport, followed by the wired NSS measurements below.
The wider hardware matrix remains open: other access ports, unknown unicast,
multicast, bridge join/leave, customer VLAN isolation and control/trap traffic.
QinQ parsing, acceleration and egress generation remain three separate later
gates. A shared bridge VID still cannot preserve the precise ingress jack.

### Verified transport build (2026-11-01)

A full `make -j12 V=s` passed with the completed source held fixed. The actual
sysupgrade rootfs was extracted successfully (excluding the unused console
device node), and all eleven selected NSS/YT/ath11k modules were checked as
AArch64 ELF with kernel 6.18.55 vermagic. The final driver's parameter
relocations confirm `experimental_8021q` is a read-only bool backed by
zero-initialized storage. Its load option is absent from boot files. The
image has both YT taggers and no NSS/ECM takeover autoload. Embedded DT wiring
matches the table above. Compiled driver sources and object timestamps match
the final overlay sources.

The eight register-model test groups passed, including all four VLAN-add and
all four VLAN-delete write-failure points under ASan/UBSan. Leak detection is
disabled for the traced sandbox; the harness does not allocate on the heap.
The Qoder mapping audit completed; focused implementation reviews on Qoder
and one InferX fallback timed out without findings. Primary review and the
actual build/artifact checks supply acceptance for this source milestone.

Images are under
`bin/targets/qualcommax/ipq50xx/nss-yt921x-8021q-20261101/`.
The sysupgrade SHA256 is
`93c72387faa9f40c187978d9f0619cc9522757f7b161f35aabda8712f8b9607d`.
Detailed evidence and the next device gates are in
`logs/nss-yt921x-20261101/RESULT.md`. This image has not been flashed; runtime
transport, acceleration and QinQ remain unvalidated.


### Wired hardware validation (2026-11-01/02)

The default-mode foundation was first flashed and checked on slot 0: both
radios registered, saved configuration matched, and routed TCP measured
538.4/504.3 Mbit/s. That exact image remains in slot 0 as the fallback. The
original pre-NSS image and fresh raw/configuration backups are retained offline.

The first early-boot transport trial exposed missing RTNL around
`dsa_tag_8021q_register()`. It was rejected before NSS loading. Registration and
unregistration now take RTNL, matching the kernel SJA1105 lifecycle; a focused
Qoder source review found no blocking issue. The corrected image booted without
the assertions and runs from slot 1:

`bin/targets/qualcommax/ipq50xx/nss-transport-rtnl-20261101/`

Sysupgrade SHA256:
`2aa6fc735e613a59c0118427eca077b4b912e1128a0bfa0d2b4e27ac62e21877`.

Both conduits report `yt921x-8021q`. Host-only routed TCP reached 543.1/536.4
Mbit/s. Raw TX captures show one 0x8100 header at byte 12: WAN standalone VID
3075 on eth0 and the current LAN bridge VID 3088 on eth1. The generic kernel
helper encodes bridge IDs into the VID; do not allocate these as consecutive
`3088+n` numbers. This libpcap marks the new tagger `DLT_DEBUG_ONLY` (303),
rejects VLAN filters, and omits RX VLAN metadata from those raw captures. Thus
the capture directly establishes TX bytes; it does not show the original RX
wire tag after receive stripping. Incoming host routing and DSA decoding were
verified independently.

The actual manual startup order was:

1. Confirm netifd settled, both conduits up/promiscuous, configuration unchanged
   and no active Wi-Fi interfaces. Temporarily unload unused host Wi-Fi modules
   for initial firmware testing.
2. Load `qca-dwmac-nss`, then set debugfs `ifmap` to `0:eth0,1:eth1`. Verify both
   entries and `fw_mask=0x0`; load `qca-nss-drv` with its probe still deferred.
   This kmodloader's `modprobe` ignores trailing module arguments, so do not
   rely on `modprobe ... ifmap=...`.
3. Through serial, write `3` to `qca-dwmac-nss/fw_mask`. Wait for **both** phys_ifs
   to report `started` with the intended netdev and `fw_link=up`.
4. Load `qca-nss-vlan`, then `qca-dsa-nss`. The latter created two nodes without
   allocation failures: LAN3, VID 3088, phys_if 1, interface 28; WAN, VID 3075,
   phys_if 0, interface 29. Dynamic interface numbers are runtime allocations.
5. A five-second routed TCP check passed before ECM. Load ECM with `insmod
   /lib/modules/6.18.55/ecm.ko front_end_selection=1`, and set
   `ecm_classifier_default/accel_delay_pkts=1`. No ECM init service was invoked,
   no nft flowtable enabled, and no persistent network/module options changed.

| Test | Receiver | Router CPU idle | Firmware accelerated RX/TX delta |
| --- | ---: | ---: | ---: |
| TCP LAN -> WAN, one stream, 30s | 939.0 Mbit/s | 98.98% | 2,786,776 each |
| TCP WAN -> LAN, one stream, 30s | 938.8 Mbit/s | 99.00% | 2,717,331 each |
| UDP WAN -> LAN, 400M, 10s, 1400-byte payload | 399.7 Mbit/s; 0% loss | 97.94% | 356,906 each |
| UDP WAN -> LAN, 900M, 10s, 1400-byte payload | 899.0 Mbit/s; 0.0306% loss | 99.17% | 803,003 each |

These are single runs through the same Linux LAN client and Windows WAN server.
Firmware **special** IPv4 packet counters prove acceleration; the common RX
counter also counts host exceptions and is insufficient alone. ECM had active
rules during every run. No netdev RX/TX drops increased in these runs. Endpoints,
configuration hashes, route, boot identity and CPU windows were recorded.

Detailed evidence: `logs/nss-transport-rtnl-live-20261101/`, especially
`transport-verified.json`, `packet-transport-verified.json`, `nss-owned-no-ecm.json`
and `nss-tcp/*/summary.json` / `nss-udp/*/summary.json`. Build inspection is in
`logs/nss-transport-rtnl-image-20261101/verification.json`. Private backups must
not be sent to third-party helpers.

This validates this single-tag wired unicast path. Full QinQ, Wi-Fi acceleration,
transparent stop/rearm recovery and the broader feature matrix above are still
separate work. Startup is manual and is not installed as a boot service. A normal boot
of the transport image starts on the host data path with no NSS modules loaded.


### Host Wi-Fi coexistence and recovery follow-up before cleanup fixes (2026-11-02)

Hot-reloading the previously unloaded host ath11k driver failed: Q6 startup
timed out and a later retry emitted an IRQ enable warning. The cause has not
been isolated between the host remoteproc lifecycle and interaction with NSS;
this is not evidence that Wi-Fi firmware or NSS parsing is at fault. A controlled
NSS teardown and clean reboot recovered both PHYs. Do not hot-unload/reload
ath11k as the normal NSS preparation procedure.

The follow-up left both normally booted host Wi-Fi PHYs loaded, with no active
Wi-Fi interfaces. NSS boot, both GMAC takeovers and both VLAN nodes succeeded.
Two ten-second TCP confirmations reached 938.8 Mbit/s LAN -> WAN (98.18% CPU
idle) and 936.7 Mbit/s WAN -> LAN (98.92% CPU idle), with increasing firmware
accelerated counters. Thus unloading Wi-Fi is not needed for this wired test.
This establishes driver/PHY coexistence, not Wi-Fi client traffic or Wi-Fi NSS
offload. Evidence is under `nss-host-wifi-tcp/` and `final-state.json` in the live
log directory above.

Stopping ECM, removing the VLAN/DSA managers, and clearing `fw_mask` released
both GMACs and reinitialized host DMA successfully. It also left the DSA user
ports administratively down; this bare sequence did not restore working LAN/WAN
by itself. Core/glue modules were then unloaded through their normal cleanup
paths and a serial reboot restored the configured network. No GCC reset was
pulsed and no persistent network setting was changed. A complete stop helper
must preserve/restore DSA user-port state before transparent fallback can be
claimed. The source also treats rearming without reloading the NSS core module
as unresolved; it was not attempted here.

Final live state is slot 1 with wired NSS/ECM manually enabled and both normal
host Wi-Fi PHYs present. The current boot has no Q6/IRQ/kernel failure signatures.
Slot 0 remains the hash-verified default-mode fallback. Boot autostart remains
disabled. A reboot after controlled NSS unload has been checked; an ordinary
warm reboot with NSS still active has not been certified by these tests.


### Manual stop/restart cleanup (2026-11-02)

The glue now snapshots running DSA users attached to exactly the conduit being
restored, then reopens that same set after the required host DMA close/open.
A previously down port stays down, and users on the other conduit are untouched.
Notifier suppression spans the whole restore so user NETDEV_UP cannot reenter
the held glue mutex. Status reports per-phys_if host restore attempts and the
last error; allocation or reopen failure is reported as incomplete recovery.
The actual function passes nine DSA state/failure model cases plus the no-DSA
path under ASan/UBSan. This is source testing, not a substitute for the device
results below.

A separate unload bug was also found: the IPQ50xx HAL did not implement the
optional `core_halt` callback, so common removal never stopped its firmware.
An unmodified full module reload passed Linux probe/IRQ setup but failed to
receive firmware initialization. The local patch adds the core-local reset
clamp and readback before mappings/IRQs are removed. It does not pulse GCC
resets. The tested stops read back `0x1`, and two subsequent NSS starts in the
same kernel boot succeeded, including routed TCP at 938.7 Mbit/s.

The feed base remains pinned. Its one local patch is tracked in
`tools/nss/patches/0900-ipq50xx-halt-core-before-remove.patch`; apply it after
updating the feed with `python3 tools/nss/apply-local-patches.py`. The helper
checks the exact feed revision, refuses tracked feed edits and conflicting
patch files, and otherwise copies only the explicit local overlay. The manual
controller checks the core's read-only `ipq50xx_core_halt` capability before
arming, so an image missing this patch is rejected before takeover.

`/usr/sbin/cr881x-nss` provides manual `start`, `stop`, `restart` and `status`.
It waits for the network and bridge to settle, maps both GMACs explicitly,
arms both, loads VLAN/DSA managers only after takeover, and loads ECM with its
NSS front end. Stop defuncts/removes ECM, removes the managers, releases the
GMACs, checks DSA state restoration, and unloads core then glue. Restart reloads
both modules so firmware/ring state is fresh. Operations are serialized. A
failed start only cleans up state created by that invocation. It rejects an
existing nft flowtable, unknown NSS clients and WAN protocols other than DHCP
or static; it does not change UCI, reload Wi-Fi or install boot autostart.

Device checks kept both host Wi-Fi PHYs loaded throughout. The deliberately
disabled, disconnected LAN2 remained down after stop and restart, while LAN1,
LAN3 and WAN retained their original UP states. Host routing recovered in the
same boot at 493.7 Mbit/s; firmware routing after restart reached 938.7 Mbit/s.
A second complete restart with all four original user ports UP also passed.
The controller now waits for link/bridge recovery when the network was ready
before stop. The live ath11k unload/reload issue itself is not fixed; these
operations avoid that path. QinQ and Wi-Fi NSS remain separate work.

Full image build and inspection passed. Only the NSS core/glue changed among
the eleven checked modules; their image hashes match the tested RAM modules,
the kernel configuration is unchanged, and the controller matches source.
Image export: `bin/targets/qualcommax/ipq50xx/nss-restart-20261102/`.
Sysupgrade SHA256:
`b3cc8b4eb4229bc8ef5dd79c28587c7869f3f833a20b934387c120566c6de1da`.
Build evidence: `logs/nss-restart-image-20261102/verification.json`.
Hardware evidence: `logs/nss-restart-20261102/`, especially
`stop-preserved-down-port.json`, `same-boot-restart-verified.json` and
`two-restarts-verified.json`. The bounded source-cause Qoder audit completed;
the final implementation review timed out without a verdict. Primary source
review, compilation, model tests and hardware checks provide acceptance.


The restart image was subsequently flashed to slot 1 and accepted with both
PHYs, unchanged saved settings and the default-mode slot 0 intact. The installed
`cr881x-nss start` and `restart` commands passed. After restart, LAN -> WAN TCP
reached 938.7 Mbit/s at 98.67% CPU idle (10s); WAN -> LAN reached 938.8 Mbit/s at
98.98% idle (30s with 3s warmup). Firmware accelerated packet deltas increased
by 842,498 and 2,775,627 respectively. The initial reverse 10s run averaged
896.2 Mbit/s due to its first 512.2 Mbit/s second; subsequent intervals were
938-940 Mbit/s. Raw results are retained in
`logs/nss-restart-flash-20261102/RESULT.md` and its TCP directories.

Final state: NSS is enabled manually, both normal host PHYs are present, and
all original DSA administrative UP states are restored. Startup remains manual.
Before a system reboot, use `cr881x-nss stop`; ordinary system shutdown with
NSS still active is a separate unverified lifecycle. Wi-Fi hot-reload remains
unfixed and is avoided by this controller. No QinQ or Wi-Fi NSS claim is made.
