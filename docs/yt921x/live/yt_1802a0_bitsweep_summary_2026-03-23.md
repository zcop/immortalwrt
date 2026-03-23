# YT9215 `0x1802a0` bit-sweep summary (2026-03-23)

## Source
- Raw log: `docs/yt921x/live/yt_1802a0_bitsweep_20260323_221509.txt`

## Test controls
- Fixed during sweep:
  - `wan@eth0`
  - `0x1802a4=0x000006e0`
  - `0x1802b4=0x000007ff`
- Per-case hygiene:
  - Router/Pi neighbor flush before each case
  - Per-case Pi `tcpdump` capture (ARP/ICMP)

## Cases and outcomes

| `0x1802a0` | Router ping to `172.16.9.1` | Pi capture |
|---|---|---|
| `0x000007ef` | PASS | ARP + ICMP |
| `0x000007ff` | FAIL | ARP only |
| `0x000007ee` | PASS | ARP + ICMP |
| `0x000007ed` | PASS | ARP + ICMP |
| `0x000007eb` | PASS | ARP + ICMP |
| `0x000007e7` | PASS | ARP + ICMP |
| `0x000007cf` | PASS | ARP + ICMP |
| `0x000007af` | PASS | ARP + ICMP |
| `0x0000076f` | PASS | ARP + ICMP |
| `0x000006ef` | PASS | ARP + ICMP |
| `0x000005ef` | PASS | ARP + ICMP |
| `0x000003ef` | PASS | ARP + ICMP |

Additional confirmation case:
- `0x1802a0=0x000006ff` (under same fixed controls) => FAIL, ARP only.

## Inference
- The failure condition is tied to **bit 4** of `0x1802a0`:
  - bit4 = `1` -> ARP-only, no ICMP progress to Pi (fail)
  - bit4 = `0` -> normal ICMP request/reply (pass)
- In this topology, WAN-over-eth0 requires `0x1802a0` bit4 cleared.

## Misroute symptom status
- Across passing and failing masks, dmesg still reports
  `Unexpected EtherType ... ingress=eth1 cpu_dp=8`.
- So this sweep found a reachability gate, but did not move return-path ingress off `cpu_dp=8`.

## Restore
- End state restored to normal runtime profile:
  - `wan@eth1`
  - `0x08000c=0x0000c008`
  - `0x1802a0=0x000006ff`
  - `0x1802a4=0x000006e8`
  - `0x1802b4=0x000007f7`
