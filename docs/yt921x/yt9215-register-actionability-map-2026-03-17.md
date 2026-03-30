# YT9215 Register Actionability Map (from `/mnt/wsl/tmp`)

## Scope
This map classifies discovered YT9215 register areas by how safe/useful they are
for immediate driver work on CR881x.

Primary sources:
- `/mnt/wsl/tmp/immortalwrt-save/yt921x-register-notes.md`
- `/mnt/wsl/tmp/yt-control-regs-2026-03-14.log`
- `/mnt/wsl/tmp/immortalwrt-save/AGENTS.md`
- `/mnt/wsl/tmp/immortalwrt-save/uart-dumps/switch-dump-*.txt`
- `/mnt/wsl/tmp/immortalwrt-save/uart-dumps-normal/switch-dump-*.txt`

Classification:
- `A`: safe and useful now (driver can rely on it)
- `B`: writable and coupled, but needs masked handling / care
- `C`: writable but semantics unclear (low immediate value)
- `D`: coerced or effectively non-writable at runtime (including likely fused/gated windows)
- `E`: backend/window reliability problem (do not trust yet)
- `F`: data/counter window (not control)

## A. Safe And Useful Now
| Range / register | Why it is actionable now |
|---|---|
| `0x08000c` (`EXT_CPU_PORT`) | Stable and decoded path for CPU port/tag enable; runtime confirms this is a single primary tagged-conduit selector, not a multi-CPU fanout register. |
| `0x080010` (`CPU_TAG_TPID`) | Stable fixed-function control (`0x9988`). |
| `0x80100 + 4*p` (`PORTn_CTRL`) | Per-port MAC control domain used by working driver path. |
| `0x80200 + 4*p` (`PORTn_STATUS`) | Deterministic link signature (`0x1fa` up vs `0x0e2` down style). |
| `0x180294 + 4*p` (`PORTn_ISOLATION`, ports `0..10`) | Active in bridge emulation and conduit steering; behavior observed and reproducible. Current conduit profiles on CR881x are: `wan@eth1` => `0x1802a0=0x6ff`, `0x1802a4=0x6e8`, `0x1802b4=0x7f7`, `0x1802b8=0x6ef`; `wan@eth0` => `0x1802a0=0x7ef`, `0x1802a4=0x6e0`, `0x1802b4=0x7ff`, `0x1802b8=0x6ef`. Additional 2026-03-23 probes show `0x1802a4` is a critical WAN-over-eth0 reachability gate (`0x6e0` pass, `0x6e8` hard fail with no egress on Pi), and `0x1802a0` bit4 is a deterministic fail trigger under fixed eth0 controls (`bit4=1` -> ARP-only fail, `bit4=0` -> ICMP pass). Minimal directional check also confirms directional semantics: `0x1802a0 bit4` blocks `3->4` while `0x1802a4 bit3` blocks `4->3` (`docs/yt921x/live/yt_port_isolation_directional_check_2026-03-23.md`). 2026-03-29 host-to-host pulses further confirmed active directional LAN control: `0x180294:0x6f9->0x6fb` and `0x180298:0x6fa->0x6fb` caused ~25-29% LAN1<->LAN2 loss while host->router control remained `0%` loss (`docs/yt921x/live/yt_180294_host2host_directional_pulse_summary_2026-03-29.md`). WAN-linked direct-capture hole-punch check (`0x1802b4:0x7f8->0x7f0`) still showed `0` UU/MC packets on Pi `eth0`, so this single-row change was not sufficient to expose WAN flood (`docs/yt921x/live/yt_bum_boundary_probe_uu_2026-03-29.md`). |
| `0x1803d0 + 4*p` (`PORTn_LEARN`, ports `0..10`) | Active in learning control path; used by DSA logic (`0x1803d8` toggled with `lan3` membership). |
| `0x18038c + 4*n` (`STPn`, instances `0..12`) | STP instance state words; `STPn(0)` is active runtime word, `STPn(1..12)` are writable and default zero in single-bridge runtime. |
| `0x180440..0x180464` (`AGEING/FDB_*`) | Consistent with FDB/ageing ops in current driver. |
| `0x080400/0x080408` (`XMIIn`) | Stable programmed XMII lane config, strong board-path relevance. |
| MBUS `int` (`0x0f0000` command path) | Reliable per-port PHY/runtime read access for `0..4`. |
| `0x354000..0x354024` (`PSCH_SHAPER[0..4]`) | Live-tested writable shaping control (`en/ebs/eir`) with reproducible bandwidth caps on CR881x. |

