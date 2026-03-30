# YT9215 Register Map Changelog

## 2026-03-30: `0x080230` loop-detect safe bit sweep (live runtime)

Capture:
- `docs/yt921x/live/yt_080230_loop_detect_safe_bitsweep_2026-03-30.md`

What was confirmed:
- Baseline on this runtime:
  - `0x080230 = 0x03266624`
  - decoded fields: `f5=1`, `f6=0x9989`, `f8=0`, unit slices `f4=0`, `f3=1`, `f2=2`
- Controlled pulses on bits `18, 0..8` all:
  - latched to trial value (readback matched)
  - restored to baseline cleanly
  - showed `0%` packet loss on management ping path during each pulse

Interpretation update:
- `0x080230` behaves as an active writable control word in this runtime.
- Bit-level writes in tested range are operationally safe for short pulses with restore.
- This run does **not** prove BPDU classifier ownership yet:
  - no external BPDU injector was used
  - only `lan1` was link-up during test
  - userspace `bridge` tool was unavailable on this image

## 2026-03-30: stock module reverse decode (`yt_switch.ko`) for storm/RMA/loop paths

Capture and source:
- `docs/yt921x/yt9215-stock-behavior-reverse-2026-03-30.md`
- stock module: `Collection-Data/cr881x/mtd22_rootfs/lib/modules/4.4.60/yt_switch.ko`

What was confirmed:
- Decoded `tbl_reg_list` entries (table-id -> base MMIO):
  - `0x0d -> 0x00080230` (loop-detect top control)
  - `0x98 -> 0x00180508` (unknown-ucast filter mask)
  - `0x99 -> 0x0018050c` (unknown-mcast filter mask)
  - `0xa3 -> 0x001805d0` (RMA control)
  - `0xad -> 0x00180734` (unknown-ucast action)
  - `0xae -> 0x00180738` (unknown-mcast action/bypass)
  - `0xc6 -> 0x00220100` (storm rate input/timeslot side)
  - `0xc9 -> 0x00220140` (storm mc-type mask)
  - `0xcc -> 0x00220200` (storm global config)
- ARM disassembly (`fal_tiger_*`) mapped concrete field-id usage:
  - storm:
    - `enable_set`: `tbl 0xcc field4` (+ `tbl 0xc9 field0` for type==2 path)
    - `rate_mode_set`: `tbl 0xcc field3`
    - `rate_include_gap_set`: `tbl 0xcc field2`
    - `rate_set`: reads `tbl 0xc6 field0` + `tbl 0xcc field3`, writes `tbl 0xcc field0/field1`
  - ctrlpkt/RMA:
    - `unknown_ucast_act_set`: `tbl 0xad field0`
    - `unknown_mcast_act_set`: `tbl 0xae field2`
    - `rma_bypass_unknown_mcast_filter_set`: `tbl 0xae field0`
    - `igmp_bypass_unknown_mcast_filter_set`: `tbl 0xae field1`
    - `l2_filter_unknown_ucast_set`: `tbl 0x98 field0`
    - `l2_filter_unknown_mcast_set`: `tbl 0x99 field0`
    - `rma_action_set`: `tbl 0xa3` fields `4/3/7`
  - loop detect:
    - `enable_set`: `tbl 0x0d field5`
    - `tpid_set`: `tbl 0x0d field6`
    - `generate_way_set`: `tbl 0x0d field8`
    - `unitID_set`: `tbl 0x0d fields 4/3/2`
- Decoded field descriptors from `.rodata` for these tables:
  - `storm_ctrl_config_tblm_field`: `0:19@13`, `1:10@3`, `2:1@2`, `3:1@1`, `4:1@0`
  - `loop_detect_top_ctrlm_field`: `0:2@26`, `1:1@25`, `2:2@23`, `3:2@21`, `4:2@19`, `5:1@18`, `6:16@2`, `7:1@1`, `8:1@0`
  - `rma_ctrlnm_field`: `0..6` as single bits `@12..@6`, `7:6@0`
  - unknown filter masks (`ucast`/`mcast`): `11@0`

Interpretation update:
- Prior storm-control probing on `0x1805d0..0x1806bc` targeted the wrong hardware block for stock policer logic.
- Stock hardware storm path is in `0x2201xx/0x2202xx`.
- `0x180734/0x180738` split roles are now confirmed by disassembly, not only by runtime behavior.
- Loop-detect programming anchor is now concretely identified at `0x80230`.

## 2026-03-29: egress-tagging coupling (`VTU word1` vs `PORTn_VLAN_CTRL1`)

Capture:
- `docs/yt921x/live/yt_egress_tagging_vtu_vs_ctrl1_2026-03-29.md`

What was confirmed:
- `0x230080 + 4*p` (`PORTn_VLAN_CTRL1`, `p0..p10`) stayed `0x00000000` across
  tested tagged/untagged VLAN-42 states.
- `VID42` VTU words changed as expected for tagged vs untagged+PVID:
  - tagged (`lan1:t`): `0x188150=0x15008080`, `0x188154=0x00000000`
  - untagged+PVID (`lan1:u*`): `0x188150=0x15008080`, `0x188154=0x00000100`

Interpretation update:
- Egress untag behavior in this path is carried by VTU untag bitmap
  (`YT921X_VLANn_CTRL` word1), not by `0x230080`.

## 2026-03-29: `wan -> br-wan(vlan_filtering=1)` bind probe (`0x230010/0x230080`)

Capture:
- `docs/yt921x/live/yt_br_wan_bind_pvid_probe_2026-03-29.md`

What was confirmed:
- Baseline (`vf=0`): all ports `PVID=4095`.
- After runtime bind `wan` (`p3`) into `br-wan`:
  - `p3` changed to `CTRL=0xc0040040` (`PVID=1`)
  - `p0..p2`, `p4..p10` unchanged (`PVID=4095`)
  - `CTRL1` stayed `0` for all ports.
- Cleanup restored baseline for all ports.
- Conduit/isolation sanity words stayed constant across pre/bind/post:
  - `0x08000c=0x0000c008`
  - `0x1802a0=0x000007ef`
  - `0x1802a4=0x000007e7`
  - `0x1802b4=0x000007f8`

Interpretation update:
- User WAN port membership is reflected directly in `PORTn_VLAN_CTRL` PVID field.
- CPU conduit ports (`p4`,`p8`) did not show direct changes in this register
  family during this bind event.
- Sampled conduit selector/isolation words also did not change in this event.

## 2026-03-29: full-port `0x230010/0x230080` map under `vlan_filtering 0->1->0`

Capture:
- `docs/yt921x/live/yt_port_vlan_ctrl_full_ports_vf_toggle_2026-03-29.md`

What was confirmed:
- `state_pre` (`vf=0`): `p0..p10` all at
  - `CTRL=0xc007ffc0` => `PVID=4095` (`VID_UNWARE`)
  - `CTRL1=0x00000000`
- `state_on` (`vf=1`): only `p0..p2` changed to
  - `CTRL=0xc0040040` => `PVID=1`
  - `CTRL1=0x00000000`
  while `p3..p10` stayed VLAN-unaware (`PVID=4095`).
