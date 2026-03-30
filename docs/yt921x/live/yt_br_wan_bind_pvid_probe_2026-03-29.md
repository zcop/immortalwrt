# YT9215 runtime `br-wan` bind probe (`wan` enslaved to VLAN-aware bridge) (2026-03-29)

## Goal
Force WAN user port into VLAN-aware bridge context and observe whether CPU conduit
ports (`p4` / `p8`) are reprogrammed in:
- `0x230010 + 4*p` (`PORTn_VLAN_CTRL`, PVID in bits `[17:6]`)
- `0x230080 + 4*p` (`PORTn_VLAN_CTRL1`)

## Runtime sequence (non-persistent)
1. Baseline dump with `br-lan vlan_filtering=0`
2. `ip link add br-wan type bridge vlan_filtering 1`
3. `ip link set br-wan up`
4. `ip link set wan master br-wan`
5. Dump again
6. Cleanup: `wan nomaster`, remove `br-wan`, final dump

## Result summary

### Baseline
- `p0..p10`: `CTRL=0xc007ffc0`, `CTRL1=0x00000000`, `PVID=4095`

### After `wan -> br-wan` bind
- `p3` only: `CTRL=0xc0040040`, `PVID=1`
- `p0..p2`, `p4..p10`: unchanged (`PVID=4095`)
- `CTRL1` remained `0x00000000` for all ports

### After cleanup
- All ports restored to baseline (`PVID=4095`, `CTRL1=0`)

### Conduit/isolation sanity (same pre/bind/post)
- `0x08000c = 0x0000c008`
- `0x1802a0 = 0x000007ef`
- `0x1802a4 = 0x000007e7`
- `0x1802b4 = 0x000007f8`
- No delta observed during `wan -> br-wan` bind in these words.

## Interpretation
- WAN user port (`p3`) enters VLAN-aware domain as expected.
- No visible change in this register family for CPU conduit ports (`p4`, `p8`)
  during this transition.
- Conduit selector/isolation words above also stayed unchanged, so this runtime
  bind event appears confined to per-port VLAN control for `p3`.
- In this runtime, CPU trunking behavior is not represented by a direct change in
  `PORTn_VLAN_CTRL/CTRL1` for `p4`/`p8` under this specific bind event.
