# YT9215 VTU stride + PVID hunt summary (fixed range run) (2026-03-24)

## Source
- Raw log: `docs/yt921x/live/yt_vlan_vtu_stride_pvid_probe_fix_20260324_093111.txt`

## Goal
- Decode VTU entry behavior for tagged vs untagged membership.
- Verify VID table stride (`VID10` vs `VID11`).
- Locate per-port PVID control coupling.

## Method
- Force `br-lan vlan_filtering=1` during probe window.
- Capture snapshots:
  - `baseline_vf1`
  - `vid10_tagged_wan_lan1`
  - `vid10_lan1_untagged`
  - `vid11_added`
  - `wan_pvid20_untagged`
  - `restore`
- Per snapshot, read:
  - VTU anchors: `0x188050/0x188054/0x188058/0x18805c`
  - VLAN ingress filter: `0x180280`
  - per-port VLAN control: `0x230010..0x230038`, `0x230080..0x2300a8`
  - candidate neighborhood: `0x180200..0x1802fc`

## Deterministic deltas
- `baseline_vf1 -> vid10_tagged_wan_lan1`
  - `0x188050: 0x00000000 -> 0x00000c80`
- `vid10_tagged_wan_lan1 -> vid10_lan1_untagged`
  - `0x188054: 0x00000000 -> 0x00000100`
- `vid10_lan1_untagged -> vid11_added`
  - `0x188058: 0x00000000 -> 0x00000c00`
- `vid11_added -> wan_pvid20_untagged`
  - `0x23001c: 0xc0040040 -> 0xc0040500`

## Inference
- VTU entry stride is confirmed as 8 bytes (`VID10` at `0x188050`, `VID11` at `0x188058`).
- `0x188050` acts as active VID10 table word for membership in this topology.
- `0x188054` changes when `lan1` flips tagged -> untagged for VID10, consistent with an egress-untag control field in the second VTU word.
- `0x23001c` is the strongest per-port PVID control candidate observed in this run.
