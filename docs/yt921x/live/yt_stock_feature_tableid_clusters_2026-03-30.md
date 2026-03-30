# YT9215 Stock Feature Table-ID Clusters (from `yt_switch.ko`, 2026-03-30)

## Inputs
- Full table decode:
  - `docs/yt921x/live/yt_stock_tbl_reg_list_full_decode_2026-03-30.tsv`
- Function -> table-id extraction:
  - `docs/yt921x/live/yt_stock_feature_function_tableids_2026-03-30.tsv`
- Aggregated table-id/base/function summary:
  - `docs/yt921x/live/yt_stock_tableid_base_func_summary_2026-03-30.tsv`

## What was extracted
- Parsed stock feature families (`fal_tiger_l2_*`, `multicast_*`, `qos_*`, `rate_*`, `vlan_*`, `storm_*`, `stp_*`, `rma_*`, `mirror_*`).
- Recovered **73 valid table IDs** (present in `tbl_reg_list`) that are actively referenced by stock feature code paths.

## New high-value clusters beyond the earlier minimal set
Earlier documented set was focused on:
- `0x0d`, `0x98`, `0x99`, `0xa3`, `0xad`, `0xae`, `0xc6`, `0xc9`, `0xcc`

Expanded extraction adds these major clusters:

1. QoS classification/int-pri maps:
- `0x6e..0x71` -> `0x180000/0x180100/0x180180/0x180200`
- Used by `fal_tiger_qos_intPri_*` and `fal_tiger_qos_intDP_*` map functions.

2. Multicast control and router-port policy:
- `0x89..0x93`, `0x96`, `0xa0`, `0xaa`, `0xab`
- Mostly in `0x180468..0x180700`.
- Used by `fal_tiger_multicast_*` paths (fast leave, router port, group learning, bypass policies, multicast VLAN ops).

3. Port learning / isolation / STP:
- `0x73`, `0x78`, `0x7a`, `0x7c`
- `0x180280`, `0x180294`, `0x18038c`, `0x1803d0`
- Match current DSA runtime observations for ingress filter, isolation, STP state, and per-port learning controls.

4. Storm/rate meter path expansion:
- `0xc7`, `0xc8`, `0xce` in addition to `0xc6/0xc9/0xcc`
- `0x220104`, `0x220108`, `0x220800`
- Used by ingress bandwidth/rate-meter enable/mode/rate paths.

5. Mirror + queue map + scheduler:
- `0xd3`, `0xd4`, `0xd5`, `0xd6`, `0xd7`, `0xe6`, `0xe7`, `0xe8`
- `0x300200..0x300304`, `0x300400`, `0x341000..0x343000`
- Used by queue-map, mirror-priority mapping, SP/DWRR scheduler mode/weights.

6. Port/queue shaping blocks:
- `0xe4`, `0xe5`, `0xe9`, `0xea`, `0xeb`, `0xec`
- `0x340008`, `0x34000c`, `0x34c000`, `0x34f000`, `0x354000`, `0x357000`
- Used by stock shaping enable/mode/rate functions.

7. VLAN translation / egress-tagging / remark path:
- `0x2d`, `0x2e`, `0x2f`, `0x30`, `0x32`, `0x33`, `0xbb`, `0xd9`, `0xda`, `0xdb`, `0xdc`, `0xdd`, `0xde`, `0xdf`, `0xe0`, `0xe1`
- Covers VLAN trans mode/range profiles, ingress/egress translation entries, egress default VID/tag mode, TPID/remark selectors.

## Known extraction caveats
- Two immediate constants seen in code (`0x1000`, `0xff`) are not valid table IDs in `tbl_reg_list`; treated as non-table constants and excluded from the valid-ID set.
- This extraction maps **which table IDs/features stock code touches**, not the full per-field semantic decode for every table.

## Next probe shortlist (runtime)
1. `0x300300/0x300304` (`0xd5/0xd6`): mirror + QoS mirror priority map interaction.
2. `0x341000..0x343000` (`0xe6/0xe7/0xe8`): SP vs DWRR behavior under controlled queue contention.
3. `0x34c000/0x34f000/0x354000/0x357000` (`0xe9/0xea/0xeb/0xec`): shaping enable/mode/rate effect verification.
4. `0x220104/0x220108/0x220800` (`0xc7/0xc8/0xce`): ingress meter model correlation with packet drops.
5. `0x00100000..0x001004a8` (`0xd9..0xe1`): egress remark/TPID/translation interplay.
