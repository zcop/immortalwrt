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
  - `docs/yt921x/live/yt_stock_qos_field_decode_2026-03-31.tsv` (expanded with `egr_*` QoS fields)
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
| Mirror + mirror-prio map | `0xd5`, `0xd6` | `0x300300`, `0x300304` | `fal_tiger_mirror_port_*`, `fal_tiger_qos_intPri_map_{igr,egr}Mirror_*` | Mirror source/destination and igr/egr mirror-priority maps. |
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

### Mirror control + mirror-prio maps
- `mirror_ctrlm_field` (`tbl 0xd5`):
  - `field0: 11@w0:16` (ingress source port mask)
  - `field1: 11@w0:4` (egress source port mask)
  - `field2: 4@w0:0` (mirror destination port id)
- `mirror_qos_ctrlm_field` (`tbl 0xd6`):
  - `field0: 1@w0:7` (igr mirror map enable)
  - `field1: 3@w0:4` (igr mirror mapped priority)
  - `field2: 1@w0:3` (egr mirror map enable)
  - `field3: 3@w0:0` (egr mirror mapped priority)

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

### Egress port VLAN QoS control (`tbl 0xda`)
- `egr_port_vlan_ctrlnm_field`:
  - `field0: 1@w0:31`
  - `field1: 1@w0:30`
  - `field2: 3@w0:27`
  - `field3: 12@w0:15`
  - `field4: 3@w0:12`
  - `field5: 12@w0:0`

Stock function correlation (from `fal_tiger_*` disassembly):
- `fal_tiger_vlan_port_egrTagMode_set/get` on `tbl 0xda` use field IDs `2` and `4` (3-bit mode fields).
- `fal_tiger_vlan_port_egrDefaultVid_set/get` on `tbl 0xda` use field IDs `3` and `5` (12-bit default-VID fields).
- `egrTagMode_get` includes API-enum remap behavior (`raw mode 5` mapped to returned mode `6`).

### Mirror + remark function-field usage (`tbl 0xd5/0xd6/0xd9/0xdb/0xdc`)
- Artifact:
  - `docs/yt921x/live/yt_stock_mirror_remark_field_usage_2026-03-31.md`
- Mirror:
  - `fal_tiger_mirror_port_set/get` (`0xd5`) use fields `2`, `0`, `1`.
  - `fal_tiger_qos_intPri_map_igrMirror_set/get` (`0xd6`) use fields `1`, `0`.
  - `fal_tiger_qos_intPri_map_egrMirror_set/get` (`0xd6`) use fields `3`, `2`.
- Remark:
  - `fal_tiger_qos_remark_port_set/get` (`0xd9`) use fields `4`, `1`, `3`, `0`, `2`.
  - `fal_tiger_qos_remark_dscp_set/get` (`0xdb`) use field `0`.
  - `fal_tiger_qos_remark_cpri_set/get` (`0xd9` + `0xdc`) use `0xd9:f7`, `0xdc:f1/f0`.
  - `fal_tiger_qos_remark_spri_set/get` (`0xd9` + `0xdc`) use `0xd9:f8`, `0xdc:f1/f0`.
  - `fal_tiger_vlan_port_egrTpidIdx_set/get` uses `0xd9:f5/f6`.

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
| `0xd5`/`0xd6` mirror path | mirror + mirror-prio map offload | Decoded only (not yet wired in backport DSA path) |
| `0xd7`/`0xe6`/`0xe7`/`0xe8` scheduler | `mqprio`/`ets` SP+DWRR policy | Implemented (safe subset, no full stock parity) |
| `0xe9`/`0xea`/`0xe4` queue shaper | queue-level `tc tbf` offload | Implemented (token-path subset on queue shaper/meter) |
| `0xd9`/`0xda`/`0xdb`/`0xdc` remark | tc flower skbedit/pedit remark offload | Partial: default init wired (`0xd9/0xdb/0xdc`), `0xda` still docs-only |

## Practical next coding order
1. Extend scheduler path from safe subset to stock-parity behavior where needed (`0xd7`, `0xe6`, `0xe7`, `0xe8`).
2. Expand queue shaper/meter path beyond token subset (`0xe9`, `0xea`, `0xe4`).
3. Add policy offload wiring for egress remark (`0xd9..0xdc`), including `0xda` mode/VID policy fields.
4. Add mirror + mirror-prio offload wiring (`0xd5/0xd6`).

## Notes
- `tools/yt921x/stock_field_decode.sh` now supports symbols with shared start
  address (uses next different symbol address as span terminator).
- This document is decode/mapping output only; no behavior claim is made for
  blocks not yet runtime A/B validated on hardware.
