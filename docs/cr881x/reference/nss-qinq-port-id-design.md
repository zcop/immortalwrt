> Reference only: these tests used the Kuncy7 foundation, not the new qosmio branch.

The prototype/uncommitted state below is historical. For the active native
Qosmio implementation and validation, use [current status](../NSS%20Offload%20status.md)
and [implementation notes](../nss-qosmio-port.md).

# CR881x NSS QinQ port-ID transport (design)

Status: working experimental wired IPv4 NAT prototype, integrated as
uncommitted changes and tested with RAM modules on CR881x/kernel 6.18.55.
Exact LAN1/LAN3 steering, inner-VLAN membership enforcement, learned LAN
switching, and NSS/ECM acceleration now have hardware evidence. Default
unaware LAN plus standalone WAN reached about 936 Mbit/s in both TCP
directions with about 94% CPU idle. A VLAN-aware LAN using customer VID 1
also passed. No full image was flashed or boot autostart enabled. See
[routing acceptance, architecture comparison and limits](nss-qinq-routing-test.md).

## Wire format

```
DA | SA | 88a8 | outer S-TCI | 8100 | inner C-TCI | payload ethertype | payload
```

- Outer S-TPID `0x88a8`; outer VID is
  `dsa_tag_8021q_standalone_vid(dp)` (switch index plus physical user port).
  It is never a bridge VID and is not shared between ports.
- Inner C-TPID `0x8100`.
- Headroom is `2 * VLAN_HLEN`; only zero or one customer C-tag is supported.

## Packet ownership

Ingress (wire to CPU):
- The switch does not insert the outer S-tag at the user port itself.
  Untagged or customer-tagged ingress is classified into the port's
  `DEFAULT_SVID` and PVID; on CPU egress the switch writes the SVID as the
  outer `0x88a8` tag and the CVID as the inner `0x8100` tag. The outer SVID
  is the per-port transport label and the only source of exact port
  identity.
- The Linux tagger validates and strips the outer tag, then validates the
  inner tag. NSS/ECM must preserve these tags and add no further VLAN header.

Egress (CPU to wire):
- Software path: this tagger inserts the inner tag when needed and always
  inserts the outer S-tag. Outer PCP comes from `netdev_txq_to_tc()`, not
  from customer or `skb->priority` bits.
- NSS/ECM accelerated path: NSS/ECM must insert the identical outer S-tag
  for the destination port and leave the inner tag as delivered.

## Implemented tagger slice

RX:
- accepts inline or hardware-accelerated outer metadata, requiring TPID
  `0x88a8`;
- resolves the standalone VID to an exact user port on the same conduit;
  rejects bridge outer VIDs, invalid switch/port, wrong conduit, reserved-bit
  aliases, truncated frames, and extra VLAN layers;
- standalone: requires the inner VID to equal that port's standalone VID,
  then strips it;
- VLAN-unaware bridge: requires the inner VID to equal the shared
  `dsa_tag_8021q_bridge_vid(bridge_num)`, then strips it;
- VLAN-aware bridge: requires a real customer VID (not 0, 4095, or the
  internal tag_8021q range with RSV bits set), live membership on this exact
  user port, and learning/forwarding STP state; retains the C-tag inline;
- applies the normal DSA offload forwarding mark.

TX:
- fails closed if `skb->offload_fwd_mark` is set;
- standalone: untagged only; inserts the standalone VID as the inner tag;
- VLAN-unaware bridge: untagged only; inserts the shared bridge VID;
- VLAN-aware bridge: preserves one existing C-tag, or inserts the port PVID
  for untagged traffic after live port-membership validation;
- rejects user S-tags/legacy QinQ, deeper QinQ, VID 0/4095/internal range,
  and tagged traffic on standalone or unaware ports;
- pushes hardware-accelerated customer tags inline before the outer tag.

## Implemented qca-dsa-nss slice

The glue module gains a port-ID mode selected by its module parameter
`port_id_tpid` (0 keeps the existing single-tag nodes; 0x8100 or 0x88a8
selects the QinQ transport):

