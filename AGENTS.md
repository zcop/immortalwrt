# AGENTS.md

## Scope
This file tracks major vendor `1806_SDK` switch features outside ACL that still
exceed the current Linux `yt921x` driver surface.

ACL-specific status and next steps live in:
- `Collection-Data/yt921x/YT9215-ACL-GAP-TRACKER.md`

## Source of truth
Compare these two surfaces first:
- vendor switch API:
  `/home/zcop/workspaces/siflower/openwrt/1806_SDK/openwrt-18.06/package/kernel/sf_eswitch/src/yt9215rb_src`
- current Linux driver:
  `target/linux/generic/files/drivers/net/dsa/yt921x*.c`

Do not describe a vendor feature as "missing from hardware". For non-ACL work,
the question is usually:
- Linux already has a subsystem and the driver has not mapped it yet
- Linux has only a reduced or indirect API for it
- the feature is vendor-private and not worth exposing directly

## Vendor features with no meaningful Linux counterpart yet

### Vendor features with no meaningful Linux counterpart yet
- `gpio`
  Vendor surface:
  `yt_gpio.h`
  Current Linux state:
  no switch GPIO subsystem mapping.

- `oam`
  Vendor surface:
  `yt_oam.h`
  Current Linux state:
  no comparable Linux control surface.

- `rspan`
  Vendor surface:
  `yt_rspan.h`
  Current Linux state:
  local mirror support exists, but no vendor-style RSPAN support.

- `nic`
  Vendor surface:
  `yt_nic.h`
  Current Linux state:
  no equivalent switch NIC / CPU packet-engine control API.

- `dos`
  Vendor surface:
  `yt_dos.h`
  Current Linux state:
  no meaningful Linux user-facing implementation.

- `interrupt`
  Vendor surface:
  `yt_interrupt.h`
  Current Linux state:
  no comparable runtime interrupt feature surface.

- `sensor`
  Vendor surface:
  `yt_sensor.h`
  Current Linux state:
  hwmon temperature readout now exists for the DT-enabled subset, but live
  CR881x validation shows the raw sensor register stays `0x00000000`, so this
  board likely has no usable on-chip temperature source.

- `vlan_translate`
  Vendor surface:
  `yt_vlan_translate.h`
  Current Linux state:
  VLAN filtering/membership exists, but not the vendor translation surface.

### Vendor features only partially represented in Linux
- `igmp_mld` / `multicast`
  Vendor surface:
  `yt_igmp_mld.h`, `yt_multicast.h`
  Current Linux state:
  bridge MDB/mrouter/snooping basics exist, and partial runtime exposure now
  exists through devlink for:
  `mcast_igmp_opmode`,
  `mcast_mld_opmode`,
  `mcast_report_allow_mask`,
  `mcast_query_allow_mask`,
  `mcast_leave_allow_mask`,
  `mcast_vlan_leaky_mask`,
  `mcast_igmp_bypass_iso_mask`,
  `mcast_router_port_only`,
  `mcast_router_port_primary`,
  `mcast_report_leave_fwd`,
  `mcast_dynamic_routerport_allow_mask`,
  `mcast_bypass_grouprange_mask`.
  Live status:
  CR881x sysupgrade validation is done. The new devlink params are present on
  the running image, and runtime set/get was confirmed on-device for parser
  mode, per-port policy masks, router-port forwarding flags, and dynamic
  router-port allow masks.
  Remaining gap:
  this still does not cover the full vendor multicast control surface,
  especially dynamic router-port aging, learn-limit controls, IP multicast
  entry management, and the vendor-specific MVR-style/multicast VLAN pieces.

- `dot1x`
  Vendor surface:
  `yt_dot1x.h`
  Current Linux state:
  partial runtime exposure exists through devlink/debug hooks, including:
  `dot1x_mac_based_mask`,
  `dot1x_port_based_mask`,
  `dot1x_rx_permit_mask`,
  `dot1x_tx_permit_mask`.
  Live status:
  raw gate control is proven on CR881x, and a full wired 802.1X EAP-MD5
  exchange is also proven when the test port is detached from `br-lan` and
  owned directly by the supplicant.
  Remaining gap:
  normal bridged DSA operation is not integrated yet. A `br-lan` slave port
  does not currently behave like a finished Linux 802.1X deployment path, so
  this stays partial rather than closed.

