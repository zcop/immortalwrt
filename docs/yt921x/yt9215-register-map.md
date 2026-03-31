# YT9215 Register Map (CR881x Baseline, Unified)

## Scope
This document is the canonical, deduplicated register map for CR881x.

- Purpose: one place for `symbol/address -> stock value -> usage -> value semantics`.
- Deep probe history and raw trial logs stay in:
  - `docs/yt921x/yt9215-register-map-changelog.md`
  - `docs/yt921x/live/*`

## Conventions
- `Stock value`: baseline read in stable runtime on CR881x (if available).
- `dynamic / see probes`: depends on bridge/VLAN/conduit/runtime state.
- `offset only`: value is an offset under a base register (not an absolute MMIO).
- Confidence:
  - `High`: symbol + behavior + live reads align.
  - `Medium`: symbol aligns, some behavior still inferred.
  - `Low`: only positional/probe hint.

## Port Identity (CR881x)
| Port | Role |
|---|---|
| `p0` | `lan1` |
| `p1` | `lan2` |
| `p2` | `lan3` |
| `p3` | `wan` |
| `p4` | `cpu2` (`eth0`, secondary conduit) |
| `p8` | `cpu1` (`eth1`, primary conduit) |
| `p10` | internal `mcu` |

## Special Values And Bit Semantics
| Value / bits | Meaning |
|---|---|
| `0xdeadbeef` | gated/unmapped read window (no decoded function yet) |
| `0xdeaddead` | hazardous global state observed during unsafe `YT921X_FUNC` bit-5 probe |
| `0x000007ff` | 11-bit all-port mask (`p0..p10`) |
| `0x0000003f` | 6-bit local mask pattern seen in unknown matrix block |
| `YT921X_PORTn_ISOLATION` | negative logic: bit `1` = block `src->dst`, bit `0` = allow |
| `YT921X_PORTn_VLAN_CTRL` | `PVID = (reg >> 6) & 0xFFF` |
| `YT921X_VLANn_CTRL(v)` stride | `0x188000 + 8 * VID`; word0 membership, word1 egress-untag |
| `YT921X_ACT_UNK_ACTn(port,x)` | `x=0` flood, `x=1` trap-to-CPU, `x=2` drop, `x=3` copy (unsafe in tag path) |
| `YT921X_CPU_COPY` | bit0 ext-CPU, bit1 int-CPU, bit2 force-int-port |
| `YT921X_FDB_RESULT` | bit15 `DONE`, bit14 `NOTFOUND`, bit13 `OVERWRITED`, bits11:0 index |

