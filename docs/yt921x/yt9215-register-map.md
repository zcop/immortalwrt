# YT9215 Register Map (CR881x Baseline)

## Scope
This is the current mapped view for CR881x from live switch reads.

Primary data:
- `docs/yt921x/live/yt_regmap_live_cr881x_20260316_182545.txt`
- `docs/yt921x/live/yt_regmap_live_cr881x_20260316_full_chunked_ssh.txt`
- `docs/yt921x/live/yt_regmap_live_cr881x_20260316_195556_postfix_recovery.txt`
- `docs/yt921x/live/yt_1803xx_probe_chunked_20260319_054624.txt`
- `docs/yt921x/live/yt_18038c_combo_probe_20260319_055738.txt`
- `docs/yt921x/live/yt_1803xx_membership_probe_20260319_061341.txt`
- `docs/yt921x/live/yt_18028x_1803cx_membership_probe_20260319_061424.txt`
- `docs/yt921x/live/yt_180390_write_probe_20260319_061654.txt`
- `docs/yt921x/live/yt_gated_stock_map_probe_20260319_063619.txt`
- `docs/yt921x/live/yt_gated_write_read_probe_20260319_063928.txt`
- `docs/yt921x/live/yt_gated_write_side_effect_probe_20260319_063944.txt`
- `docs/yt921x/live/yt_gated_admin_toggle_probe_20260319_064228.txt`
- `docs/yt921x/live/yt_18030c_write_probe_20260319_064402.txt`
- `docs/yt921x/live/yt_18030x_mask_probe_20260319_064433.txt`
- `docs/yt921x/live/yt_18030c_334_full_mask_probe_20260319_064504.txt`
- `docs/yt921x/live/yt_18030c_persistence_membership_probe_20260319_064550.txt`
- `docs/yt921x/live/yt_18030c_bit_coupling_probe_20260319_065150.txt`
- `docs/yt921x/live/yt_18030x_word_coupling_ping_probe_20260319_065753.txt`
- `docs/yt921x/live/yt_18030x_all_ones_table_probe_20260319_065951.txt`
- `docs/yt921x/live/yt_18030x_live_toggle_from_101_20260319_071411.txt`
- `docs/yt921x/live/yt_18030x_live_toggle_from_101_warmfix_20260319_071530.txt`
- `docs/yt921x/live/yt_post_flash_sanity_busybox_20260319_082647.txt`
- `docs/yt921x/live/yt_18030x_vlan_mcast_tcp_probe_20260319_084804.txt`
- `docs/yt921x/live/yt_18030x_ipfull_neigh_probe_20260319_085513.txt`
- `docs/yt921x/live/yt_18030x_bridge_fdb_probe_20260319_085923.txt`
- `docs/yt921x/live/yt_18038c_stp_validate_20260319_092708.txt`
- `docs/yt921x/live/yt_18038c_stp_sysfs_probe_20260319_092809.txt`
- `docs/yt921x/live/yt_18038c_postbuild_test_20260319_094407.txt`
- `docs/yt921x/live/yt_gate_sweep_lowbits_20260319_135946.txt` (parser-syntax mismatch run; superseded)
- `docs/yt921x/live/yt_80014_lowbit_stage1_20260319_130039.txt`
- `docs/yt921x/live/yt_80014_lowbit_stage2_20260319_130132.txt`
- `docs/yt921x/live/yt_80004_lowbit_stage1_20260319_130230.txt`
- `docs/yt921x/live/yt_80004_lowbit_stage2_20260319_130318.txt`
- `docs/yt921x/live/yt_18028c_blackhole_probe_20260319_110516.txt`
- `docs/yt921x/live/yt_180510_180514_probe_20260319_111723.txt`
- `docs/yt921x/live/yt_180310_bit_coupling_probe_20260324_154311.txt`
- `docs/yt921x/live/yt_180314_bit_coupling_probe_20260324_154902.txt`
- `docs/yt921x/live/yt_180318_bit_coupling_probe_20260324_155116.txt`
- `docs/yt921x/live/yt_18031c_bit_coupling_probe_20260324_161111.txt`
- `docs/yt921x/live/yt_180320_bit_coupling_probe_20260324_091453.txt`
- `docs/yt921x/live/yt_180324_bit_coupling_probe_20260324_091516.txt`
- `docs/yt921x/live/yt_180328_bit_coupling_probe_20260324_091712.txt`
- `docs/yt921x/live/yt_18032c_bit_coupling_probe_20260324_091713.txt`
- `docs/yt921x/live/yt_180330_bit_coupling_probe_20260324_091714.txt`
- `docs/yt921x/live/yt_180334_bit_coupling_probe_20260324_091715.txt`
- `docs/yt921x/live/yt_vlan_membership_probe_wan_vid10_20260324_161032.txt`
- `docs/yt921x/live/yt_vlan_filtering_probe_wan_vid10_20260324_161244.txt`
- `docs/yt921x/live/yt_gated_window_recheck_80014_bit0_20260324_154715.txt`
- `docs/yt921x/live/yt_gate_candidate_18028c_1803cc_probe_20260324_092049.txt`
- `docs/yt921x/live/yt_80004_combo_gate_probe_bits8_10_11_12_20260324_092541.txt`
- `docs/yt921x/live/yt_vlan_ctrl_decode_probe_wan_vid10_20260324_092626.txt`
- `docs/yt921x/live/yt_vlan_vtu_stride_pvid_probe_fix_20260324_093111.txt`
- `docs/yt921x/live/yt_pvid_field_sensitivity_wan_v2_20260324_093322.txt`
- `docs/yt921x/live/yt_tbf_tc_offload_probe_wan_20260324_094055.txt`
- `docs/yt921x/live/yt_multicast_mdb_probe_snoop_on_20260324_094305.txt`
- `docs/yt921x/live/yt_bum_storm_candidate_probe_20260325_1345.md`
- `docs/yt921x/live/yt_global_gate_sweep_180500_355000_20260325_143350.txt`
- `docs/yt921x/live/yt_mib_map_broadcast_20260325_1450.txt`
- `docs/yt921x/live/yt_ab_1805d4_mib_broadcast_20260325_1450.txt`
- `docs/yt921x/live/yt_uu_ab_candidates_20260325_1500.txt`
- `docs/yt921x/live/yt_uu_ab_dump_mib_20260325_1505.txt`
- `docs/yt921x/live/yt_port_identity_map_cr881x_2026-03-29.md`
- `docs/yt921x/yt9215-register-actionability-map-2026-03-17.md`
- Driver symbols in `target/linux/generic/backport-6.12/830-02-v6.19-net-dsa-yt921x-Add-support-for-Motorcomm-YT921x.patch`
- UART transition run:
  - `docs/yt921x/live/yt_link_event_monitor_20260316_210441_uart_transitions_final.txt`

