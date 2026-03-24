# YT9215 Register Map Changelog

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