- `state_post` (`vf=0`): all ports returned to baseline.

Interpretation update:
- Per-port PVID programming is directly visible and profile-dependent:
  only ports participating in current `br-lan` membership transition to
  awareness/PVID during `vlan_filtering=1`.

## 2026-03-29: `tbl info` ID scan (`0x00..0xff`)

Capture:
- `docs/yt921x/live/yt_tbl_info_id_scan_2026-03-29.md`

What was confirmed:
- Runtime `tbl` map exposes only these IDs:
  - `0xc7`, `0xce`, `0xe4`, `0xe5`, `0xe9`, `0xea`, `0xeb`, `0xec`
- All other IDs in `0x00..0xff` returned `err=-2`.

Interpretation update:
- Current `yt921x_cmd` table path is QoS/meter/shaper-only.
- VTU/FDB/ACL tables are not exposed via this `tbl` ID map in current patch.

## 2026-03-29: safe `vlan_filtering` toggle (`0->1->0`) vs `0x2c01xx/0x31c0xx/0x1880xx`

Capture:
- `docs/yt921x/live/yt_vlan_filter_toggle_2c01_31c0_1880_probe_2026-03-29.md`

What was confirmed:
- Bridge state changed as expected:
  - `pre`: `vlan_filtering 0`
  - `on`: `vlan_filtering 1`
  - `post`: `vlan_filtering 0`
- Snapshot diffs across all 48 sampled words showed no deltas in:
  - `0x2c0100..0x2c013c`
  - `0x31c000..0x31c03c`
  - `0x188000..0x18803c`
- Runtime sanity control in the same sequence:
  - `0x180280` toggled `0x00000000 -> 0x00000007 -> 0x00000000`
  - `0x180598` stayed `0x00000000`
  - `0x188000/0x188004` (VID0) stayed `0x0043ff80/0x00000000`

Interpretation update:
- `vlan_filtering` coupling is present at known ingress filter control
  (`0x180280`) but not observed in sampled `0x2c01xx`, `0x31c0xx`, or `0x1880xx`
  direct-read windows for this transition.

## 2026-03-29: `0x2c0100..0x2c013c` idle/traffic poll (read-only)

Capture:
- `docs/yt921x/live/yt_2c0100_idle_traffic_poll_2026-03-29.md`

What was confirmed:
- Full 16-word dump (`0x2c0100..0x2c013c`) is stable and reproducible.
- `0x2c0100` polled x200:
  - idle: single value (`0xf2f6685b`)
  - under WAN traffic load: same single value
- All 16 words polled x50 snapshots at idle and x50 under load:
  - every address had `unique=1`
  - idle and traffic values matched exactly.

Interpretation update:
- In this runtime, direct read path does not reveal active command/busy
  behavior for the `0x2c01xx` window.
- Treat as opaque/read-only until a separate selector/trigger backend is found.

## 2026-03-29: `0x180690` bit sweep (`0..10`) with WAN+CPU capture deltas

Capture:
- `docs/yt921x/live/yt_cpu_copy_180690_sweep_2026-03-29.md`

What was confirmed:
- Baseline neighborhood:
  - `0x180688=0x0000003f`
  - `0x18068c=0x0000003f`
  - `0x180690=0x00000001`
  - `0x180694/0x180698/0x18069c=0xdeadbeef`
  - `0x1806a0=0x00000000`
- For each test case (`base`, then `b0..b10`), UU+multicast stimulus was
  generated and register restored to baseline after each pulse.
- WAN-side capture (`Pi eth0`, target-filtered UU/MC) stayed `0` packets for
  all tested bits.
- Router-side captures showed small deltas on `eth1` in all cases and stronger
  `eth0` deltas for `b9` (`0x00000201`) and `b10` (`0x00000401`).
- Follow-up split runs (`UU-only` vs `MC-only`) on `b9/b10`:
  - `b9_uu`: `pi=0`, `eth0=0`, `eth1=4`
  - `b9_mc`: `pi=0`, `eth0=6`, `eth1=7`
  - `b10_uu`: `pi=0`, `eth0=0`, `eth1=6`
  - `b10_mc`: `pi=0`, `eth0=6`, `eth1=8`

Interpretation update:
- In this runtime, `0x180690` bit flips `0..10` did not expose the probe stream
  on WAN side.
- Bits `9/10` remain interesting candidates for CPU-side exception/copy
  behavior; in this probe they correlated with MC-side deltas on router `eth0`.

## 2026-03-29: WAN-linked direct-capture boundary recheck (no leak under mask overrides)

Capture:
- `docs/yt921x/live/yt_bum_boundary_probe_uu_2026-03-29.md`

What was confirmed:
- With `wan` physically linked to Pi (`eth0`, `172.16.9.1/24`), direct WAN-side
  capture was used as the oracle:
  - `tcpdump -ni eth0 "icmp and (host 192.168.2.199 or host 239.1.2.3)"`
- Under UU + multicast stimulus from router CPU/LAN side, these temporary
  overrides produced zero captured packets on WAN:
  - `0x180510 -> 0x000007ff`
  - `0x180514 -> 0x000007ff`
  - `0x1805d4 -> 0x00000000`
  - `0x1805d8 -> 0x00000000`
- Additional isolation hole-punch test:
  - `0x1802b4: 0x000007f8 -> 0x000007f0` (clear bit3, `p8 -> p3`), still no
    observed WAN-side UU/MC packets.
- Post-test restore verified:
  - `0x180510=0x00000400`
  - `0x180514=0x00000400`
  - `0x1805d4=0x0000023f`
  - `0x1805d8=0x0000023f`
  - `0x1802b4=0x000007f8`

Interpretation update:
- In this runtime path, these candidate words are not sufficient to breach the
  LAN->WAN BUM boundary, even with WAN link up and direct packet capture.
- There is at least one additional gate (likely outside these tested words)
  governing WAN-side BUM visibility.

## 2026-03-29: UU boundary recheck for `0x1805d4/0x1805d8` and `0x180510/0x180514`

Capture:
- `docs/yt921x/live/yt_bum_boundary_probe_uu_2026-03-29.md`

What was confirmed:
- Baseline constants used/restored:
  - `0x180510=0x00000400`
  - `0x180514=0x00000400`
  - `0x1805d4=0x0000023f`
  - `0x1805d8=0x0000023f`
- Under unknown-unicast stimulus (`192.168.2.199` fake dst MAC),
  tcpdump counts on `lan1/lan2` remained unchanged for:
  - `0x1805d4 -> 0x00000000`
  - `0x1805d8 -> 0x00000000`
  - both zeroed together
  - low-nibble toggles around `0x180510/0x180514`
    (`0x401`, `0x402`, `0x404`, `0x408`)
- Every case observed the same flood delivery signature (`2/2` ICMP requests on
  both `lan1` and `lan2`).
- Full-mask sweep on both registers (`0x000`, `0x400`, `0x7ff`) with UU and
  multicast (`239.1.2.3`) also stayed invariant:
  - `lan1=2`, `lan2=2`, `lan3=0`, `wan=0` in all cases.

Interpretation update:
- In this runtime/workload, these tested values/bits did not act as direct UU
  flood gates.
