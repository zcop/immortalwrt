# YT9215 VLAN membership probe (`wan` add/del vid10) summary (2026-03-24)

## Source
- Raw log: `docs/yt921x/live/yt_vlan_membership_probe_wan_vid10_20260324_161032.txt`

## Goal
- Detect register deltas when changing software VLAN membership (`bridge vlan add/del dev wan vid 10`).

## Method
- Baseline/after-add/after-del/final snapshots of:
  - bridge VLAN table (`bridge vlan show`)
  - key registers:
    - `0x180280`, `0x18028c`
    - `0x180294`, `0x180298`, `0x18029c`, `0x1802a0`, `0x1802a4`, `0x1802b4`, `0x1802b8`
    - `0x18030c`, `0x180310`, `0x180314`, `0x180318`, `0x18031c`
    - `0x18038c`, `0x180390`, `0x1803d0`, `0x1803d4`, `0x1803d8`
    - `0x08000c`, `0x080014`
- Cleanup ensured vid10 removed at end.

## Observed
- Software bridge table changed as expected:
  - `wan` temporarily showed VLAN 10 entry, then returned to VLAN 1 only.
- No observed changes in sampled hardware registers across phases.

## Inference
- In this runtime, adding/removing VLAN 10 on `wan` alone did not produce observable hardware-reg deltas in sampled key words.
- Most likely reason: `br-lan` currently has `vlan_filtering=0`, so VLAN membership edits are not driving the full hardware VLAN programming path.

## Next high-signal step
- Repeat with controlled `vlan_filtering=1` window (with strict pre/post snapshot and restore) to force hardware VLAN path activity and re-check deltas.