## B. Writable And Coupled (Use Masked Updates)
| Range / register | Observed behavior | Driver guidance |
|---|---|---|
| `0x80100 + 4*p` vs `0x80200 + 4*p` | Direct control writes can mutate status domain in non-trivial ways. | Keep masked writes and preserve unknown bits. |
| `0x080004` (`FUNC`) | Low-bit sweep showed mixed latch behavior and a destructive trigger: bit `5` (`trial=0x0000082b`) causes `0xdeaddead` readback across core/gated words and drops SSH until reboot. | Treat as hazardous global control; no exploratory writes in normal runtime. |
| `0x080014` (`PVID_SEL`) | Low-bit sweep: bits `15..11` ignored; bits `10..0` latch/restore. No gate-open observed, but still high-impact global domain. | Keep writes gated to controlled debug runs; always restore baseline. |
| `0x18038c` (`STPn(0)` runtime word) | Changed with bridge membership (`lan2` out: `...f3 -> ...ff`, `wan` in: `...f3 -> ...33`, combined: `...3f`). Direct write/readback works, but bridge events rewrite it to derived value. | Treat as coupled policy/state word; do not use as persistent config source. Capture before/after topology changes only. |
| `0x180510` (`FILTER_MCAST`), `0x180514` (`FILTER_BCAST`) | `0x00000400` is a safe runtime baseline on tested boots; a bad runtime was observed with both words at `0x000007ff`, correlating with host->router blackhole/asymmetric reachability until reset to `0x00000400`. 2026-03-29 UU boundary recheck (`docs/yt921x/live/yt_bum_boundary_probe_uu_2026-03-29.md`) showed low-nibble toggles around baseline (`0x401/0x402/0x404/0x408`) did not change observed UU flood delivery on `lan1/lan2` (`2/2` each case). Full-mask sweep (`0x000/0x400/0x7ff`) with UU+multicast also stayed invariant (`lan1=2`, `lan2=2`, `lan3=0`, `wan=0`). WAN-linked direct capture on Pi `eth0` with `0x180510=0x7ff` and `0x180514=0x7ff` also captured `0` target UU/MC packets. | Treat as high-impact policy words. Initialize to `0x00000400` and avoid broad all-port masks in normal runtime (`docs/yt921x/live/yt_180510_180514_blackhole_recovery_20260319_1655.txt`). |
| `0x180734-0x180738` (`ACT_UNK_*`) | 2026-03-23 sweep (`docs/yt921x/live/yt_180734_180738_action_probe_20260323_204904.txt`) shows only `0x180734[9:8]` is action-sensitive in WAN->`eth0` scenario: action `1/2` blocks router->Pi ICMP; action `0/3` passes. 2026-03-29 CPU8 LAN UU matrix (`docs/yt921x/live/yt_uu_cpu8_action_matrix_2026-03-29.md`) shows `[17:16]` actions on both `0x180734` and `0x180738` do not change observed flood to `lan1/lan2` (all cases `4/4`). Additional bit22 isolation on `0x180738` (`0x00420000` vs `0x00020000`, `[17:16]=2`) also showed no effect for UU or multicast (`4/4` both ports in all cases). Baseline in current image: `0x180734=0x00020000`, `0x180738=0x00420000`. Stock disassembly confirms table-role split: `0x180734` is unknown-ucast action table (`tbl 0xad` field0), `0x180738` is unknown-mcast/bypass table (`tbl 0xae`: field2 action, field1 IGMP bypass, field0 RMA bypass). | Keep defaults unless actively probing. Do not rely on `0x180738` for steering fixes; treat `0x180734[9:8]` as scenario-specific sensitive field (WAN/CPU4 path only, as observed so far). |
| `0x220800..` (`METER_CFG`) and `0x34c000..` (`QSCH_SHAPER`) | Structurally decodable but lane coupling is still incomplete. | Keep debug-access only until lane mapping is proven per-port. |

