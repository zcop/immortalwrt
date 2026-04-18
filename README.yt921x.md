# YT921x Enhanced DSA Driver README

This document is the feature-focused README for the YT921x switch driver used in this tree.

Scope:
- Driver: `target/linux/generic/files/drivers/net/dsa/yt921x*.{c,h}`
- Tagger: `target/linux/generic/files/net/dsa/tag_yt921x.c`
- Validation baseline: Xiaomi CR881x runtime in this workspace

Key register-map references:
- Canonical map: `docs/yt921x/yt9215-register-map.md`
- Register-map changelog: `docs/yt921x/yt9215-register-map-changelog.md`
- Discovered inventory (includes unresolved addresses): `docs/yt921x/yt9215-discovered-register-inventory-2026-03-16.md`

Important:
- The first major section below is organized in the same feature-family style as the YT9215S datasheet.
- The rest of this README maps those families to implemented driver surfaces.

## YT9215S Key Features (Datasheet Form)

Source: `Collection-Data/motorcom~yt9215s.pdf`
Status semantics used below:
- `DONE`: Implemented and validated on live CR881x dataplane/control-plane tests.
- `CODE READY`: Implemented in driver with user/kernel control surface, but this board cannot fully validate end-to-end behavior.
- `TODO`: Not implemented yet, or implemented partially without sufficient validation.

Board-specific validation note:
- CR881x does not expose per-port LED wiring from YT9215S, so LED-controller code can be `CODE READY` without board-level visual validation.

### High performance nonblocking switch
- 7-port switch architecture - DONE
- Integrated 5x 10/100/1000 PHY - DONE
- IEEE 802.3x flow control/backpressure support - DONE

### Interface
- Embedded 5-port copper PHY block - DONE
- Embedded SerDes interface (2.5G/1G class modes) - DONE
- Embedded extension MAC interface (RGMII/MII/RMII class) - DONE

### Advanced features
- LED output modes - CODE READY
- MDIO/I2C slave and master control interfaces - TODO
- Interrupt signaling toward external CPU - TODO
- EEPROM-assisted configuration - TODO
- Jumbo frame support (up to 9K class) - DONE

### IEEE 802.3ad link aggregation
- Two LAG groups supported by silicon feature set - DONE

### Security filtering
- Per-port learning control - DONE
- Per-port aging control - DONE
- Unknown DA filtering mask controls - DONE
- Port isolation - DONE
- Broadcast/multicast/unknown-DA storm-control feature family - TODO

### Control, management and statistics
- RFC-family MIB coverage (including bridge/RMON classes) - DONE
- Temperature sensor telemetry (DT-gated per board) - TODO
- OAM and EEE LLDP feature family - CODE READY
- Loop detection feature family - DONE
- Loop prevention/enforcement feature family - TODO

### Packet process engine
- 802.1Q VLAN with 4K table class - DONE
- Untag/tag decision controls - DONE
- VLAN forwarding/policy controls - DONE
- Port/tag/protocol-based VLAN modes - TODO
- Per-port egress VLAN tag/untag controls - DONE
- STP family (802.1D/s/w) - DONE
- MVR feature - DONE
- IVL/SVL/IVL+SVL modes - DONE
- 802.1ad stacking VLAN - DONE
- VLAN translation families (1:1 / 2:1 / 2:2 / N:1 / 1:N) - DONE
- 802.1X access-control (port-based) - DONE
- 802.1X access-control (MAC-based) - DONE
- 802.1X access-control (guest VLAN) - DONE
- ACL rule engine with multi-slice extension model - DONE
- ACL advanced datasheet parity beyond current mapped shapes - TODO
- IGMP/MLD snooping core - DONE
- IGMP/MLD fast-leave - DONE
- IGMP/MLD router-port policy parity (unknown multicast data path) - DONE
- IGMP/MLD router-port policy parity (IGMP/MLD control frames) - TODO
- Mirror feature families (port-based and flow-based) - DONE
- Reserved multicast control family - DONE
- WoL feature family - CODE READY

### Quality of service (QoS)
- Queue scheduling families (SP/DWRR classes) - DONE
- Queue/port shaping families - TODO (token-level conversion and telemetry alignment landed; remaining dataplane parity still pending)
- trTCM class policing families - TODO
- Multi-source traffic classification - DONE
- 8 unicast queues + 4 multicast queues per port class - DONE
- Tail-drop and WRED feature families - TODO

### Microprocessor
- Integrated RISC-V microprocessor in silicon feature set - DONE

## Hardware Offload Highlights

- VLAN offload:
  - bridge VLAN filtering
  - per-port VLAN add/del (PVID/untagged semantics via DSA VLAN path)
  - hardware FDB/MDB forwarding domains tied to VLAN context
- L2 forwarding offload:
  - hardware FDB learning/aging path and fast-age operations
  - MDB hardware multicast replication path
- ACL offload:
  - ingress/egress `tc flower` offload with hardware actions
  - UDF-backed TTL/hop-limit matching path
