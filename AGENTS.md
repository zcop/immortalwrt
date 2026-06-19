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
- `dot1x`
  Vendor surface:
  `yt_dot1x.h`
  Current Linux state:
  only internal/devlink/debug hooks around DOT1X registers; no comparable
  public feature surface.

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
  bridge MDB/mrouter/snooping basics exist, but not the full vendor control
  surface.

- `storm_ctrl`
  Vendor surface:
  `yt_storm_ctrl.h`
  Current Linux state:
  policer/storm-guard approximations exist, not the full vendor API.

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
  `rma_slow_action`,
  `ctrlpkt_arp_act_mask`,
  `ctrlpkt_nd_act_mask`,
  `ctrlpkt_lldp_eee_act_mask`,
  `ctrlpkt_lldp_act_mask`.
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