## Unified Register Table
| Symbol | Address/formula | Stock value | Usage / value meaning | Confidence |
|---|---|---|---|---|
| `YT921X_RST` | `0x80000` | dynamic / see probes | Global reset control (HW/SW bits). | Medium |
| `YT921X_FUNC` | `0x80004` | 0x0000080b | Global function control; baseline stable. Hazard: bit5 probe caused 0xdeaddead + management collapse. | High |
| `YT921X_CHIP_ID` | `0x80008` | 0x90020001 | Chip identifier register. | High |
| `YT921X_EXT_CPU_PORT` | `0x8000c` | 0x0000c008 | Select tagged CPU conduit. 0x0000c008 => p8; 0x0000c004 => p4. | High |
| `YT921X_CPU_TAG_TPID` | `0x80010` | 0x00009988 | CPU-tag TPID register; default 0x9988. | High |
| `YT921X_PVID_SEL` | `0x80014` | 0x00000000 | Global PVID select path; low bits latch, no gate-unlock effect found. | High |
| `YT921X_SERDES_CTRL` | `0x80028` | 0x00000041 | Header-defined register/formula; behavior not yet fully profiled. | Medium |
| `YT921X_IO_LEVEL` | `0x80030` | dynamic / see probes | Pad IO voltage level control. | Medium |
| `YT921X_MAC_ADDR_HI2` | `0x80080` | dynamic / see probes | Global MAC address registers. | Medium |
| `YT921X_MAC_ADDR_LO4` | `0x80084` | dynamic / see probes | Global MAC address registers. | Medium |
| `YT921X_SERDESn(port)` | `(0x8008c + 4 * ((port) - 8))` | dynamic / see probes | Serdes lane mode and forced-link/speed/pause controls. | Medium |
| `YT921X_PORTn_CTRL(port)` | `(0x80100 + 4 * (port))` | dynamic / see probes | Per-port admin MAC control (link/duplex/pause/speed bits). | Medium |
| `YT921X_PORTn_STATUS(port)` | `(0x80200 + 4 * (port))` | dynamic / see probes | Per-port runtime link/MAC status. | Medium |
| `YT921X_PON_STRAP_FUNC` | `0x80320` | dynamic / see probes | Power-on strap function/value/capture registers. | Medium |
| `YT921X_PON_STRAP_VAL` | `0x80324` | dynamic / see probes | Power-on strap function/value/capture registers. | Medium |
| `YT921X_PON_STRAP_CAP` | `0x80328` | dynamic / see probes | Power-on strap function/value/capture registers. | Medium |
| `YT921X_MDIO_POLLINGn(port)` | `(0x80364 + 4 * ((port) - 8))` | dynamic (p8=0x1a, p9=0x12 sample) | Serdes polling summary (link/duplex/speed code fields). | Medium |
| `YT921X_SENSOR` | `0x8036c` | dynamic / see probes | Sensor/temperature path registers. | Medium |
| `YT921X_TEMP` | `0x80374` | dynamic / see probes | Sensor/temperature path registers. | Medium |
| `YT921X_CHIP_MODE` | `0x80388` | 0x00000002 | Chip mode profile selection/readback. | Medium |
| `YT921X_XMII_CTRL` | `0x80394` | 0x00000000 | XMII global/per-port mode and delay control. | Medium |
| `YT921X_XMIIn(port)` | `(0x80400 + 8 * ((port) - 8))` | dynamic / see probes | XMII global/per-port mode and delay control. | Medium |
| `YT921X_MACn_FRAME(port)` | `(0x81008 + 0x1000 * (port))` | dynamic / see probes | Per-port max frame-size control field. | Medium |
| `YT921X_EEEn_VAL(port)` | `(0xa0000 + 0x40 * (port))` | dynamic / see probes | EEE per-port/global controls. | Medium |
| `YT921X_EEE_CTRL` | `0xb0000` | dynamic / see probes | EEE per-port/global controls. | Medium |
| `YT921X_MIB_CTRL` | `0xc0004` | 0x00000001 | MIB control: bit30 clean, bits6:3 port-id, bit1 one-port op, bit0 all-port op. | High |
| `YT921X_MIBn_DATA0(port)` | `(0xc0100 + 0x100 * (port))` | dynamic / see probes | Base of per-port MIB data window; use offsets below. | Medium |
| `YT921X_MIB_DATA_RX_BROADCAST` | `0x00` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_PAUSE` | `0x04` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_MULTICAST` | `0x08` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_CRC_ERR` | `0x0c` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_ALIGN_ERR` | `0x10` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_UNDERSIZE_ERR` | `0x14` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_FRAG_ERR` | `0x18` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_PKT_SZ_64` | `0x1c` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_PKT_SZ_65_TO_127` | `0x20` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_PKT_SZ_128_TO_255` | `0x24` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_PKT_SZ_256_TO_511` | `0x28` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_PKT_SZ_512_TO_1023` | `0x2c` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_PKT_SZ_1024_TO_1518` | `0x30` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_PKT_SZ_1519_TO_MAX` | `0x34` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_GOOD_BYTES` | `0x3c` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_BAD_BYTES` | `0x44` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_OVERSIZE_ERR` | `0x4c` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_DROPPED` | `0x50` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_BROADCAST` | `0x54` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_PAUSE` | `0x58` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_MULTICAST` | `0x5c` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_UNDERSIZE_ERR` | `0x60` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_PKT_SZ_64` | `0x64` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_PKT_SZ_65_TO_127` | `0x68` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_PKT_SZ_128_TO_255` | `0x6c` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_PKT_SZ_256_TO_511` | `0x70` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_PKT_SZ_512_TO_1023` | `0x74` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_PKT_SZ_1024_TO_1518` | `0x78` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_PKT_SZ_1519_TO_MAX` | `0x7c` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_GOOD_BYTES` | `0x84` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_COLLISION` | `0x8c` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_EXCESSIVE_COLLISION` | `0x90` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_MULTIPLE_COLLISION` | `0x94` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_SINGLE_COLLISION` | `0x98` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_PKT` | `0x9c` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_DEFERRED` | `0xa0` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_LATE_COLLISION` | `0xa4` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_RX_OAM` | `0xa8` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_MIB_DATA_TX_OAM` | `0xac` | offset only | MIB counter offset under YT921X_MIBn_DATA0(port). | High |
| `YT921X_EDATA_CTRL` | `0xe0000` | dynamic / see probes | EDATA command/data path for extended data operations. | Medium |
| `YT921X_EDATA_DATA` | `0xe0004` | 0x00000003 (sample) | EDATA command/data path for extended data operations. | Medium |
| `YT921X_EXT_MBUS_OP` | `0x6a000` | dynamic / see probes | MBUS op trigger (start bit). | Medium |
| `YT921X_INT_MBUS_OP` | `0xf0000` | dynamic / see probes | MBUS op trigger (start bit). | Medium |
| `YT921X_EXT_MBUS_CTRL` | `0x6a004` | dynamic (0x00000900 sample) | MBUS command fields (port/reg/type/op). | Medium |
| `YT921X_INT_MBUS_CTRL` | `0xf0004` | dynamic (0x006a0908 sample) | MBUS command fields (port/reg/type/op). | Medium |
| `YT921X_EXT_MBUS_DOUT` | `0x6a008` | dynamic / see probes | MBUS write-data register. | Medium |
| `YT921X_INT_MBUS_DOUT` | `0xf0008` | dynamic / see probes | MBUS write-data register. | Medium |
| `YT921X_EXT_MBUS_DIN` | `0x6a00c` | dynamic / see probes | MBUS read-data register. | Medium |
| `YT921X_INT_MBUS_DIN` | `0xf000c` | dynamic / see probes | MBUS read-data register. | Medium |
| `YT921X_PORTn_EGR(port)` | `(0x100000 + 4 * (port))` | dynamic / see probes | Per-port egress control (shared with stock `tbl 0xd9` QoS remark port-control bits in current reverse). | Medium |
| `YT921X_TPID_EGRn(x)` | `(0x100300 + 4 * (x))` | dynamic / see probes | Egress TPID profile table. | Medium |
| `YT921X_IPM_DSCPn(n)` | `(0x180000 + 4 * (n))` | dynamic / see probes | DSCP to internal-priority map table. | Medium |
| `YT921X_IPM_PCPn(map, dei, pcp)` | `(0x180100 + 4 * (16 * (map) + 8 * (dei) + (pcp)))` | dynamic / see probes | PCP/DEI to internal-priority map table. | Medium |
| `YT921X_PORTn_QOS(port)` | `(0x180180 + 4 * (port))` | dynamic / see probes | Per-port QoS source-selection + default priority enable. | Medium |
| `YT921X_PORTn_PRIO_ORD(port)` | `(0x180200 + 4 * (port))` | dynamic / see probes | Per-port application priority order selection. | Medium |
| `YT921X_VLAN_IGR_FILTER` | `0x180280` | dynamic (0x0/0x7 in 3-LAN profile) | Ingress VLAN filter enable mask. 0x0 off; 0x7 enables p0..p2 in current 3-LAN profile. | High |
| `YT921X_PORTn_ISOLATION(port)` | `(0x180294 + 4 * (port))` | dynamic / see probes | Directional block mask (negative logic): bit d=1 blocks src->dst d, bit d=0 allows. | High |
| `YT921X_STPn(n)` | `(0x18038c + 4 * (n))` | dynamic / see probes | STP state word per instance. STP0 is active runtime bank. | High |
| `YT921X_PORTn_LEARN(port)` | `(0x1803d0 + 4 * (port))` | dynamic / see probes | Per-port learning policy and limits. | High |
| `YT921X_AGEING` | `0x180440` | 0x0000003c | FDB aging interval register. | High |
| `YT921X_FDB_IN0` | `0x180454` | dynamic (0xccd84354 sample) | FDB input word0 (MAC high bits / key payload for add/del/get). | Medium |
| `YT921X_FDB_IN1` | `0x180458` | dynamic (0x70011c7a sample) | FDB input word1 (status bits + FID/VID `[27:16]` + MAC low bits). | Medium |
| `YT921X_FDB_IN2` | `0x18045c` | dynamic (0x04000000 sample) | FDB input word2 (egress mask/copy/prio/new-VID fields for command payload). | Medium |
| `YT921X_FDB_OP` | `0x180460` | dynamic / see probes | FDB command register: op `[3:1]` (add/del/get/flush), start bit0, index/mode/flush selectors. | High |
| `YT921X_FDB_RESULT` | `0x180464` | dynamic (0x00008f05 sample) | FDB result: bit15 DONE, bit14 NOTFOUND, bit13 OVERWRITED, index `[11:0]` (sample index `0x0f05`). | High |
| `YT921X_FDB_OUT0` | `0x1804b0` | dynamic / see probes | Header-defined register/formula; behavior not yet fully profiled. | Medium |
| `YT921X_FDB_OUT1` | `0x1804b4` | dynamic / see probes | FDB payload word1: status + FID/VID + MAC low bits. | Medium |
| `YT921X_FDB_OUT2` | `0x1804b8` | dynamic / see probes | FDB payload word2: egress mask/copy-to-cpu/priority/new-VID fields. | Medium |
| `YT921X_FILTER_UNK_UCAST` | `0x180508` | dynamic / see probes | Unknown-unicast destination-port mask (`[10:0]` => p0..p10). | Medium |
| `YT921X_FILTER_UNK_MCAST` | `0x18050c` | dynamic / see probes | Unknown-multicast destination-port mask (`[10:0]` => p0..p10). | Medium |
| `YT921X_FILTER_MCAST` | `0x180510` | 0x00000400 (safe baseline) | Multicast flood/filter mask. 0x400 safe baseline; 0x7ff correlated with blackhole in bad runtime. | High |
| `YT921X_FILTER_BCAST` | `0x180514` | 0x00000400 (safe baseline) | Broadcast flood/filter mask. 0x400 safe baseline; 0x7ff correlated with blackhole in bad runtime. | High |
| `YT921X_VLAN_EGR_FILTER` | `0x180598` | dynamic / see probes | Per-port VLAN egress filter enable mask. | Medium |
| `YT921X_LAG_GROUPn(n)` | `(0x1805a8 + 4 * (n))` | dynamic / see probes | LAG group member-port mask and member count. | Medium |
| `YT921X_LAG_MEMBERnm(n, m)` | `(0x1805b0 + 4 * (4 * (n) + (m)))` | dynamic / see probes | LAG member slot to physical port mapping. | Medium |
| `YT921X_CPU_COPY` | `0x180690` | 0x00000001 | CPU copy policy: bit0 copy to ext CPU, bit1 to int CPU, bit2 force-int-port path. | Medium |
| `YT921X_ACT_UNK_UCAST` | `0x180734` | 0x00020000 | Per-port unknown-ucast action in 2-bit fields: `00` flood, `01` trap, `10` drop, `11` copy (unsafe). | Medium |
| `YT921X_ACT_UNK_MCAST` | `0x180738` | 0x00420000 | Per-port unknown-mcast action in 2-bit fields; bit22/bit23 are bypass-drop controls for IGMP/RMA classes. | Medium |
| `YT921X_FDB_HW_FLUSH` | `0x180958` | dynamic / see probes | HW FDB flush policy; bit0 enables auto flush on link-down. | Medium |
| `YT921X_VLANn_CTRL(vlan)` | `(0x188000 + 8 * (vlan))` | dynamic / see probes | VTU entry base (8 bytes/VID): member ports `[17:7]`, FID `[34:23]`, untag ports `[50:40]`, plus STP/prio/learn controls. | High |
| `YT921X_TPID_IGRn(x)` | `(0x210000 + 4 * (x))` | dynamic / see probes | Ingress TPID profile table. | Medium |
| `YT921X_PORTn_IGR_TPID(port)` | `(0x210010 + 4 * (port))` | dynamic / see probes | Per-port ingress TPID selector mask. | Medium |
| `YT921X_LAG_HASH` | `0x210090` | dynamic / see probes | Global LAG hash key-select bits (MAC/IP/L4/src-port). | Medium |
| `YT921X_PORTn_VLAN_CTRL(port)` | `(0x230010 + 4 * (port))` | dynamic / see probes | Per-port default VLAN control. PVID = (value >> 6) & 0xFFF. | High |
| `YT921X_PORTn_VLAN_CTRL1(port)` | `(0x230080 + 4 * (port))` | 0x00000000 (sampled) | VLAN range/drop control: range-en bit8, profile `[7:4]`, drop tagged/untagged bits for C/S-VLAN at `[3:0]`. | High |
| `YT921X_STOCK_LOOP_DETECT_TOP_CTRL` | `0x00080230` | stock-path only | Stock loop-detect control table (`tbl id 0x0d`). Decoded fields: `f5=1@18` enable, `f6=16@2` TPID, `f8=1@0` generate-way, `f4/f3/f2` unit-id slices. | Medium |
| `YT921X_STOCK_STORM_RATE_IO` | `0x00220100` | stock-path only | Stock storm table (`tbl id 0xc6`) used by `fal_tiger_storm_ctrl_rate_set/get` as rate/timeslot input side. | Medium |
| `YT921X_STOCK_STORM_MC_TYPE_CTRL` | `0x00220140` | stock-path only | Stock storm multicast-type mask table (`tbl id 0xc9`), decoded `11@0` port/type mask field. | Medium |
| `YT921X_STOCK_STORM_CONFIG` | `0x00220200` | stock-path only | Stock storm global config (`tbl id 0xcc`), decoded fields: `f4=bit0` enable, `f3=bit1` mode, `f2=bit2` include-gap, plus rate fields `f1=10@3`, `f0=19@13`. | Medium |
| `YT921X_STOCK_RATE_IGR_BW_CTRL` | `0x00220104` | dynamic / policer-mapped | Ingress-meter timeslot table (`tbl id 0xc7`), decoded `field0=12@w0:0` (`meter_timeslot`); used by current `port_policer` path. | High |
| `YT921X_STOCK_RATE_IGR_BW_ENABLE` | `0x00220108` | dynamic / policer-mapped | Ingress-meter per-port control (`tbl id 0xc8`): `field0=bit4` enable, `field1=[3:0]` meter-id; used by current `port_policer` path. | High |
| `YT921X_STOCK_RATE_METER_CONFIG` | `0x00220800` | dynamic / policer-mapped | Ingress-meter config base (`tbl id 0xce`), 3-word entries with decoded CIR/CBS/token/mode fields; used by current `port_policer` path. | High |
| `YT921X_STOCK_RATE_METER_CONFIGn(n)` | `(0x220800 + 0x10 * n)` | dynamic / policer-mapped | Meter entry stride (`0x10` per meter id). Word0/1/2 fields decoded in stock field map. | High |
| `YT921X_STOCK_QOS_QUEUE_MAP_UCAST` | `0x00300200` | dynamic / mqprio-mapped | Queue map base (`tbl id 0xd3`): internal prio `0..7` to unicast qid (`3-bit` each slot). | High |
| `YT921X_STOCK_QOS_QUEUE_MAP_MCAST` | `0x00300280` | dynamic / mqprio-mapped | Queue map base (`tbl id 0xd4`): internal prio `0..7` to multicast qid (`2-bit` each slot). | High |
| `YT921X_STOCK_QOS_SCHED_SP` | `0x00300400` | dynamic / ets-mapped (subset) | Scheduler SP control (`tbl id 0xd7`), programmed by current ETS/mqprio scheduler path. | Medium |
| `YT921X_STOCK_QOS_SCHED_DWRR` | `0x00341000` | dynamic / ets-mapped (subset) | DWRR scheduler config (`tbl id 0xe6`), programmed by current ETS/mqprio scheduler path. | Medium |
| `YT921X_STOCK_QOS_SCHED_DWRR_MODE0` | `0x00342000` | dynamic / ets-mapped (subset) | DWRR mode bank0 (`tbl id 0xe7`), used by current ETS/mqprio scheduler path. | Medium |
| `YT921X_STOCK_QOS_SCHED_DWRR_MODE1` | `0x00343000` | dynamic / ets-mapped (subset) | DWRR mode bank1 (`tbl id 0xe8`), used by current ETS/mqprio scheduler path. | Medium |
| `YT921X_STOCK_QOS_QSCH_SLOT_TIME` | `0x00340008` | dynamic / queue-tbf-mapped | Queue scheduler slot-time word (`tbl id 0xe4`), currently ensured/set by queue `tc tbf` path. | Medium |
| `YT921X_STOCK_QOS_QSCH_SHAPER` | `0x0034c000` | dynamic / queue-tbf-mapped | Queue shaper config base (`tbl id 0xe9`), per-flow shaper words used by queue `tc tbf`. | High |
| `YT921X_STOCK_QOS_QSCH_METER` | `0x0034f000` | dynamic / queue-tbf-mapped (token subset) | Queue meter config base (`tbl id 0xea`), token-path subset used by current queue `tc tbf` implementation. | Medium |
| `YT921X_STOCK_QOS_REMARK_PORT_CTRL` | `0x00100000` | dynamic / remark-default-mapped | Per-port remark enable control (`tbl id 0xd9`): current driver enables CPRI/SPRI remark bits by default. | Medium |
| `YT921X_STOCK_QOS_REMARK_PORT` | `0x00100080` | stock-path decoded / not actively programmed | Egress port remark control (`tbl id 0xda`) for VID/tag-mode related remark policy. | Medium |
| `YT921X_STOCK_QOS_REMARK_DSCP` | `0x00100100` | dynamic / remark-default-mapped | Egress DSCP remark table (`tbl id 0xdb`), initialized with class-selector baseline and updated by DSCP-prio API path. | Medium |
| `YT921X_STOCK_QOS_REMARK_CPRI_SPRI` | `0x00100200` | dynamic / remark-default-mapped | Egress CPRI/SPRI remark table (`tbl id 0xdc`), current driver initializes identity rewrite with enable bit set. | Medium |
| `YT921X_MIRROR` | `0x300300` | dynamic / see probes | Mirror routing: ingress-src mask `[26:16]`, egress-src mask `[14:4]`, destination port `[3:0]`. | High |
| `YT921X_PSCH_SHPn_EBS_EIR(port)` | `(0x354000 + 8 * (port))` | dynamic (tc-validated) | Backport-only helper: shaper EBS/EIR word. Used with `tc tbf` offload mapping on `wan`. | High |
| `YT921X_PSCH_SHPn_CTRL(port)` | `(0x354004 + 8 * (port))` | dynamic (tc-validated) | Backport-only helper: shaper enable/mode control word. | High |
| `UNKNOWN_18028C` | `0x18028c` | `0x000007ff` | Unknown global mask-like word; writable, no deterministic forwarding-path coupling confirmed yet. | Medium |
| `UNKNOWN_1802C0_180308` | `0x1802c0-0x180308` | `0xdeadbeef` | Gated window A; reads gated, no stable unlock sequence confirmed. | Low |
| `UNKNOWN_18030C_180334` | `0x18030c-0x180334` | `0x00000000` | Writable 11-word table (`& 0x7ff` values persist); function still unknown. | Medium |
| `UNKNOWN_180338_180388` | `0x180338-0x180388` | `0xdeadbeef` | Gated window B; reads gated, no stable unlock sequence confirmed. | Low |
| `UNKNOWN_180500_18053C` | `0x180500-0x18053c` | mostly `0x00000000` | Unknown policy block near flood/action regs; low-bit sweeps showed no deterministic path effect. | Low |
| `UNKNOWN_1805D0_18068C` | `0x1805d0-0x18068c` | mostly `0x0000003f` (`0x1805d4/0x1805d8=0x0000023f`) | Unknown mask matrix; no deterministic coupling to tested known-ucast/UU/MC paths. | Low |
| `UNKNOWN_1806B8` | `0x1806b8` | `0x000007ff` | Threshold-like candidate; A/B sweeps showed no stable limiter effect. | Low |
| `UNKNOWN_1806BC` | `0x1806bc` | `0x00000010` | Mode-like selector candidate; static in tested workloads. | Low |
| `UNKNOWN_355000_355028` | `0x355000-0x355028` | mostly `0x00000000` | QoS-neighbor island; no deterministic effect from low-bit sweeps. | Low |

