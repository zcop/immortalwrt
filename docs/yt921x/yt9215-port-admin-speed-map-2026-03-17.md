# YT9215 Port Admin/Speed Mapping (CR881x, 2026-03-17)

## Scope
This note records live mapping results for per-port `PORTn_CTRL` (`0x80100+4*p`)
and `PORTn_STATUS` (`0x80200+4*p`) on CR881x with the current YT921x driver.

Captured from UART/debugfs on build with kernel `6.12.74` (Mar 17, 2026).

## Port To Register Mapping
- `lan1` -> `0x80100` / `0x80200`
- `lan2` -> `0x80104` / `0x80204`
- `lan3` -> `0x80108` / `0x80208`
- `wan`  -> `0x8010c` / `0x8020c`

## Observed State Signatures

### User port linked at 1G (lan1 baseline)
- `PORTn_CTRL`: `0x000005fa`
- `PORTn_STATUS`: `0x000001fa`

### User port linked at 100M (lan1, AN advertisement limited to 100)
- `PORTn_CTRL`: `0x000005f9`
- `PORTn_STATUS`: `0x000001f9`

### User port admin down
- `PORTn_CTRL`: `0x000005e2` (or `0x00000582` on WAN lane)
- `PORTn_STATUS`: `0x000000e2` (or `0x00000082` on WAN lane)

## Delta Interpretation (from live toggles)
- Link/admin up to down (typical user lane):
  - `0x5fa -> 0x5e2`
  - `0x1fa -> 0x0e2`
- Main clear mask observed during down is consistent with:
  - `YT921X_PORT_LINK | YT921X_PORT_RX_MAC_EN | YT921X_PORT_TX_MAC_EN`
  - hex: `0x200 | 0x10 | 0x08 = 0x218`
- Speed bit transitions observed:
  - `1G (..fa)` vs `100M (..f9)` confirms low speed bit usage in `PORT_SPEED_M`.

## Internal MDIO Test Notes
Internal MDIO helper (`int read/write`) was used to force link renegotiation on
port 0 (lan1):
- baseline: BMCR `0x1140`, ANAR `0x1de1`, CTRL/STATUS `0x5fa/0x1fa`
- 100-only advertisement (`ANAR=0x0101`, restart AN) produced 100M link and
  `0x5f9/0x1f9`
- restoring advertisement (`ANAR=0x1de1`, `1000T=0x0600`) returned to
  1G `0x5fa/0x1fa`

Attempted 10-only advertisement did not produce a 10M link in this setup
(partner still came up at 100M), so no stable 10M signature was confirmed.

## Driver Impact
These results justify treating admin control as one shared mask in both port
disable and re-enable paths:
- `YT921X_PORT_CTRL_ADMIN_M = LINK | RX_MAC_EN | TX_MAC_EN`

This is now reflected in backport commit:
- `f0904d858c` (`backport: dsa: yt921x unify admin port control mask`)

## Raw Capture Logs
- `/tmp/yt-mbus-map-live-20260317-131508.log`
- `/tmp/yt-mbus-mdio-force-live-20260317-131704.log`
- `/tmp/yt-mbus-mdio-anadv-live-20260317-131801.log`
- `/tmp/yt-port-admin-map-allports-20260317-132123.log`