Confidence key:
- `High`: symbol + behavior + live value all align.
- `Medium`: symbol aligns, behavior still needs delta tests.
- `Low`: currently only positional hint.

## Port Identity Map (CR881x, finalized 2026-03-29)
- User ports:
  - `p0=lan1`, `p1=lan2`, `p2=lan3`, `p3=wan`
- CPU conduits:
  - `p8=cpu1` (primary, `eth1`)
  - `p4=cpu2` (secondary, `eth0`)
- Internal-only:
  - `p10=mcu`

Evidence summary:
- DTS binds `port@2` to `lan3`, `port@4` to `dp1/eth0`, `port@8` to `dp2/eth1`.
- Driver picks `port8` as primary CPU when present and keeps `port10` in MCU
  drop-mask handling.
- Runtime DSA port names confirm `lan3 -> p2` and `wan -> p3`; conduit behavior
  remains consistent with `cpu1=p8`, `cpu2=p4`.

## Block Map
| Range | Current mapping | Confidence |
|---|---|---|
| `0x080000-0x08003f` | global core control (`RST/FUNC/CHIP_ID/CPU_PORT/TPID`) | High |
| `0x080080-0x080090` | MAC base + per-serdes lane register (`SERDESn`) | High |
| `0x080100-0x08012b` | per-port MAC control (`PORTn_CTRL`) | High |
| `0x080200-0x08022b` | per-port MAC status (`PORTn_STATUS`) | High |
| `0x080320-0x080328` | strap function/value/capture | Medium |
| `0x080364-0x080368` | serdes MDIO polling summary | Medium |
| `0x08036c-0x080374` | sensor + temperature registers | Medium |
| `0x080388-0x080394` | chip mode + XMII global control | High |
| `0x080400+` | per-XMII port config (`XMIIn`) | High |
| `0x081008+` | per-port frame-size control (`MACn_FRAME`) | Medium |
| `0x0a0000+` | per-port EEE value latch (`EEEn_VAL`) | Medium |
| `0x0b0000` | global EEE control (`EEE_CTRL`) | Medium |
| `0x06a000-0x06a00c` | external MBUS command window | High |
| `0x0f0000-0x0f000c` | internal MBUS command window | High |
| `0x354000-0x354024` | PSCH per-port shaper window (`SHP[0..4]`) | High |
| `0x0c0100+` | per-port MIB data window (`MIBn_DATA*`) | High |
| `0x100000-0x100328` | egress TPID/profile area (`PORTn_EGR`,`TPID_EGRn`) | Medium |
| `0x180000-0x18022c` | DSCP/PCP priority maps + per-port QoS/prio order | High |
| `0x180280-0x180470` | VLAN/isolation/learning/ageing/FDB op | High |
| `0x1802c0-0x180308` | gated sub-window (`0xdeadbeef`) | Low |
| `0x18030c-0x180334` | writable 11-word table with per-word `0x7ff` mask | Medium |
| `0x180338-0x180388` | gated sub-window (`0xdeadbeef`) | Low |
| `0x18038c` | `YT921X_STPn(0)` active STP-instance control word | High |
| `0x180390-0x1803bc` | `YT921X_STPn(1..12)` inactive STP-instance bank (defaults zero) | High |
| `0x1804b0-0x1804b8` | FDB output/result payload window (`FDB_OUT*`) | Medium |
| `0x180500-0x18053c` | unknown L2 policy/control window (mostly zero in sampled runtime) | Low |
| `0x180510-0x180514` | mcast/bcast filter masks | Medium |
| `0x180598-0x1805cc` | VLAN egress filter + LAG group/member tables | Medium |
| `0x1805d0-0x18068c` | unknown mask matrix (mostly `0x3f`) | Low |
| `0x1806b8-0x1806bc` | unknown threshold/mode pair (`0x7ff` / `0x10`) | Low |
| `0x180690` | CPU copy policy (`CPU_COPY`) | High |
| `0x180734-0x180738` | unknown L2 action profile (`ACT_UNK_*`) | High |
| `0x180958` | FDB HW flush policy | Medium |
| `0x188000+` | VID-indexed VLAN table (`VLANn_CTRL`) | High |
| `0x210000-0x210090` | ingress TPID and LAG hash selector | High |
| `0x230010-0x230080` | per-port VLAN default/drop control | High |
| `0x300300` | mirror control register | High |
| `0x355000-0x355028` | unknown QoS-neighbor island (all zero in sampled runtime) | Low |
| `0x0c0004` | MIB control | Medium |
| `0x0e0000-0x0e0004` | EDATA control/status | Medium |

