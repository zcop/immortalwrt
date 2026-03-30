# YT9215 stock storm-control block A/B probe (`0x220100/0x220140/0x220200`)

Date: 2026-03-30

## Goal
- Move to next vendor feature after loop-detect header mapping.
- Validate whether stock storm-control block reacts on live traffic with safe A/B toggles.

## Baseline
- `0x220100 = 0x00000064`
- `0x220140 = 0x00000000`
- `0x220200 = 0x00000000`

## Stimulus used
- Workspace -> router UDP broadcast stream:
  - `yes x | head -n 5000 | nc -u -b -w 1 192.168.2.255 9`
- Measured on switch MIB (port `p0` / `lan1`):
  - `RX_BROADCAST @ 0x0c0100`
  - `RX_DROPPED   @ 0x0c0150`
  - `TX_PKT       @ 0x0c019c` (activity sanity)

Control observed repeatedly:
- `d_rb ≈ 8`, `d_rd = 0`, `d_rxp ≈ 53..58`

## A/B cases

Each case latched (readback matched) and preserved management reachability (`ping_loss=0%`).

1. `control`
- regs: `0x00000064 / 0x00000000 / 0x00000000`
- delta: `d_rb=8 d_rd=0 d_rxp=56`

2. `en_only`
- regs: `0x00000064 / 0x00000000 / 0x00000001`
- delta: `d_rb=8 d_rd=0 d_rxp=53`

3. `en_maskp0`
- regs: `0x00000064 / 0x00000001 / 0x00000001`
- delta: `d_rb=8 d_rd=0 d_rxp=53`

4. `en_maskp0_f1min`
- regs: `0x00000064 / 0x00000001 / 0x00000009`
- delta: `d_rb=8 d_rd=0 d_rxp=58`

5. `en_maskp0_mode1`
- regs: `0x00000064 / 0x00000001 / 0x0000000b`
- delta: `d_rb=8 d_rd=0 d_rxp=55`

6. `timeslot1_en`
- regs: `0x00000001 / 0x00000001 / 0x0000000b`
- delta: `d_rb=8 d_rd=0 d_rxp=58`

7. `timeslot4095_en`
- regs: `0x00000fff / 0x00000001 / 0x0000000b`
- delta: `d_rb=8 d_rd=0 d_rxp=58`

## Restore check
- Post-test readback:
  - `0x220100 = 0x00000064`
  - `0x220140 = 0x00000000`
  - `0x220200 = 0x00000000`

## Conclusion
- Stock storm registers are writable and stable in runtime.
- Under this broadcast stimulus, no measurable policer/drop effect appeared (`RX_DROPPED` unchanged, `RX_BROADCAST` unchanged vs control).
- This likely means one of:
  1. this traffic pattern does not hit the stock storm classifier path being configured,
  2. additional stock control tables/fields are required beyond tested tuples, or
  3. ingress class/type used here is not governed by these exact toggles.

## Notes
- Multicast stimulus attempt (`nc -u 239.1.2.3`) did not move `RX_MULTICAST` on this setup.
- Unicast sanity (`nc -u 192.168.2.1`) moved packet counters as expected.
