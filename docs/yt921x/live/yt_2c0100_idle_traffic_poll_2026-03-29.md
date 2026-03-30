# YT9215 `0x2c0100..0x2c013c` idle/traffic poll (2026-03-29)

## Goal
Test whether `0x2c0100..0x2c013c` behaves like an active indirect-command window
(`busy/op/index` style) under normal runtime and traffic pressure.

## Method
- Read-only only (`reg read` through `yt921x_cmd`).
- Idle dump of all 16 words (`0x2c0100..0x2c013c`).
- Polling:
  - `0x2c0100` x200 at idle and under WAN ping load.
  - all 16 words x50 snapshots at idle and under WAN ping load.

## Idle dump
- `0x2c0100 = 0xf2f6685b`
- `0x2c0104 = 0x62f0d95f`
- `0x2c0108 = 0xe2f001b9`
- `0x2c010c = 0x49856f9f`
- `0x2c0110 = 0x73ce7c04`
- `0x2c0114 = 0x5c57f505`
- `0x2c0118 = 0x00007d1c`
- `0x2c011c = 0x150a0c00`
- `0x2c0120 = 0x735f6a05`
- `0x2c0124 = 0x47ae1667`
- `0x2c0128 = 0x0e536e22`
- `0x2c012c = 0x2da5e0ee`
- `0x2c0130 = 0x0ad39e84`
- `0x2c0134 = 0x52726bb5`
- `0x2c0138 = 0x4e80d6bd`
- `0x2c013c = 0xc4e03d8f`

## Polling result
- `0x2c0100` unique values:
  - idle: `1` (`0xf2f6685b`)
  - traffic: `1` (`0xf2f6685b`)
- Full 16-word set:
  - every address had `unique=1` at idle and under traffic (50 snapshots each).
  - idle and traffic values were identical for every address.

## Interpretation
- In this runtime, the block is fully static under load and does not expose a
  visible busy/opcode/index state transition.
- This strongly suggests either:
  - this is not the active runtime command/data aperture for the table backend,
    or
  - it requires an external selector/trigger path not exercised by direct reads.