- one root node per user port carrying the outer tag
  `(port_id_tpid, dsa_tag_8021q_standalone_vid())` on the conduit's phys_if;
- one leaf node per inner VID, with the root's dynamic interface number as
  `next_hop`: the customer VIDs of a VLAN-aware bridge, the shared bridge VID
  when `dsa_port_bridge_num_get()` names one, or the port's own standalone
  VID on standalone ports;
- nodes are keyed `(svid, vid, phys_if)`, so ports are no longer collapsed by
  VID, and a leaf is rebuilt whenever its root is rebuilt;
- the node cap is 64, inside the firmware pool of 128 dynamic interfaces;
- lookups: `qca_dsa_nss_if_num(conduit, svid)` returns a port root, and the
  new exported `qca_dsa_nss_if_num_qinq(conduit, svid, vid)` returns a leaf
  (`vid == 0` returns the root).

Validation (2026-11-03): a forced out-of-tree rebuild against the prepared
6.18.55 tree, with `KBUILD_EXTRA_SYMBOLS` pointing at the `qca-nss-drv`
symvers, completed with exit 0. The compiler emitted no warnings. The only
diagnostic is modpost's missing-MODULE_DESCRIPTION warning, which this
target also emits for sources that do define `MODULE_DESCRIPTION` (line 868
here) because of `CONFIG_MODULE_STRIPPED=y`. The resulting `qca-dsa-nss.ko`
exports `qca_dsa_nss_if_num` and `qca_dsa_nss_if_num_qinq`, carries the
`port_id_tpid` parameter, and hashes to `e0dc297f6d633db129d8017518b565addc2dd51d134f81f90af37c420c8a61f5`
from source
`338f1e76a61e78cd70f69aeb6367464823f97144db8a1d7a325c1636d3e10f23`.
Checkpatch reports `0 errors, 2 warnings, 868 lines`; both warnings are the
extern prototypes in the `.c` file at lines 75-76. It had not been loaded at that build checkpoint; the later routing
acceptance below validates the port-ID nodes on hardware.

## Implemented yt921x selection slice

- New module parameter `experimental_qinq` (baked by
  `CONFIG_DSA_YT921X_EXPERIMENTAL_QINQ`) makes
  `yt921x_dsa_get_tag_protocol()` return `DSA_TAG_PROTO_YT921X_QINQ` on both
  CPU conduits; it implies `experimental_8021q`, so the tag_8021q standalone
  and bridge VIDs, and the CR881x dual-conduit validation, stay active.
- `yt921x_dsa_port_bridge_join()` clears `tx_fwd_offload` after
  `dsa_tag_8021q_bridge_join()` in this mode, because the tagger replicates
  bridge traffic in Linux and rejects hardware-forwarded frames.
- Bridge numbering still comes from `ds->max_num_bridges`, which the glue's
  leaf selection keys on.

The slice cross-compiles cleanly as an external module against the prepared
Linux 6.18.55 tree with a staged `net/dsa.h` carrying the pending enum, exit
status 0; `yt921x_driver.ko` sha256
`9eaa27e7148e9c14d4f1d7e948d4e4df2fb8c5d4e45a2f22390d2f62a6959335`.

## Implemented switch slice

The switch programming lives in `yt921x_8021q.c` and
`yt921x_qinq_acl.c`; the driver Makefile includes the new ACL object. It runs
only when `priv->qinq` is set, inside the normal `reg_lock` paths:

- Global: `yt921x_qinq_setup()` sets ingress and egress TPID profile 1 to
  `0x88a8`; profile 0 stays `0x8100` for customer tags. The live baseline has
  `TPID_EGR1 = 0`, so programming it is a hard requirement, not a no-op.
  Every port selects STAG profile 1 and CTAG profile 0 on both directions.
  The egress selector is a numeric profile index; the ingress selector
  (`PORTn_IGR_TPID`) is a profile *mask*, so `0x21` accepts both layers.