- QoS bandwidth offload:
  - queue bandwidth shaping via `tc tbf` offload path
  - shaper telemetry/conversion path aligned to token-level + slot-time model
  - ingress bandwidth policing via DSA policer path
  - hardware queue scheduling policies via `mqprio`/`ets`
- Bridge dataplane controls offload:
  - STP/MST state programming
  - bridge flag programming where hardware semantics are supported

## Capability Inventory (Broader Than 13 Targets)

### Switch Bring-Up and Buses
- MDIO-managed switch probing and reset/setup pipeline
- Optional internal MDIO child bus init (`mdio`)
- Optional external MDIO child bus init (`mdio-external`)

### Port and Link Management
- Port setup/enable/disable
- Conduit retarget (`port_change_conduit`)
- MTU control (`port_change_mtu`, `port_max_mtu`)
- Phylink MAC ops (`mac_config`, `mac_link_up`, `mac_link_down`)
- Interface capability advertisement (`phylink_get_caps`)

### L2 Bridge and Forwarding
- Bridge join/leave
- Bridge flags (`learning`, mcast/bcast flood, fast-leave, hairpin, isolation) with strict pre-checks
- STP/MST (`port_stp_state_set`, `port_mst_state_set`, `vlan_msti_set`)
- VLAN filtering/add/del
- FDB add/del/dump, fast age, ageing time
- MDB add/del (hardware multicast group programming)
- LAG join/leave (bounded hardware support)
- Port mirror add/del

### Statistics and Telemetry
- ethtool stats strings/count/data
- MAC/CTRL/RMON/pause statistics surfaces
- 64-bit per-port stats path

### QoS, Trust, and DCB Surfaces
- DCB default priority get/set
- Application trust profile get/set
- DSCP priority mapping add/del/get
- TC offload families:
  - `flower` (ACL)
  - `mqprio`
  - `ets`
  - `tbf`
  - ingress policer add/del path

### ACL Engine Offload (`tc flower`)
- Ingress and egress ACL offload (egress uses supported subset)
- Actions implemented:
  - drop
  - redirect
  - mirror
  - trap
  - skbedit priority
  - pedit DSCP rewrite (`ip dsfield`, `ip6 traffic_class`)
  - constrained csum-chain acceptance for IPv4 header update form
- Parser behavior is intentionally strict:
  - unsupported shapes are rejected (`-EOPNOTSUPP`/`-EINVAL`)
  - resource exhaustion returns clean `-ENOSPC` (no silent wrap/corruption)

### ACL UDF Surfaces
- UDF-backed `ip_ttl` / IPv6 hop-limit match path
- UDF selector remap/refcount lifecycle handling
- Strict guardrails for unsupported combined shapes

### Tagger and CPU Conduit Behavior
- Custom DSA tag protocol (`DSA_TAG_PROTO_YT921X`)
- Tagged primary CPU conduit path
- Secondary conduit raw-frame fallback handling
- RX source-port decode and forwarded-mark integration

### Advanced/Engineering Surfaces (Debug Build)
- Full low-level register and table command surface:
  - `reg/int/ext` read-write
  - `tbl info/read/write`
  - `field get/set`
  - raw range dump
- Additional engineering controls exposed via debug commands:
  - unknown flood/action controls
  - ctrlpkt controls (ARP/ND/LLDP/EEE-LLDP)
  - 802.1X VLAN bypass controls
  - RMA policy controls
  - loop-detect register controls
  - WoL register controls
  - storm guard controls
  - ACL chain helper controls

## User-Facing APIs

Primary:
- `bridge` (VLAN/FDB/MDB/bridge flags)
- `tc` (`flower`, `mqprio`, `ets`, `tbf`, policer-related setup paths)
- Standard DSA netdevices/ports
- `devlink` runtime params for VLAN ingress parser mode bits (`vlan_mode_port`, `vlan_mode_ctag`, `vlan_mode_stag`, `vlan_mode_proto`)

Runtime VLAN parser mode (`devlink`) example:
```bash
# discover the devlink handle first
devlink dev show

# show current values
devlink dev param show <handle> name vlan_mode_port
devlink dev param show <handle> name vlan_mode_ctag
devlink dev param show <handle> name vlan_mode_stag
devlink dev param show <handle> name vlan_mode_proto

# set runtime value (true/false)
devlink dev param set <handle> name vlan_mode_ctag value true cmode runtime
devlink dev param set <handle> name vlan_mode_proto value false cmode runtime
```

Behavioral rule:
- Unsupported hardware semantics are intentionally rejected with explicit fast-fail (`-EOPNOTSUPP`, `-EINVAL`, `-ENOSPC`) to avoid silent partial offload.
- Temperature sensor enable is DT-gated (`motorcomm,temp-sensor-supported` / `temp-sensor-supported`); boards without this property keep the sensor path disabled.

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

- Live reverse-engineering notes: `docs/yt921x/live/`
