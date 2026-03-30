# YT9215 Stock Ingress Meter Decode (`0xc7/0xc8/0xce`) - 2026-03-30

Source:
- `Collection-Data/cr881x/mtd22_rootfs/lib/modules/4.4.60/yt_switch.ko`

## Summary
- `0xc7` (`0x220104`): meter timeslot table
- `0xc8` (`0x220108`): per-port meter control table
- `0xce` (`0x220800`): meter configuration table

## Field tuple format (from `.rodata`)
Stock field tables use 4-byte tuples:
- byte0: field index
- byte1: field width (bits)
- byte2: word index (within table entry)
- byte3: lsb position

## Decoded field tables

### `meter_timeslotm_field` (table `0xc7`)
- `field0: 12@w0:0`

### `port_meter_ctrlnm_field` (table `0xc8`)
- `field0: 1@w0:4` (enable)
- `field1: 4@w0:0` (meter-id)

### `meter_config_tblm_field` (table `0xce`)
- `field0: 1@w2:14`
- `field1: 1@w2:13`
- `field2: 2@w2:11`
- `field3: 1@w2:10`
- `field4: 3@w2:7`
- `field5: 1@w2:6`
- `field6: 1@w2:5`
- `field7: 1@w2:4`
- `field8: 4@w2:0`
- `field9: 12@w1:20`
- `field10: 18@w1:2`
- `field11: 2@w1:0`
- `field12: 14@w0:18`
- `field13: 18@w0:0`

## Function correlations (disassembly)

- `fal_tiger_rate_igrBandwidthCtrlEnable_set/get`
  - uses table `0xc8`
  - set/get field `0`
  - set path also programs field `1`

- `fal_tiger_rate_igrBandwidthCtrlMode_set/get`
  - uses table `0xce`
  - fields `6`, `5`

- `fal_tiger_rate_igrBandwidthCtrlRate_set/get`
  - reads table `0xc7` field `0`
  - uses table `0xce` fields including `0x0a`, `4`, and mode-related fields

- `fal_tiger_rate_meter_enable_set/get`
  - uses table `0xce` field `0`

- `fal_tiger_rate_meter_mode_set/get`
  - uses table `0xce` fields `7/6/3/2/5/1`

- `fal_tiger_rate_meter_rate_set/get`
  - reads table `0xc7` field `0`
  - uses table `0xce` fields `0x0d`, `0x0a` (+ mode helpers)

## Integration note
Current OpenWrt backport now uses this stock ingress-meter path as the primary
`.port_policer_add/del` implementation (`0xc7/0xc8/0xce`), with legacy storm
path kept as fallback.