## Key Registers (Live Values)
| Address | Symbol | Live value | Decode / meaning | Confidence |
|---|---|---|---|---|
| `0x080008` | `YT921X_CHIP_ID` | `0x90020001` | chip ID register valid | High |
| `0x08000c` | `YT921X_EXT_CPU_PORT` | `0x0000c008` | single tagged CPU-port selector (`TAG_EN=1`, `PORT_EN=1`, primary CPU port=`8`); secondary CPU conduit uses plain Ethernet path (no YT tag) | High |
| `0x080010` | `YT921X_CPU_TAG_TPID` | `0x00009988` | default Motorcomm CPU tag TPID | High |
| `0x080004` | `YT921X_FUNC` | `0x0000080b` (baseline) | low-bit sweep: bits `23..13` ignored, bit `12` latches, bits `11/10/8` latch, bits `9/7/6` ignored; toggling bit `5` drives `0xdeaddead` readback and management-plane collapse until reboot; safe combo sweep of bits `{8,10,11,12}` did not unlock gated windows | High |
| `0x080014` | `YT921X_PVID_SEL` | `0x00000000` (baseline) | low-bit sweep: bits `15..11` ignored; bits `10..0` latch and restore; no gated-window opening observed | High |
| `0x080028` | `YT921X_SERDES_CTRL` | `0x00000041` | serdes global control bits set | Medium |
| `0x080388` | `YT921X_CHIP_MODE` | `0x00000002` | chip mode profile is non-zero active mode | High |
| `0x080394` | `YT921X_XMII_CTRL` | `0x00000000` | no extra global XMII override bits | Medium |
| `0x080400` | `YT921X_XMIIn(8)` | `0x04184108` | per-port XMII config programmed | High |
| `0x080408` | `YT921X_XMIIn(9)` | `0x04184108` | per-port XMII config programmed | High |
| `0x080364` | `YT921X_MDIO_POLLINGn(8)` | `0x0000001a` | link=`1`, full duplex=`1`, speed code=`2` | High |
| `0x080368` | `YT921X_MDIO_POLLINGn(9)` | `0x00000012` | link=`0`, full duplex=`1`, speed code=`2` | High |
| `0x06a004` | `YT921X_EXT_MBUS_CTRL` | `0x00000900` | ext MBUS last command encoding (volatile latch) | High |
| `0x0f0004` | `YT921X_INT_MBUS_CTRL` | `0x006a0908` | int MBUS last command encoding (volatile latch) | High |
| `0x354000` | `YT921X_PSCH_SHPn_EBS_EIR(0)` | tc-validated | shaper tuple for port 0 (`EIR[17:0]`, `EBS[31:18]`) | High |
| `0x354004` | `YT921X_PSCH_SHPn_CTRL(0)` | tc-validated | shaper control (`EN`,`DUAL_RATE`,`METER_ID`) | High |
| `0x354008-0x354024` | `YT921X_PSCH_SHPn_* (1..4)` | tc-validated | same layout for ports 1..4 (stride 8 bytes/port); live `tc tbf` offload on `wan` (port3) mapped to `0x354018/0x35401c` with expected EIR/EBS scaling | High |
| `0x180280` | `YT921X_VLAN_IGR_FILTER` | dynamic (`0x00000000` / `0x0000000f`) | with `bridge vlan_filtering=0` readback is `0x00000000`; enabling `vlan_filtering=1` toggles low nibble to `0x0000000f` and returns to `0x00000000` when disabled | Medium |
| `0x188050` | `YT921X_VLANn_CTRL(10)` word0 | dynamic (`0x00000000` / `0x00000c80`) | VID10 tagged-membership word toggles when adding/removing VID10 on `wan` + `lan1` (+ `br-lan self`) | High |
| `0x188054` | `YT921X_VLANn_CTRL(10)` word1 | dynamic (`0x00000000` / `0x00000100`) | toggles when `lan1` on VID10 flips tagged -> untagged, consistent with egress-untag mask bits | High |
| `0x188058` | `YT921X_VLANn_CTRL(11)` word0 | dynamic (`0x00000000` / `0x00000c00`) | VID11 table word toggles on VID11 add/remove; confirms 8-byte VTU stride (`VID10@0x188050`, `VID11@0x188058`) | High |
| `0x230010-0x23001c` | `YT921X_PORTn_VLAN_CTRL(0..3)` | dynamic (`0xc007ffc0` / `0xc0040040`) | flips to `0xc0040040` when `br-lan vlan_filtering=1` and restores on disable; confirms active per-port VLAN mode control coupling | High |
| `0x23001c` | `YT921X_PORTn_VLAN_CTRL(3)` (WAN in this mapping) | dynamic | WAN PVID sensitivity maps as `PVID << 6` in this word (`20->0x0500`, `21->0x0540`, `30->0x0780`, `100->0x1900`) | High |
| `0x230080-0x2300a8` | `YT921X_PORTn_VLAN_CTRL1(0..10)` | `0x00000000` (sampled) | remained zero across the `vlan_filtering` and VID10 add/del probe sequence | Medium |
| `0x180294-0x1802bc` | `YT921X_PORTn_ISOLATION(n=0..10)` | dynamic | directional per-source destination block mask (`bit d` blocks `src n -> dst d`) used by bridge/conduit steering; low 11 bits are active while unrelated upper bits are preserved. 2026-03-29 host-to-host confirmation (`lan1`/`lan2` up): pulsing `0x180294: 0x6f9->0x6fb` and `0x180298: 0x6fa->0x6fb` induced repeatable LAN horizontal loss (~25-29%) while simultaneous host->router control stayed `0%` loss | High |
| `0x18028c` | unknown (portmask-like, pre-`PORTn_ISOLATION`) | `0x000007ff` | writable (`0x7ff <-> 0x0`); single-host LAN->router probe (`192.168.2.100 -> 192.168.2.1`) showed 0% ICMP loss, and one-hot gate-candidate sweeps did not unlock `0xdeadbeef` windows | Medium |
| `0x18030c-0x180334` | unknown writable table (11 words) | baseline `0x00000000` | each word is writable with mask `0x000007ff`; values persist across bridge-membership toggles; per-word bit-coupling sweeps (`0x180310..0x180334`) showed no immediate deltas in sampled isolation/STP/learn words | Medium |
| `0x1803cc` | unknown (portmask-like) | `0x000007ff` | all 11 ports set in baseline; one-hot gate-candidate sweeps did not unlock `0xdeadbeef` windows | Low |
| `0x180734` | `YT921X_ACT_UNK_UCAST` | `0x00020000` | unknown-unicast action profile; static MDB add/del probe showed no direct delta | High |
| `0x180738` | `YT921X_ACT_UNK_MCAST` | `0x00020000` | unknown-multicast action profile; static MDB add/del probe showed no direct delta | High |
| `0x18038c` | `YT921X_STPn(0)` | `0x000300f3` (earlier), `0x003cfc0c` (post-build flash) | active STP instance 0 per-port state word; bridge membership/state changes update low-byte fields in-place: `lan2` `[3:2]` (`0x...0c -> 0x...00` when removed), `lan3` `[5:4]` (`0x...0c -> 0x...3c` on re-add), `wan` `[7:6]` (`0x...3c -> 0x...fc` on add) | High |
| `0x180390` | `YT921X_STPn(1)` | `0x00000000` | inactive STP instance bank default; direct write/readback verified (`0 -> 0x3 -> 0`) | High |
| `0x1803d0` | `YT921X_PORTn_LEARN(0)` | `0x00000000` | learn defaults for lower ports | Medium |
| `0x180510` | `YT921X_FILTER_MCAST` | `0x00000400` (safe baseline) | 11-bit filter mask; `0x00000400` and `0x00000000` both passed single-host tests, while `0x000007ff` was observed in a bad runtime and correlated with host->router blackhole until restored to `0x00000400`; static MDB add/del test showed no direct delta here | High |
| `0x180514` | `YT921X_FILTER_BCAST` | `0x00000400` (safe baseline) | 11-bit filter mask; `0x00000400` and `0x00000000` both passed single-host tests, while `0x000007ff` was observed in a bad runtime and correlated with host->router blackhole until restored to `0x00000400`; static MDB add/del test showed no direct delta here | High |
| `0x180500` | unknown (global-policy candidate) | `0x00000000` | low-bit sweep (`bit0..bit7`) under synchronized broadcast load showed baseline-equivalent `lan1_rx` deltas for all trials; no measurable gating effect, restored to baseline after each trial | Low |
| `0x1805d0..0x18068c` | unknown mask matrix | mostly `0x0000003f` (`0x1805d4`/`0x1805d8`=`0x0000023f`) | synchronized high-rate UDP broadcast probe kept these words static; A/B on `0x1805d0` (`0x3f` vs `0x3e`) and timed toggles on `0x18068c` showed no measurable broadcast policing effect on `lan1_rx`. 2026-03-29 host-to-host pulses (`0x1805d4`/`0x1805d8` to `0x0`) also did not cut known-unicast LAN1<->LAN2 traffic | Low |
| `0x1806b8` | unknown threshold-like word | `0x000007ff` | A/B test (`0x7ff` vs `0x0ff`) under identical broadcast bursts produced near-identical `lan1_rx` deltas (no observed limiter effect) | Low |
| `0x1806bc` | unknown mode/selector-like word | `0x00000010` | static during synchronized broadcast probe | Low |
| `0x355000` | unknown (QoS/global-enable candidate) | `0x00000000` | low-bit sweep (`bit0..bit7`) under synchronized broadcast load showed baseline-equivalent `lan1_rx` deltas for all trials; no measurable gating effect, restored to baseline after each trial | Low |
| `0x0c0100`, `0x0c0130`, `0x0c013c`, `0x0c0184` | unknown MIB words (signal set) | dynamic under load | in sampled `0x0c0100..0x0c01fc` window these were strongest stress-path signals during ~3s broadcast flood; `0x1805d4` bit9 A/B (`0x23f -> 0x3f`) did not produce extra drop-like deltas beyond send-volume scaling | Medium |
| `0x180440` | `YT921X_AGEING` | `0x0000003c` | ageing interval=`0x003c` | High |
| `0x180454` | `YT921X_FDB_IN0` | `0xccd84354` | active FDB input/result window | Medium |
| `0x180458` | `YT921X_FDB_IN1` | `0x70011c7a` | active FDB input/result window | Medium |
| `0x18045c` | `YT921X_FDB_IN2` | `0x04000000` | active FDB input/result window | Medium |
| `0x180464` | `YT921X_FDB_RESULT` | `0x00008f05` | `DONE=1`, `INDEX=0x0f05` | Medium |
| `0x0c0004` | `YT921X_MIB_CTRL` | `0x00000001` | MIB logic enabled/running (`bit31` seen as transient latch) | Medium |
| `0x0e0004` | `YT921X_EDATA_DATA` | `0x00000003` | EDATA status idle | Medium |