## Write Recipes (Value -> Effect)
Concrete examples from CR881x live tests.

| Target register | Apply this value | Observed effect | Restore / safe value |
|---|---|---|---|
| `YT921X_EXT_CPU_PORT` (`0x08000c`) | `0x0000c008` | CPU tagged path uses `p8` (`eth1` primary conduit). | `0x0000c008` |
| `YT921X_EXT_CPU_PORT` (`0x08000c`) | `0x0000c004` | CPU tagged path switches to `p4` (`eth0`). | `0x0000c008` |
| `YT921X_VLAN_IGR_FILTER` (`0x180280`) | `0x00000000` | VLAN ingress filtering off for user ports. | profile-dependent (`0x00000007` in 3-LAN vf=1 profile) |
| `YT921X_VLAN_IGR_FILTER` (`0x180280`) | `0x00000007` | Enables ingress VLAN filtering on `p0..p2` (lan1..lan3). | `0x00000000` when vf=0 |
| `YT921X_PORTn_ISOLATION(p0)` (`0x180294`) | `0x000006fb` (from `0x6f9`) | Sets bit1=1, blocks `p0 -> p1` (lan1 to lan2) directionally. | `0x000006f9` |
| `YT921X_PORTn_ISOLATION(p1)` (`0x180298`) | `0x000006fb` (from `0x6fa`) | Sets bit0=1, blocks `p1 -> p0` (lan2 to lan1) directionally. | `0x000006fa` |
| `YT921X_FILTER_MCAST` (`0x180510`) | `0x00000400` | Stable/safe multicast filter baseline on this board. | `0x00000400` |
| `YT921X_FILTER_BCAST` (`0x180514`) | `0x00000400` | Stable/safe broadcast filter baseline on this board. | `0x00000400` |
| `YT921X_FILTER_MCAST/BCAST` (`0x180510/14`) | `0x000007ff` | Correlated with traffic blackhole in bad runtime profile. | set both back to `0x00000400` |
| `YT921X_ACT_UNK_UCAST` (`0x180734`) | `field(port)=00/01/10/11` | Per-port unknown-ucast action: flood / trap / drop / copy(unsafe). | board baseline `0x00020000` |
| `YT921X_ACT_UNK_MCAST` (`0x180738`) | `field(port)=00/01/10/11` | Per-port unknown-mcast action: flood / trap / drop / copy(unsafe). | board baseline `0x00420000` |
| `YT921X_CPU_COPY` (`0x180690`) | `0x00000001` | Copy/trap to external CPU path enabled (baseline). | `0x00000001` |
| `YT921X_FDB_HW_FLUSH` (`0x180958`) | `bit0=1` | Enable auto FDB flush on link-down events. | policy-dependent |
| `YT921X_PORTn_VLAN_CTRL(p)` (`0x230010 + 4*p`) | set CVID field: `value = (value & ~0x0003ffc0) | (VID << 6)` | Changes port PVID/CVID. Example: `VID=100` writes `0x1900` into `[17:6]`. | restore prior snapshot value |
| `YT921X_VLANn_CTRL(vid)` (`0x188000 + 8*vid`) | word0 membership bits + word1 untag bits | Programs VLAN member ports and egress-untag behavior for that VID. | restore original 2-word pair |
| `YT921X_STOCK_STORM_CONFIG` (`0x220200`) | `bit0=1` (`f4`) | Stock enables hardware storm engine on this control bit (from `fal_tiger_storm_ctrl_enable_set`). | keep other bits unchanged |
| `YT921X_STOCK_STORM_CONFIG` (`0x220200`) | `bit1=mode` (`f3`), `bit2=include_gap` (`f2`) | Stock mode/include-gap toggles map to these fields (`fal_tiger_storm_ctrl_rate_mode_set`, `...include_gap_set`). | preserve `f0/f1` |
| `YT921X_STOCK_LOOP_DETECT_TOP_CTRL` (`0x00080230`) | `f5=1`, `f6=tpid`, `f8=gen_way` | Stock loop-detect enable/tpid/generate-way path fields (`fal_tiger_loop_detect_*_set`). | preserve `f4/f3/f2` unless changing unit-id |
| `YT921X_FUNC` (`0x080004`) bit5 | set bit5 | Hazardous on CR881x runtime: `0xdeaddead` readback + management collapse. | avoid; reboot recovery |

