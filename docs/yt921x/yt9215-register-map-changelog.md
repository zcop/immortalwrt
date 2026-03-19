# YT9215 Register Map Changelog

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
