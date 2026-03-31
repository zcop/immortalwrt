# YT9215 Stock Scheduler Field Usage (`0xd7/0xe6/0xe7/0xe8`) - 2026-03-31

## Source
- Module:
  - `Collection-Data/cr881x/mtd22_rootfs/lib/modules/4.4.60/yt_switch.ko`
- Disassembled symbols:
  - `fal_tiger_qos_schedule_sp_{set,get}`
  - `fal_tiger_qos_schedule_dwrr_{set,get}`
  - `fal_tiger_qos_schedule_dwrr_mode_{set,get}`

## Table Bases
- `0xd7` -> `0x300400`
- `0xe6` -> `0x341000`
- `0xe7` -> `0x342000`
- `0xe8` -> `0x343000`

## Confirmed Function -> Field-ID Mapping

### `fal_tiger_qos_schedule_sp_set`
- table `0xd7`: fields `1`, `0`, `2` are set in sequence.
- table `0xe6`: fields `3` and `2` are set from API input path.

Observed behavior:
- `0xd7` write is followed by a poll loop that repeatedly reads `0xd7:f1`
  until non-zero (or timeout), then proceeds to `0xe6` updates.

### `fal_tiger_qos_schedule_sp_get`
- table `0xe6`: field `3` is read as output mode/state value.

### `fal_tiger_qos_schedule_dwrr_set`
- table `0xe6`: field `1` is set explicitly.
- table `0xe6`: field `0` is also set in the same path.

### `fal_tiger_qos_schedule_dwrr_get`
- table `0xe6`: field `1` is read.

### `fal_tiger_qos_schedule_dwrr_mode_set/get`
- table `0xe7`: field `0`
- table `0xe8`: field `0`

Observed behavior:
- mode set writes the same mode value to both `0xe7:f0` and `0xe8:f0`.
- mode get reads from `0xe7:f0`.

## Cross-check With Field Decode
- `qsch_e_dwrr_cfg_tblm_field` (`0xe6`): `f0=1@w0:0`
- `qsch_c_dwrr_cfg_tblm_field` (`0xe7/0xe8` side cfg helpers): `f0=1@w0:0`
- `qsch_flow_map_tblm_field` (`0xe6` family): `f0..f3` at
  `[27:18]`, `[17:8]`, `[7:4]`, `[3:0]`

## Confidence Notes
- Table and field IDs above are high-confidence (direct call-site evidence).
- Semantic naming of each bitfield remains medium-confidence without complete
  live A/B for every field combination.
