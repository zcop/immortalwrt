# YT9215 Discovered Register Inventory (from cleanup docs)

Source scope:
- `/tmp/immortalwrt-cleanup-20260316-064443/yt9215s-consolidated-map-2026-03-11.md`
- `/tmp/immortalwrt-cleanup-20260316-064443/yt9215-init-gap-analysis.md`
- `/tmp/immortalwrt-cleanup-20260316-064443/target/linux/generic/hack-6.12.disabled/941-net-dsa-yt921x-add-optional-debugfs-register-dumps.patch`
- `/tmp/immortalwrt-cleanup-20260316-064443/target/linux/generic/hack-6.12.disabled/942-net-dsa-yt921x-add-post-setup-control-register-dump.patch`
- `/tmp/immortalwrt-cleanup-20260316-064443/import_docs/wsl_driver_docs_2026-03-16/AX3000cv2/Stock Setting Capture/uart-stage-2026-03-07/compare/GATE-REG-PROBE-2026-03-08-continued.md`
- `/tmp/immortalwrt-cleanup-20260316-064443/import_docs/wsl_driver_docs_2026-03-16/AX3000cv2/Stock Setting Capture/uart-stage-2026-03-07/compare/GATE-TRY-80000-80004-2026-03-08.md`
- `/tmp/immortalwrt-cleanup-20260316-064443/import_docs/wsl_driver_docs_2026-03-16/AX3000cv2/Stock Setting Capture/uart-stage-2026-03-07/compare/WRITEABILITY-SCAN-2026-03-07.md`
- `/tmp/immortalwrt-cleanup-20260316-064443/import_docs/wsl_driver_docs_2026-03-16/mnt/wsl/tmp/immortalwrt-save/yt921x-register-notes.md`

Notes:
- This list is address-like tokens only (not all runtime values).
- `count` = number of mentions in the selected docs.
- Mentions do not imply register semantics are fully known.

## Core control block (0x80000-0x80408)
- `0x80000` (count 8)
- `0x80004` (count 11)
- `0x80008` (count 1)
- `0x8000c` (count 6)
- `0x80010` (count 2)
- `0x80014` (count 9)
- `0x80018` (count 1)
- `0x80028` (count 8)
- `0x8002c` (count 5)
- `0x80030` (count 3)
- `0x80038` (count 3)
- `0x80040` (count 3)
- `0x80044` (count 17)
- `0x8008c` (count 8)
- `0x80100` (count 1)
- `0x80108` (count 7)
- `0x80120` (count 6)
- `0x80128` (count 7)
- `0x80200` (count 6)
- `0x80208` (count 6)
- `0x80220` (count 9)
- `0x80228` (count 6)
- `0x80358` (count 2)
- `0x8035c` (count 2)
- `0x80364` (count 1)
- `0x80388` (count 2)
- `0x80394` (count 4)
- `0x80400` (count 9)
- `0x80408` (count 5)

## Gated / unknown windows
- `0x2c0040` (count 1)
- `0x2c006c` (count 1)
- `0x2c0080` (count 1)
- `0x2c0098` (count 1)
- `0x2c0100` (count 4)
- `0x2c0108` (count 9)
- `0x2c011c` (count 35)
- `0x2c013c` (count 2)
- `0x2c0148` (count 1)
- `0x2c016c` (count 1)
- `0x2c0180` (count 1)
- `0x2c0198` (count 1)
- `0x2c0240` (count 1)
- `0x2c0258` (count 1)
- `0x31c000` (count 3)
- `0x31c004` (count 1)
- `0x31c008` (count 1)
- `0x31c030` (count 20)
- `0x31c050` (count 1)
- `0x31c058` (count 7)
- `0x31c090` (count 7)
- `0x31c0b0` (count 1)
- `0x31c0f0` (count 1)
- `0x31c0fc` (count 2)

## Vendor patch/tuning blocks
- `0x180294` (count 1)
- `0x180508` (count 1)
- `0x18050c` (count 1)
- `0x180690` (count 1)
- `0x180734` (count 1)
- `0x180738` (count 1)
- `0x180904` (count 3)
- `0x2801d0` (count 6)
- `0x2801d4` (count 1)
- `0x2801d8` (count 1)
- `0x2801dc` (count 1)
- `0x281000` (count 3)
- `0x281004` (count 1)
- `0x301000` (count 3)
- `0x301004` (count 1)
- `0x301140` (count 1)
- `0x301144` (count 1)
- `0x303000` (count 3)
- `0x303004` (count 1)
- `0x303008` (count 1)
- `0x30300c` (count 1)
- `0x303020` (count 1)

## Additional 0x0cxxxx window entries
- `0xc0004` (count 4)
- `0xc0100` (count 1)
- `0xc01b0` (count 1)
- `0xc01fc` (count 1)
