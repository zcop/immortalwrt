# YT9215 VLAN filtering probe (`br-lan vlan_filtering=1` + `wan` vid10) summary (2026-03-24)

## Source
- Raw log: `docs/yt921x/live/yt_vlan_filtering_probe_wan_vid10_20260324_161244.txt`

## Goal
- Force VLAN-aware bridge path and detect hardware-register deltas under VLAN membership changes.

## Method
- Baseline snapshot with `orig_vlan_filtering=0`.
- Temporary enable: `ip link set br-lan type bridge vlan_filtering 1`.
- Apply membership change:
  - `bridge vlan add dev br-lan vid 10 self`
  - `bridge vlan add dev wan vid 10`
- Remove membership and restore original filtering state.
- Snapshot phases:
  - `baseline`
  - `after_filtering_on`
  - `after_add_vid10`
  - `after_del_vid10`
  - `final_restored`
- Sampled key regs same as prior probe set.

## Observed
- Software bridge state changed as expected:
  - `vlan_filtering` toggled `0 -> 1 -> 0`
  - VLAN 10 appeared on `wan` and `br-lan` during add phase.
- Hardware register delta observed:
  - `0x180280` changed with filtering state:
    - baseline/final: `0x00000000`
    - with `vlan_filtering=1`: `0x0000000f`
- No other sampled regs changed in this test:
  - `0x18028c`, `0x180294/98/9c`, `0x1802a0/a4/b4/b8`,
    `0x18030c/10/14/18/1c`, `0x18038c`, `0x180390`,
    `0x1803d0/d4/d8`, `0x08000c`, `0x080014`.

## Inference
- `0x180280` is now strongly coupled to bridge VLAN filtering mode in this runtime.
- Membership add/del of vid10 alone still did not move sampled policy/control words beyond that filtering toggle.
