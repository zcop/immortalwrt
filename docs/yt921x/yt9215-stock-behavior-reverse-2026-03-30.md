# YT9215 Stock Behavior Reverse Notes (CR881x, 2026-03-30)

## Scope
- Reverse stock firmware artifacts under:
  - `Collection-Data/cr881x/mtd22_rootfs`
- Goal:
  - Identify what stock switch stack enables/uses.
  - Compare with current OpenWrt DSA `yt921x` driver coverage.

## Stock Components Confirmed
- Kernel modules:
  - `Collection-Data/cr881x/mtd22_rootfs/lib/modules/4.4.60/yt_switch.ko` (not stripped)
  - `Collection-Data/cr881x/mtd22_rootfs/lib/modules/4.4.60/qca-ssdk.ko` (not stripped)
- Userland:
  - `Collection-Data/cr881x/mtd22_rootfs/usr/sbin/ssdk_sh`
  - `Collection-Data/cr881x/mtd22_rootfs/usr/sbin/switch_ctl`
  - `Collection-Data/cr881x/mtd22_rootfs/usr/lib/libfal.so` (stripped)
- Init:
  - `Collection-Data/cr881x/mtd22_rootfs/etc/init.d/qca-ssdk`

## What Stock Actually Does At Boot (Observed in Init Scripts)
Source: `etc/init.d/qca-ssdk`

- Programs ACL + service-code paths for control/reserved traffic (function `create_acl_byp_egstp_rules`):
  - DA `01-80-c2-00-00-00` (STP group MAC)
  - EtherType `0x8809` (slow protocols/LACP family)
  - EtherType `0x888e` (802.1X/EAPOL)
- Uses `ssdk_sh servcode` and ACL bind/unbind flows.
- Contains QoS/class mapping templates via `ssdk_sh cosmap`.
- Contains optional IPQ50xx uniphy/phy debug tuning hooks (`debug uniphy`, `debug phy`) that are currently commented in the shipped script.

Also found:
- `usr/sbin/gateway_acl.sh` directly creates ACL rules with `ssdk_sh`.

## Symbol-Level Capability Evidence (Stock `yt_switch.ko`)
Symbols indicate vendor stack support for these hardware features:

### Loop Detection
- `yt_loop_detect_enable_set/get`
- `yt_loop_detect_tpid_set/get`
- `yt_loop_detect_generate_way_set/get`
- `yt_loop_detect_unitID_set/get`
- Matching lower-level `fal_tiger_loop_detect_*`

### Reserved Multicast / RMA / Control Packet Actions
- `yt_rma_action_set/get`
- `yt_rma_bypass_port_isolation_set/get`
- `yt_rma_bypass_vlan_filter_set/get`
- `yt_ctrlpkt_unknown_ucast_act_set/get`
- `yt_ctrlpkt_unknown_mcast_act_set/get`
- `yt_ctrlpkt_arp_act_set/get`
- `yt_ctrlpkt_nd_act_set/get`
- `yt_ctrlpkt_lldp_act_set/get`
- Matching `fal_tiger_rma_*`, `fal_tiger_ctrlpkt_*`

### Hardware Storm Control / Policer
- `yt_storm_ctrl_init`
- `yt_storm_ctrl_enable_set/get`
- `yt_storm_ctrl_rate_set/get`
- `yt_storm_ctrl_rate_mode_set/get`
- `yt_storm_ctrl_rate_include_gap_set/get`
- Matching `fal_tiger_storm_ctrl_*`

### Multicast/IGMP/MLD Control
- `yt_multicast_igmp_opmode_set/get`
- `yt_multicast_mld_opmode_set/get`
- `yt_multicast_*routerport*`
- `yt_multicast_*bypass_port_isolation*`

### Other Advanced Blocks Visible In Symbols
- ACL offload (`yt_acl_*`, `fal_tiger_acl_*`)
- 802.1X controls (`yt_dot1x_*`, `fal_tiger_dot1x_*`)
- QoS scheduling/remap (`yt_qos_*`, `fal_tiger_qos_*`)
- VLAN translation (`yt_vlan_trans_*`, `fal_tiger_vlan_trans_*`)

## Concrete Table-ID -> MMIO Mapping (Decoded From Stock Module)
Method:
- Parsed `tbl_reg_list` symbol in `yt_switch.ko` `.rodata`.
- Correlated with ARM disassembly of `fal_tiger_*` functions.

`tbl_reg_list` selected entries (24-byte records):

| Table ID | Base MMIO | Notes |
|---|---:|---|
| `0x0d` | `0x00080230` | loop-detect top control table |
| `0x98` | `0x00180508` | unknown-ucast filter mask table |
| `0x99` | `0x0018050c` | unknown-mcast filter mask table |
| `0xa3` | `0x001805d0` | RMA control table |
| `0xad` | `0x00180734` | unknown-ucast action table |
| `0xae` | `0x00180738` | unknown-mcast action/bypass table |
| `0xc6` | `0x00220100` | storm rate input/timeslot-side table |
| `0xc9` | `0x00220140` | storm multicast-type control mask |
| `0xcc` | `0x00220200` | storm global config table |

