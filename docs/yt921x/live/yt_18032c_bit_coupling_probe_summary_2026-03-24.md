# YT9215 `0x18032c` bit-coupling probe summary (2026-03-24)

## Source
- Raw log: `docs/yt921x/live/yt_18032c_bit_coupling_probe_20260324_091713.txt`

## Goal
- Check whether writable unknown word `0x18032c` couples into known active policy/state registers.

## Method
- Baseline read of:
  - `0x18032c`
  - known active words: `0x180294`, `0x180298`, `0x18029c`, `0x1802a0`, `0x1802a4`, `0x1802b4`, `0x1802b8`, `0x18038c`, `0x1803d0`, `0x1803d4`, `0x1803d8`
- Bit sweep:
  - write `0x18032c = 1 << b` for `b=0..10`
  - read back `0x18032c`
  - re-read all known active words each step
- Restore original `0x18032c` value.

## Observed
- `0x18032c` accepted all one-hot writes and read back as written.
- No observed deltas in sampled active words across all bit cases:
  - `0x180294=0x000007e1`
  - `0x180298=0x000007e2`
  - `0x18029c=0x000007e4`
  - `0x1802a0=0x000007e8`
  - `0x1802a4=0x000006e0`
  - `0x1802b4=0x000007ff`
  - `0x1802b8=0x000006ef`
  - `0x18038c=0x003cfc30`
  - `0x1803d0=0x00000000`
  - `0x1803d4=0x00000000`
  - `0x1803d8=0x00020000`

## Inference
- In this runtime/topology, `0x18032c` behaves as writable but not immediately coupled to sampled forwarding/learning/isolation control words.