## Read-Modify-Write Snippets (Mask/Set/Clear)
Use these to avoid clobbering unrelated bits.

| Register | Keep-mask / field | Set/Clear operation | Example |
|---|---|---|---|
| `YT921X_EXT_CPU_PORT` (`0x08000c`) | clear `GENMASK(3,0)` then set port id | `v = (v & ~0x0000000f) | port; v |= BIT(15) | BIT(14)` | set tagged CPU to `p8`: `port=8` -> `0x0000c008` |
| `YT921X_VLAN_IGR_FILTER` (`0x180280`) | per-port bit `BIT(port)` | enable: `v |= BIT(port)`; disable: `v &= ~BIT(port)` | enable `p0..p2`: `v |= 0x00000007` |
| `YT921X_PORTn_ISOLATION(src)` (`0x180294 + 4*src`) | per-dst bit `BIT(dst)` (negative logic) | block `src->dst`: `v |= BIT(dst)`; allow: `v &= ~BIT(dst)` | block `p0->p1`: `v=0x6f9 -> 0x6fb` |
| `YT921X_FILTER_UNK_UCAST` (`0x180508`) | ports field `[10:0]` | `v = (v & ~0x000007ff) | (mask & 0x7ff)` | send unknown-ucast to `p10`: `mask=0x400` |
| `YT921X_FILTER_UNK_MCAST` (`0x18050c`) | ports field `[10:0]` | `v = (v & ~0x000007ff) | (mask & 0x7ff)` | flood to `p0..p2,p8`: `mask=0x107` |
| `YT921X_FILTER_MCAST` (`0x180510`) | ports field `[10:0]` | `v = (v & ~0x000007ff) | (mask & 0x7ff)` | safe baseline: `mask=0x400` |
| `YT921X_FILTER_BCAST` (`0x180514`) | ports field `[10:0]` | `v = (v & ~0x000007ff) | (mask & 0x7ff)` | safe baseline: `mask=0x400` |
| `YT921X_CPU_COPY` (`0x180690`) | low bits `[2:0]` | ext CPU: `v |= BIT(0)`; int CPU: `v |= BIT(1)`; force-int: `v |= BIT(2)`; clear with `&= ~BIT(n)` | baseline ext-only: `v = (v & ~0x7) | 0x1` |
| `YT921X_ACT_UNK_UCAST` (`0x180734`) | per-port action field `2*port+1:2*port` | `v = (v & ~(0x3 << (2*port))) | (act << (2*port))` | `act=2` drops unknown-ucast on that port |
| `YT921X_ACT_UNK_MCAST` (`0x180738`) | per-port action field `2*port+1:2*port` + bypass bits 22/23 | action set as above; bypass IGMP: `v |= BIT(22)`; bypass RMA: `v |= BIT(23)` | baseline on CR881x: `0x00420000` |
| `YT921X_FDB_HW_FLUSH` (`0x180958`) | bit0 | enable link-down auto-flush: `v |= BIT(0)`; disable: `v &= ~BIT(0)` | set bit0 for automatic stale FDB cleanup |
| `YT921X_PORTn_VLAN_CTRL(p)` (`0x230010 + 4*p`) | CVID field `[17:6]`, SVID field `[29:18]` | CVID: `v = (v & ~0x0003ffc0) | ((cvid & 0xfff) << 6)`; SVID: `v = (v & ~0x3ffc0000) | ((svid & 0xfff) << 18)` | set `CVID=100`: inject `0x00001900` |
| `YT921X_PORTn_VLAN_CTRL1(p)` (`0x230080 + 4*p`) | range/drop bits `[8:0]` | enable range: `v |= BIT(8)`; profile: `v = (v & ~0x000000f0) | ((id & 0xf) << 4)`; drop bits set/clear individually | drop C-VLAN untagged: `v |= BIT(0)` |
| `YT921X_VLANn_CTRL(vid)` word0 (`0x188000 + 8*vid`) | member field `[17:7]` | `w0 = (w0 & ~0x0003ff80) | ((member_mask & 0x7ff) << 7)` | VLAN members `p0,p1,p8`: `mask=0x103` |
| `YT921X_VLANn_CTRL(vid)` word1 (`0x188004 + 8*vid`) | untag field `[18:8]` (maps global bits `[50:40]`) | `w1 = (w1 & ~0x0007ff00) | ((untag_mask & 0x7ff) << 8)` | untag egress on `p0,p1`: `mask=0x003` |
| `YT921X_MIRROR` (`0x300300`) | igr-src `[26:16]`, egr-src `[14:4]`, dst `[3:0]` | `v = (v & ~0x07ff7fff) | ((igr&0x7ff)<<16) | ((egr&0x7ff)<<4) | (dst&0xf)` | mirror ingress `p0` to `p8`: `igr=0x001,dst=8` |
| `YT921X_STOCK_STORM_CONFIG` (`0x220200`) | `f4:bit0`, `f3:bit1`, `f2:bit2`, `f1:[12:3]`, `f0:[31:13]` | `v=(v&~BIT(0))|en`; `v=(v&~BIT(1))|(mode<<1)`; `v=(v&~BIT(2))|(ig<<2)`; `v=(v&~GENMASK(12,3))|((f1&0x3ff)<<3)`; `v=(v&~GENMASK(31,13))|((f0&0x7ffff)<<13)` | stock uses table-api field IDs `4/3/2/1/0` |
| `YT921X_STOCK_STORM_MC_TYPE_CTRL` (`0x220140`) | mask `[10:0]` | `v = (v & ~0x000007ff) | (mask & 0x7ff)` | driven by stock table `0xc9` field `0` |
| `YT921X_STOCK_LOOP_DETECT_TOP_CTRL` (`0x00080230`) | `f5:bit18`, `f6:[17:2]`, `f8:bit0`, `f4:[20:19]`, `f3:[22:21]`, `f2:[24:23]` | `enable: v=(v&~BIT(18))|(en<<18)`; `tpid: v=(v&~GENMASK(17,2))|((tpid&0xffff)<<2)`; `gen: v=(v&~BIT(0))|(g<<0)`; unit-id: set `f4/f3/f2` | stock table id `0x0d` |
| `YT921X_STOCK_RATE_IGR_BW_ENABLE` (`0x220108`) | `enable:bit4`, `meter_id:[3:0]` | `v=(v&~BIT(4))|(en<<4); v=(v&~GENMASK(3,0))|(meter_id&0xf)` | current `port_policer` path uses this table (`tbl 0xc8`) |
| `YT921X_STOCK_RATE_IGR_BW_CTRL` (`0x220104`) | `meter_timeslot:[11:0]` | `v=(v&~GENMASK(11,0))|(timeslot&0xfff)` | current `port_policer` path uses this table (`tbl 0xc7`) |
| `YT921X_STOCK_RATE_METER_CONFIGn(n)` (`0x220800+0x10*n`) | decoded fields across words 0/1/2 | set with field RMW per decoded map (`meter_config_tblm_field` entries 0..13) | current `port_policer` path uses this table (`tbl 0xce`) |
| `YT921X_STOCK_QOS_QUEUE_MAP_UCASTn(port)` (`0x300200+4*port`) | 8 slots, each `3-bit` (`prio0..7`) | `v = Σ((qid[i] & 0x7) << shift_i)` where shifts=`28,24,20,16,12,8,4,0` | current `mqprio` offload programs this |
| `YT921X_STOCK_QOS_QUEUE_MAP_MCASTn(port)` (`0x300280+4*port`) | 8 slots, each `2-bit` (`prio0..7`) | `v = Σ((qid[i] & 0x3) << shift_i)` where shifts=`14,12,10,8,6,4,2,0` | current driver mirrors ucast map into this table |
| `YT921X_STOCK_QOS_SCHED_SP` / `DWRR*` (`0x300400`, `0x341000..0x343000`) | scheduler policy fields (driver-wired subset) | flow-indexed tables: `idx = port*12 + qid` (ucast), `idx = port*12 + 8 + qid` (mcast). `0xe6` decoded as `f0[27:18], f1[17:8], f2[7:4], f3[3:0]`; `0xe7/0xe8` have `bit0` cfg | current `ets`/`mqprio` programs safe subset; full stock parity still pending |
| `YT921X_STOCK_QOS_QSCH_SLOT_TIME` / `SHAPER` / `METER` (`0x340008`, `0x34c000`, `0x34f000`) | queue shaper fields | slot-time and per-flow shaper words are programmed by queue `tc tbf`; meter token field in `0xea` currently set/reset in paired path | queue `tbf` path is active; non-token meter fields remain low-confidence |
| `YT921X_STOCK_QOS_REMARK_PORT_CTRL` / `DSCP` / `CPRI_SPRI` (`0x100000`, `0x100100`, `0x100200`) | remark control and maps | `0xd9` enables CPRI/SPRI remark per port; `0xdb` sets DSCP remark value; `0xdc` sets PCP remark + enable bit | initialized by driver defaults; `0xda` policy table still not actively programmed |