- In this runtime/workload, full-mask extremes on these two words did not
  breach observed flood traffic into `lan3`/`wan`.

## 2026-03-29: `ACT_UNK_*` CPU8 matrix on LAN unknown-unicast path

Capture:
- `docs/yt921x/live/yt_uu_cpu8_action_matrix_2026-03-29.md`

What was confirmed:
- Baseline at test time:
  - `0x180734=0x00020000`
  - `0x180738=0x00420000`
- For CPU8 (`eth1`) unknown-unicast source path (`br-lan` -> fake dst MAC),
  sweeping action values `0..3` on `[17:16]` produced identical observed flood
  behavior:
  - `0x180734`: all actions gave `lan1=4`, `lan2=4` ICMP requests captured.
  - `0x180738`: all actions gave `lan1=4`, `lan2=4` ICMP requests captured.
- Restore was verified:
  - `0x180734=0x00020000`
  - `0x180738=0x00420000`
- Bit22 isolation (`0x00400000`) on `0x180738` with `[17:16]=2` held constant
  also showed no effect in this workload:
  - unknown-unicast: on=`4/4`, off=`4/4`
  - multicast (`239.1.2.3`): on=`4/4`, off=`4/4`

Interpretation update:
- In this workload, CPU8 action fields on `0x180734/0x180738` did not alter
  LAN unknown-unicast flood behavior.
- In this workload, `0x180738` bit22 did not act as a visible master on/off
  gate for either UU or multicast flooding.
- This is consistent with prior evidence that only `0x180734[9:8]` was
  action-sensitive in the WAN/CPU4 scenario.

## 2026-03-29: UART-only baseline snapshot via `yt921x_cmd`

Capture:
- `docs/yt921x/live/yt_uart_port_status_isolation_snapshot_2026-03-29.md`

What was confirmed:
- On this image, live register access is available through debugfs
  (`/sys/kernel/debug/yt921x_cmd`) even when `reg`/`ssdk_sh`/`devmem` are not
  present in userspace.
- Full per-port status (`p0..p10`) was collected without truncation using
  per-port `port_status <n>` queries:
  - notable active control/status signatures:
    - `p4: ctrl=0x000005fa status=0x000001fa` (`cpu2/eth0`)
    - `p8: ctrl=0x000005fa status=0x000001fa` (`cpu1/eth1`)
    - `p10: ctrl=0x000007fa status=0x000001fa` (internal `mcu`)
- `PORTn_ISOLATION` rows (`0x180294 + 4*n`) current snapshot:
  - `p0=0x6f9`, `p1=0x6fa`, `p2=0x6fc`, `p3=0x7ef`, `p4=0x7e7`,
    `p5=0x6ef`, `p6=0x6ef`, `p7=0x6ef`, `p8=0x7f8`, `p9=0x6ef`,
    `p10=0x6ef`

Interpretation update:
- The CR881x identity anchors remain consistent with this runtime:
  `p2=lan3`, `p4=cpu2`, `p8=cpu1`, `p10=mcu`.
- This snapshot provides a UART-safe baseline for further directional pulses on
  dark rows/ports.

## 2026-03-29: Port identity finalized (`lan3/cpu1/cpu2/mcu`)

Capture:
- `docs/yt921x/live/yt_port_identity_map_cr881x_2026-03-29.md`

What was confirmed:
- Final CR881x switch port identity:
  - `p2=lan3`
  - `p8=cpu1` (primary conduit, `eth1`)
  - `p4=cpu2` (secondary conduit, `eth0`)
  - `p10=mcu` (internal-only)
- Isolation row anchors for this mapping:
  - `lan3 -> 0x18029c`
  - `cpu2 -> 0x1802a4`
  - `cpu1 -> 0x1802b4`
  - `mcu  -> 0x1802bc`

Interpretation update:
- `0x7ff` global masks map cleanly to an 11-port model (`0..10`).
- Conduit/isolation tuning should continue to treat `p8` as primary CPU and
  `p4` as secondary CPU on CR881x.

## 2026-03-29: Host-to-host directional proof for `0x180294+`; negative confirmation for `0x1805d4/0x1805d8`

Capture:
- `docs/yt921x/live/yt_180294_host2host_directional_pulse_summary_2026-03-29.md`

What was confirmed:
- Runtime link state at test time:
  - `lan1` up, `lan2` up, `lan3` down, `wan` down.
- Isolation baseline rows:
  - `0x180294=0x000006f9`
  - `0x180298=0x000006fa`
  - `0x18029c=0x000006fc`
- Candidate matrix baseline:
  - `0x1805d0..0x18068c` mostly `0x0000003f`
  - outliers `0x1805d4=0x0000023f`, `0x1805d8=0x0000023f`.
- Host-to-host pulses:
  - `0x180294: 0x6f9 -> 0x6fb` caused repeatable LAN1<->LAN2 loss
    (`80/60` and `70/51` received in two runs).
  - `0x180298: 0x6fa -> 0x6fb` caused repeatable LAN1<->LAN2 loss
    (`70/50` received).
  - Simultaneous control ping to router (`192.168.2.1`) stayed `0%` loss.
- Negative controls:
  - `0x180298: 0x6fa -> 0x6fe` did not cut LAN1<->LAN2.
  - `0x18029c: 0x6fc -> 0x6fd` and `0x6fc -> 0x6fe` did not cut LAN1<->LAN2.
  - `0x1805d4` / `0x1805d8` pulses to `0x00000000` did not cut LAN1<->LAN2.

Interpretation update:
- `0x180294+` is the active directional isolation matrix for the tested LAN
  forwarding path.
- `0x1805d4` / `0x1805d8` are not the primary known-unicast LAN forwarding
  matrix in this runtime; keep in lower-confidence policy/BUM candidate bucket.

## 2026-03-25: Unknown-unicast (UU) trap from `10.1.0.178` (`eth0`) + candidate A/B

Captures:
- `docs/yt921x/live/yt_uu_ab_candidates_20260325_1500.txt`
- `docs/yt921x/live/yt_uu_ab_dump_mib_20260325_1505.txt`

What was confirmed:
- UU source was moved to external host `10.1.0.178` and bound to `eth0`
  (`192.168.2.178`) with static neighbor:
  - `192.168.2.199 lladdr 02:de:ad:be:ef:00 dev eth0`
- Two timed UU runs completed:
  - `done 10746149` packets (first run)
  - `done 8191720` packets (second run)
- Router side per-port netdev counters changed strongly during the first run
  (`lan2 tx`, `lan1 tx`, `lan3 rx`), confirming active L2 flood-path activity
  under UU stimulus in this topology.
- Candidate A/B toggles tested:
  - `0x1805d4` (`bit9`: `0x23f <-> 0x03f`)
  - `0x1805d8` (`bit9`: `0x23f <-> 0x03f`)
  - `0x180690` (`bit0`: `0x1 <-> 0x0`)
  - `0x1806bc` (`bit4`: `0x10 <-> 0x0`)
- Across both A/B passes, no deterministic clamp signature was observed between
  baseline and trial windows for these candidates.