- `yt921x_vlan_aware_set()` is QinQ-aware: the bridge
  VLAN-filtering/unbridge paths rewrite the whole ingress TPID-select
  register, and without the override they would narrow the mask back to one
  profile and break the outer tag after a filtering toggle.
- Egress tag modes: `TAGGED` always writes the frame's classified VID for
  that layer. `KEEP_ALL` keeps only tags already present, which the user
  port does not create; live traffic disproved its use on CPU egress. The
  per-layer `*_KEEP` bit (31/30) is only meaningful for `KEEP_ALL` and stays
  clear.
- CPU ports (p8/p4): keep the existing tag_8021q CPU setup, then egress
  STAG mode `TAGGED` and CTAG mode `TAGGED`, so the classified SVID and CVID
  leave as inline `0x88a8` and `0x8100` tags. Keep ingress lookup on the
  CVID (`LOOK_UP_VLAN_SEL[port]` clear) so host and user frames share one
  FID; set `SVLAN_DROP_UNTAGGED` so an S-untagged CPU frame cannot fall back
  to the default SVID (VLAN 1) and flood outside the conduit.
- User ports: keep the existing single-tag user setup, then
  `PORT_VLAN_CTRL.DEFAULT_SVID = dsa_tag_8021q_standalone_vid(dp)`, egress
  STAG mode `UNTAGGED` (strip the outer tag, leave the C-tag to the VLAN
  entry), C lookup (`LOOK_UP_VLAN_SEL` clear), and `SVLAN_DROP_TAGGED` so a
  customer-supplied S-tag is rejected rather than mistaken for transport
  identity.
- Bridge join: `dsa_tag_8021q_bridge_join()` deletes the port's standalone
  VID and drops the CPU membership through refcounting. In QinQ mode
  `yt921x_qinq_restore_standalone()` re-adds the standalone VID to the user
  port and to every CPU port afterwards, because that VID is the permanent
  outer SVID, not a temporary standalone label.
- `yt921x_8021q_filtering()` does not delete the standalone VID when it
  equals the port's internal VID in QinQ mode, so enabling bridge VLAN
  filtering cannot silently remove the transport label.
- The QinQ port keeps its per-port standalone VID as the outer SVID after
  bridge join; the inner C-VID is the bridge/customer identity. The outer
  SVID identifies the exact source port for the tagger, while forwarding and
  FDB lookups use the inner C-VID so host and CPU frames share one FID.
- Reserved ACL block 47 matches CPU8 plus SVID 3072/3073/3074 to LAN1/2/3,
  and CPU4 plus SVID 3075 to WAN. Both tag formats must be tagged; the CVID
  is wildcarded. The action uses `REDIR_EN | REDIR_STEER | DPORTn` in
  action word 2. CPU unmatched ACL permission is disabled, and user egress
  VLAN filtering stays enabled. Ordinary TC rules skip this entire block
  in QinQ mode (eight reserved slots, four used).
- Rules use the final DT-pinned conduit policy, not `dp->cpu_dp` during
  switch setup: DSA initially assigns its first CPU to every user port and
  netifd applies the board split later. The conduit-change callback rejects
  remapping outside that policy. Failed rule installation keeps CPU
  unmatched traffic denied; teardown clears the block before reopening it.

Live hardware semantics (CR881x, 2026-11-04, boot `9aee09cd`):

- An untagged user-port ingress frame is classified into `DEFAULT_SVID`
  (0xc02 on p2) and the bridge PVID (0xc10). With CPU egress `TAGGED` modes
  the switch emits `88a8 0c02 | 8100 0c10` towards the conduit. The user
  port itself does not insert the S-tag, which is why the earlier `KEEP_ALL`
  attempt emitted untagged frames.
- `EGR_STAG_MODE_UNTAGGED` on the user port strips the outer tag without
  consuming the inner C-tag, and entry-based CTAG egress leaves PVID/untagged
  VLANs untagged, so the host sees plain untagged frames.

