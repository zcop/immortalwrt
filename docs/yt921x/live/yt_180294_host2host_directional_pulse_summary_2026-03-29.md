# YT9215 `0x180294+` host-to-host directional pulse summary (2026-03-29)

## Goal
- Distinguish the active LAN known-unicast forwarding/isolation control from BUM-policy candidates.

## Runtime state
- Link state during tests:
  - `lan1`: up @ 1G
  - `lan2`: up @ 1G
  - `lan3`: down
  - `wan`: down
- Active hosts:
  - `192.168.2.100` (workspace host)
  - `192.168.2.169` (second LAN endpoint)

## Baseline registers
- Isolation rows (`0x180294 + 4*n`):
  - `0x180294=0x000006f9`
  - `0x180298=0x000006fa`
  - `0x18029c=0x000006fc`
  - `0x1802a0=0x000007ef`
  - `0x1802a4=0x000007e7`
  - `0x1802a8=0x000006ef`
  - `0x1802ac=0x000006ef`
  - `0x1802b0=0x000006ef`
  - `0x1802b4=0x000007f8`
  - `0x1802b8=0x000006ef`
  - `0x1802bc=0x000006ef`
- Candidate mask matrix:
  - `0x1805d0..0x18068c`: mostly `0x0000003f`
  - outliers: `0x1805d4=0x0000023f`, `0x1805d8=0x0000023f`

## Test A: `0x1805d4` / `0x1805d8` pulses under live host-to-host ICMP
- Traffic probe: continuous `ping 192.168.2.169` from `192.168.2.100`.
- Pulses:
  - `0x1805d4: 0x23f -> 0x23b -> 0x23f`
  - `0x1805d4: 0x23f -> 0x00000000 -> 0x23f`
  - `0x1805d8: 0x23f -> 0x00000000 -> 0x23f`
- Observed:
  - No deterministic host-to-host drop event in these pulses.
  - Representative results: `60/60`, `70/70`, `70/70` received.

## Test B: directional row pulse on `0x180294`
- Pulse:
  - `0x180294: 0x000006f9 -> 0x000006fb` for ~4s, then restore to `0x000006f9`.
- Observed (host-to-host):
  - run 1: `80 tx / 60 rx` (`25%` loss)
  - run 2: `70 tx / 51 rx` (`27.14%` loss)
- Simultaneous control probe:
  - `ping 192.168.2.1` from `192.168.2.100` stayed `70/70` (`0%` loss).

## Test C: remaining row pulses (`0x180298`, `0x18029c`)
- Traffic probes during each pulse:
  - host-to-host: `192.168.2.100 -> 192.168.2.169`
  - control: `192.168.2.100 -> 192.168.2.1`
- Pulses and outcomes:
  - `0x180298: 0x000006fa -> 0x000006fb -> 0x000006fa`
    - host-to-host: `70 tx / 50 rx` (`28.57%` loss)
    - control: `70 tx / 70 rx` (`0%` loss)
  - `0x180298: 0x000006fa -> 0x000006fe -> 0x000006fa`
    - host-to-host: `70/70`
    - control: `70/70`
  - `0x18029c: 0x000006fc -> 0x000006fd -> 0x000006fc`
    - host-to-host: `70/70`
    - control: `70/70`
  - `0x18029c: 0x000006fc -> 0x000006fe -> 0x000006fc`
    - host-to-host: `70/70`
    - control: `70/70`

## Inference
- `0x180294+` is active directional port-isolation / forwarding control for LAN-to-LAN path.
- In this link topology (`lan1` and `lan2` up), setting `0x180294` bit1 or `0x180298` bit0 produced deterministic LAN horizontal loss while CPU path remained intact.
- `0x180298` bit2 and tested `0x18029c` pulses did not affect this active LAN1/LAN2 path (likely target inactive/non-participating egress bits in this runtime).
- `0x1805d4` / `0x1805d8` are not the primary known-unicast forwarding matrix in this runtime; keep them as lower-confidence policy/BUM candidates.

## Post-test restore
- Restored:
  - `0x180294=0x000006f9`
  - `0x1805d4=0x0000023f`
  - `0x1805d8=0x0000023f`