Interpretation update:
- In this runtime, the tested `0x1805d4/0x1805d8/0x180690/0x1806bc` bit toggles
  did not reveal a clear UU storm-policer gate.
- Keep these registers in low-confidence policy bucket and continue with other
  candidate regions for UU-specific policing logic.

## 2026-03-25: MIB signal mapping (`0x0c0100..0x0c01fc`) + `0x1805d4` bit9 A/B

Captures:
- `docs/yt921x/live/yt_mib_map_broadcast_20260325_1450.txt`
- `docs/yt921x/live/yt_ab_1805d4_mib_broadcast_20260325_1450.txt`

What was confirmed:
- Under synchronized ~3s high-rate UDP broadcast load, the strongest moving
  MIB words in this sampled window were:
  - `0x0c0100` and `0x0c0130` (both near packet-send count)
  - `0x0c013c` (large high-rate accumulator)
  - `0x0c0184` (secondary high-delta counter)
- A/B on `0x1805d4` (`0x0000023f -> 0x0000003f`, i.e. bit9 clear) with matched
  load showed only send-volume-proportional deltas and no independent drop-like
  counter surge in sampled MIB words.

Interpretation update:
- `0x0c0100..0x0c01fc` can be used as a practical live signal window for
  stress-path comparison.
- Clearing `0x1805d4` bit9 did not expose a broadcast-path policer effect in
  this runtime.

## 2026-03-25: Global-enable candidate sweep (`0x180500`, `0x355000`) under live broadcast load

Capture:
- `docs/yt921x/live/yt_global_gate_sweep_180500_355000_20260325_143350.txt`

What was confirmed:
- Low-bit (`bit0..bit7`) sweep with per-trial rollback was run on:
  - `0x180500` (orig `0x00000000`)
  - `0x355000` (orig `0x00000000`)
- Method used synchronized ~4s high-rate UDP broadcast bursts from host and
  sampled `lan1_rx` deltas on the router for each trial.
- All trial deltas tracked packet send counts and stayed baseline-equivalent:
  - `0x180500`: no measurable clamp/gate effect for `bit0..bit7`
  - `0x355000`: no measurable clamp/gate effect for `bit0..bit7`
- Both registers restored cleanly to original values after sweep.

Interpretation update:
- No evidence that `0x180500` low bits or `0x355000` low bits are the missing
  global storm-control enable in the tested broadcast path.
- Unknown-unicast-focused probes remain required to continue narrowing BUM
  policer control points.

## 2026-03-25: BUM storm candidate probe (`0x1805d0..0x1806bc`) under live broadcast load

Capture:
- `docs/yt921x/live/yt_bum_storm_candidate_probe_20260325_1345.md`

What was confirmed:
- In `0x353000..0x355f00`, only three readable islands were observed in this
  runtime:
  - `0x353000..0x3531ff` (all zeros)
  - `0x354000..0x354054` (known PSCH/TBF region)
  - `0x355000..0x355028` (all zeros)
- Candidate L2 cluster in `0x1805d0..0x1806bc` remained static during
  synchronized high-rate UDP broadcast stress:
  - `0x1805d0..0x18068c` mostly `0x0000003f`
  - `0x1805d4`/`0x1805d8` were `0x0000023f`
  - `0x180690=0x00000001`, `0x1806b8=0x000007ff`, `0x1806bc=0x00000010`
- A/B probes under identical ~4s broadcast bursts (about `339k` datagrams) did
  not show policing behavior on `lan1_rx`:
  - `0x1805d0: 0x3f -> 0x3e` produced near-identical deltas
  - `0x1806b8: 0x7ff -> 0x0ff` produced near-identical deltas
  - timed `0x18068c` toggles (`0x3f <-> 0x3e`) showed no sustained gating

Interpretation update:
- On the tested broadcast path, this cluster did not act like active storm-rate
  limiter control.
- `0x1805d0..0x18068c` and `0x1806b8/0x1806bc` should stay in low-confidence
  policy/config buckets until unknown-unicast stress testing is completed.

## 2026-03-24: Unknown-table expansion (`0x180310..0x180334`) + VLAN ingress filter delta

Captures:
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
- `docs/yt921x/live/yt_gated_window_recheck_80014_bit0_20260324_154715.txt`
- `docs/yt921x/live/yt_gate_candidate_18028c_1803cc_probe_20260324_092049.txt`
- `docs/yt921x/live/yt_80004_combo_gate_probe_bits8_10_11_12_20260324_092541.txt`
- `docs/yt921x/live/yt_vlan_membership_probe_wan_vid10_20260324_161032.txt`
- `docs/yt921x/live/yt_vlan_filtering_probe_wan_vid10_20260324_161244.txt`
- `docs/yt921x/live/yt_vlan_ctrl_decode_probe_wan_vid10_20260324_092626.txt`
- `docs/yt921x/live/yt_vlan_vtu_stride_pvid_probe_fix_20260324_093111.txt`
- `docs/yt921x/live/yt_pvid_field_sensitivity_wan_v2_20260324_093322.txt`

What was confirmed:
- unknown writable table words `0x180310..0x180334` all accepted one-hot writes
  (`bit0..bit10`) and restored cleanly.
- during those sweeps, sampled active policy words stayed constant:
  - isolation: `0x180294=0x7e1`, `0x180298=0x7e2`, `0x18029c=0x7e4`,
    `0x1802a0=0x7e8`, `0x1802a4=0x6e0`, `0x1802b4=0x7ff`, `0x1802b8=0x6ef`
  - STP/learning: `0x18038c=0x003cfc30`, `0x1803d0=0x0`, `0x1803d4=0x0`,
    `0x1803d8=0x00020000`
- safe recheck of gate candidate `0x080014` bit0 still did not unlock gated
  windows (`0x1802c0..0x180308`, `0x180338..0x180388` remained `0xdeadbeef`).
- direct sweeps of gate candidates `0x18028c` and `0x1803cc` (`0x0`, `0x7ff`,
  one-hot `bit0..bit10`) also left sampled gated words unchanged at
  `0xdeadbeef`.
- safe combo sweep of `0x080004` bits `{8,10,11,12}` (all 16 combinations)
  also left sampled gated words unchanged at `0xdeadbeef`.
- VLAN tests:
  - membership-only add/del (`bridge vlan_filtering=0`) did not move sampled
    hardware words.
  - enabling `bridge vlan_filtering=1` toggled `0x180280` from `0x00000000` to
    `0x0000000f`, and disabling filtering returned it to `0x00000000`.
  - focused VLAN control decode showed:
    - `0x188050` (`VLANn_CTRL(10)` word0) toggles
      `0x00000000 <-> 0x00000c80` with VID10 membership changes
    - `0x188054` (`VLANn_CTRL(10)` word1) toggles
      `0x00000000 <-> 0x00000100` when `lan1` on VID10 flips tagged -> untagged
    - `0x188058` (`VLANn_CTRL(11)` word0) toggles
      `0x00000000 <-> 0x00000c00` on VID11 add/del, confirming 8-byte VTU stride
    - `0x230010..0x23001c` (`PORTn_VLAN_CTRL(0..3)`) toggles
      `0xc007ffc0 <-> 0xc0040040` with `vlan_filtering`
    - WAN PVID field sensitivity on `0x23001c` follows `PVID << 6` for
      probes `{20,21,30,100}`