Build status: the module rebuilds with the OpenWrt toolchain both
out-of-tree (`logs/yt921x-qinq-build/`) and in the prepared 6.18.55 tree
(`drivers/net/dsa/yt921x_driver.ko`), `RC=0`, vermagic
`6.18.55 SMP mod_unload aarch64`; the only diagnostics are the known
missing-MODULE_DESCRIPTION and `STAGING_DIR` warnings. In-tree sha256
`148809d5d51f985e89294b2be9ad4795138379728ad760419011565f576c98f0`; this is
the build that passed the live validation below.

The QinQ tagger was pre-flighted on the live kernel (boot
`55d75c8c-2661-4591-80e6-87feacf49dd9`): `tag_yt921x_qinq.ko`
(sha256 `b53f08a285d2a3273afdda1769b07ca5878ea312bd2af3c89ff0da5e0b3e652f`)
loaded and unloaded cleanly without touching the datapath. That proved module
ABI compatibility and protocol registration; packet behavior was then proven
by the module swap below.

No image has been built or flashed with
`CONFIG_DSA_YT921X_EXPERIMENTAL_QINQ=y`.

## Live module-swap validation (2026-11-04)

The switch transport and the Linux tagger were validated end to end by an
in-memory module swap on the CR881x (boot `9aee09cd`), without an image flash
and without changing router configuration:

- The stock modules in `/lib/modules` were left untouched. The QinQ driver
  (sha256 `148809d5d51f985e89294b2be9ad4795138379728ad760419011565f576c98f0`)
  and `tag_yt921x_qinq.ko` were uploaded to `/tmp`; the switch was unbound,
  the stock `yt921x_driver` removed, and both modules loaded with
  `experimental_qinq=1`. A 300 s watchdog restored the stock modules unless
  the host confirmed the transport.
- Both conduits reported `yt921x-qinq`, and `lan3` rejoined `br-lan`.
  `experimental_8021q` read back `N` because QinQ implies it internally.
- Register readback matched the design: `TPID_IGR1`/`TPID_EGR1` = `0x88a8`;
  p2/p4/p8 ingress TPID mask `0x21`; p2 egress STAG `UNTAGGED` + CTAG
  `ENTRY_BASED`; p8 egress STAG/CTAG `TAGGED` with KEEP bits clear; p2
  `DEFAULT_SVID` `0xc02` with PVID `0xc10`; p2 drops customer S-tags
  (`VLAN_CTRL1` `0x8`); p8 drops S-untagged frames (`VLAN_CTRL1` `0x4`); CPU
  ingress lookup select clear (CVID lookup).
- VLAN table: 3074 members `{p2,p4,p8}` untagged `{p2}`; 3088 members
  `{p0,p1,p2,p4,p8}` untagged `{p0,p1,p2}`.
- Capture on the eth1 conduit: TX carried inline `88a8 0c02 | 8100 0c10`;
  RX carried the inner `8100 0c10` inline with the outer tag lifted into
  conduit metadata. The host saw plain untagged frames.
- Datapath: three TCP handshakes bound to the test NIC reached dropbear in
  0.03 s each, and SSH over the same QinQ transport stayed usable throughout.
  ICMP is not a usable gate on this host (router-to-host echo fails on the
  stock transport too), so it stayed informational only.
- Result: `PASS`; the watchdog was disarmed (`/tmp/qinq-ok`) and the router
  was left running the QinQ modules for this boot. A reboot restores the
  stock single-tag modules from `/lib/modules`; no persistent change exists.
- At that checkpoint, NSS nodes and acceleration were unvalidated. The
  later routing acceptance now covers them; wider gates 3-6 remain open.

### Multiport steering acceptance, 2026-11-04

The later RAM-only test used Linux on LAN3 and Windows on LAN1, with NSS
stopped. The new driver is
`f8fce6f237c68cc94ab10776ba1eed9e37bacda59997599024969a29887338dc`;
the tagger remains `b53f08a2...`. Both conduits report `yt921x-qinq`.
This supersedes the earlier single-host result for destination steering.