## Driver Symbol Coverage Added (was missing in map)
These addresses are explicitly defined and used by the current driver, and are now
tracked here even where we do not yet have a stable board baseline value dump.

| Address / range | Driver symbol(s) | Purpose |
|---|---|---|
| `0x08036c` | `YT921X_SENSOR` | sensor global control bits |
| `0x080374` | `YT921X_TEMP` | temperature readout register |
| `0x81008 + 0x1000*n` | `YT921X_MACn_FRAME` | per-port max frame size control |
| `0x0a0000 + 0x40*n` | `YT921X_EEEn_VAL` | per-port EEE status/value latch |
| `0x0b0000` | `YT921X_EEE_CTRL` | global EEE enable bitmap |
| `0x1804b0..0x1804b8` | `YT921X_FDB_OUT0/1/2` | FDB entry payload decode window |
| `0x180510`, `0x180514` | `YT921X_FILTER_MCAST`, `YT921X_FILTER_BCAST` | per-port multicast/broadcast filter masks (`bit10` stock-safe baseline) |
| `0x180598..0x1805cc` | `YT921X_VLAN_EGR_FILTER`, `YT921X_LAG_GROUP*`, `YT921X_LAG_MEMBER*` | VLAN egress and LAG tables |
| `0x180690` | `YT921X_CPU_COPY` | trap/copy routing toward CPU ports |
| `0x180958` | `YT921X_FDB_HW_FLUSH` | link-down automatic flush policy |
| `0x188000 + 8*VID` | `YT921X_VLANn_CTRL` | hardware VLAN membership/attributes |
| `0x210000..0x210090` | `YT921X_TPID_IGRn`, `YT921X_PORTn_IGR_TPID`, `YT921X_LAG_HASH` | ingress TPID selection and LAG hash keys |
| `0x230010..0x230080` | `YT921X_PORTn_VLAN_CTRL*` | per-port default VID/PCP and ingress drop policy |
| `0x300300` | `YT921X_MIRROR` | ingress/egress mirroring source+destination |