Interpretation update:
- `0x18030c..0x180334` currently behaves as writable masked state without
  immediate coupling to sampled L2 forwarding/isolation/STP words in this
  runtime model.
- `0x180280` now has a deterministic software-coupled transition tied to Linux
  bridge `vlan_filtering`.
- active VLAN control coupling is now confirmed in both VID table and per-port
  control blocks (`0x188050/0x188054/0x188058`, `0x230010..0x23001c`).
- gated-window unlock control remains unresolved; `0x080014 bit0` is ruled out
  as a direct unlock in this runtime, and safe `0x080004` bit-combo sweeps are
  also ruled out.

## 2026-03-24: QoS TBF offload stress + multicast MDB path check

Captures:
- `docs/yt921x/live/yt_tbf_tc_offload_probe_wan_20260324_094055.txt`
- `docs/yt921x/live/yt_multicast_mdb_probe_20260324_094150.txt`
- `docs/yt921x/live/yt_multicast_mdb_probe_vid1_20260324_094214.txt`
- `docs/yt921x/live/yt_multicast_mdb_probe_snoop_on_20260324_094305.txt`

What was confirmed:
- `tc tbf` offload path is active on `wan`:
  - qdisc reports `offloaded`
  - port3 PSCH slot (`0x354018/0x35401c`) updates with expected EIR/EBS scaling
    for `10mbit/32k` and `50mbit/64k` profiles.
- after qdisc delete:
  - shaper `EN` bit clears (`0x35401c=0`)
  - `EIR/EBS` payload retention was observed (`0x354018` kept last programmed
    value in this run).
- multicast MDB path:
  - with `multicast_snooping=0`, `bridge mdb add` is rejected (`EINVAL`)
  - with `multicast_snooping=1`, static MDB add succeeds and shows `offload`
  - sampled multicast-related regs (`0x180510`, `0x180514`, `0x180734`,
    `0x180738`) did not change during that static MDB add/del sequence.

Interpretation update:
- QoS/TBF offload datapath is functionally validated on live hardware.
- MDB offload is operational when bridge snooping is enabled.
- Immediate multicast policy coupling is not through the sampled flood/action
  words above; likely maintained in other tables/paths.

Driver follow-up:
- `yt921x_tbf_del()` teardown path was hardened to hard-clear both PSCH words
  (`SHPn_CTRL` and `SHPn_EBS_EIR`) via direct writes to avoid stale EIR/EBS
  retention across qdisc recreate cycles.

## 2026-03-23: Dual-conduit isolation validation after CR881x port4 remap (`wan` eth1 <-> eth0)

Runtime context:
- dual-uplink `yt921x` conduit switching enabled (`port_change_conduit` path)
- CR881x live switching of `wan` between primary (`eth1`) and secondary (`eth0`)
  CPU conduits

What was confirmed:
- `EXT_CPU_PORT` (`0x08000c`) remains a single primary tagged CPU-port selector.
  On this board/runtime it switches between:
  - `0x0000c008` for primary conduit (`wan@eth1`)
  - `0x0000c004` for secondary conduit (`wan@eth0`)
- after the CR881x DTS remap (`cpu8 <-> cpu4` secondary path), switching `wan`
  to secondary conduit requires symmetric isolation between `port3` and `port4`.

Observed signatures:
- `wan@eth1` (primary): `0x1802a0=0x000006ff`, `0x1802a4=0x000006e8`,
  `0x1802b4=0x000007f7`, `0x1802b8=0x000006ef`, `0x08000c=0x0000c008`
- `wan@eth0` (secondary): `0x1802a0=0x000007ef`, `0x1802a4=0x000006e0`,
  `0x1802b4=0x000007ff`, `0x1802b8=0x000006ef`, `0x08000c=0x0000c004`

Traffic validation:
- Pi capture on upstream (`172.16.9.1`) observed ARP request/reply and ICMP
  echo request/reply while `wan` was on `eth0`, confirming bidirectional path.

## 2026-03-19: UART-safe low-bit sweeps on `0x80014`/`0x80004`

Captures:
- `docs/yt921x/live/yt_gate_sweep_lowbits_20260319_135946.txt`
  - first attempt used old debugfs syntax (`reg <addr>`), produced parser help
    output and was superseded by staged UART sweeps
- `docs/yt921x/live/yt_80014_lowbit_stage1_20260319_130039.txt`
- `docs/yt921x/live/yt_80014_lowbit_stage2_20260319_130132.txt`
- `docs/yt921x/live/yt_80004_lowbit_stage1_20260319_130230.txt`
- `docs/yt921x/live/yt_80004_lowbit_stage2_20260319_130318.txt`

What was confirmed:
- `0x80014` (`PVID_SEL` path) low bits:
  - bits `15..11`: write-ignored (readback stayed `0x00000000`)
  - bits `10..0`: latched and restored deterministically
  - sampled gated windows (`0x1802c0`, `0x180338`) remained `0xdeadbeef`
  - no SSH/LAN outage during staged sweep
- `0x80004` (`FUNC` path) low bits:
  - bits `23..13`: write-ignored
  - bit `12`: latched (`0x0000180b`) and restored
  - bits `11`, `10`, `8`: latched and restored
  - bits `9`, `7`, `6`: write-ignored in this runtime
  - bit `5` (`trial=0x0000082b`): destructive in-runtime transition:
    - `0x80004`, `0x80014`, `0x1802c0`, `0x180338` readback became
      `0xdeaddead`
    - PHY state-machine warnings emitted (`phy_check_link_status ... -110`)
    - SSH path dropped (`No route to host`) and router required reboot for
      normal register-path recovery

Operational note:
- Gate-candidate sweeps should treat `0x80004` bit `5` as hazardous on CR881x.
- Continue probing this area only with UART attached and reboot budgeted.

## 2026-03-19: `0x18028c` blackhole probe (host->router path)

Capture:
- `docs/yt921x/live/yt_18028c_blackhole_probe_20260319_110516.txt`

What was tested:
- live ICMP from host (`192.168.2.100`) to router (`192.168.2.1`) while toggling:
  - `0x18028c: 0x000007ff -> 0x00000000 -> 0x000007ff`

Observed:
- register writes/readback were accepted and deterministic
- ICMP remained uninterrupted:
  - `60/60` replies, `0%` loss, no spike beyond normal sub-ms jitter

Interpretation:
- `0x18028c` is writable but did not act as a host<->router forwarding
  kill switch in the tested single-host LAN->router path.
- Further semantics likely require multi-host LAN-LAN isolation/topology probes.

## 2026-03-19: `0x180510`/`0x180514` filter-word live probe

Capture:
- `docs/yt921x/live/yt_180510_180514_probe_20260319_111723.txt`

What was tested:
- continuous host->router ICMP while toggling:
  - `0x180510` (`FILTER_MCAST`): baseline `0x00000400 -> 0x00000000 -> 0x00000400`
  - `0x180514` (`FILTER_BCAST`): baseline `0x00000400 -> 0x00000000 -> 0x00000400`

