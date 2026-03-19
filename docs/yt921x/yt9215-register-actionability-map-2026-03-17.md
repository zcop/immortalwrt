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
| `0x08000c` (`EXT_CPU_PORT`) | Stable and decoded path for CPU port/tag enable. |
| `0x080010` (`CPU_TAG_TPID`) | Stable fixed-function control (`0x9988`). |
| `0x80100 + 4*p` (`PORTn_CTRL`) | Per-port MAC control domain used by working driver path. |
| `0x80200 + 4*p` (`PORTn_STATUS`) | Deterministic link signature (`0x1fa` up vs `0x0e2` down style). |
| `0x180294 + 4*p` (`PORTn_ISOLATION`, ports `0..10`) | Active in bridge emulation; behavior observed and reproducible (including `0x1802a0` on `wan` membership changes). |
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
| `0x180510` (`FILTER_MCAST`), `0x180514` (`FILTER_BCAST`) | `0x00000400` is a safe runtime baseline on tested boots; a bad runtime was observed with both words at `0x000007ff`, correlating with host->router blackhole/asymmetric reachability until reset to `0x00000400`. | Treat as high-impact policy words. Initialize to `0x00000400` and avoid broad all-port masks in normal runtime (`docs/yt921x/live/yt_180510_180514_blackhole_recovery_20260319_1655.txt`). |
| `0x220800..` (`METER_CFG`) and `0x34c000..` (`QSCH_SHAPER`) | Structurally decodable but lane coupling is still incomplete. | Keep debug-access only until lane mapping is proven per-port. |

## C. Writable But Low-Confidence Semantics
| Range / register | Notes |
|---|---|
| `0x080028` (`SERDES_CTRL`) | Writable and stable readback, but bit-by-bit meaning still partial. |
| `0x08002c`, `0x080030`, `0x080040` | Writable in tests; no strong immediate side-effects mapped yet. |
| `0x08008c` (`SERDESn(8)`) | Writable and stable; safe for controlled uplink experiments only. |
| `0x080394` (`XMII_CTRL`) | Writable (`0->1->0`), no immediate externally visible effect in test. |
| `0x18028c` (11-bit mask before `PORTn_ISOLATION`) | Writable (`0x7ff <-> 0x0`), but host->router blackhole probe showed 0% ICMP loss in current topology (`docs/yt921x/live/yt_18028c_blackhole_probe_20260319_110516.txt`). |
| `0x18030c..0x180334` (11-word mask table) | Writable with stable `0x000007ff` readback mask per word; values persist across bridge membership toggles. Single-bit, per-word, all-words, cross-host live-toggle, post-flash TCP/ARP/multicast, and bridge-FDB probes still showed no immediate coupling to known active control words in current CR881x bridge runtime. |

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
| `0x2c0100..0x2c013c` | Raw dump shows structured values; normal runtime reads show printable ASCII-like words. |

Interpretation:
- Do not promote `0x2c01xx` semantics into driver logic yet.
- First fix/validate an independent low-level read backend.

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