### Bitfield Notes For Newly Added Blocks
- `0x08036c` (`YT921X_SENSOR`)
  - `bit18` => `YT921X_SENSOR_TEMP` (temperature/sensor logic enable path).
- `0x81008 + 0x1000*n` (`YT921X_MACn_FRAME`)
  - `bits[21:8]` => `YT921X_MAC_FRAME_SIZE_M` (max frame size field).
- `0x0a0000 + 0x40*n` (`YT921X_EEEn_VAL`)
  - `bit1` => `YT921X_EEE_VAL_DATA`.
- `0x0b0000` (`YT921X_EEE_CTRL`)
  - `bit n` => `YT921X_EEE_CTRL_ENn(n)` per-port EEE enable bitmap.
- `0x1804b0..0x1804b8` (`YT921X_FDB_OUT0/1/2`)
  - `OUT1 bits[30:28]` => entry status.
  - `OUT1 bits[27:16]` => `FID`/VID.
  - `OUT2 bits[28:18]` => egress port mask.
  - `OUT2 bit16` => copy-to-CPU flag.
  - `OUT2 bits[14:12]` => entry priority.
- `0x180598..0x1805cc` (`VLAN_EGR_FILTER`, `LAG_GROUP*`, `LAG_MEMBER*`)
  - `VLAN_EGR_FILTER bit n` => per-port egress VLAN filter enable.
  - `LAG_GROUP ports[13:3]` + member count `[2:0]`.
  - `LAG_MEMBER` low nibble => physical port id.
- `0x180690` (`YT921X_CPU_COPY`)
  - `bit2` => force internal CPU port path.
  - `bit1` => copy to internal CPU.
  - `bit0` => copy to external CPU.
- `0x180958` (`YT921X_FDB_HW_FLUSH`)
  - `bit0` => flush behavior on link-down.
- `0x210000..0x210090`
  - `TPID_IGRn` and `PORTn_IGR_TPID` select TPID profiles for ingress tag parsing.
  - `0x210090` (`YT921X_LAG_HASH`) controls hash key include bits:
    - MAC SA/DA, IP src/dst, L4 sport/dport, IP protocol, source port.
- `0x230010..0x230080` (`PORTn_VLAN_CTRL`, `PORTn_VLAN_CTRL1`)
  - `PORTn_VLAN_CTRL` carries default `SVID/CVID` and default PCP fields.
  - `PORTn_VLAN_CTRL1` carries tagged/untagged drop controls and VLAN-range profile.
- `0x300300` (`YT921X_MIRROR`)
  - ingress source mask in `[26:16]`, egress source mask in `[14:4]`, mirror destination in `[3:0]`.

## Port Control/Status Snapshot
`PORTn_CTRL` (`0x80100 + 4*n`), ports `0..10`:
- `p0=0x000007fa`, `p1=0x000000fa`, `p2=0x000007fa`, `p3=0x000007fa`, `p4=0x000007fa`
- `p5=0x000007fa`, `p6=0x000007fa`, `p7=0x000007fa`, `p8=0x000000fa`, `p9=0x000007fa`, `p10=0x000007fa`

`PORTn_STATUS` (`0x80200 + 4*n`), ports `0..10`:
- `p0=0x000000e2`, `p1=0x000001fa`, `p2=0x000000e2`, `p3=0x000000e2`, `p4=0x000001fa`
- `p5=0x000000e2`, `p6=0x000000e2`, `p7=0x000000e2`, `p8=0x000001fa`, `p9=0x000000e2`, `p10=0x000001fa`

Latest UART baseline (2026-03-29, per-port query via
`/sys/kernel/debug/yt921x_cmd`, capture:
`docs/yt921x/live/yt_uart_port_status_isolation_snapshot_2026-03-29.md`):
- `PORTn_CTRL`:
  - `p0=0x000007fa`, `p1=0x0000079a`, `p2=0x000007fa`, `p3=0x000007fa`, `p4=0x000005fa`
  - `p5=0x000007fa`, `p6=0x000007fa`, `p7=0x000007fa`, `p8=0x000005fa`, `p9=0x000007fa`, `p10=0x000007fa`
