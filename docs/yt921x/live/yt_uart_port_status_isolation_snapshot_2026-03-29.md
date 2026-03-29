# YT921x UART Port/Isolation Snapshot (2026-03-29)

## Context
- Access path: UART (`/dev/ttyUSB0`) after SSH-loss-safe recovery.
- Register interface on this image: `yt921x` debugfs command file.
  - `echo "<cmd>" > /sys/kernel/debug/yt921x_cmd`
  - `cat /sys/kernel/debug/yt921x_cmd`
- Tooling note: `reg`/`ssdk_sh`/`devmem` were not available in router userspace.

## Runtime netdev state (`ip -br link`)
- `lan1@eth1`: `UP`
- `lan2@eth1`: `UP`
- `lan3@eth1`: `LOWERLAYERDOWN`
- `wan@eth0`: `LOWERLAYERDOWN`

## `port_status` (queried per-port to avoid reply truncation)
- `p0`: `ctrl=0x000007fa`, `status=0x000001fa`
- `p1`: `ctrl=0x0000079a`, `status=0x0000019a`
- `p2`: `ctrl=0x000007fa`, `status=0x000000e2`
- `p3`: `ctrl=0x000007fa`, `status=0x000000e2`
- `p4`: `ctrl=0x000005fa`, `status=0x000001fa`
- `p5`: `ctrl=0x000007fa`, `status=0x000000e2`
- `p6`: `ctrl=0x000007fa`, `status=0x000000e2`
- `p7`: `ctrl=0x000007fa`, `status=0x000000e2`
- `p8`: `ctrl=0x000005fa`, `status=0x000001fa`
- `p9`: `ctrl=0x000007fa`, `status=0x000000e2`
- `p10`: `ctrl=0x000007fa`, `status=0x000001fa`

## `PORTn_ISOLATION` rows (`0x180294 + 4*n`)
- `0x180294 (p0) = 0x000006f9`
- `0x180298 (p1) = 0x000006fa`
- `0x18029c (p2) = 0x000006fc`
- `0x1802a0 (p3) = 0x000007ef`
- `0x1802a4 (p4) = 0x000007e7`
- `0x1802a8 (p5) = 0x000006ef`
- `0x1802ac (p6) = 0x000006ef`
- `0x1802b0 (p7) = 0x000006ef`
- `0x1802b4 (p8) = 0x000007f8`
- `0x1802b8 (p9) = 0x000006ef`
- `0x1802bc (p10) = 0x000006ef`

## Notes
- This snapshot is consistent with the established CR881x identity map:
  - `p2=lan3`, `p4=cpu2 (eth0)`, `p8=cpu1 (eth1)`, `p10=mcu`.
- `0x180294/0x180298/0x18029c` remained at the expected directional LAN baseline.