Expanded extraction artifacts:
- Full `tbl_reg_list` decode (`0x00..0xec` valid entries):
  - `docs/yt921x/live/yt_stock_tbl_reg_list_full_decode_2026-03-30.tsv`
- Feature-family function mapping (`fal_tiger_*` -> table IDs):
  - `docs/yt921x/live/yt_stock_feature_function_tableids_2026-03-30.tsv`
- Aggregated table-id/base/function summary:
  - `docs/yt921x/live/yt_stock_tableid_base_func_summary_2026-03-30.tsv`
- Clustered summary of newly surfaced vendor blocks:
  - `docs/yt921x/live/yt_stock_feature_tableid_clusters_2026-03-30.md`
- QoS-focused stock field decode (`*_field` tuples):
  - `docs/yt921x/live/yt_stock_qos_field_decode_2026-03-30.tsv`
- QoS pipeline -> Linux driver integration map:
  - `docs/yt921x/live/yt_stock_qos_driver_map_2026-03-30.md`

Expanded findings (beyond prior minimal set):
- Recovered 73 valid table IDs used by stock feature paths.
- Major additional MMIO clusters now evidence-linked by function paths:
  - QoS map/scheduler: `0x180000..0x180200`, `0x300200..0x300400`, `0x341000..0x343000`
  - Rate/meter/shaper: `0x220104/0x220108/0x220800`, `0x34c000/0x34f000/0x354000/0x357000`
  - VLAN translation/remark: `0x230010..0x230600`, `0x188000`, `0x100000..0x1004a8`
  - Multicast/IGMP policy surfaces: `0x180468..0x180700`

## Field Descriptor Decode (From `.rodata` Field Tables)
Descriptor format observed in stock blob: 4-byte tuples per field:
- byte0 = field index
- byte1 = field width (bits)
- byte2 = word index (within table entry words)
- byte3 = lsb position (within that word)

Decoded tables:

| Field table symbol | Decoded fields (`index: width@word:lsb`) |
|---|---|
| `storm_ctrl_config_tblm_field` | `0:19@w0:13`, `1:10@w0:3`, `2:1@w0:2`, `3:1@w0:1`, `4:1@w0:0` |
| `storm_ctrl_mc_type_ctrlm_field` | `0:11@w0:0` |
| `storm_ctrl_timeslotm_field` | `0:12@w0:0` |
| `port_meter_ctrlnm_field` | `0:1@w0:4`, `1:4@w0:0` |
| `meter_timeslotm_field` | `0:12@w0:0` |
| `meter_config_tblm_field` | `0:1@w2:14`, `1:1@w2:13`, `2:2@w2:11`, `3:1@w2:10`, `4:3@w2:7`, `5:1@w2:6`, `6:1@w2:5`, `7:1@w2:4`, `8:4@w2:0`, `9:12@w1:20`, `10:18@w1:2`, `11:2@w1:0`, `12:14@w0:18`, `13:18@w0:0` |
| `cpu_copy_dst_ctrlm_field` | `0:1@w0:2`, `1:1@w0:1`, `2:1@w0:0` |
| `rma_ctrlnm_field` | `0:1@w0:12`, `1:1@w0:11`, `2:1@w0:10`, `3:1@w0:9`, `4:1@w0:8`, `5:1@w0:7`, `6:1@w0:6`, `7:6@w0:0` |
| `l2_unknown_mcast_filter_maskm_field` | `0:11@w0:0` |
| `l2_unknown_ucast_filter_maskm_field` | `0:11@w0:0` |
| `loop_detect_top_ctrlm_field` | `0:2@w0:26`, `1:1@w0:25`, `2:2@w0:23`, `3:2@w0:21`, `4:2@w0:19`, `5:1@w0:18`, `6:16@w0:2`, `7:1@w0:1`, `8:1@w0:0` |

## Function -> Register Path (What Stock Actually Programs)
Extracted from ARM disassembly (`fal_tiger_*`):

### Storm control
- `fal_tiger_storm_ctrl_enable_set`:
  - reads/writes table `0xcc` (`0x220200`), field `4` (enable)
  - for storm type==2 path also updates table `0xc9` (`0x220140`), field `0` bitmask
- `fal_tiger_storm_ctrl_rate_mode_set`:
  - table `0xcc`, field `3`
- `fal_tiger_storm_ctrl_rate_include_gap_set`:
  - table `0xcc`, field `2`
- `fal_tiger_storm_ctrl_rate_set`:
  - reads table `0xc6` (`0x220100`) field `0`
  - reads table `0xcc` field `3` (mode)
  - writes computed values to table `0xcc` fields `0` and `1`

### Ingress meter / ingress bandwidth control
- `fal_tiger_rate_igrBandwidthCtrlEnable_set/get`:
  - table `0xc8` (`0x220108`)
  - field `0` = enable bit, field `1` = meter-id selector