- `storm_ctrl`
  Vendor surface:
  `yt_storm_ctrl.h`
  Current Linux state:
  partial runtime exposure now exists through devlink for the software
  storm-guard approximation:
  `storm_guard_enable`,
  `storm_guard_pps`,
  `storm_guard_hold_ms`,
  `storm_guard_interval_ms`.
  Live status:
  CR881x sysupgrade validation is done. The new devlink params are present on
  the running image, and broadcast flood traffic from `lan2` was observed
  driving the live storm mask from idle state to active blocked-port masks.
  Remaining gap:
  this is still not the vendor hardware API. Linux does not yet expose the
  vendor per-port/per-storm-type enable, rate-mode, and rate controls from
  `yt_storm_ctrl.h`.

- `led`
  Vendor surface:
  `yt_led.h`
  Current Linux state:
  limited DT/debugfs handling only, not a full LED subsystem mapping.

- `ctrlpkt` / `rma`
  Vendor surface:
  `yt_ctrlpkt.h`, `yt_rma.h`
  Current Linux state:
  setup defaults remain narrower than vendor, but runtime exposure now exists
  through devlink for:
  `rma_bpdu_action`,
  `rma_eapol_action`,
  `rma_slow_action`,
  `ctrlpkt_arp_act_mask`,
  `ctrlpkt_nd_act_mask`.
  Live status:
  register/API programming is confirmed on CR881x. The new `rma_*_action`
  devlink params are present on the running image, and runtime action changes
  were confirmed live for BPDU, EAPOL, LLDP, and slow-protocol slots.
  Packet-fate validation is still open. We do not yet know, from live
  traffic, which frames are forwarded, trapped, copied, or dropped after each
  setting.
  Updated live result:
  the newly added `rma_*_bypass_iso` / `rma_*_bypass_vlan` devlink params are
  present on the running CR881x image, accept runtime set/get, and the rebuilt
  module is confirmed live rather than stale.
  LLDP packet-fate is not proven as a useful normal Linux data-path control.
  Direct CR881x A/B register tests show crafted LLDP frames entering on
  `lan2`, but they do not emerge as ordinary switched `lan1` traffic even
  with `rma_lldp_action=forward` and even when the real `lan2` ingress
  `ctrlpkt_lldp_act_mask` bit is toggled.
  Trap/copy forcing also did not produce a Linux-visible LLDP signature on the
  CPU conduit capture, so the frame currently looks consumed by a special
  hardware path before ordinary switched egress.
  `bypass_iso` is therefore still not behavior-proven. Under the tested
  mutual-isolation case (`lan1` and `lan2` both isolated), LLDP forwarding
  stopped with `rma_lldp_bypass_iso=0` as expected, but still did not resume
  with `rma_lldp_bypass_iso=1`.
  Practical conclusion:
  LLDP/RMA exposure is not urgent for normal Linux use and is better kept
  behind debug-oriented controls unless a concrete Linux-facing use case
  appears.
  Full vendor runtime coverage is still not present.

## Practical reading
The largest non-ACL gaps are:
- `dot1x`
- `gpio`
- `oam`
- `rspan`
- `nic`
- `dos`
- `interrupt`
- `sensor`
- `vlan_translate`

The largest partial areas are:
- `igmp_mld` / `multicast`
- `dot1x`
- `storm_ctrl`
- `led`
- `ctrlpkt` / `rma`

## Guardrails
- Prefer existing Linux subsystems over vendor-private user APIs.
- Treat vendor API presence as evidence of hardware capability, not automatic
  justification to expose the same control shape in Linux.
- Distinguish clearly between:
  missing Linux mapping,
  partial Linux mapping,
  and vendor-private features that should likely stay private.
