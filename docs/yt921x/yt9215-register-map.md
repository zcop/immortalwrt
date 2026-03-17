# YT9215 Register Map (CR881x Baseline)

## Scope
This is the current mapped view for CR881x from live switch reads.

Primary data:
- `docs/yt921x/live/yt_regmap_live_cr881x_20260316_182545.txt`
- `docs/yt921x/live/yt_regmap_live_cr881x_20260316_full_chunked_ssh.txt`
- `docs/yt921x/live/yt_regmap_live_cr881x_20260316_195556_postfix_recovery.txt`
- Driver symbols in `target/linux/generic/backport-6.12/830-02-v6.19-net-dsa-yt921x-Add-support-for-Motorcomm-YT921x.patch`
- UART transition run:
  - `docs/yt921x/live/yt_link_event_monitor_20260316_210441_uart_transitions_final.txt`

Confidence key:
- `High`: symbol + behavior + live value all align.
- `Medium`: symbol aligns, behavior still needs delta tests.
- `Low`: currently only positional hint.

## Block Map
| Range | Current mapping | Confidence |
|---|---|---|
| `0x080000-0x08003f` | global core control (`RST/FUNC/CHIP_ID/CPU_PORT/TPID`) | High |
| `0x080080-0x080090` | MAC base + per-serdes lane register (`SERDESn`) | High |
| `0x080100-0x08012b` | per-port MAC control (`PORTn_CTRL`) | High |
| `0x080200-0x08022b` | per-port MAC status (`PORTn_STATUS`) | High |
| `0x080320-0x080328` | strap function/value/capture | Medium |
| `0x080364-0x080368` | serdes MDIO polling summary | Medium |
| `0x080388-0x080394` | chip mode + XMII global control | High |
| `0x080400+` | per-XMII port config (`XMIIn`) | High |
| `0x06a000-0x06a00c` | external MBUS command window | High |
| `0x0f0000-0x0f000c` | internal MBUS command window | High |
| `0x100000+` | egress/TPID area, partially gated | Medium |
| `0x180280-0x180470` | VLAN/isolation/learning/ageing/FDB op | High |
| `0x0c0004` | MIB control | Medium |
| `0x0e0000-0x0e0004` | EDATA control/status | Medium |

## Key Registers (Live Values)
| Address | Symbol | Live value | Decode / meaning | Confidence |
|---|---|---|---|---|
| `0x080008` | `YT921X_CHIP_ID` | `0x90020001` | chip ID register valid | High |
| `0x08000c` | `YT921X_EXT_CPU_PORT` | `0x0000c008` | `TAG_EN=1`, `PORT_EN=1`, CPU port=`8` | High |
| `0x080010` | `YT921X_CPU_TAG_TPID` | `0x00009988` | default Motorcomm CPU tag TPID | High |
| `0x080028` | `YT921X_SERDES_CTRL` | `0x00000041` | serdes global control bits set | Medium |
| `0x080388` | `YT921X_CHIP_MODE` | `0x00000002` | chip mode profile is non-zero active mode | High |
| `0x080394` | `YT921X_XMII_CTRL` | `0x00000000` | no extra global XMII override bits | Medium |
| `0x080400` | `YT921X_XMIIn(8)` | `0x04184108` | per-port XMII config programmed | High |
| `0x080408` | `YT921X_XMIIn(9)` | `0x04184108` | per-port XMII config programmed | High |
| `0x080364` | `YT921X_MDIO_POLLINGn(8)` | `0x0000001a` | link=`1`, full duplex=`1`, speed code=`2` | High |
| `0x080368` | `YT921X_MDIO_POLLINGn(9)` | `0x00000012` | link=`0`, full duplex=`1`, speed code=`2` | High |
| `0x06a004` | `YT921X_EXT_MBUS_CTRL` | `0x00000900` | ext MBUS last command encoding (volatile latch) | High |
| `0x0f0004` | `YT921X_INT_MBUS_CTRL` | `0x006a0908` | int MBUS last command encoding (volatile latch) | High |
| `0x180280` | `YT921X_VLAN_IGR_FILTER` | `0x00000000` | no explicit bypass bits set | Medium |
| `0x1803d0` | `YT921X_PORTn_LEARN(0)` | `0x00000000` | learn defaults for lower ports | Medium |
| `0x180440` | `YT921X_AGEING` | `0x0000003c` | ageing interval=`0x003c` | High |
| `0x180454` | `YT921X_FDB_IN0` | `0xccd84354` | active FDB input/result window | Medium |
| `0x180458` | `YT921X_FDB_IN1` | `0x70011c7a` | active FDB input/result window | Medium |
| `0x18045c` | `YT921X_FDB_IN2` | `0x04000000` | active FDB input/result window | Medium |
| `0x180464` | `YT921X_FDB_RESULT` | `0x00008f05` | `DONE=1`, `INDEX=0x0f05` | Medium |
| `0x0c0004` | `YT921X_MIB_CTRL` | `0x00000001` | MIB logic enabled/running (`bit31` seen as transient latch) | Medium |
| `0x0e0004` | `YT921X_EDATA_DATA` | `0x00000003` | EDATA status idle | Medium |

## Port Control/Status Snapshot
`PORTn_CTRL` (`0x80100 + 4*n`), ports `0..10`:
- `p0=0x000007fa`, `p1=0x000000fa`, `p2=0x000007fa`, `p3=0x000007fa`, `p4=0x000007fa`
- `p5=0x000007fa`, `p6=0x000007fa`, `p7=0x000007fa`, `p8=0x000000fa`, `p9=0x000007fa`, `p10=0x000007fa`

`PORTn_STATUS` (`0x80200 + 4*n`), ports `0..10`:
- `p0=0x000000e2`, `p1=0x000001fa`, `p2=0x000000e2`, `p3=0x000000e2`, `p4=0x000001fa`
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
- `p0=0x6f9`, `p1=0x6fa`, `p2=0x6fc`, `p3=0x6ff`, `p4=0x6ff`, `p5=0x6ff`
- `p6=0x6ff`, `p7=0x6ff`, `p8=0x7ff`, `p9=0x6ff`, `p10=0x6ff`

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
