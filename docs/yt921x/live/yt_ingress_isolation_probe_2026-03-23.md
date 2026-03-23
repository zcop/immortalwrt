# YT9215 ingress/isolation probe (2026-03-23)

## Scope
Focused probe on non-`ACT_UNK` steering registers observed to change with conduit flips:
- `0x1802a4`
- `0x1802b4`

Target symptom:
- `wan@eth0` active, but return traffic still reported as `ingress=eth1 cpu_dp=8`.

## Baseline snapshots by conduit
From live probe:
- `wan@eth0`
  - `0x08000c=0x0000c004`
  - `0x1802a0=0x000007ef`
  - `0x1802a4=0x000006e0`
  - `0x1802b4=0x000007ff`
  - `0x1802b8=0x000006ef`
- `wan@eth1`
  - `0x08000c=0x0000c008`
  - `0x1802a0=0x000006ff`
  - `0x1802a4=0x000006e8`
  - `0x1802b4=0x000007f7`
  - `0x1802b8=0x000006ef`

## Controlled A/B (with ARP/neighbor flush)
Per-case setup:
- force `wan@eth0`
- flush router neighbor: `ip neigh flush dev wan 172.16.9.1`
- flush Pi neighbor: `ip neigh flush dev eth0 192.168.2.1`
- run `ping -I wan -c 3 172.16.9.1`
- capture on Pi `eth0` via `tcpdump`.

### Case A
- `0x1802a4=0x000006e0`, `0x1802b4=0x000007ff`
- Result: PASS (`3/3` ping)
- Pi capture: ARP + ICMP request/reply (`arp=3`, `icmp=6`)

### Case B
- `0x1802a4=0x000006e0`, `0x1802b4=0x000006ef`
- Result: PASS (`3/3` ping)
- Pi capture: ARP + ICMP request/reply (`arp=3`, `icmp=6`)

### Case C
- `0x1802a4=0x000006e8`, `0x1802b4=0x000007ff`
- Result: FAIL (`0/3` ping)
- Pi capture: no packets (`arp=0`, `icmp=0`)

## Interpretation
- In this workload, `0x1802a4` is the dominant reachability gate:
  - `0x6e0` allows WAN-over-eth0 traffic.
  - `0x6e8` hard-breaks it (no egress seen on Pi).
- `0x1802b4` alone is not a deterministic fail trigger once ARP/neighbor state is normalized.
- None of these tests changed the core return-path symptom: dmesg continues to show
  `Unexpected EtherType ... ingress=eth1 cpu_dp=8`.

## State restore
After probe:
- `wan` conduit restored to `eth1`.
- temporary register overrides reset to eth1 baseline (`0x1802a4=0x6e0` then conduit switch reapplied runtime profile).