Every probe used the same Windows destination MAC, three ARP requests per
case, and the driver's own ACL rules without raw ACL overrides:

| CPU8 input | Linux on LAN3 sees requests | Windows on LAN1 replies |
| --- | ---: | ---: |
| SVID 3072, CVID 2717, both LAN ports members | 0 | 3 |
| SVID 3074, same CVID and destination MAC | 3 | 0 |
| Unknown SVID 3071 | 0 | 0 |
| WAN SVID 3075 on wrong CPU8 conduit | 0 | 0 |
| Missing outer S-tag | 0 | 0 |
| SVID 3072, CVID 2718 excludes LAN1 | 0 | 0 |
| SVID 3074, CVID 2718 includes LAN3 | 3 | 0 |
| Repeat valid LAN1 case | 0 | 3 |

LAN3 wire captures show no internal VLAN tags. The two temporary CVID
entries were restored and read back. A separate ordinary, untagged
LAN3-to-LAN1 test passed three requests and three replies, with zero marked
requests in the CPU-conduit capture: learned LAN switching stayed in silicon.
The CPU capture uses link type 303; this router's tcpdump needs an unfiltered
capture for the new protocol.

An initial module load rejected DSA's temporary all-on-first-CPU assignment.
The subsequent source fix uses the board's pinned final mapping. Switching
straight from this QinQ RAM experiment to the stock single-tag module did
not restore Ethernet access; serial reloaded the previously working QinQ
module before retry. The retry's watchdog therefore used that verified
QinQ module as rollback. The successful run remains on the same boot,
with saved network/firewall/wireless/DHCP/system hashes unchanged and no
writes to `/lib/modules`. No firmware flash or reboot occurred.

Evidence: `logs/qinq-steering-live-v3-20261104/` contains the module-swap
result, eight probe JSON/pcap records, VLAN restoration readback, and
`lan-switch-result.json` with host and CPU captures. Build logs and source
hashes are in `logs/qinq-steering-fix-20261104/` and the v2 run directory.
The software transport/fault tests pass, including 60 ACL commit write
failure points; those tests model register intent, not real MDIO failures.

The manual controller detects either transport, selects
`port_id_tpid=34984` for QinQ, and verifies the loaded glue parameter and
status before reporting readiness. Seventeen mocked mode/readiness cases
and `sh -n` pass. It subsequently armed QinQ NSS successfully in the routing
acceptance test; the stop/reset/takeover order is unchanged.


## Implemented ECM rule slice

`feeds/nss/qca-nss-ecm/patches/0061-frontends-nss-carry-the-qinq-port-id-transport.patch`
(mirrored in `tools/nss/patches/qca-nss-ecm/` and installed into the pinned
feed by `tools/nss/apply-local-patches.py`) carries the port-ID
transport into the NSS offload rules:

- `ecm_interface_dsa_qinq_port()` recognises a user port whose DSA tag
  protocol is `DSA_TAG_PROTO_YT921X_QINQ` (35, spelled out so the package
  builds on kernels without the tagger) and reports the conduit and the
  port's `dsa_tag_8021q_standalone_vid()`.
- `ecm_interface_dsa_nss_if_num_qinq()` resolves the glue's leaf node for
  (port SVID, inner VID) at first use with `symbol_get()`, like the existing
  single-tag lookup.
- `ecm_nss_common_vlan_aware_dsa_side()` keeps taking the inner VID from the
  connection's bridge VLAN filter data; on a QinQ port it additionally
  returns the port SVID and the leaf node.
- The ported IPv4/IPv6 frontends write the outer tag
  `(ETH_P_8021AD << 16) | svid` into `vlan_primary_rule`, the inner tag
  `(ETH_P_8021Q << 16) | vid` into `vlan_secondary_rule` and the leaf as that
  direction's next hop. Single-tag ports keep the previous primary-only rule
  and the conduit next hop; `port_id_tpid=0` is untouched.
