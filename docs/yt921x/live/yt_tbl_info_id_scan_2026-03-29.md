# YT9215 `tbl info <id>` scan (`0x00..0xff`) (2026-03-29)

## Goal
Discover which table IDs are exposed by `yt921x_cmd` runtime debug interface.

## Method
- On router: loop `id=0x00..0xff`.
- For each: run `tbl info <id>` and record non-`unknown table id` outputs.

## Result
Only the following IDs are implemented:

- `0xc7` meter-timeslot (`base=0x220104`)
- `0xce` meter-config (`base=0x220800`)
- `0xe4` qsch-slot-time (`base=0x340008`)
- `0xe5` psch-slot-time (`base=0x34000c`)
- `0xe9` qsch-shp-cfg (`base=0x34c000`)
- `0xea` qsch-meter-cfg (`base=0x34f000`)
- `0xeb` psch-shp-cfg (`base=0x354000`)
- `0xec` psch-meter-cfg (`base=0x357000`)

All other IDs returned `err=-2` (unknown/unimplemented in this debug table map).

## Conclusion
- Current `tbl` interface exposes QoS/meter/shaper tables only.
- No VTU/FDB/ACL-style table IDs are exposed through this path in current patch.
