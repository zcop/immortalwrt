# YT9215 Stock Mirror/Remark Field Usage (2026-03-31)

## Scope
- Source module:
  - `Collection-Data/cr881x/mtd22_rootfs/lib/modules/4.4.60/yt_switch.ko`
- Method:
  - ARM disassembly (`r2`) by symbol address ranges.
  - Correlate `mov r0, <tbl-id>` + `mov r1, <field-id>` before
    `hal_tbl_reg_field_set/get`.

## Mirror path (`0xd5`, `0xd6`)

### `fal_tiger_mirror_port_set/get` -> `tbl 0xd5` (`0x300300`)
- `set` uses field IDs: `2`, `0`, `1`.
- `get` uses field IDs: `2`, `0`, `1`.
- Correlates with decoded mirror routing bits:
  - ingress source mask
  - egress source mask
  - mirror destination port

### `fal_tiger_qos_intPri_map_igrMirror_set/get` -> `tbl 0xd6` (`0x300304`)
- `set` uses field IDs: `1`, `0`.
- `get` uses field IDs: `1`, `0`.

### `fal_tiger_qos_intPri_map_egrMirror_set/get` -> `tbl 0xd6` (`0x300304`)
- `set` uses field IDs: `3`, `2`.
- `get` uses field IDs: `3`, `2`.

Interpretation:
- `0xd6` carries separate igr/egr mirror-priority mapping banks.

## Remark path (`0xd9`, `0xdb`, `0xdc`)

### `fal_tiger_qos_remark_port_set/get` -> `tbl 0xd9` (`0x100000`)
- `set` uses field IDs: `4`, `1`, `3`, `0`, `2`.
- `get` uses field IDs: `4`, `1`, `3`, `0`, `2`.

### `fal_tiger_qos_remark_dscp_set/get` -> `tbl 0xdb` (`0x100100`)
- `set/get` use field ID `0`.
- Entry index is dynamic (per DSCP-grouped table index), field selector stays `0`.

### `fal_tiger_qos_remark_cpri_set/get` -> `tbl 0xd9` + `tbl 0xdc`
- `0xd9` uses field ID `7`.
- `0xdc` uses field IDs `1`, `0`.

### `fal_tiger_qos_remark_spri_set/get` -> `tbl 0xd9` + `tbl 0xdc`
- `0xd9` uses field ID `8`.
- `0xdc` uses field IDs `1`, `0`.

## Egress TPID index on remark control table (`0xd9`)

### `fal_tiger_vlan_port_egrTpidIdx_set/get`
- `set` chooses field ID:
  - `5` when selector `arg2 == 0`
  - `6` when selector `arg2 != 0`
- `get` reads:
  - field `5` for selector `0`
  - field `6` for selector `1`

## Notes
- This artifact is function-field mapping only.
- It does not claim runtime behavioral validation for every decoded field.