## Notes And Usage Links
Use these for full procedure, A/B deltas, and raw captures.

- Changelog:
  - `docs/yt921x/yt9215-register-map-changelog.md`
- Port identity and role mapping:
  - `docs/yt921x/live/yt_port_identity_map_cr881x_2026-03-29.md`
- Isolation matrix directional proof:
  - `docs/yt921x/live/yt_180294_host2host_directional_pulse_summary_2026-03-29.md`
  - `docs/yt921x/live/yt_port_isolation_directional_check_2026-03-23.md`
- VTU/PVID and VLAN behavior:
  - `docs/yt921x/live/yt_vlan_vtu_stride_pvid_probe_fix_summary_2026-03-24.md`
  - `docs/yt921x/live/yt_port_vlan_ctrl_full_ports_vf_toggle_2026-03-29.md`
  - `docs/yt921x/live/yt_egress_tagging_vtu_vs_ctrl1_2026-03-29.md`
  - `docs/yt921x/live/yt_br_wan_bind_pvid_probe_2026-03-29.md`
- CPU copy / UU / MC action probes:
  - `docs/yt921x/live/yt_cpu_copy_180690_sweep_2026-03-29.md`
  - `docs/yt921x/live/yt_uu_cpu8_action_matrix_2026-03-29.md`
  - `docs/yt921x/live/yt_bum_boundary_probe_uu_2026-03-29.md`
- Gated windows and gate-candidate sweeps:
  - `docs/yt921x/live/yt_gated_stock_map_probe_20260319_063619.txt`
  - `docs/yt921x/live/yt_gate_candidate_18028c_1803cc_probe_summary_2026-03-24.md`
  - `docs/yt921x/live/yt_80004_combo_gate_probe_bits8_10_11_12_summary_2026-03-24.md`
- Table-id/window scans and MCU/MIB follow-up:
  - `docs/yt921x/live/yt_tbl_info_id_scan_2026-03-29.md`
  - `docs/yt921x/live/yt_p10_mcu_trap_mib_ab_uart_2026-03-30.md`
- Stock reverse (table-id decode + disassembly mapping):
  - `docs/yt921x/yt9215-stock-behavior-reverse-2026-03-30.md`
