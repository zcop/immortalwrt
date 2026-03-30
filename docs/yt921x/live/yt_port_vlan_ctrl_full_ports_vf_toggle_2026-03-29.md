# YT9215 full-port `PORTn_VLAN_CTRL/CTRL1` probe with `vlan_filtering` toggle (2026-03-29)

## Goal
Map all 11 ports (`0..10`) for:
- `YT921X_PORTn_VLAN_CTRL` (`0x230010 + 4*p`)
- `YT921X_PORTn_VLAN_CTRL1` (`0x230080 + 4*p`)
and decode `PVID = (CTRL >> 6) & 0xFFF` under `br-lan vlan_filtering 0 -> 1 -> 0`.

## Observed states

### `state_pre`: `vlan_filtering 0`
- `p0..p10`: `CTRL=0xc007ffc0`, `CTRL1=0x00000000`, `PVID=4095`

### `state_on`: `vlan_filtering 1`
- `p0..p2`: `CTRL=0xc0040040`, `CTRL1=0x00000000`, `PVID=1`
- `p3..p10`: `CTRL=0xc007ffc0`, `CTRL1=0x00000000`, `PVID=4095`

### `state_post`: `vlan_filtering 0`
- `p0..p10`: `CTRL=0xc007ffc0`, `CTRL1=0x00000000`, `PVID=4095`

## Interpretation
- `PVID=4095` (`YT921X_VID_UNWARE`) is used when bridge VLAN filtering is disabled.
- Enabling `vlan_filtering` only updates the bridge-member user ports in this profile
  (`lan1..lan3` => `p0..p2`) to `PVID=1`.
- Non-member/internal ports (`p3..p10`) remain VLAN-unaware in this transition.
- `PORTn_VLAN_CTRL1` stayed `0` for all ports in this probe.
