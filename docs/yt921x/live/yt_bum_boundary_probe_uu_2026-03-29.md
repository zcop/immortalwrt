# YT9215 BUM Boundary Probe (UU) - 2026-03-29

## Goal
Check whether these candidates gate unknown-unicast flood delivery to LAN ports:
- per-port matrix candidates: `0x1805d4`, `0x1805d8`
- global flood candidates: `0x180510`, `0x180514` (low nibble bits)

## Method
- Unknown destination pinned:
  - `192.168.2.199 lladdr 02:de:ad:be:ef:00 dev br-lan`
- Stimulus:
  - `ping -I br-lan -c 2 -W 1 192.168.2.199`
- Observation:
  - concurrent `tcpdump` on `lan1` and `lan2`
  - counted `ICMP echo request` per test case.

## Baseline and safety
- Baseline constants used and restored:
  - `0x180510=0x00000400`
  - `0x180514=0x00000400`
  - `0x1805d4=0x0000023f`
  - `0x1805d8=0x0000023f`
- Final readback confirmed baseline restored.

## Results (unknown unicast)
- baseline: `lan1=2`, `lan2=2`
- `0x1805d4 -> 0x00000000`: `lan1=2`, `lan2=2`
- `0x1805d8 -> 0x00000000`: `lan1=2`, `lan2=2`
- `0x1805d4=0x0` + `0x1805d8=0x0`: `lan1=2`, `lan2=2`
- `0x180510` toggles:
  - `0x00000401`: `2/2`
  - `0x00000402`: `2/2`
  - `0x00000404`: `2/2`
  - `0x00000408`: `2/2`
- `0x180514` toggles:
  - `0x00000401`: `2/2`
  - `0x00000402`: `2/2`
  - `0x00000404`: `2/2`
  - `0x00000408`: `2/2`

## Full-mask sweep (`0x000`, `0x400`, `0x7ff`) with UU + multicast
Tested both registers with full-mask extremes and baseline:
- `0x180510`: `0x00000000`, `0x00000400`, `0x000007ff`
- `0x180514`: `0x00000000`, `0x00000400`, `0x000007ff`

Per case, two stimuli were used:
- UU: destination `192.168.2.199`
- MC: destination `239.1.2.3`

Capture counters (`ICMP echo request`) were taken on:
- `lan1`, `lan2`, `lan3`, `wan`

Observed in all full-mask cases:
- `lan1=2`, `lan2=2`, `lan3=0`, `wan=0`
- i.e. no observed leakage across boundary into `lan3`/`wan`, and no change in
  UU/MC flood behavior for this workload.

## Conclusion
- In this workload, none of the tested toggles changed observed UU flood delivery
  to `lan1`/`lan2`.
- In this workload, full-mask extremes on `0x180510/0x180514` (`0x000/0x7ff`)
  also did not change observed UU/MC delivery pattern and did not breach into
  `lan3`/`wan`.
- This further demotes these specific tested bits as direct UU flood gates in the
  current runtime path.

## WAN-linked direct capture recheck (Pi `eth0`, same date)

Additional setup:
- `wan` physically linked (`LOWER_UP`) to Pi `eth0` at `172.16.9.1/24`.
- Observation moved from LAN-side counters to direct WAN-side packet capture:
  - `tcpdump -ni eth0 "icmp and (host 192.168.2.199 or host 239.1.2.3)"`

Additional trials:
- baseline (no register override)
- `0x180510 -> 0x000007ff` (temporary), then restore
- `0x180514 -> 0x000007ff` (temporary), then restore
- `0x1805d4 -> 0x00000000` (temporary), then restore
- `0x1805d8 -> 0x00000000` (temporary), then restore
- isolation hole-punch attempt:
  - `0x1802b4: 0x000007f8 -> 0x000007f0` (clear bit3 for `p8 -> p3`), then
    restore to `0x000007f8`

Observed in every trial above:
- Pi WAN capture remained `0 packets captured` for the UU/MC probe filter.

Post-test safe state verified:
- `0x180510=0x00000400`
- `0x180514=0x00000400`
- `0x1805d4=0x0000023f`
- `0x1805d8=0x0000023f`
- `0x1802b4=0x000007f8`
