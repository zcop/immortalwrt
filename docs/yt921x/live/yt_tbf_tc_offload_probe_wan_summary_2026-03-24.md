# YT9215 TBF offload validation summary (`wan`) (2026-03-24)

## Source
- Raw log: `docs/yt921x/live/yt_tbf_tc_offload_probe_wan_20260324_094055.txt`

## Goal
- Verify `tc tbf` offload path programs PSCH shaper registers with expected rate/burst scaling.

## Method
- On `wan`:
  - baseline (no qdisc)
  - `tc qdisc replace ... tbf rate 10mbit burst 32k latency 50ms`
  - `tc qdisc replace ... tbf rate 50mbit burst 64k latency 50ms`
  - `tc qdisc del dev wan root`
- Capture per step:
  - `tc qdisc show dev wan`
  - debugfs `tbf` dump (`eir`, `ebs`, `en`)
  - raw PSCH regs `0x354000..0x354024`

## Observed
- Offload is active (`tc` shows `offloaded`).
- Only port3 shaper slot changed (matches WAN port mapping).
- 10mbit/32k profile:
  - `tbf p3: en=1 eir=15480 (~10000 kbps) ebs=512 (~32768 bytes)`
  - `0x354018 = 0x08003c78`, `0x35401c = 0x00000010`
- 50mbit/64k profile:
  - `tbf p3: en=1 eir=76880 (~50000 kbps) ebs=1024 (~65536 bytes)`
  - `0x354018 = 0x10012c50`, `0x35401c = 0x00000010`
- After `tc qdisc del`:
  - `en` bit cleared (`0x35401c = 0x00000000`)
  - `EIR/EBS` word (`0x354018`) remained at last programmed value (`0x10012c50`) in this run.

## Inference
- TBF offload programming/scaling is correct and deterministic for active profiles.
- Teardown path disables shaping correctly (`EN=0`), but EIR/EBS retention was observed; this may be harmless state retention or a destroy-path cleanup gap worth follow-up.
