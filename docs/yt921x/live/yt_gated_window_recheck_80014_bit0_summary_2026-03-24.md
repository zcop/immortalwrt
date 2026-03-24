# YT9215 gated-window recheck with safe `0x080014` bit0 toggle (2026-03-24)

## Source
- Raw log: `docs/yt921x/live/yt_gated_window_recheck_80014_bit0_20260324_154715.txt`

## Goal
- Re-validate whether gated windows remain locked (`0xdeadbeef`) under a known-safe global gate candidate change (`0x080014` bit0).

## Method
- Baseline reads:
  - core regs: `0x080014`, `0x080004`, `0x180294`, `0x180298`, `0x18029c`, `0x18038c`
  - gated dump A: `0x1802c0..0x180308` stride 4
  - gated dump B: `0x180338..0x180388` stride 4
- Toggle:
  - `0x080014: 0x00000000 -> 0x00000001 -> 0x00000000`
  - re-read core regs and both gated dumps after set/restore.

## Observed
- `0x080014` toggled and restored as expected.
- Core policy words stayed unchanged during toggle:
  - `0x180294=0x000007e1`
  - `0x180298=0x000007e2`
  - `0x18029c=0x000007e4`
  - `0x18038c=0x003cfc30`
- All words in both gated windows remained `0xdeadbeef` before/after toggle:
  - `0x1802c0..0x180308`
  - `0x180338..0x180388`

## Inference
- Safe `0x080014` low-bit toggle still does not open the gated windows in current runtime.
- Confirms previous understanding: these windows remain gated and require a different control path (if any exists on this board/runtime).
