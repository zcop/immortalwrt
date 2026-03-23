# YT9215 port-isolation directional check (2026-03-23)

## Source
- Raw log: `docs/yt921x/live/yt_port_isolation_directional_check_20260323_223450.txt`

## Goal
Verify directional meaning of two isolation bits using minimal cases:
- `0x1802a0` bit4 (port3 -> port4 block)
- `0x1802a4` bit3 (port4 -> port3 block)

Test setup:
- `wan@eth0`
- fixed companion: `0x1802b4=0x000007ff`
- per-case router/Pi neighbor flush + Pi tcpdump capture.

## Cases

### 1) Control
- `0x1802a0=0x000007ef`, `0x1802a4=0x000006e0`
- Result: PASS (`3/3`)
- Pi capture: ARP + ICMP request/reply (`arp=3`, `icmp=6`)

### 2) Block port3->port4
- `0x1802a0=0x000007ff` (bit4 set), `0x1802a4=0x000006e0`
- Result: FAIL (`0/3`)
- Pi capture: ARP only (`arp=6`, `icmp=0`)
- Interpretation: Pi sees ARP exchange, but ICMP path fails because return toward port4 is blocked.

### 3) Block port4->port3
- `0x1802a0=0x000007ef`, `0x1802a4=0x000006e8` (bit3 set)
- Result: FAIL (`0/3`)
- Pi capture: no packets (`arp=0`, `icmp=0`)
- Interpretation: source path from port4 to port3 is blocked, so nothing reaches Pi.

## Conclusion
This directly confirms directional semantics:
- In `PORT3_ISOLATION` (`0x1802a0`), bit4 blocks `3 -> 4`.
- In `PORT4_ISOLATION` (`0x1802a4`), bit3 blocks `4 -> 3`.

## Restore
End state after test is normal runtime:
- `wan@eth1`
- `0x1802a0=0x000006ff`
- `0x1802a4=0x000006e8`
- `0x1802b4=0x000007f7`