- `PORTn_STATUS`:
  - `p0=0x000001fa`, `p1=0x0000019a`, `p2=0x000000e2`, `p3=0x000000e2`, `p4=0x000001fa`
  - `p5=0x000000e2`, `p6=0x000000e2`, `p7=0x000000e2`, `p8=0x000001fa`, `p9=0x000000e2`, `p10=0x000001fa`

Interpretation note:
- Only part of status/control bit semantics is confirmed; values are stable and mapped, but exact link-bit behavior still needs controlled plug/unplug delta tests.

Post-fix live signature (2026-03-16 19:55 snapshot):
- Link-up style `PORTn_STATUS` value on this board state is `0x000001fa` (seen on ports `1`, `4`, `8`, `10`).
- Link-down style `PORTn_STATUS` value is `0x000000e2` (seen on ports `0`, `2`, `3`, `5`, `6`, `7`, `9`).
- Internal PHY `BMSR` (`int read <port> 0x1`) tracks link state for copper PHY ports:
  - link-up pattern: `0x796d` (ports `1`, `4`)
  - link-down pattern: `0x7949` (ports `0`, `2`, `3`)
- Internal PHY reg `0x11` also splits active/inactive PHYs in this state:
  - active: non-zero (`0xbc4c` on port `1`, `0xbc0c` on port `4`)
  - inactive: `0x0000` (ports `0`, `2`, `3`)

Controlled UART cable-transition deltas (2026-03-16, lan1/lan3/wan):
- For user ports `p0` (lan1), `p2` (lan3), `p3` (wan):
  - `PORTn_STATUS` toggles exactly between:
    - up: `0x000001fa`
    - down: `0x000000e2`
  - observed status delta mask: `0x00000118`
- Internal PHY `BMSR` (`int read <p> 0x1`) transitions lead carrier/status by about `3-4s`:
  - up sequence: `0x7949 -> 0x796d` before netdev carrier/status up
  - down sequence: `0x796d -> 0x7949` before netdev carrier/status down
- Practical mapping result:
  - `BMSR` is a faster PHY-level indication.
  - `PORTn_STATUS` is a MAC/port-state indication that follows after driver/phylib state propagation.

## L2 Tables Snapshot
`PORTn_ISOLATION` (`0x180294 + 4*n`, ports `0..10`):
- Historical baseline dump (2026-03-16/19 captures):
  - `p0=0x6f9`, `p1=0x6fa`, `p2=0x6fc`, `p3=0x6ff`, `p4=0x6ff`, `p5=0x6ff`
  - `p6=0x6ff`, `p7=0x6ff`, `p8=0x7ff`, `p9=0x6ff`, `p10=0x6ff`
- Current conduit-aware runtime signatures (2026-03-23 probes):
  - `wan@eth1` profile:
    - `0x1802a0=0x000006ff` (`p3`)
    - `0x1802a4=0x000006e8` (`p4`)
    - `0x1802b4=0x000007f7` (`p8`)
    - `0x1802b8=0x000006ef` (`p9`)
  - `wan@eth0` profile:
    - `0x1802a0=0x000007ef` (`p3`)
    - `0x1802a4=0x000006e0` (`p4`)
    - `0x1802b4=0x000007ff` (`p8`)
    - `0x1802b8=0x000006ef` (`p9`)
- Directional semantics validated:
  - `0x1802a0 bit4` blocks `port3 -> port4`
  - `0x1802a4 bit3` blocks `port4 -> port3`

Conduit-switch signatures (`wan` port 3 moved between CPU conduits):
- `wan@eth1` (primary conduit): `0x1802a0=0x000006ff`,
  `0x1802a4=0x000006e8`, `0x1802b4=0x000007f7`,
  `0x1802b8=0x000006ef`, `0x08000c=0x0000c008`
- `wan@eth0` (secondary conduit): `0x1802a0=0x000007ef`,
  `0x1802a4=0x000006e0`, `0x1802b4=0x000007ff`,
  `0x1802b8=0x000006ef`, `0x08000c=0x0000c004`
  - key invariant on current CR881x mapping: both directions must be open
  (`port3<->port4`) for ARP/ICMP to pass on the secondary conduit.
- row identity anchors used by current driver/runtime:
  - `lan3 (p2) -> 0x18029c`
  - `cpu2 (p4, eth0) -> 0x1802a4`
  - `cpu1 (p8, eth1) -> 0x1802b4`
  - `mcu (p10) -> 0x1802bc`
- Latest UART baseline (2026-03-29):
  - `p0=0x000006f9`, `p1=0x000006fa`, `p2=0x000006fc`, `p3=0x000007ef`, `p4=0x000007e7`
  - `p5=0x000006ef`, `p6=0x000006ef`, `p7=0x000006ef`, `p8=0x000007f8`, `p9=0x000006ef`, `p10=0x000006ef`

`PORTn_LEARN` (`0x1803d0 + 4*n`, ports `0..10`):
- `p0=0x00000000`, `p1=0x00000000`, `p2=0x00000000`
- `p3..p10=0x00020000` (`PORT_LEARN_DIS` set)

## MDIO Responder Map
Internal C22 (`int read`):
- coherent PHY responders: ports `0..4` (`ID 0x01e0:0x4281`)
- zero responder: ports `5..7`
- non-standard placeholder pattern (`0x1140` repeated): ports `8..9`

External C22 (`ext read`):
- scanned ports `0..31`, regs `0..31`: all zeros in this board state
- conclusion: no discovered external PHY responder on CR881x baseline wiring/runtime

