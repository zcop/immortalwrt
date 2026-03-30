# YT9215 stock RMA control sweep (`0x1805d0`) — safe latching map

Date: 2026-03-30

## Goal
- Move to next vendor feature (stock RMA/control packet path).
- Establish writable field map and safety envelope for `0x1805d0` before classifier-specific traffic tests.

## Baseline
- `reg 0x1805d0 = 0x0000003f`

## Method
- For bits `0..12`:
  - pulse one bit (set/clear from baseline)
  - verify readback latches
  - run management ping (`ping -c 5 192.168.2.1`) during pulse
  - restore baseline after each pulse
- Final restore check at end.

## Results

All tested bits latched exactly and restored cleanly.
No management disruption observed (`ping_loss=0%` in all cases).

- bit0: `0x0000003f -> 0x0000003e` (clear), latched, stable
- bit1: `0x0000003f -> 0x0000003d` (clear), latched, stable
- bit2: `0x0000003f -> 0x0000003b` (clear), latched, stable
- bit3: `0x0000003f -> 0x00000037` (clear), latched, stable
- bit4: `0x0000003f -> 0x0000002f` (clear), latched, stable
- bit5: `0x0000003f -> 0x0000001f` (clear), latched, stable
- bit6: `0x0000003f -> 0x0000007f` (set),  latched, stable
- bit7: `0x0000003f -> 0x000000bf` (set),  latched, stable
- bit8: `0x0000003f -> 0x0000013f` (set),  latched, stable
- bit9: `0x0000003f -> 0x0000023f` (set),  latched, stable
- bit10:`0x0000003f -> 0x0000043f` (set),  latched, stable
- bit11:`0x0000003f -> 0x0000083f` (set),  latched, stable
- bit12:`0x0000003f -> 0x0000103f` (set),  latched, stable

Final restore:
- `reg 0x1805d0 = 0x0000003f`

## Interpretation
- `0x1805d0` is active/writable in current runtime and safe for controlled short pulses on bits `0..12`.
- This run maps latching/safety only; it does not assign per-bit semantic actions yet.
- Next step for semantic decode requires classifier-aligned frames (RMA/BPDU/LLDP style) with simultaneous CPU-port capture and policy A/B.