## C. Writable But Low-Confidence Semantics
| Range / register | Notes |
|---|---|
| `0x080028` (`SERDES_CTRL`) | Writable and stable readback, but bit-by-bit meaning still partial. |
| `0x08002c`, `0x080030`, `0x080040` | Writable in tests; no strong immediate side-effects mapped yet. |
| `0x08008c` (`SERDESn(8)`) | Writable and stable; safe for controlled uplink experiments only. |
| `0x080394` (`XMII_CTRL`) | Writable (`0->1->0`), no immediate externally visible effect in test. |
| `0x080230` (`loop_detect_top_ctrl`) | Stock reverse confirms this is loop-detect control (`tbl 0x0d`) with decoded fields: `f5=enable`, `f6=tpid`, `f8=generate-way`, `f4/f3/f2=unit-id`. Not yet wired in OpenWrt driver path. |
| `0x220100/0x220140/0x220200` (stock storm tables) | Stock reverse confirms hardware storm engine path (`tbl 0xc6/0xc9/0xcc`) with concrete field IDs used by `fal_tiger_storm_ctrl_*`; this is separate from current software `storm_guard` behavior. |
| `0x18028c` (11-bit mask before `PORTn_ISOLATION`) | Writable (`0x7ff <-> 0x0`), but the single-host LAN->router blackhole probe (`192.168.2.100 -> 192.168.2.1`) showed 0% ICMP loss in current topology (`docs/yt921x/live/yt_18028c_blackhole_probe_20260319_110516.txt`). |
| `0x18030c..0x180334` (11-word mask table) | Writable with stable `0x000007ff` readback mask per word; values persist across bridge membership toggles. Single-bit, per-word, all-words, cross-host live-toggle, post-flash TCP/ARP/multicast, and bridge-FDB probes still showed no immediate coupling to known active control words in current CR881x bridge runtime. |
| `0x1805d0..0x18068c`, `0x1806b8`, `0x1806bc` | 2026-03-25 broadcast-stress probe (`docs/yt921x/live/yt_bum_storm_candidate_probe_20260325_1345.md`) observed static values (`0x3f`/`0x23f`, `0x7ff`, `0x10`) during synchronized high-rate UDP broadcast. A/B tests on `0x1805d0` (`0x3f` vs `0x3e`) and `0x1806b8` (`0x7ff` vs `0x0ff`) produced near-identical `lan1_rx` deltas, so no measurable broadcast policing effect in this path. Follow-up MIB-delta A/B on `0x1805d4` bit9 (`0x23f -> 0x3f`) also showed no independent drop-counter surge beyond send-volume scaling (`docs/yt921x/live/yt_ab_1805d4_mib_broadcast_20260325_1450.txt`). Additional UU-source tests from `10.1.0.178` (`eth0`) with static-neighbor trap and A/B toggles on `0x1805d4/0x1805d8/0x180690/0x1806bc` did not reveal a deterministic clamp signature either (`docs/yt921x/live/yt_uu_ab_candidates_20260325_1500.txt`, `docs/yt921x/live/yt_uu_ab_dump_mib_20260325_1505.txt`). 2026-03-29 host-to-host pulses (`0x1805d4` and `0x1805d8` down to `0x00000000`) also did not cut known-unicast LAN1<->LAN2 path (`docs/yt921x/live/yt_180294_host2host_directional_pulse_summary_2026-03-29.md`). Additional 2026-03-29 LAN UU boundary recheck (`docs/yt921x/live/yt_bum_boundary_probe_uu_2026-03-29.md`) showed no UU flood change when `0x1805d4/0x1805d8` were zeroed individually or together (`2/2` captures on both `lan1` and `lan2`). WAN-linked direct capture also showed `0` UU/MC packets on Pi `eth0` when forcing `0x1805d4=0` or `0x1805d8=0`. New `0x180690` sweep (`docs/yt921x/live/yt_cpu_copy_180690_sweep_2026-03-29.md`) showed WAN side remained `0` for bits `0..10`, with router-side `eth0` deltas rising on `b9/b10`; split runs suggest those were MC-correlated (`b9_mc`/`b10_mc`), not UU-correlated. |
| `0x180500`, `0x355000` | 2026-03-25 low-bit sweep (`bit0..bit7`) under synchronized high-rate UDP broadcast (`docs/yt921x/live/yt_global_gate_sweep_180500_355000_20260325_143350.txt`) showed no measurable gating/policing effect on `lan1_rx` deltas; both regs restored cleanly to baseline `0x00000000`. |
| `0x31c000..0x31c03c` | 2026-03-29 safe bridge-state coupling check (`docs/yt921x/live/yt_vlan_filter_toggle_2c01_31c0_1880_probe_2026-03-29.md`): with `vlan_filtering` confirmed `0 -> 1 -> 0`, sampled words in this window remained byte-identical (`pre==on==post`). Treat as not coupled to this VLAN filter transition in current runtime. |
| `0x230080..0x2300a8` | 2026-03-29 egress-tag probe (`docs/yt921x/live/yt_egress_tagging_vtu_vs_ctrl1_2026-03-29.md`) kept all `PORTn_VLAN_CTRL1` words at `0x00000000` across tagged/untagged VLAN42 variants, while VTU word1 (`0x188154`) carried the untag bitmap transition (`0x0 -> 0x100`). Treat this block as non-coupled to observed egress tag membership in current runtime. |