## Gated / Unmapped Windows (observed `0xdeadbeef`)
Contiguous ranges from chunked dump:
- `0x08001c-0x080024`
- `0x080034-0x080034`
- `0x08003c-0x08003c`
- `0x080048-0x08007c`
- `0x080094-0x0800b0`
- `0x10002c-0x10007c`
- `0x1000ac-0x1000fc`
- `0x100180-0x1001fc`
- `0x100310-0x10031c`
- `0x1802c0-0x180308`
- `0x180338-0x180388`
- `0x180444-0x180444`
- `0x18044c-0x180450`

These should be treated as read-only unknowns until a specific functional test shows stable behavior.

Additional gated-window behavior (live probes):
- direct writes to representative addresses in both gated windows are accepted
  by command path but readback remains `0xdeadbeef` immediately.
- writing gated words caused no observable side effects on nearby active policy
  registers (`0x180294/0x180298/0x18029c`, `0x18038c`, `0x1803d0/0x1803d8`).
- `lan3` admin down/up transition did not change any word in the gated windows;
  all sampled words remained `0xdeadbeef`.
- stock-map translation places sampled gated words on page `0x001`, phy
  `0x13..0x16`, but direct `ext` MDIO reads there returned zeros; this access
  path does not bypass the gated behavior.
- UART-safe low-bit sweeps on global gate candidates:
  - `0x80014` staged sweep (`bit15..0`) completed without link loss; bits
    `15..11` read back as ignored, bits `10..0` latched and restored; sampled
    gated windows stayed `0xdeadbeef` throughout.
  - `0x80004` staged sweep (`bit23..2`) found mixed latch behavior and one
    hazardous trigger: bit `5` (`trial=0x0000082b`) flipped `0x80004`,
    `0x80014`, `0x1802c0`, and `0x180338` readback to `0xdeaddead`, dropped
    SSH/LAN, and required reboot for full recovery.
  - captures:
    - `docs/yt921x/live/yt_80014_lowbit_stage1_20260319_130039.txt`
    - `docs/yt921x/live/yt_80014_lowbit_stage2_20260319_130132.txt`
    - `docs/yt921x/live/yt_80004_lowbit_stage1_20260319_130230.txt`
    - `docs/yt921x/live/yt_80004_lowbit_stage2_20260319_130318.txt`
- direct gate-candidate sweeps on `0x18028c` and `0x1803cc` (`0x0`, `0x7ff`,
  one-hot `bit0..bit10`) did not change sampled gated words
  (`0x1802c0/0x1802c4/0x1802f0/0x180304`, `0x180338/0x18033c/0x180368/0x180388`);
  all remained `0xdeadbeef`.

Adjacent readable (non-gated) sub-windows in the same `0x1803xx` block:
- `0x18030c-0x180334` is writable and masked:
  - each of 11 words accepts writes but readback is masked to `0x000007ff`
    (write `0xffffffff` => read `0x000007ff`)
  - values persist across `lan2` bridge membership toggles (not immediately
    state-machine overwritten in this path)
  - single-bit sweep on `0x18030c` (`bit0..bit10`) produced stable readback for
    each bit but no immediate deltas in known active policy words
    (`0x180294/0x180298/0x18029c`, `0x18038c`, `0x1803d0/0x1803d4/0x1803d8`)
    and no router->host ping loss during the sweep.
  - per-word probe (`0x1` on each of 11 words) and all-words=`0x7ff` probe both
    preserved router->host connectivity (0% ICMP loss) and showed no immediate
    control-register coupling in this single-host topology.
  - live toggle test under host `192.168.2.101 -> 192.168.2.100` traffic with
    warm-up-fixed writes/readbacks showed all 11 words cleanly switching between
    `0x000` and `0x7ff`; `enp2s0` receive counters on `192.168.2.100` continued
    at similar pace in `prep_zero`, `set_all_7ff`, and `restore_zero` windows,
    with no observable outage in this workload.
  - post-flash mixed workload probes (`tcp+arp+icmp+mcast` and neighbor churn)
    still showed no phase-correlated deltas in known active control words.
  - per-word bit-coupling probes now cover `0x180310`, `0x180314`, `0x180318`,
    `0x18031c`, `0x180320`, `0x180324`, `0x180328`, `0x18032c`, `0x180330`,
    and `0x180334`; all accepted one-hot writes (`bit0..bit10`) with stable
    readback and no immediate deltas in sampled active words
    (`0x180294/0x180298/0x18029c/0x1802a0/0x1802a4/0x1802b4/0x1802b8`,
    `0x18038c`, `0x1803d0/0x1803d4/0x1803d8`).
  - bridge-FDB instrumentation showed stable learning state across all phases:
    `fdb_total=22`, target host MAC remained on `lan1` (master `br-lan`) with
    unchanged `self` entry, while table words toggled `0x000 <-> 0x7ff`.
- `0x180390-0x1803bc` is `YT921X_STPn(1..12)` and remains zero in normal single-bridge runtime.
- `0x18038c` is `YT921X_STPn(0)` and dynamic with bridge membership/state:
  - baseline (`lan2` in, `wan` out): `0x000300f3`
  - `lan2` out: `0x000300ff`
  - `wan` in: `0x00030033`
  - `lan2` out + `wan` in: `0x0003003f`
  - direct write test (`0x000300f3 -> 0x000300ff`) sticks immediately, but a
    subsequent `lan2` nomaster/master cycle rewrites it to membership-derived
    values (`0x000300ff -> 0x000300f3`).
  - restored baseline after reverting membership.
- low-byte interpretation candidate for `STPn(0)`:
  - `lan2` removed from bridge sets bits `[3:2]` (`+0x0c`)
  - `wan` added to bridge clears bits `[7:6]` (`-0xc0`)
