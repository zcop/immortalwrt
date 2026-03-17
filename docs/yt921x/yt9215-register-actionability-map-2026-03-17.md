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
- `D`: coerced or effectively non-writable at runtime
- `E`: backend/window reliability problem (do not trust yet)
- `F`: data/counter window (not control)

## A. Safe And Useful Now
| Range / register | Why it is actionable now |
|---|---|
| `0x08000c` (`EXT_CPU_PORT`) | Stable and decoded path for CPU port/tag enable. |
| `0x080010` (`CPU_TAG_TPID`) | Stable fixed-function control (`0x9988`). |
| `0x80100 + 4*p` (`PORTn_CTRL`) | Per-port MAC control domain used by working driver path. |
| `0x80200 + 4*p` (`PORTn_STATUS`) | Deterministic link signature (`0x1fa` up vs `0x0e2` down style). |
| `0x180294 + 4*p` (`PORTn_ISOLATION`) | Active in bridge emulation; behavior observed and reproducible. |
| `0x1803d0 + 4*p` (`PORTn_LEARN`) | Active in learning control path; used by DSA logic. |
| `0x180440..0x180464` (`AGEING/FDB_*`) | Consistent with FDB/ageing ops in current driver. |
| `0x080400/0x080408` (`XMIIn`) | Stable programmed XMII lane config, strong board-path relevance. |
| MBUS `int` (`0x0f0000` command path) | Reliable per-port PHY/runtime read access for `0..4`. |

## B. Writable And Coupled (Use Masked Updates)
| Range / register | Observed behavior | Driver guidance |
|---|---|---|
| `0x80100 + 4*p` vs `0x80200 + 4*p` | Direct control writes can mutate status domain in non-trivial ways. | Keep masked writes and preserve unknown bits. |
| `0x080004` (`FUNC`) | Toggling bits can collapse `0x80044` state quickly. | Avoid exploratory writes in normal runtime. |
| `0x080014` (`PVID_SEL`) | Also perturbs `0x80044` and nearby runtime signature. | Treat as high-impact global control. |

## C. Writable But Low-Confidence Semantics
| Range / register | Notes |
|---|---|
| `0x080028` (`SERDES_CTRL`) | Writable and stable readback, but bit-by-bit meaning still partial. |
| `0x08002c`, `0x080030`, `0x080040` | Writable in tests; no strong immediate side-effects mapped yet. |
| `0x08008c` (`SERDESn(8)`) | Writable and stable; safe for controlled uplink experiments only. |
| `0x080394` (`XMII_CTRL`) | Writable (`0->1->0`), no immediate externally visible effect in test. |

## D. Coerced / Non-Plain Runtime Control
| Range / register | Notes |
|---|---|
| `0x080044` | Strongly runtime/gated; writes read back coerced or unchanged. |
| `0x080018`, `0x080038`, `0x080388` | Writes observed as ignored/coerced in runtime probe. |
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
3. Keep `0x2c01xx` out of functional logic until backend validity is proven.
4. Use MBUS (`int`) primarily for observation and PHY diagnostics, not forced persistent state.