## D. Coerced / Non-Plain Runtime Control
| Range / register | Notes |
|---|---|
| `0x080044` | Strongly runtime/gated; writes read back coerced or unchanged. |
| `0x080018`, `0x080038`, `0x080388` | Writes observed as ignored/coerced in runtime probe. |
| `0x1802c0..0x180308`, `0x180338..0x180388` | Reads stay `0xdeadbeef` across bridge/admin and staged `0x80004/0x80014` gate sweeps; no discovered unlock path. Classify as effectively hardware fused/gated on tested CR881x runtime. |
| MBUS reg `int[p].0x0` / `int[p].0x11` (selected ports) | Writes may report success but revert immediately; state-machine owned. |
| External MBUS reads on CR881x baseline | No discovered responders (all zeros in scans). |

## E. Unreliable Window Through Current Proc Backend
| Range | Evidence |
|---|---|
| `0x2c0100..0x2c013c` | 2026-03-29 idle/traffic poll (`docs/yt921x/live/yt_2c0100_idle_traffic_poll_2026-03-29.md`) showed fully static readback: all 16 words remained constant (unique=1) across 50 snapshots at idle and under WAN load; `0x2c0100` also stayed fixed over x200 polls in both conditions. Additional safe `vlan_filtering` coupling probe (`docs/yt921x/live/yt_vlan_filter_toggle_2c01_31c0_1880_probe_2026-03-29.md`) also showed `pre==on==post` for all sampled words while `0x180280` toggled as expected, reinforcing that direct reads on this window are opaque in this runtime. |

Interpretation:
- Do not promote `0x2c01xx` semantics into driver logic yet.
- Treat this window as read-only opaque until an explicit selector/trigger path
  is identified (direct reads alone show no command-state behavior).
- First fix/validate an independent low-level read backend.
- 2026-03-29 `tbl info 0x00..0xff` scan also showed debug table IDs are limited
  to QoS/meter/shaper (`0xc7/0xce/0xe4/0xe5/0xe9/0xea/0xeb/0xec`), so VTU/FDB
  is not currently reachable through the exposed `tbl` map.

## F. Data/Counter Window (Not Hidden Control)
| Range | Notes |
|---|---|
| `0x0c0100..0x0c01ff` | MIB/statistics window; naturally changes over time. |
| `0x0c0004` (`MIB_CTRL`) | Control register exists, but bit behavior is latched/masked and not plain RW. |

## Raw Vs Normal High-Value Deltas
Confirmed in archived dumps:
- `0x080004`: `0x00000809 -> 0x0000080b`
- `0x08000c`: `0x00008008 -> 0x0000c008`
- `0x080028`: `0x00000040 -> 0x00000041`
- `0x080044`: `0x00080000 -> 0x00488a24`
- `0x08008c`: `0x0000007a -> 0x000000fa`
- `0x31c030`: `0x000f000e -> 0x000f190e`
- `0x31c058`: `0x0c808000 -> 0x0c808200`
- `0x31c090`: `0x001b800e -> 0x000f190e`

## Immediate Driver-Oriented Use
1. Keep current strategy: preserve unknown `PORTn_CTRL` bits.
2. Treat `0x31c0xx` as init-programmed state; diff pre/post setup before writing.
3. Treat `0x80004` bit `5` as forbidden in normal test loops unless UART + reboot budget are in place.
4. Keep `0x2c01xx` out of functional logic until backend validity is proven.
5. Use MBUS (`int`) primarily for observation and PHY diagnostics, not forced persistent state.
