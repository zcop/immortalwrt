# YT9215 VLAN control decode summary (`vlan_filtering` + `wan` VID10) (2026-03-24)

## Source
- Raw log: `docs/yt921x/live/yt_vlan_ctrl_decode_probe_wan_vid10_20260324_092626.txt`

## Goal
- Identify concrete register coupling for VLAN control path using reversible Linux bridge operations.

## Method
- Start from `vlan_filtering=0` baseline.
- Capture snapshots for:
  - `baseline`
  - `vf1` (`ip link set br-lan type bridge vlan_filtering 1`)
  - `add_vid10` (`bridge vlan add dev br-lan vid 10 self`; `bridge vlan add dev wan vid 10`)
  - `del_vid10`
  - `restore` (`vlan_filtering` back to original)
- Per snapshot read:
  - `0x180280` (`VLAN_IGR_FILTER`)
  - VLAN table words: `0x188000/04` (VID0), `0x188008/0c` (VID1), `0x188010/14` (VID2), `0x188050/54` (VID10)
  - per-port VLAN controls: `0x230010+4*p`, `0x230080+4*p` (`p=0..10`)

## Observed
- `vlan_filtering` coupling:
  - `0x180280`: `0x00000000` (vf=0) -> `0x0000000f` (vf=1) -> `0x00000000` (restore)
- Per-port VLAN control coupling when `vf=1`:
  - `0x230010..0x23001c` changed from `0xc007ffc0` -> `0xc0040040`
  - `0x230020..0x230038` stayed `0xc007ffc0`
  - `0x230080..0x2300a8` stayed `0x00000000`
- VID10 membership coupling:
  - `0x188050`: `0x00000000` -> `0x00000c00` when VID10 added to `wan` + `br-lan self` -> `0x00000000` after delete
  - `0x188054` stayed `0x00000000`
- VID0/VID1/VID2 sampled words were unchanged in this probe.

## Inference
- `0x180280` is confirmed as a deterministic runtime-coupled VLAN ingress filter control.
- `0x188050` is a confirmed active VLAN table word for VID10 membership in this topology.
- `0x230010..` block is active per-port VLAN control, with a subset of ports switching mode under `vlan_filtering=1`.