Observed:
- both registers are writable with deterministic readback
- host->router ICMP remained uninterrupted (`60/60`, `0%` loss)

Interpretation:
- in this single-host topology, these words do not behave as immediate global
  unicast path killers.
- semantics are still likely flood/filter policy related; multi-host
  broadcast/multicast traffic tests are required for definitive behavior.

Driver follow-up:
- `yt921x_chip_setup_dsa()` now initializes both `YT921X_FILTER_MCAST` and
  `YT921X_FILTER_BCAST` alongside unknown unicast/multicast filter masks, so
  all mapped filter words are explicitly programmed during switch setup.

## 2026-03-19: `0x180510`/`0x180514` blackhole recovery + post-fix validation

Capture:
- `docs/yt921x/live/yt_180510_180514_blackhole_recovery_20260319_1655.txt`

What was observed:
- during recovery on a bad runtime slot, both words were read as:
  - `0x180510 = 0x000007ff`
  - `0x180514 = 0x000007ff`
- in that state, host->router (`192.168.2.100 -> 192.168.2.1`) was down while
  router->host ping remained working (asymmetric reachability).

Recovery action:
- wrote both words back to stock-safe baseline:
  - `0x180510 = 0x00000400`
  - `0x180514 = 0x00000400`
- host->router ICMP recovered immediately after write.

Post-fix image validation:
- corrected image booted into `rootfs_1`
- `yt921x` probe path was present (`lan1/lan2/lan3/wan` created)
- post-boot readback remained:
  - `0x180510 = 0x00000400`
  - `0x180514 = 0x00000400`

Interpretation update:
- these words should be treated as high-impact flood/filter policy controls.
- `0x00000400` is confirmed safe baseline on CR881x runtime.
- broad all-port mask (`0x000007ff`) is hazardous and can blackhole host->router
  control-plane reachability.

## 2026-03-19: Post-build STP word verification (`0x18038c`) on flashed image

Runtime context:
- rebuilt + flashed image with updated STP mapping in `yt921x` driver patch
- validation captures:
  - `docs/yt921x/live/yt_18038c_stp_validate_20260319_092708.txt`
  - `docs/yt921x/live/yt_18038c_stp_sysfs_probe_20260319_092809.txt`
  - `docs/yt921x/live/yt_18038c_postbuild_test_20260319_094407.txt`

What was confirmed:
- active STP word remains `0x18038c` on this image; baseline observed:
  - `0x003cfc0c` (before STP global enable)
- bridge membership deltas were reproduced on the new build:
  - `lan2` nomaster: `0x...0c -> 0x...00` (delta `-0x0c`, bits `[3:2]`)
  - `lan3` master re-add: `0x...0c -> 0x...3c` (delta `+0x30`, bits `[5:4]`)
  - `wan` add to bridge: `0x...3c -> 0x...fc` (delta `+0xc0`, bits `[7:6]`)
- this confirms low-byte composition in one STP instance word with 2-bit stride
  per port index, not separate per-port registers.
- full rebuild + reflash pass reproduced the same deltas with the tightened STP
  callback mapping code path.

Notes:
- `bridge` userspace command was absent post-sysupgrade in this test image, and
  direct writes to `/sys/class/net/*/brport/state` were permission-rejected in
  this runtime, so forced per-state transitions could not be scripted through
  userspace in this pass.
- despite that limitation, membership-driven deltas fully confirmed `lan3` and
  `wan` placement inside the same `0x18038c` control word.

## 2026-03-19: Post-flash runtime validation with `ip-full` + `bridge fdb`

Runtime context:
- flashed and validated on `ImmortalWrt SNAPSHOT r38281+4-dbb6c88688`
- post-flash sanity capture:
  - `docs/yt921x/live/yt_post_flash_sanity_busybox_20260319_082647.txt`
- added offline tooling packages on-router for deeper probes:
  - `ip-full`, `ip-bridge`, `tcpdump`, `ethtool`, `iperf3`

New captures:
- `docs/yt921x/live/yt_18030x_vlan_mcast_tcp_probe_20260319_084804.txt`
- `docs/yt921x/live/yt_18030x_ipfull_neigh_probe_20260319_085513.txt`
- `docs/yt921x/live/yt_18030x_bridge_fdb_probe_20260319_085923.txt`

What was confirmed:
- `0x18030c..0x180334` remains fully writable with stable per-word mask:
  - write `0x7ff` -> read `0x7ff` on all 11 words
  - checkerboard patterns (`0x155/0x2aa`) read back cleanly
  - restore to zero read back cleanly
- known active coupled words remained unchanged across all new phases:
  - `0x180294=0x6f9`, `0x180298=0x6fa`, `0x18029c=0x6fc`
  - `0x18038c=0x000300f3`
  - `0x1803d0=0x00000000`, `0x1803d4=0x00020000`, `0x1803d8=0x00000000`
- with `bridge` instrumentation, FDB state stayed phase-invariant:
  - `fdb_total=22` at baseline and every toggle phase
  - target host MAC `9c:6b:00:11:d3:f4` remained on `lan1` (`master br-lan`)
    with stable `vlan 4095 self` entry
- packet capture stayed active during all phases (no outage signature):
  - probe counts: `arp=14`, `icmp=125`, `syn=430`, `vlan=0`
- neighbor state churn (`DELAY -> REACHABLE`) occurred under traffic but did not
  correlate to table-toggle phases.

Interpretation update:
- in current CR881x bridge runtime/workloads, `0x18030c..0x180334` continues to
  look writable-but-not-coupled to the active forwarding/learning paths that are
  visible through `PORTn_ISOLATION`, `PORTn_LEARN`, `STPn(0)`, and `bridge fdb`.

## 2026-03-19: Split `0x1803xx` gated vs readable sub-windows

What was clarified from existing CR881x chunked dumps:
- still gated (`0xdeadbeef`):
  - `0x1802c0-0x180308`
  - `0x180338-0x180388`
- readable but currently unmapped:
  - `0x18030c-0x180334` (11 words, same cardinality as ports `0..10`)
  - `0x180390-0x1803bc` (11 words, stable zeros in baseline)
  - `0x18038c` is dynamic (not zero in baseline)

Additional map refinements:
- Added low-confidence portmask-like key-register notes:
  - `0x18028c = 0x000007ff`
  - `0x1803cc = 0x000007ff`
- Added a focused UART/debugfs probe sequence for `0x1803xx` unknowns, centered
  on bridge/learning/VLAN deltas and one-at-a-time gate-candidate toggles.

Live probe update (capture:
`docs/yt921x/live/yt_1803xx_probe_chunked_20260319_054624.txt`):
- `lan2` bridge detach (`ip link set lan2 nomaster`) caused deterministic deltas:
  - `0x180294: 0x000006f9 -> 0x000006fb`
  - `0x180298: 0x000006fa -> 0x000006ff`
  - `0x18029c: 0x000006fc -> 0x000006fe`
  - `0x18038c: 0x000300f3 -> 0x000300ff`
- Re-attaching `lan2` to `br-lan` restored those values.
- Direct gate-candidate toggles (`0x80004`, `0x80014`, `0x18028c`, `0x1803cc`)
  did not open `0x1802c0..0x180308` or `0x180338..0x180388`; both stayed
  `0xdeadbeef`.
