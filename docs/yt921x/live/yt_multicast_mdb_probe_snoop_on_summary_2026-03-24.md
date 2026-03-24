# YT9215 multicast MDB probe summary (2026-03-24)

## Sources
- `docs/yt921x/live/yt_multicast_mdb_probe_20260324_094150.txt`
- `docs/yt921x/live/yt_multicast_mdb_probe_vid1_20260324_094214.txt`
- `docs/yt921x/live/yt_multicast_mdb_probe_snoop_on_20260324_094305.txt`

## Goal
- Validate MDB programming path and check immediate coupling to mapped multicast-related registers.

## Observed
- With `br-lan multicast_snooping=0`:
  - `bridge mdb add ...` failed (`RTNETLINK answers: Invalid argument`).
- With `multicast_snooping=1`:
  - `bridge mdb add dev br-lan port lan1 grp 239.1.1.1 vid 1 permanent` succeeded.
  - `bridge mdb show` reported: `permanent offload` on `lan1`.
- During successful add/del sequence, sampled regs were unchanged:
  - `0x180510 = 0x00000400`
  - `0x180514 = 0x00000400`
  - `0x180734 = 0x00020000`
  - `0x180738 = 0x00020000`

## Inference
- MDB offload path is functionally present (kernel reports `offload`) when snooping is enabled.
- The sampled flood/action registers above are not directly toggled by this specific static MDB operation, so multicast forwarding state is likely maintained in other tables/pathways.
