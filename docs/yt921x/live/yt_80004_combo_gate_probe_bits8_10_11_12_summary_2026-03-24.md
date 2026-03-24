# YT9215 `0x080004` combo gate sweep summary (bits 8/10/11/12) (2026-03-24)

## Source
- Raw log: `docs/yt921x/live/yt_80004_combo_gate_probe_bits8_10_11_12_20260324_092541.txt`

## Goal
- Check whether safe combinations of latched `0x080004` bits (`8`, `10`, `11`, `12`) unlock gated windows.

## Method
- Baseline from current runtime (`orig_080004=0x0000080b`).
- Sweep all 16 combinations of bits `{8,10,11,12}` while preserving other bits.
- For each combo, read:
  - `0x080004`, `0x080014`
  - gated sample words: `0x1802c0`, `0x1802c4`, `0x1802f0`, `0x180304`, `0x180338`, `0x18033c`, `0x180368`, `0x180388`
- Restore original `0x080004`.

## Observed
- `0x080004` accepted all 16 combo writes/readbacks.
- `0x080014` remained `0x00000000` for all combos.
- All sampled gated words remained `0xdeadbeef` for all combos.
- No management-plane drop occurred in this safe-bit combo run.

## Inference
- In this runtime, combinations of bits `{8,10,11,12}` in `0x080004` are not sufficient to unlock gated windows.
- Remaining high-risk candidate is still `0x080004` bit `5` (previously observed hazardous), which should be tested only with reboot/UART guard.