- `ip link ... type bridge_slave learning` syntax was not supported by this
  router image (`ip: either "dev" is duplicate, or "type" is garbage`), so
  learning-specific deltas were validated via direct `0x1803d4` writes instead.

Follow-up composition probe (capture:
`docs/yt921x/live/yt_18038c_combo_probe_20260319_055738.txt`):
- confirmed independent composition on `0x18038c` low byte:
  - baseline (`lan2` in, `wan` out): `0x...f3`
  - `lan2` out: `0x...ff` (`+0x0c`)
  - `wan` in: `0x...33` (`-0xc0`)
  - `lan2` out + `wan` in: `0x...3f`
- this strongly suggests two bridge-membership coupled bit groups:
  - bits `[3:2]` (lan2-related)
  - bits `[7:6]` (wan-related)

Write-persistence probe (capture:
`docs/yt921x/live/yt_18038c_write_persistence_20260319_060750.txt`):
- direct write to `0x18038c` (`0x000300f3 -> 0x000300ff`) is accepted and
  immediate readback matches the written value
- subsequent bridge membership event (`lan2` nomaster/master cycle) rewrites
  `0x18038c` back to membership-derived baseline (`0x000300f3`)
- this confirms `0x18038c` is writable but owned by a higher-level switch
  policy/state machine, not a stable standalone configuration register

Follow-up range probe (captures:
`docs/yt921x/live/yt_1803xx_membership_probe_20260319_061341.txt`,
`docs/yt921x/live/yt_18028x_1803cx_membership_probe_20260319_061424.txt`,
`docs/yt921x/live/yt_180390_write_probe_20260319_061654.txt`):
- `0x180390..0x1803bc` now mapped to `YT921X_STPn(1..12)`:
  - defaults stay zero in single-bridge runtime
  - direct write/readback on `0x180390` verified (`0 -> 0x3 -> 0`)
- `0x18038c` is `YT921X_STPn(0)` (active STP instance word), not a random latch
- wider policy window mapping confirmed by membership deltas:
  - `0x1802a0` (`PORTn_ISOLATION(3)`) changed on `wan` bridge membership
  - `0x1803d8` (`PORTn_LEARN(2)`) toggled on `lan3` bridge membership
- `0x18030c..0x180334` remained all-zero in all tested membership transitions;
  still unresolved and lower immediate value than gated windows.

Gated-window hardening probes (captures:
`docs/yt921x/live/yt_gated_stock_map_probe_20260319_063619.txt`,
`docs/yt921x/live/yt_gated_write_read_probe_20260319_063928.txt`,
`docs/yt921x/live/yt_gated_write_side_effect_probe_20260319_063944.txt`,
`docs/yt921x/live/yt_gated_admin_toggle_probe_20260319_064228.txt`):
- sampled words in `0x1802c0..0x180308` and `0x180338..0x180388` stayed
  `0xdeadbeef` across:
  - direct write/read attempts
  - `lan3` admin down/up state transitions
- write attempts showed no side effects on nearby active control words.
- stock-map translation resolves these words to page `0x001`, phy `0x13..0x16`,
  but direct `ext` MDIO reads there returned zeros, so current debug paths do
  not provide a bypass.

Writable-table discovery for `0x18030c..0x180334` (captures:
`docs/yt921x/live/yt_18030c_write_probe_20260319_064402.txt`,
`docs/yt921x/live/yt_18030x_mask_probe_20260319_064433.txt`,
`docs/yt921x/live/yt_18030c_334_full_mask_probe_20260319_064504.txt`,
`docs/yt921x/live/yt_18030c_persistence_membership_probe_20260319_064550.txt`,
`docs/yt921x/live/yt_18030c_bit_coupling_probe_20260319_065150.txt`,
`docs/yt921x/live/yt_18030x_word_coupling_ping_probe_20260319_065753.txt`,
`docs/yt921x/live/yt_18030x_all_ones_table_probe_20260319_065951.txt`,
`docs/yt921x/live/yt_18030x_live_toggle_from_101_20260319_071411.txt`,
`docs/yt921x/live/yt_18030x_live_toggle_from_101_warmfix_20260319_071530.txt`):
- this range is not inert zero-space; all 11 words are writable with a stable
  readback mask of `0x000007ff`.
- full-mask writes (`0xffffffff`) read back as `0x000007ff` on every tested
  word, then restore cleanly to zero.
- one-bit value persistence test on `0x18030c` survived `lan2` nomaster/master
  bridge transitions, indicating this table is not immediately rewritten by the
  same membership state machine that drives `STPn(0)`.
- one-bit sweep (`bit0..bit10`) on `0x18030c` showed no immediate coupling to
  known active policy words (`0x180294/0x180298/0x18029c`, `0x18038c`,
  `0x1803d0/0x1803d4/0x1803d8`) and no router->host ICMP loss during sweep.
- per-word and all-words stress probes (`0x1` and `0x7ff`) also kept ICMP
  connectivity intact in current single-host runtime, suggesting this table is
  either inactive in this topology/mode or coupled to functions not exercised by
  router<->host ping alone.
- cross-host live toggle probe under active `192.168.2.101 -> 192.168.2.100`
  traffic (warm-up fixed so all 11 writes apply) showed:
  - verified readback transitions `0x000` -> `0x7ff` -> `0x000` on all words
  - continuous receive-counter growth on host NIC during each phase, with no
    obvious traffic collapse while table remained at `0x7ff`.
- short-run side checks showed no immediate disturbance to active isolation,
  STP0, or learn words, and host ping stayed healthy.

## 2026-03-18: PSCH shaper (`0xeb`) live calibration on CR881x

Context:
- Tested on CR881x with the new debugfs helper (`/sys/kernel/debug/yt921x_cmd`)
- Active lane under test was `0xeb[0]` (port scheduler shaping entry tied to the
  host-facing test path)
- Test method: `iperf3 -R` from host to router with per-step `field set`

Observed rate model (with `en=1`, `ebs=1`, default slot/token settings):
- `cap_mbps ~= 0.0006516 * EIR - 0.085`
- inverse: `EIR ~= 1535 * target_mbps + 130`

Calibration points (receiver rate):
- `EIR=10000 -> ~6.47 Mbps`
- `EIR=20000 -> ~12.9 Mbps`
- `EIR=50000 -> ~32.5 Mbps`
- `EIR=100000 -> ~65.2 Mbps`
- `EIR=150000 -> ~97.9 Mbps`
- `EIR=200000 -> ~130 Mbps`

Low-rate behavior:
- Very low `EIR` can starve management traffic on the same lane.
- Stable non-zero region started around `EIR ~= 300` (`~175 Kbit/s` observed).

Scope note:
- This model is empirical for current CR881x runtime defaults; if global
  scheduler slot/token registers are changed, recalibration is required.

## 2026-03-17: Port admin/speed mapping from live UART/debugfs

New doc:
- `docs/yt921x/yt9215-port-admin-speed-map-2026-03-17.md`

What was added:
- Per-port register mapping for CR881x user lanes:
  - `lan1/lan2/lan3/wan -> 0x8010x / 0x8020x`
