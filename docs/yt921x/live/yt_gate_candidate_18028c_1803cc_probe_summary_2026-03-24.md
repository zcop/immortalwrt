# YT9215 gate-candidate probe summary (`0x18028c`, `0x1803cc`) (2026-03-24)

## Source
- Raw log: `docs/yt921x/live/yt_gate_candidate_18028c_1803cc_probe_20260324_092049.txt`

## Goal
- Check whether toggling `0x18028c` or `0x1803cc` unlocks known gated windows returning `0xdeadbeef`.

## Method
- For each target (`0x18028c`, `0x1803cc`):
  - read baseline + capture gated words
  - write `0x00000000`, `0x000007ff`, then one-hot `bit0..bit10`
  - capture gated words after each step
  - restore original value
- Gated sample set:
  - `0x1802c0`, `0x1802c4`, `0x1802f0`, `0x180304`
  - `0x180338`, `0x18033c`, `0x180368`, `0x180388`

## Observed
- Both targets are writable and read back expected values over the full sweep.
- All sampled gated words remained `0xdeadbeef` for all toggle cases.
- No gated-window unlock signature was observed.

## Inference
- In this runtime, neither `0x18028c` nor `0x1803cc` appears to be the direct unlock gate for `0x1802c0..0x180308` or `0x180338..0x180388`.
