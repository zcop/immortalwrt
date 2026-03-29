# CR881x YT9215 Port Identity Map (2026-03-29)

## Goal
Lock final identity for `lan3`, `cpu1`, `cpu2`, and `mcu` so isolation/FDB work
targets correct rows.

## Static evidence (DTS + driver)
- DTS (`target/linux/qualcommax/dts/ipq5018-cr881x.dts`):
  - `port@2` label `lan3`
  - `port@4` CPU port (`ethernet = <&dp1>`) -> `eth0`
  - `port@8` CPU port (`ethernet = <&dp2>`) -> `eth1`
- Driver (`.../830-02-v6.19-net-dsa-yt921x-Add-support-for-Motorcomm-YT921x.patch`):
  - prefers `BIT(8)` as primary CPU port when present
  - keeps `BIT(10)` as internal MCU-only blocked mask in unknown flood filters
  - global model: `8 internal + 2 external + 1 mcu` (`PORT_NUM=11`)

## Runtime evidence
- `ip -d link show type dsa`:
  - `lan1@eth1` -> `portname p0`
  - `lan2@eth1` -> `portname p1`
  - `lan3@eth1` -> `portname p2`
  - `wan@eth0`  -> `portname p3`
- `yt921x_cmd port_status` + `reg read` on `PORTn_ISOLATION` rows:
  - rows for `p4` and `p8` match dual-conduit behavior previously validated.

## Final mapping
- `p2` = `lan3`
- `p8` = `cpu1` (primary CPU conduit, `eth1`)
- `p4` = `cpu2` (secondary CPU conduit, `eth0`)
- `p10` = internal `mcu` port

## Matrix row addresses used by this mapping
- `lan3 (p2)` -> `0x18029c`
- `cpu2 (p4)` -> `0x1802a4`
- `cpu1 (p8)` -> `0x1802b4`
- `mcu  (p10)` -> `0x1802bc`