- In the wider policy windows:
  - `0x18028c` tolerated `0x7ff -> 0x000 -> 0x7ff` while host->router ICMP
    (`192.168.2.100 -> 192.168.2.1`) stayed at `0%` loss in this runtime.
  - initial probe: `0x180510`/`0x180514` both read baseline `0x00000400` and
    accepted `->0->baseline` writes with stable readback; host->router ICMP
    stayed `0%` loss during that toggle window.
  - later recovery session: both words were found at `0x000007ff` in a bad
    runtime and host->router path was down while router->host ping still
    worked; restoring both to `0x00000400` immediately restored host->router
    reachability. See:
    `docs/yt921x/live/yt_180510_180514_blackhole_recovery_20260319_1655.txt`.
  - `PORTn_ISOLATION` conduit signature updates are multi-word, not `0x1802a0`
    only: on current CR881x mapping the key set is
    `0x1802a0/0x1802a4/0x1802b4/0x1802b8`, with
    `wan@eth1={0x6ff,0x6e8,0x7f7,0x6ef}` and
    `wan@eth0={0x7ef,0x6e0,0x7ff,0x6ef}`.
  - conduit switching requires symmetric CPU-side isolation update:
    allowing `port3 -> cpu` alone is not enough; reverse `cpu -> port3` must
    also be unblocked (current mapping uses cpu port 4) or return traffic
    blackholes.
  - `0x1803d8` (`PORTn_LEARN(2)`) toggled with `lan3` bridge membership (`0x00000000 <-> 0x00020000`).
  - VLAN decode probe (`vlan_filtering` + `wan` VID10):
    - `0x180280` toggled `0x00000000 <-> 0x0000000f` with
      `br-lan vlan_filtering 0/1`.
    - `0x188050` (`VLANn_CTRL(10)` word0) toggled
      `0x00000000 <-> 0x00000c80` when VID10 membership changed.
    - `0x188054` (`VLANn_CTRL(10)` word1) toggled
      `0x00000000 <-> 0x00000100` when `lan1` on VID10 changed from tagged to
      untagged.
    - `0x188058` (`VLANn_CTRL(11)` word0) toggled
      `0x00000000 <-> 0x00000c00` on VID11 add/remove, confirming 8-byte VTU
      stride.
    - `0x230010..0x23001c` (`PORTn_VLAN_CTRL(0..3)`) toggled
      `0xc007ffc0 <-> 0xc0040040` with `vlan_filtering` state.
    - WAN PVID sensitivity on `0x23001c` followed `PVID << 6` exactly for
      test set `{20,21,30,100}`.
- Do not classify these as `0xdeadbeef` gated windows; they are accessible and mostly policy/state coupled.

## Focused Probe Plan For Remaining Unknowns
Goal: decode semantics of the writable `0x18030c..0x180334` mask table and find
what gate controls expose `0x1802c0..0x180308` and `0x180338..0x180388`.

Current status:
- bit-coupling coverage is now complete for `0x180310..0x180334` under the
  current runtime model, with no immediate coupling into sampled active policy
  words.
- VLAN membership + `vlan_filtering` toggles identified deterministic control
  deltas in VTU/filter/per-port blocks: `0x180280`,
  `0x188050/0x188054/0x188058`, and `0x230010..0x23001c`.
- `0x080004` safe combo sweep across bits `{8,10,11,12}` did not unlock
  `0xdeadbeef` windows.
- remaining high-value unknown is gated-window unlock control.

Recommended sequence (debug build, UART shell):
1. Snapshot baseline:
   - `dump 0x180280 0x1803fc 4`
2. Per-port bridge isolation toggles:
   - move one port in/out of bridge and resnapshot `0x180280..0x1803fc`
3. Bit-coupling probes on `0x18030c..0x180334`:
   - set single-bit masks (`0x1`, `0x2`, `0x4`...) per word and watch known
     policy/FDB/forwarding behavior deltas.
   - keep one-word-at-a-time mutation and immediate restore snapshots.
4. VLAN ingress filter toggles:
   - `bridge vlan` add/del on one user port
   - resnapshot and diff same window
5. Global gate candidates:
   - one-at-a-time masked toggles on `0x80004`, `0x80014`, `0x18028c`, `0x1803cc`
   - after each toggle, read `0x1802c0..0x180388` and immediately restore
   - safety: `0x80004` bit `5` is now confirmed hazardous on CR881x test
     runtime; only probe with UART attached and reboot budgeted
6. Record strict pre/post dumps and reboot between hazardous toggles.

Interpretation rule:
- Promote only bitfields that show deterministic, single-variable coupling in
  at least two independent runs.

## PSCH Shaper Window (Mapped)
`0x354000..0x354024` controls per-port egress shaping for MAC ports `0..4`.

Layout (per port `p`, stride `0x8`):
- `0x354000 + 8*p` => `YT921X_PSCH_SHPn_EBS_EIR(p)`
  - `EIR` bits `[17:0]`
  - `EBS` bits `[31:18]`
- `0x354004 + 8*p` => `YT921X_PSCH_SHPn_CTRL(p)`
  - `EN` bit `[4]`
  - `DUAL_RATE` bit `[3]`
  - `METER_ID` bits `[2:0]`

Status:
- This block is not just symbolic: it is live write/readback verified on CR881x.
- Driver `port_setup_tc(TBF)` currently programs this window for hardware rate-limit offload.
- Live `tc tbf` validation on `wan` confirms deterministic scaling on port3:
  - `10mbit/32k` -> `eir=15480`, `ebs=512`, raw `0x354018=0x08003c78`
  - `50mbit/64k` -> `eir=76880`, `ebs=1024`, raw `0x354018=0x10012c50`
- Teardown note: `EN` bit clears on qdisc delete (`0x35401c=0x0`), while
  `EIR/EBS` payload retention was observed in this run (`0x354018` preserved).
