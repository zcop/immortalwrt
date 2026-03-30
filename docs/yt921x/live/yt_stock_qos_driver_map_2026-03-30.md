# YT9215 Stock QoS Reverse -> Driver Mapping (2026-03-30)

## Scope
- Source binary:
  - `Collection-Data/cr881x/mtd22_rootfs/lib/modules/4.4.60/yt_switch.ko`
- Goal:
  - Decode stock QoS/rate pipeline tables and bitfields.
  - Map each block to Linux DSA offload status in current `yt921x` backport.

## Inputs
- Stock table-id/base extraction:
  - `docs/yt921x/live/yt_stock_tableid_base_func_summary_2026-03-30.tsv`
- Stock function -> table-id extraction:
  - `docs/yt921x/live/yt_stock_feature_function_tableids_2026-03-30.tsv`
- Field decode (from `*_field` symbols):
  - `docs/yt921x/live/yt_stock_qos_field_decode_2026-03-30.tsv`
- Decoder tooling:
  - `tools/yt921x/stock_table_decode.sh`
  - `tools/yt921x/stock_field_decode.sh`

## QoS Pipeline Blocks (Stock)

| Stage | Table ID(s) | Base MMIO | Stock symbol family | Notes |
|---|---|---:|---|---|
| Ingress priority classification | `0x6e`, `0x6f`, `0x70`, `0x71`, `0xbb` | `0x180000..0x188000` | `fal_tiger_qos_intPri_*`, `fal_tiger_qos_intDP_*` | DSCP/PCP/default-prio to internal prio+drop precedence. |
| Queue mapping (prio -> qid) | `0xd3`, `0xd4` | `0x300200`, `0x300280` | `fal_tiger_qos_que_map_*` | Ucast and mcast maps are separate. |
| Scheduler selection + DWRR | `0xd7`, `0xe6`, `0xe7`, `0xe8` | `0x300400`, `0x341000..0x343000` | `fal_tiger_qos_schedule_*` | SP path and DWRR mode/enable path. |
| Queue shaping/meter | `0xe4`, `0xe9`, `0xea` | `0x340008`, `0x34c000`, `0x34f000` | `fal_tiger_rate_shaping_queue_*` | Per-queue CIR/CBS/EIR/EBS + mode. |
| Port shaping/meter | `0xe5`, `0xeb`, `0xec` | `0x34000c`, `0x354000`, `0x357000` | `fal_tiger_rate_shaping_port_*` | Per-port rate shaper. |
| Ingress policer/meter | `0xc7`, `0xc8`, `0xce` | `0x220104`, `0x220108`, `0x220800` | `fal_tiger_rate_igrBandwidthCtrl*`, `fal_tiger_rate_meter_*` | Stock ingress-meter path. |
| Egress remarking | `0xd9`, `0xda`, `0xdb`, `0xdc` | `0x100000..0x100200` | `fal_tiger_qos_remark_*` | DSCP/PCP/port remark controls. |

## Key Field Maps (decoded)

### Queue mapping
- `int_prio_to_ucast_qid_mapnm_field`:
  - fields `0..7`: `3@w0` at shifts `28,24,20,16,12,8,4,0`.
- `int_prio_to_mcast_qid_mapnm_field`:
  - fields `0..7`: `2@w0` at shifts `14,12,10,8,6,4,2,0`.

Implication:
- Ucast map supports queue IDs up to `0..7`.
- Mcast map supports queue IDs up to `0..3` in this table format.

### Scheduler / DWRR helpers
- `qsch_e_dwrr_cfg_tblm_field`: `field0: 1@w0:0`
- `qsch_c_dwrr_cfg_tblm_field`: `field0: 1@w0:0`
- `qsch_flow_map_tblm_field`:
  - `field0: 10@w0:18`
  - `field1: 10@w0:8`
  - `field2: 4@w0:4`
  - `field3: 4@w0:0`
- `tbl_reg_list` shape for `0xe6/0xe7/0xe8`: `132` entries (`0x84`), 1 word per entry.
  - This matches `11 ports * 12 flows/port` where `12 = 8 unicast + 4 multicast`.
  - Practical index model used by driver header macros:
    - `idx = port * 12 + qid` (unicast `qid 0..7`)
    - `idx = port * 12 + 8 + qid` (multicast `qid 0..3`)

### Queue shaper
- `qsch_shp_cfg_tblm_field`:
  - `field0: 1@w2:6` (`en`)
  - `field1: 1@w2:5` (`dual_rate`)
  - `field2: 1@w2:4` (`couple`)
  - `field3: 1@w2:3` (`mode`)
  - `field4: 3@w2:0` (`meter_id`)
  - `field5: 14@w1:18` (`ebs`)
  - `field6: 18@w1:0` (`eir`)
  - `field7: 14@w0:18` (`cbs`)
  - `field8: 18@w0:0` (`cir`)
- `qsch_meter_cfg_tblm_field`: `field0: 2@w0:0`
- `qsch_shp_slot_time_cfgm_field`: `field0: 12@w0:0`

### Port shaper
- `psch_shp_cfg_tblm_field`:
  - `field0: 1@w1:4` (`en`)
  - `field1: 1@w1:3` (`dual_rate`)
  - `field2: 3@w1:0` (`meter_id`)
  - `field3: 14@w0:18` (`ebs`)
  - `field4: 18@w0:0` (`eir`)
- `psch_meter_cfg_tblm_field`: `field0: 2@w0:0`
- `psch_shp_slot_time_cfgm_field`: `field0: 12@w0:0`

### Ingress meter
- `port_meter_ctrlnm_field`:
  - `field0: 1@w0:4` (`enable`)
  - `field1: 4@w0:0` (`meter_id`)
- `meter_timeslotm_field`: `field0: 12@w0:0`
- `meter_config_tblm_field`:
  - same 14-field layout already wired in driver debug table (`0xce` path).

## Driver Coverage (current backport)

| Block | Linux hook path | Status |
|---|---|---|
| `0xd3`/`0xd4` queue map | `.port_setup_tc` + `TC_SETUP_QDISC_MQPRIO` | Implemented (`yt921x_mqprio_apply`) |
| `0xeb` port shaper | `.port_setup_tc` + `TC_SETUP_QDISC_TBF` | Implemented (port shaper EIR/EBS/en) |
| `0xc7`/`0xc8`/`0xce` ingress meter | `.port_policer_add/.del` | Implemented (stock ingress-meter first, storm fallback) |
| `0xd7`/`0xe6`/`0xe7`/`0xe8` scheduler | `mqprio` SP/DWRR policy | Not wired yet |
| `0xe9`/`0xea`/`0xe4` queue shaper | queue-level tc offload | Not wired yet |
| `0xd9`/`0xda`/`0xdb`/`0xdc` remark | tc flower skbedit/pedit remark offload | Not wired yet |

## Practical next coding order
1. Scheduler wiring (`0xd7`, `0xe6`, `0xe7`, `0xe8`) behind existing `mqprio` path.
2. Queue shaper wiring (`0xe9`, `0xea`, `0xe4`) as extension after scheduler.
3. Remark offload (`0xd9..0xdc`) only after scheduler/shaper path is stable.

## Notes
- `tools/yt921x/stock_field_decode.sh` now supports symbols with shared start
  address (uses next different symbol address as span terminator).
- This document is decode/mapping output only; no behavior claim is made for
  blocks not yet runtime A/B validated on hardware.