- A QinQ port outside a VLAN-aware bridge, and a VLAN upper of one, are
  refused by the ported VLAN cases and by the non-ported IPv4/IPv6
  frontends, so those flows stay on the host path instead of being tagged
  with the outer VID as if it were an 802.1Q tag. The refusal is a transport
  test (`ecm_interface_dsa_qinq_vlan_unsupported()`), so it also applies
  while the glue has built no node for the port.

Checks: the patch is hand-authored in quilt form (25 hunks across the seven
files), not generated by `git diff`. Every hunk was checked line by line
against the post-0060 source copy in
`logs/nss-qinq-tagger-20261102/ecm-src.pre0061/`: all context and removed
lines match, and the edited tree in `ecm-src/` matches the patch. That copy
already carries the first hunk, so the other `ecm_interface.c` hunks sit at
a +8 line offset there; against the pristine `build_dir` tree they apply at
the stated offsets.

Apply and compile are now proven. `patch -p1 --dry-run -F0` against a scratch
copy of the pristine post-0060 `build_dir` source applied all seven files with
zero offsets and zero fuzz, after one fix: the patch file lacked a trailing
newline, which made GNU patch treat the last hunk's final context line as
truncated (`patch unexpectedly ends in middle of line`) and fail that hunk.
`make package/feeds/nss/qca-nss-ecm/clean` then
`make package/feeds/nss/qca-nss-ecm/compile -j1 V=s` finished with
`build_rc=0`, no warnings and no errors
(`logs/nss-qinq-tagger-20261102/ecm-compile.log`). The resulting `ecm.ko`
contains the three new helpers and an undefined `dsa_tag_8021q_standalone_vid`;
`qca_dsa_nss_if_num_qinq` appears only as the `symbol_get()` name, so the
package still loads without the glue module present. Module sha256
`ca59b7069f33af3f487a143e76579a4cbd647b913a37f9fd1f3aec96e6f8529e`.
Compile review found no missing API: the DSA code is
gated on `IS_ENABLED(CONFIG_NET_DSA)` (the legacy
`ECM_INTERFACE_DSA_ENABLE` macro is unset and brackets only unrelated
includes here); `dp->ds->dst` and `ds->dst->tag_ops->proto` exist in
6.18.55; `dsa_tag_8021q_standalone_vid()` is `EXPORT_SYMBOL_GPL` and ECM is
`Dual BSD/GPL` (the single-tag path already calls it); `vlan_dev_next_dev()`
is an `EXPORT_SYMBOL` QCA helper in this kernel's `include/linux/if_vlan.h`;
and `vlan_primary_rule`/`vlan_secondary_rule` exist in both staged
`nss_ipv4.h` and `nss_ipv6.h` under the existing
`NSS_IPV{4,6}_RULE_CREATE_VLAN_VALID` flag. No hardware was touched and no
rule is validated.

## Current integration and remaining gates

ECM patch 0062 extends 0061 to ordinary unaware-LAN and standalone-WAN
ports. It models QinQ ports as Ethernet leaves, obtains their inner VID
from bridge/standalone state without requiring customer VLAN-filter data,
and resolves the exact `(conduit, SVID, CVID)` firmware leaf. Routed bridge
traffic also uses that leaf as its next hop. Non-ported flows keep their
host fallback and real VLAN-upper restrictions remain in place.

The manual controller and the normal NSS/ECM builds passed real routed
TCP/UDP checks, VLAN-aware PVID1 checks and same-boot stop/restart. The
separate routing report records the measurements and cleanup. Full spoof,
STP, multicast, QoS, cold boot and wider VLAN feature acceptance remain open.

## Integration prerequisites

- Draft registration uses `DSA_TAG_PROTO_YT921X_QINQ` (ID 35), with
  Kconfig/Makefile/package wiring. `experimental_qinq=1` (module parameter,
  baked by `CONFIG_DSA_YT921X_EXPERIMENTAL_QINQ`) selects it and implies
  `experimental_8021q`. The drafted slice is integrated into the main
  working tree; the wired routing prototype passed live validation, while
  the mode stays disabled by default pending broader acceptance.
