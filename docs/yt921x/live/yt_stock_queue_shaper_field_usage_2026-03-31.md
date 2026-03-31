# YT9215 Stock Queue-Shaper Field Usage (`0xe4/0xe9/0xea`) - 2026-03-31

## Source
- Module:
  - `Collection-Data/cr881x/mtd22_rootfs/lib/modules/4.4.60/yt_switch.ko`
- Disassembled symbols:
  - `fal_tiger_rate_shaping_queue_enable_{set,get}`
  - `fal_tiger_rate_shaping_queue_mode_{set,get}`
  - `fal_tiger_rate_shaping_queue_rate_{set,get}`

## Table Bases
- `0xe4` -> `0x340008` (`qsch_shp_slot_time_cfgm_field`)
- `0xe9` -> `0x34c000` (`qsch_shp_cfg_tblm_field`)
- `0xea` -> `0x34f000` (`qsch_meter_cfg_tblm_field`)

## Confirmed Function -> Field-ID Mapping

### `fal_tiger_rate_shaping_queue_enable_set/get`
- table `0xe9`, field `2`
- table `0xe9`, field `1`

Interpretation:
- `field2` and `field1` are used as queue-enable related controls in stock API path.
- Both are touched in set/get pair (not only one bit).

### `fal_tiger_rate_shaping_queue_mode_set/get`
- table `0xe9`, field `3`
- table `0xea`, field `0`

Observed transform:
- mode set writes `0xe9:f3` directly from API mode argument.
- mode set writes `0xea:f0` using inverted value (`rsb mode, 1`).
- mode get reads both and returns `mode = 1 - (0xea:f0)` for second output.

### `fal_tiger_rate_shaping_queue_rate_set/get`
- table `0xe4`, field `0` (slot-time base parameter)
- table `0xe9`, fields `4`, `3` (conversion parameters)
- table `0xe9`, fields `6`, `8` (written in `rate_set`, read in `rate_get`)

Observed conversion path:
- stock computes token values using helper conversions (`fcn.08026628` / `fcn.080266c8`) with:
  - slot-time (`0xe4:f0`)
  - mode/selector values (`0xe9:f3`, `0xe9:f4`)
- resulting token-level outputs are stored in:
  - `0xe9:f6`
  - `0xe9:f8`

## Cross-check With Field Decode
- `qsch_shp_cfg_tblm_field` (`0xe9`) decoded fields:
  - `f0=1@w2:6`
  - `f1=1@w2:5`
  - `f2=1@w2:4`
  - `f3=1@w2:3`
  - `f4=3@w2:0`
  - `f5=14@w1:18`
  - `f6=18@w1:0`
  - `f7=14@w0:18`
  - `f8=18@w0:0`
- `qsch_meter_cfg_tblm_field` (`0xea`) decoded:
  - `f0=2@w0:0`
- `qsch_shp_slot_time_cfgm_field` (`0xe4`) decoded:
  - `f0=12@w0:0`

## Confidence Notes
- Field IDs and table IDs above are high-confidence (direct call-site correlation).
- Semantic labels (for example exact names of `f1/f2/f3/f4`) remain medium-confidence unless additionally validated by live A/B tests against each field independently.
