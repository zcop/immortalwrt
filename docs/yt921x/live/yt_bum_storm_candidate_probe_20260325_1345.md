# YT9215 BUM Storm Candidate Probe (2026-03-25)

## Scope
- Targeted live probe of candidate storm-control registers in:
  - `0x1805d0..0x1806bc` (L2/flood/action neighborhood)
  - QoS-neighbor windows (`0x353000..0x355f00`) for readable islands
- Runtime device: CR881x, `yt921x_cmd` debugfs helper

## Discovery Summary
- In `0x353000..0x355f00`, only three readable islands were found:
  - `0x353000..0x3531ff` (all zeros)
  - `0x354000..0x354054` (known PSCH/TBF area)
  - `0x355000..0x355028` (all zeros)
- In `0x180500..0x1807ff`, candidate cluster observed:
  - `0x1805d0..0x18068c`: mostly `0x0000003f`
  - `0x1805d4` and `0x1805d8`: `0x0000023f`
  - `0x180690 = 0x00000001`
  - `0x1806b8 = 0x000007ff`
  - `0x1806bc = 0x00000010`

## Traffic Method
- Broadcast load generated from host using Python UDP socket:
  - Destination: `192.168.2.255`
  - Typical 4s burst: ~`339k` datagrams
  - Long run: `count=1017730`, `errs=0`
- In parallel, sampled:
  - Register snapshots from `yt921x_cmd`
  - `lan1` RX packet counter

## A/B Results
- `0x1805d0` bit test (`0x3f` vs `0x3e`) under identical 4s burst:
  - `delta=339338` vs `delta=339313` (`lan1_rx`)
  - No measurable policing effect
- `0x1806b8` threshold test (`0x7ff` vs `0x0ff`) under identical 4s burst:
  - `delta=339326` vs `delta=339329` (`lan1_rx`)
  - No measurable policing effect
- Timed toggles on `0x18068c` (`0x3f <-> 0x3e`) during active broadcast:
  - No sustained gating behavior attributable to register value

## Conclusion
- For the tested broadcast path, the probed candidates did **not** behave as
  active storm limiter controls.
- Current evidence classifies:
  - `0x1805d0..0x18068c` as static mask-like policy words in this workload
  - `0x1806b8/0x1806bc` as static config words (not active broadcast threshold/mode)
- Next probe should switch traffic type to unknown-unicast stress and include
  targeted high-bit sweeps on `0x1805d4/0x1805d8`.