- Stable signatures for:
  - `1G up` (`0x5fa/0x1fa`)
  - `100M up` (`0x5f9/0x1f9`)
  - `admin down` (`0x5e2 or 0x582` and `0x0e2 or 0x082`)
- Live-delta interpretation showing admin-control mask behavior aligns with:
  - `LINK | RX_MAC_EN | TX_MAC_EN`
- Capture references for reproducibility and audit.

## 2026-03-17: Actionability map from `/mnt/wsl/tmp` archive

New doc:
- `docs/yt921x/yt9215-register-actionability-map-2026-03-17.md`

What was added:
- Consolidated classification of registers/windows into:
  - safe now
  - writable but coupled
  - writable but low-confidence
  - coerced/non-plain
  - backend-unreliable windows
  - MIB/stat data windows
- Explicit carry-over of high-value raw-vs-normal deltas (`0x0800xx` and
  `0x31c0xx` families).
- Guidance on what can enter driver logic immediately versus what should stay
  in debug-only experimentation.

## 2026-03-16: CR881x post-fix live snapshot (switch restored)

Build / board:
- ImmortalWrt `r38233+11-2109604410`
- Target `xiaomi_cr881x` (ipq50xx + yt9215s)
- Kernel with corrected full `yt921x.c` backport hunk applied

Raw capture files:
- `docs/yt921x/live/yt_regmap_live_cr881x_20260316_195556_postfix_recovery.txt`
- `docs/yt921x/live/yt_regmap_live_cr881x_20260316_200008_lan2_traffic_delta.txt`
- `docs/yt921x/live/yt_link_event_monitor_20260316_210441_uart_transitions_final.txt`

Capture method:
- SSH + `/proc/yt921x_cmd` (`reg/int/ext`) after booting repaired image

High-value observations:
- Switch path is fully restored:
  - `lan1/lan2/lan3/wan/cpu` DSA netdevs are present again
  - Driver probe logs show:
    - `Motorcomm YT9215S ...`
    - `mdio-int responders: phyid_mask=0x31f`
    - `mdio-ext responders: none detected on ports 0..31`
- Link-state signature remained consistent with baseline:
  - `PORTn_STATUS`:
    - `0x000001fa` on active/up-style ports (`1`,`4`,`8`,`10`)
    - `0x000000e2` on inactive/down-style ports (`0`,`2`,`3`,`5`,`6`,`7`,`9`)
  - Internal PHY `BMSR` (`reg 1`) split:
    - up: `0x796d` (`port1`,`port4`)
    - down: `0x7949` (`port0`,`port2`,`port3`)
  - Internal PHY `reg 0x11` split:
    - active: non-zero (`0xbc4c`,`0xbc0c`)
    - inactive: `0x0000`
- External MDIO quick sweep (`ports 0..31`, IDs regs `2/3`) stayed all-zero.
- MBUS control window registers are confirmed volatile command latches:
  - `0x06a004` and `0x0f0004` change with last helper transaction and should not be treated as static strap/config registers.
- Live traffic sanity check over `lan2` (20 ICMP packets to `192.168.2.100`) confirmed:
  - `lan2` counters increased (`rx_packets: 318->354`, `tx_packets: 415->453`)
  - `PORTn_STATUS` classification remained unchanged
  - FDB result window stayed stable in this short run (`0x180454..0x180464`)
  - `MIB_CTRL` observed as `0x00000001` after traffic, indicating the `0x80000000` bit is not a persistent steady-state bit.
- Controlled UART plug/unplug transitions (lan1 -> lan3 -> wan) captured:
  - `PORTn_STATUS` for user ports (`p0`,`p2`,`p3`) toggles deterministically:
    - down `0x000000e2` <-> up `0x000001fa`
  - Internal PHY `BMSR` for those ports toggles:
    - down `0x7949` <-> up `0x796d`
  - Timing relation from trace:
    - `BMSR` flips first
    - `PORTn_STATUS` + netdev carrier follow about `3-4s` later
  - This confirms that PHY-level read path gives early link indication, while port status is post-propagation state.

## 2026-03-16: CR881x live baseline capture

Build / board:
- ImmortalWrt `r38233+11-2109604410`
- Target `xiaomi_cr881x` (ipq50xx + yt9215s)

Raw capture files:
- `docs/yt921x/live/yt_regmap_live_cr881x_20260316_182545.txt`
- `docs/yt921x/live/yt_regmap_live_cr881x_20260316_full_chunked_ssh.txt`
- Curated map output:
  - `docs/yt921x/yt9215-register-map.md`

Capture method:
- `/proc/yt921x_cmd` helper (`reg/int/ext/dump`)
- UART session for broad baseline + SSH chunked re-dump for non-truncated windows

High-value observations:
- Core identity/control:
  - `0x080008 = 0x90020001` (`CHIP_ID` window)
  - `0x08000c = 0x0000c008` (`EXT_CPU_PORT`: CPU tagging enabled, CPU port id = 8)
  - `0x080010 = 0x00009988` (CPU tag TPID default)
- SERDES/XMII family:
  - `0x080388 = 0x00000002` (`CHIP_MODE`)
  - `0x080394 = 0x00000000` (`XMII_CTRL`)
  - `0x080400 = 0x04184108`, `0x080408 = 0x04184108` (port 8/9 XMII config registers)
  - `0x080364 = 0x0000001a`, `0x080368 = 0x00000012` (MDIO polling status windows for serdes-facing ports)
- VLAN/FDB core:
  - `0x180294 = 0x000006f9` (port isolation table base)
  - `0x1803d0 = 0x00000000` (port learn control base)
  - `0x180440 = 0x0000003c` (ageing)
  - `0x180454 = 0xccd84354`, `0x180458 = 0x70011c7a`, `0x18045c = 0x04000000` (FDB data in-window snapshot)
- MBUS windows:
  - `0x06a004 = 0x00000900` (`EXT_MBUS_CTRL`)
  - `0x0f0004 = 0x006a0908` (`INT_MBUS_CTRL`)

Window characteristics from chunked dump (`0x80000..0x800b0`, `0x100000..0x100324`, `0x180280..0x180470`):
- Total sampled regs: `372`
- Concrete values: `224`
- Gated/unimplemented signature (`0xdeadbeef`): `148`

MDIO responder scan (Clause 22 via helper):
- Internal (`int read`, ports 0..9, regs 0..31):
  - Ports `0..4` return coherent PHY IDs (`0x01e0:0x4281` on regs 2/3)
  - Ports `5..7` return all zeros
  - Ports `8..9` return repeated `0x1140` pattern (not coherent PHY ID; treat as non-standard/placeholder path)
- External (`ext read`, ports 0..31, regs 0..31):
  - All zero responses in this baseline (no external PHY discovered on ext bus in current board wiring/runtime state)

Notes for driver work:
- Baseline confirms CPU uplink path on port 8 with Motorcomm tag path active.
- External MDIO support is now plumbed in driver path, but no live ext C22 responder was found on CR881x hardware baseline.
- `0xdeadbeef` windows should remain read-only/guarded until specific semantics are confirmed.