- The QinQ mode leaves `tx_fwd_offload == false`; Linux replicates
  CPU-originated bridge traffic per destination port.
- Switch hardware must insert the ingress outer tag, strip it on egress, and
  steer by the standalone VID independently of the DA.
- NSS/ECM must implement the same outbound outer insertion.

## Limitations

- No transparent customer QinQ: zero or one customer C-tag only.
- Tagged traffic on standalone or VLAN-unaware bridge ports is rejected.
- Switch user ingress is programmed to drop customer S-tags; packet-level
  spoof rejection still needs a dedicated negative test.
- QinQ reserves one eight-slot ACL block, leaving 376 slots for ordinary TC
  rules instead of 384. Four slots implement the board's steering rules.
- Inserted PVID/internal tags use PCP 0; existing customer tags preserve
  PCP/DEI.
- No QinQ sysupgrade image build. The switch transport and tagger are live-validated
  (module swap, not an image). The glue and ECM subsequently passed
  wired routing acceptance with the QinQ transport; see the routing report.
  Module compilation alone does not validate packet behavior.
- Raw-register Gate 1 attempt 1 made bounded ACL/VTU/TPID writes on the live
  switch and restored them, but produced no packet evidence: the capture
  helper relied on `nohup`, which this image's BusyBox lacks, and the
  compound rule never counted. Attempt 2 ran with local SSH `tcpdump` and
  proved the sender path (markers on eth1 carrying the switch's 8100
  tag_8021q tag), but its flow-stat sanity rule was a bare L4-type entry,
  a shape `yt921x_acl_parse()` never builds; it never counted and the run
  aborted before the driver-shaped chain was tried. The attempt-2 cleanup
  restore ran and the 420 s watchdog re-ran it, but the post-run register
  state was not read back.
- Attempt 3 (`logs/nss-qinq-gates-20261104/gate1-test3.py`) did run on
  2026-11-04 01:25-01:37 and failed on the driver-shaped rule: the `full`
  chain reported `silent_delta: 0`, so the STAG action never fired, and the
  `stag` capture showed an outer `8100` tag instead of the expected `88a8`.
  Post-run cleanup was verified: ACL block 47 zero, `TPID_EGR1=0`,
  `P8_EGR=0`, flowstat slot 62 zero, VTU 3074 zero. The failure means the
  tested ACL-based ingress-insertion shape did not work. It does not prove
  the hardware cannot insert tags using another rule shape. This switch
  slice uses the separately validated `DEFAULT_SVID` mechanism instead.
- Firmware raw direct-rule tests for double C/C and S/C, including correct
  accelerated egress tags, do not exercise or validate this Linux tagger.

## Hardware gates

Done in the 2026-11-04 module-swap run (see above):

0. Bounded switch programming with NSS stopped: `TPID profile 1 = 0x88a8`,
   per-port TPID select, `DEFAULT_SVID`, egress STAG modes and
   `LOOK_UP_VLAN_SEL` were applied and read back exactly as designed.
1. Ingress outer insertion with the exact per-port standalone VID: TX on the
   conduit carried `88a8 0c02 | 8100 0c10`.
2. Egress outer stripping and destination steering: the later two-host
   driver test above selects LAN1 versus LAN3 solely by the outer SVID,
   drops invalid/wrong-conduit labels and enforces inner-VLAN egress
   membership. Ordinary learned LAN switching also passed. LAN2/WAN
   endpoint delivery, full STP behavior and VLAN-aware reconfiguration
   remain outside that bounded result.

The later routing acceptance also passes NSS/ECM port-ID nodes and real
IPv4 NAT flows on both unaware and VLAN-aware LAN, with same-boot recovery.

Still open:

3. Spoofed S-tag handling, including customer S-tags and reserved VIDs.
4. VID/STP isolation for standalone, unaware, and aware modes.
5. Unknown-MAC and flooding behavior with Linux-side replication.
6. QoS: outer PCP from Linux TC, customer PCP/DEI preservation, and
   accelerated-path equivalence.
