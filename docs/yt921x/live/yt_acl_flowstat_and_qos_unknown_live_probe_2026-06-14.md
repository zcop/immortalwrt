# YT9215 ACL flow-stat and adjacent unknown-window live probe (2026-06-14)

Platform:
- CR881x running current ImmortalWrt test image with `CONFIG_NET_DSA_YT921X_DEBUG=y`
- access path: `/sys/kernel/debug/yt921x_cmd`

Scope:
- verify that the recently added register-map rows for `0x220400`, `0x221000`, `0x221400`, `0x221c00`, `0x300000`, and `0x308000` are live-readable on real hardware
- capture boundary behavior around those windows

High-signal results:
- `0x220400` (`tbl 0xcd`) is a live-readable window; sampled entries were all `0x00000000`
- `0x221000` (`tbl 0xcf`) is a live-readable window; sampled entries were all `0x00000000`
- `0x221400` (`tbl 0xd0`) and `0x221c00` (`tbl 0xd1`) are live-readable and match the mainline ACL flow-stat patch layout exactly; sampled entries were all `0x00000000` in an idle runtime
- `0x300000` (`tbl 0xd2`) is a live-readable 64-word window with stable `0x00000007` contents across the full sampled range `0x300000..0x3000fc`
- `0x300100` immediately returns `0xdeadbeef`, which cleanly marks the end of the `0x300000` window
- `0x300200` is the known queue-map base and reads expected live values:
  - `0x300200 = 0x76543210`
  - `0x300204 = 0x76543210`
- `0x308000` (`tbl 0xd8`) is live-readable; sampled entries were all `0x00000000`

Direct probe output summary:

```text
reg 0x220400 = 0x00000000
reg 0x220404 = 0x00000000
...
reg 0x221000 = 0x00000000
reg 0x221004 = 0x00000000
...
reg 0x221400 = 0x00000000
reg 0x221408 = 0x00000000
reg 0x221c00 = 0x00000000
reg 0x221c04 = 0x00000000

reg 0x2213f0 = 0xdeadbeef
reg 0x2213f4 = 0xdeadbeef
reg 0x2213f8 = 0xdeadbeef
reg 0x2213fc = 0xdeadbeef
reg 0x221400 = 0x00000000

reg 0x221bf0 = 0xdeadbeef
reg 0x221bf4 = 0xdeadbeef
reg 0x221bf8 = 0xdeadbeef
reg 0x221bfc = 0xdeadbeef
reg 0x221c00 = 0x00000000

reg 0x300000 = 0x00000007
reg 0x300020 = 0x00000007
reg 0x300040 = 0x00000007
reg 0x300060 = 0x00000007
reg 0x300080 = 0x00000007
reg 0x3000a0 = 0x00000007
reg 0x3000c0 = 0x00000007
reg 0x3000e0 = 0x00000007
reg 0x3000f8 = 0x00000007
reg 0x3000fc = 0x00000007
reg 0x300100 = 0xdeadbeef
reg 0x300200 = 0x76543210
reg 0x300204 = 0x76543210

reg 0x307ff0 = 0xdeadbeef
reg 0x307ff4 = 0xdeadbeef
reg 0x307ff8 = 0xdeadbeef
reg 0x307ffc = 0xdeadbeef
reg 0x308000 = 0x00000000
reg 0x308004 = 0x00000000
```

Interpretation:
- The ACL flow-stat blocks are physically present and readable on this hardware, but our local driver does not currently program them in the tested image.
- `0x300000` is no longer just "unknown near QoS". It is a sharply bounded live 64-entry table directly adjacent to the queue-map tables and should be treated as a QoS/global lookup candidate in future reverse-mapping work.
- The repeated `0xdeadbeef` boundary behavior is a reliable signal for stepping off a valid stock-window mapping into an unmapped/gated region.
