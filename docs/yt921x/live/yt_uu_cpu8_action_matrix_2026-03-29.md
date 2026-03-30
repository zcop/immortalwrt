# YT9215 CPU8 Unknown-Unicast Action Matrix (2026-03-29)

## Goal
Validate whether `ACT_UNK_*` action fields for CPU8 (`eth1` conduit path) alter
LAN unknown-unicast flooding behavior.

## Setup
- Access: SSH (UART remained fallback).
- Neighbor pin for fake unknown destination:
  - `192.168.2.199 lladdr 02:de:ad:be:ef:00 dev br-lan`
- Stimulus:
  - `ping -I br-lan -c 4 -W 1 192.168.2.199`
- Observation:
  - concurrent `tcpdump` on `lan1` and `lan2`
  - counted `ICMP echo request` lines per case.

## Baseline
- `0x180734 = 0x00020000`
- `0x180738 = 0x00420000`

## Sweep matrix
- `0x180734` (`cpu8` field `[17:16]`):
  - `a0` (`0x00000000`) -> `l1=4`, `l2=4`
  - `a1` (`0x00010000`) -> `l1=4`, `l2=4`
  - `a2` (`0x00020000`) -> `l1=4`, `l2=4`
  - `a3` (`0x00030000`) -> `l1=4`, `l2=4`
- `0x180738` (`cpu8` field `[17:16]`, preserving `0x00400000` high bit):
  - `a0` (`0x00400000`) -> `l1=4`, `l2=4`
  - `a1` (`0x00410000`) -> `l1=4`, `l2=4`
  - `a2` (`0x00420000`) -> `l1=4`, `l2=4`
  - `a3` (`0x00430000`) -> `l1=4`, `l2=4`

## Bit22 isolation test (`0x00400000`)
- Goal: test if high bit22 is a master policy enable/selector.
- Method:
  - keep `[17:16]=2` constant
  - compare `0x180738=0x00420000` (bit22 on) vs `0x00020000` (bit22 off)
  - run both unknown-unicast (`192.168.2.199`) and multicast (`239.1.2.3`)
    capture checks on `lan1`/`lan2`.
- Result:
  - UU:
    - bit22 on -> `l1=4`, `l2=4`
    - bit22 off -> `l1=4`, `l2=4`
  - multicast:
    - bit22 on -> `l1=4`, `l2=4`
    - bit22 off -> `l1=4`, `l2=4`
- Conclusion:
  - no observed effect from bit22 toggle in this workload.

## Restore check
- Restored:
  - `0x180734 = 0x00020000`
  - `0x180738 = 0x00420000`

## Conclusion
- In current LAN unknown-unicast workload (CPU source on `br-lan` via CPU8),
  action toggles on `[17:16]` for both `0x180734` and `0x180738` did not change
  observed flooding to `lan1`/`lan2`.
- This aligns with prior WAN-path evidence where only `0x180734[9:8]` showed
  action sensitivity in that specific scenario.
