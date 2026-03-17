# YT9215 Switch Register Map Build Plan (Long-Run)

## Objective
Build a reliable, test-backed register map for YT9215 switch core and MDIO-facing blocks, suitable for upstream-quality driver improvements.

## Scope
- Core switch registers (`0x80000+`)
- Port MAC/status/control registers
- Internal MDIO interface window
- External MDIO interface window
- SERDES/XMII family
- Stock map translation path (page/phy/word)

## Inputs
- `/proc/yt921x_cmd` live reads/writes (debug build only)
- Stock artifacts: `stock.dtb`, `yt_switch.ko`, `qca-ssdk.ko`
- Existing notes:
  - `docs/yt921x/yt9215-discovered-register-inventory-2026-03-16.md`
  - `/tmp/immortalwrt-cleanup-20260316-*/...`
  - `/mnt/wsl/.../AX3000cv2/...` references

## Execution Phases
1. Baseline capture:
   - Read and archive known stable registers at boot-idle.
   - Capture per-port link up/down deltas.
2. Functional clustering:
   - Group registers by behavior: reset, forwarding, VLAN, FDB, MIB, PHY/MDIO, CPU port, SERDES/XMII.
3. Bitfield inference:
   - Toggle one control at a time, observe deterministic deltas.
   - Mark confidence: high/medium/low.
4. Cross-check with stock behavior:
   - Compare against stock init sequences and observed runtime values.
5. Driver landing set:
   - Promote only high-confidence fields into driver code.
   - Keep uncertain fields in docs only.

## Deliverables
- `docs/yt921x/yt9215-register-map.md` (curated map, confidence-tagged)
- `docs/yt921x/yt9215-register-map-changelog.md` (per-session deltas)
- Minimal safe driver changes tied to high-confidence fields only

## Safety Rules
- No blind write to unknown regs on live path.
- Always record pre/post values before write tests.
- One variable change per test case.
- Reboot after hazardous experiments to restore known state.

## Immediate Next Step
- Phase 1 baseline capture is now done for CR881x idle state:
  - `docs/yt921x/live/yt_regmap_live_cr881x_20260316_182545.txt`
  - `docs/yt921x/live/yt_regmap_live_cr881x_20260316_full_chunked_ssh.txt`
  - `docs/yt921x/yt9215-register-map-changelog.md`
- Next: run link-transition deltas (LAN port plug/unplug and CPU-side stress traffic), then promote only stable bitfields into `yt9215-register-map.md`.
