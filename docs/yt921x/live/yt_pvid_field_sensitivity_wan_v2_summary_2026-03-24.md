# YT9215 WAN PVID field sensitivity summary (2026-03-24)

## Source
- Raw log: `docs/yt921x/live/yt_pvid_field_sensitivity_wan_v2_20260324_093322.txt`

## Goal
- Determine whether `0x23001c` encodes WAN PVID numerically and identify bit shift.

## Method
- Enable `br-lan vlan_filtering=1`.
- For WAN, apply `pvid untagged` with VID values: `20`, `21`, `30`, `100`.
- Read `0x23001c` after each change.
- Clear probe VID and return WAN to VID1 baseline between trials.

## Observed
- `PVID=20` -> `0x23001c = 0xc0040500`
- `PVID=21` -> `0x23001c = 0xc0040540`
- `PVID=30` -> `0x23001c = 0xc0040780`
- `PVID=100` -> `0x23001c = 0xc0041900`

## Decode
- Delta from base follows `VID << 6` exactly:
  - `20 << 6 = 0x0500`
  - `21 << 6 = 0x0540`
  - `30 << 6 = 0x0780`
  - `100 << 6 = 0x1900`

## Inference
- On this board/runtime, `0x23001c` contains a WAN PVID field mapped as `bits[17:6]` (`PVID << 6`).
- This aligns with `YT921X_PORTn_VLAN_CTRL(port)` being the per-port default VLAN control register class.