- `fal_tiger_rate_igrBandwidthCtrlMode_set/get`:
  - table `0xce` (`0x220800`)
  - set/get fields `6` and `5`
- `fal_tiger_rate_igrBandwidthCtrlRate_set/get`:
  - reads table `0xc7` (`0x220104`) field `0` (`meter_timeslot`)
  - writes/reads table `0xce` fields tied to token config (`4`, `10`, plus mode fields via `6`)
- `fal_tiger_rate_meter_enable_set/get`:
  - table `0xce`, field `0`
- `fal_tiger_rate_meter_mode_set/get`:
  - table `0xce`, fields `7/6/3/2/5/1`
- `fal_tiger_rate_meter_rate_set/get`:
  - reads table `0xc7` field `0`
  - writes/reads table `0xce` fields `0x0d` and `0x0a` (plus mode-dependent fields)

### Reserved multicast / ctrlpkt / unknown filters
- `fal_tiger_ctrlpkt_unknown_ucast_act_set`:
  - table `0xad` (`0x180734`), field `0`
  - action packed per-port in 2-bit slots (`(port * 2)`)
- `fal_tiger_ctrlpkt_unknown_mcast_act_set`:
  - table `0xae` (`0x180738`), field `2`
  - action packed per-port in 2-bit slots (`(port * 2)`)
- `fal_tiger_l2_filter_unknown_ucast_set`:
  - table `0x98` (`0x180508`), field `0` mask
- `fal_tiger_l2_filter_unknown_mcast_set`:
  - table `0x99` (`0x18050c`), field `0` mask
- `fal_tiger_l2_rma_bypass_unknown_mcast_filter_set`:
  - table `0xae`, field `0`
- `fal_tiger_l2_igmp_bypass_unknown_mcast_filter_set`:
  - table `0xae`, field `1`
- `fal_tiger_rma_action_set`:
  - table `0xa3` (`0x1805d0`), fields `4`, `3`, and `7` by action case

### Loop detect
- `fal_tiger_loop_detect_enable_set`: table `0x0d` (`0x80230`), field `5`
- `fal_tiger_loop_detect_tpid_set`: table `0x0d`, field `6`
- `fal_tiger_loop_detect_generate_way_set`: table `0x0d`, field `8`
- `fal_tiger_loop_detect_unitID_set`: table `0x0d`, fields `4`, `3`, `2`

## Stock SSDK (`qca-ssdk.ko`) Evidence
`qca-ssdk.ko` exposes FAL interfaces for:
- FDB controls and IVL/SVL toggles.
- Storm-control frame/rate controls.
- VLAN translation APIs.
- ACL/policer controls.

This confirms stock architecture is:
- Vendor FAL/SSDK programming paths + `ssdk_sh` userland control.

## Comparison vs Current OpenWrt DSA `yt921x` Driver
Current backport patch:
- `target/linux/generic/backport-6.12/830-02-v6.19-net-dsa-yt921x-Add-support-for-Motorcomm-YT921x.patch`

Already covered (high level):
- VLAN, bridge flags/isolation, STP/MST
- FDB/MDB
- LAG
- Port mirror
- QoS queue map offload (`mqprio` prio->qid via `0xd3/0xd4`)
- Port shaper offload (`tbf` path on `0xeb`)
- Ingress policer wired to stock ingress-meter path (`0xc7/0xc8/0xce`)

Not equivalent to stock vendor feature path:
- Full scheduler policy path (`0xd7/0xe6/0xe7/0xe8`: SP/DWRR)
- Queue-level shaping path (`0xe9/0xea/0xe4`)
- Egress remark path (`0xd9/0xda/0xdb/0xdc`)
- Dedicated loop-detect feature path (`yt_loop_detect_*`)
- Full reserved multicast/control packet policy mapping (`yt_rma_*`, `yt_ctrlpkt_*`)

## Practical Conclusion
- Stock does more than basic DSA bridging:
  - It has dedicated hardware engines for queue scheduling/shaping, loop-detect, and RMA/ctrlpkt policy.
- Current DSA driver already covers the base QoS offload primitives (mqprio queue map, port tbf shaper, ingress policer), and the remaining gap is mainly advanced scheduler/remark/control-plane parity.

## Recommended Next Reverse Targets
1. Complete scheduler parity (`SP/DWRR` tables `0xd7/0xe6/0xe7/0xe8`).
2. Map queue-level shaping (`0xe9/0xea/0xe4`) for queue tc offload.
3. Map `yt_rma_*` + `yt_ctrlpkt_*` action encodings (reserved multicast handling).
4. Map `yt_loop_detect_*` control registers and safe default mode.

## Notes
- This document records reverse evidence only.
- It does not imply all stock features should be enabled by default in OpenWrt; each requires policy decisions and safe integration path.
- Repro helper script (table-id decode):
  - `tools/yt921x/stock_table_decode.sh`
- Repro helper script (field decode):
  - `tools/yt921x/stock_field_decode.sh`
