# YT9215 `vlan_filtering` toggle probe on `0x2c01xx`, `0x31c0xx`, `0x1880xx` (2026-03-29)

## Goal
Check whether the suspected indirect windows (`0x2c0100..0x2c013c`, `0x31c000..0x31c03c`)
or VLAN VTU window (`0x188000..0x18803c`) move when Linux bridge `vlan_filtering`
is toggled in-place (`0 -> 1 -> 0`) without persistent UCI VLAN edits.

## Method
- Runtime: CR881x, router reachable at `192.168.2.1`.
- Sequence:
  - snapshot `pre` with `br-lan vlan_filtering=0`
  - `ip link set br-lan type bridge vlan_filtering 1`
  - snapshot `on`
  - `ip link set br-lan type bridge vlan_filtering 0`
  - snapshot `post`
- Each snapshot read:
  - `0x2c0100..0x2c013c` (16 words)
  - `0x31c000..0x31c03c` (16 words)
  - `0x188000..0x18803c` (16 words)

## Observed bridge state
- `pre`: `vlan_filtering 0`
- `on`: `vlan_filtering 1`
- `post`: `vlan_filtering 0`

## Diff result
- `pre` vs `on`: no changed lines.
- `on` vs `post`: no changed lines.
- `pre` vs `post`: no changed lines.

## Direct control sanity reads (same runtime)
- `0x180280` (`YT921X_VLAN_IGR_FILTER`):
  - `pre`: `0x00000000`
  - `on`: `0x00000007`
  - `post`: `0x00000000`
- `0x180598` (`YT921X_VLAN_EGR_FILTER`): stayed `0x00000000`.
- `0x188000/0x188004` (`VID0 VTU words`): stayed `0x0043ff80 / 0x00000000`.

## Conclusion
- In this runtime, `vlan_filtering` toggles the known ingress filter control
  (`0x180280`) but does not move sampled words in `0x2c01xx`, `0x31c0xx`,
  or `0x1880xx` by direct readback.
- This further supports treating `0x2c01xx` as opaque via current direct-read path,
  and indicates `0x31c0xx` is not coupled to this bridge state transition.
