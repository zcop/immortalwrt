# YT921x Enhanced DSA Driver README

This document is the feature-focused README for the YT921x switch driver used in this tree.

Scope:
- Driver: `target/linux/generic/files/drivers/net/dsa/yt921x*.{c,h}`
- Tagger: `target/linux/generic/files/net/dsa/tag_yt921x.c`
- Validation baseline: Xiaomi CR881x runtime in this workspace

Important:
- The `13` entries in `agents.md` are milestone targets, not the total driver feature count.
- The driver exposes more functionality than those 13 tracking buckets.

## What Is Implemented

### DSA Switch Ops Coverage (High-Level)
- MIB/ethtool statistics surfaces (`get_strings`, `get_ethtool_stats`, `get_stats64`, pause/RMON/MAC/ctrl stats)
- EEE support (`set_mac_eee`, `supports_eee`)
- MTU control (`port_change_mtu`, `port_max_mtu`)
- STP/MST integration (`port_stp_state_set`, `port_mst_state_set`, `vlan_msti_set`)
- Phylink MAC ops and capability advertisement (`phylink_get_caps`, `mac_config`, `mac_link_up/down`)
- Conduit migration support (`port_change_conduit`)

### Core DSA/L2
- Port bridge join/leave and bridge flags
- VLAN add/del/filtering
- FDB add/del/dump
- MDB add/del (hardware multicast group programming)
- LAG join/leave (bounded hardware mode)
- Port mirror add/del

### Control/Trust/QoS Mapping Surfaces
- DCB default priority get/set
- Application trust profile get/set
- DSCP-to-priority mapping add/del/get
- Queue scheduling/shaping control paths used by `mqprio`/`ets`/`tbf` offload

### ACL Offload (`tc flower`)
- Ingress ACL offload
- Egress ACL offload (supported subset, with fast-fail for unsupported key shapes)
- Actions:
  - drop
  - redirect
  - mirror
  - trap
  - skbedit priority
  - pedit DSCP rewrite (IPv4 + IPv6 traffic class)
  - csum action acceptance for IPv4-header recalc chain shape used with DSCP rewrite

### ACL UDF
- Userspace-offloaded UDF matching is wired for deterministic TTL/Hop-Limit match shape via `FLOW_DISSECTOR_KEY_IP` (`ip_ttl`)
- IPv4 and IPv6 parity validated for TTL/Hop-Limit style matching
- Resource/path limits are fast-failed with `-EOPNOTSUPP`/`-ENOSPC` (no silent partial behavior)

### QoS/TC
- `mqprio` offload path
- `ets` offload path (current mapped subset)
- Queue `tbf` offload path
- Ingress policer path with guarded fallback behavior in driver

### Reserved Multicast
- Reserved multicast policy surface is implemented and validated (trap/copy/drop path control via driver debug command surface)

### Advanced VLAN Control Surface
- Advanced VLAN table/control register surfaces are mapped into the driver debug command interface (translation-related table IDs and field decoding coverage)
- Live control-path and safe write/restore validation completed

### Additional Debug/Bring-Up Surfaces
- Register/table/field read-write command interface (debug build)
- RMA policy controls, loop-detect/WoL register-path controls, and unknown flood policy inspection helpers (debug-gated)

## Current Feature Status (From `agents.md`)

DONE:
- 1) ACL Egress Offload
- 2) ACL Redirect Offload
- 3) ACL Mirror Offload
- 4) ACL Trap Offload
- 5) Advanced VLAN: Translation / QinQ / MVR (current driver-phase milestone)
- 6) 802.1X + Guest VLAN Controls
- 7) IGMP/MLD Advanced Snooping Controls
- 12) Reserved Multicast Control
- 13) ACL UDF Userspace Offload

TODO:
- 8) QoS Queue Drop Policy Controls (tail-drop/WRED style behavior proof)
- 9) Loop Detection / WOL / OAM-EEE LLDP Controls (remaining deterministic dataplane proof gaps)
- 10) Port Security/Policy Knobs (policy programming path behavior gap)
- 11) ACL Advanced Parity (remaining datasheet surface not yet mapped to tc parser/actions)

## User-Facing APIs

Primary:
- `bridge` (VLAN/FDB/MDB/bridge flags)
- `tc` (`flower`, `mqprio`, `ets`, `tbf`, policer-related setup paths)
- Standard DSA netdevices/ports

Behavioral rule:
- Unsupported hardware semantics are intentionally rejected with explicit fast-fail (`-EOPNOTSUPP`, `-EINVAL`, `-ENOSPC`) to avoid silent partial offload.

## Debug-Only Surfaces

When built with debug support, the driver exposes low-level command interfaces (for bring-up/reverse-engineering/validation), including register/table field inspection and selected control helpers.

Design intent:
- Keep production user path on standard Linux APIs (`bridge`, `tc`, ethtool/DSA paths)
- Keep exploratory silicon knobs behind debug gating

## Known Limits

- Some `tc flower` key combinations that exceed silicon block shape or unsupported match geometry are rejected by design.
- Per-port unknown-unicast flood semantics do not map 1:1 to hardware in all cases; unsupported bridge flag semantics are rejected instead of partially emulated.
- Queue drop policy (WRED/tail-drop style tuning) is not yet proven as a complete userspace feature in current phase.

## Useful References

- Feature tracker and evidence log: `agents.md`
- Register map: `docs/yt921x/yt9215-register-map.md`
- Register-map changelog: `docs/yt921x/yt9215-register-map-changelog.md`
- Live reverse-engineering notes: `docs/yt921x/live/`
