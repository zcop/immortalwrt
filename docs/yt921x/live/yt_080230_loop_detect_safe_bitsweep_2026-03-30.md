# YT9215 `0x080230` loop-detect control safe bit sweep (2026-03-30)

## Goal
- Validate that decoded loop-detect control bits on `0x080230` are writable and restorable.
- Check immediate datapath impact while pulsing each bit under live management traffic.

## Runtime
- Device: CR881x (OpenWrt/ImmortalWrt 6.12)
- Link state during run:
  - `lan1` up
  - `lan2`, `lan3` down
- STP runtime:
  - `/sys/class/net/br-lan/bridge/stp_state = 1`
- Baseline register:
  - `reg 0x080230 = 0x03266624`

## Field decode from baseline (`0x03266624`)
- `f5` (enable, bit18): `1`
- `f6` (tpid, bits `[17:2]`): `0x9989`
- `f8` (generate-way, bit0): `0`
- unit-id slices:
  - `f4` (`[20:19]`) = `0`
  - `f3` (`[22:21]`) = `1`
  - `f2` (`[24:23]`) = `2`

## Method
- For each target bit, pulse from baseline to trial value for one ping window:
  - `ping -c 10 -i 0.2 -W 1 192.168.2.1`
- Readback during pulse (`reg_now`) and after restore (`restored`).
- Always restore to baseline `0x03266624`.

## Pulsed bits and outcomes
- bit18 (clear): trial `0x03226624`
  - loss `0%`, reg latched, restore ok
- bit0 (set): trial `0x03266625`
  - loss `0%`, reg latched, restore ok
- bit1 (set): trial `0x03266626`
  - loss `0%`, reg latched, restore ok
- bit2 (clear): trial `0x03266620`
  - loss `0%`, reg latched, restore ok
- bit3 (set): trial `0x0326662c`
  - loss `0%`, reg latched, restore ok
- bit4 (set): trial `0x03266634`
  - loss `0%`, reg latched, restore ok
- bit5 (clear): trial `0x03266604`
  - loss `0%`, reg latched, restore ok
- bit6 (set): trial `0x03266664`
  - loss `0%`, reg latched, restore ok
- bit7 (set): trial `0x032666a4`
  - loss `0%`, reg latched, restore ok
- bit8 (set): trial `0x03266724`
  - loss `0%`, reg latched, restore ok

## Result
- In this runtime, bits `0..8` and `18` are writable and recover cleanly.
- No immediate management-path disruption was observed from these pulses.

## Limits of this run
- No external BPDU injector was active.
- `bridge` userspace tool was unavailable on this image (`ash: bridge: not found`), limiting topology-state introspection.
- With only `lan1` link up, this run is a safe-control sanity probe, not a definitive BPDU classification proof.
