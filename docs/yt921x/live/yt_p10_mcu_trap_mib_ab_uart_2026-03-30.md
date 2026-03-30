# YT9215 p10 MCU trap mapping (UART MIB A/B) - 2026-03-30

## Goal
Probe p10/MCU-related trap behavior by watching MIB window deltas
(`0x0c0100..0x0c01fc`) while pulsing bit10-related control paths:
- `0x180690` (CPU copy candidate)
- `0x180510` (FILTER_MCAST)
- `0x180514` (FILTER_BCAST)

## Access path
- Router was not reachable via SSH from workstation (`192.168.2.1` timeout).
- Test executed over UART (`/dev/ttyUSB0`, minicom).
- Runtime command interface: `/sys/kernel/debug/yt921x_cmd`.

## Method
- Created `/tmp/p10_ab_fast.sh` on router via UART.
- For each target register:
  - Read original value.
  - Toggle bit10 (`auto`: set if clear, clear if set).
  - Generate traffic during pulse:
    - `uu`: unknown-unicast style (`192.168.2.199` + static neighbor MAC).
    - `mc`: multicast ping (`239.1.2.3`).
  - Restore original register.
  - Capture MIB snapshots before/after and print only non-zero deltas.

## Observed output pattern
- All six cases (`uu/mc` x `0x180690/0x180510/0x180514`) produced many non-zero
  deltas across `0x0c01b0..0x0c01fc`.
- No clean single-word signature was isolated from this run alone; delta set was
  broad and noisy under short flood traffic.
- Registers were restored successfully after run:
  - `0x180690 = 0x00000001`
  - `0x180510 = 0x00000400`
  - `0x180514 = 0x00000400`

## Artifacts (on router runtime tmpfs)
- `/tmp/p10_ab_20260329_161715.log` (aborted slow variant)
- `/tmp/p10_ab_fast_20260329_161750.log` (completed fast run)

## Next refinement
- Add control/no-toggle runs with same traffic generator to establish noise floor.
- Limit comparison to stable signal candidates first (instead of full 0x100-byte
  window) and compare `delta(trial) - delta(control)`.
- Run with a fixed external source path (single ingress port and fixed packet
  count) to reduce broadcast-domain variation.

## Refinement run (delta-delta, same day)

### Method update
- Added explicit control window and trial window per case:
  - `delta(control)` = same generator, no register toggle
  - `delta(trial)` = same generator, bit10 toggled
  - reported `dd = delta(trial) - delta(control)`
- Narrowed MIB candidate set:
  - `0x0c0100`, `0x0c0130`, `0x0c013c`, `0x0c0184`,
    `0x0c01b0`, `0x0c01c0`, `0x0c01d4`, `0x0c01f0`
- Runtime artifact:
  - `/tmp/p10_delta_delta_20260329_162202.log`

### Delta-delta summary
- `uu` / `0x180690`:
  - `0x0c01c0 dd=-66977792`, `0x0c01d4 dd=-16320`, `0x0c01f0 dd=-16386`
- `uu` / `0x180510`:
  - `0x0c01b0 dd=16448`, `0x0c01c0 dd=67174398`,
    `0x0c01d4 dd=-68157406`, `0x0c01f0 dd=67141628`
- `uu` / `0x180514`:
  - `0x0c01b0 dd=16448`, `0x0c01c0 dd=67239906`,
    `0x0c01d4 dd=1064960`, `0x0c01f0 dd=57340`
- `mc` / `0x180690`:
  - `0x0c01b0 dd=8194`, `0x0c01c0 dd=67067902`,
    `0x0c01d4 dd=-66191360`, `0x0c01f0 dd=8192`
- `mc` / `0x180510`:
  - `0x0c01b0 dd=-33554400`, `0x0c01c0 dd=-32704`,
    `0x0c01d4 dd=-1048640`, `0x0c01f0 dd=33603582`
- `mc` / `0x180514`:
  - `0x0c01b0 dd=32768`, `0x0c01c0 dd=67141698`,
    `0x0c01d4 dd=-67108926`, `0x0c01f0 dd=131070`

### Interpretation
- After control subtraction, signal still concentrates in:
  - `0x0c01b0`, `0x0c01c0`, `0x0c01d4`, `0x0c01f0`
- The earlier candidate words
  (`0x0c0100`, `0x0c0130`, `0x0c013c`, `0x0c0184`) did not show stable
  discriminating `dd` in this pass.
- Safe baselines re-verified post-run:
  - `0x180690=0x00000001`, `0x180510=0x00000400`, `0x180514=0x00000400`

## Refinement run #2 (fixed-size packet + median-of-5)

### Method update
- Packet payload locked to `200` bytes (`ping -s 200`) for both generators:
  - `uu`: `192.168.2.199` with static-neighbor fake MAC
  - `mc`: `239.1.2.3`
- For each case/register:
  - 5 control/trial pairs
  - computed `dd = delta(trial) - delta(control)` per run
  - reported median of 5 `dd` values
- Candidate set stayed narrowed:
  - `0x0c01b0`, `0x0c01c0`, `0x0c01d4`, `0x0c01f0`
- Runtime artifact:
  - `/tmp/p10_median5_20260329_162749.log`

### Median results
- `uu`, `0x180690` (`0x00000001 -> 0x00000401`)
  - `0x0c01b0=16386`, `0x0c01c0=33701820`,
    `0x0c01d4=-1048704`, `0x0c01f0=-16386`
- `uu`, `0x180510` (`0x00000400 -> 0x00000000`)
  - `0x0c01b0=-2`, `0x0c01c0=-2`,
    `0x0c01d4=69205954`, `0x0c01f0=-8222`
- `uu`, `0x180514` (`0x00000400 -> 0x00000000`)
  - `0x0c01b0=8190`, `0x0c01c0=16384`,
    `0x0c01d4=-16384`, `0x0c01f0=0`
- `mc`, `0x180690` (`0x00000001 -> 0x00000401`)
  - `0x0c01b0=131072`, `0x0c01c0=-16316`,
    `0x0c01d4=-1163424`, `0x0c01f0=-8192`
- `mc`, `0x180510` (`0x00000400 -> 0x00000000`)
  - `0x0c01b0=-67117058`, `0x0c01c0=131072`,
    `0x0c01d4=-64`, `0x0c01f0=-33431556`
- `mc`, `0x180514` (`0x00000400 -> 0x00000000`)
  - `0x0c01b0=-49150`, `0x0c01c0=8256`,
    `0x0c01d4=1048576`, `0x0c01f0=8192`

### Interpretation
- Fixed-size + median-of-5 confirms these four words are the practical p10-path
  signal set in this runtime.
- The dominant discriminator still depends on path/register:
  - `0x0c01d4` is strongest for `uu` with `0x180510` toggle.
  - `0x0c01b0`/`0x0c01f0` show strongest `mc` separation for `0x180510`.
- Post-run safety check remained clean:
  - `0x180690=0x00000001`, `0x180510=0x00000400`, `0x180514=0x00000400`

## Size-binning sweep (fixed count, UU path)

### Goal
- Distinguish packet vs byte vs size-bin behavior using fixed packet count and
  varying payload size (`64`, `200`, `512`).

### Run A (broad targets, high count)
- Script: `/tmp/p10_sizebin.sh`
- Count: `100000`
- Targets: `0x180690`, `0x180510`, `0x180514`
- Candidates: `0x0c01b0`, `0x0c01c0`, `0x0c01d4`, `0x0c01f0`
- Artifact: `/tmp/p10_sizebin_20260329_163236.log`

Observed:
- Large u32-domain jumps and wrap-like values occurred in multiple words,
  including values near `2^32` boundaries.
- Delta shape changed by target register and size, but not in a clean linear
  `64:200:512` ratio that would confidently label one word as pure byte counter
  under this probe path.

### Run B (reduced count sanity run)
- Script: `/tmp/p10_sizebin_small.sh`
- Count: `5000`
- Target: `0x180690` only
- Sizes: `64`, `200`, `512`
- Artifact: `/tmp/p10_sizebin_small_20260329_163319.log`

Observed (same 4 candidates):
- Signals remained non-monotonic across sizes; several words still exhibited
  wrap-domain magnitudes and polarity flips in `dd`.
- This indicates these sampled words are likely not plain standalone counters
  in this read path (possible mixed/latch/state composition), or the trap path
  updates coupled bins simultaneously enough to obscure direct size-bin decode.

### Conclusion for size-binning pass
- The 4-word p10 signature remains valid as an operational discriminator set:
  - `0x0c01b0`, `0x0c01c0`, `0x0c01d4`, `0x0c01f0`
- But this pass did **not** yet yield a definitive byte-vs-packet-vs-size-bin
  labeling for each individual word.
- Safe baselines remained restored after both runs:
  - `0x180690=0x00000001`, `0x180510=0x00000400`, `0x180514=0x00000400`

## `0x0c0004` MIB control write-probe (freeze/snapshot check)

### Goal
- Validate whether `0x0c0004` can freeze/latch MIB reads to avoid read tearing.
- Test writes: `0x00000001`, `0x00000400`, `0x00000000`.
- For each mode:
  - 1-second idle drift check.
  - deterministic injection check (`10` packets, `200`-byte payload).
  - monitored candidates + adjacent words:
    - `0x0c01b0/b4`, `0x0c01c0/c4`, `0x0c01d4/d8`, `0x0c01f0/f4`
- Artifact:
  - `/tmp/mib_ctrl_probe_20260329_163718.log`

### Key observations
- Initial read before mode tests:
  - `orig_ctrl = 0x80000001` (matches prior note that high bit can be transient).
- Mode `0x00000001`:
  - readback `0x00000001` (sticky).
  - idle and post-inject windows still showed broad multi-word movement.
- Mode `0x00000400`:
  - readback `0x00000000` (non-sticky / ignored / auto-clear in this path).
  - no freeze-like stabilization; counters still moved during idle and inject.
- Mode `0x00000000`:
  - readback `0x00000000`.
  - movement profile remained active (no freeze behavior).
- Final restore/readback:
  - `0x0c0004 = 0x00000001`

### Interpretation
- In this runtime, `0x0c0004` did **not** behave as a practical freeze/snapshot
  gate for stable reads of the tested MIB words.
- `0x1` appears to be normal-running enable state; `0x400` is not an effective
  per-port latch control from this interface.

## `0x0c0004` bit31 follow-up probe

### Goal
- Explicitly test missing bit31 write modes:
  - `0x80000000`
  - `0x80000001`
- Verify whether repeated reads of `0x0c01b0` become stable (freeze behavior).

### Runtime artifact
- `/tmp/mib_bit31_probe_20260329_164257.log`

### Results
- Initial state before this follow-up:
  - `orig=0x00000001`
- Mode `0x80000000`:
  - readback `ctrl=0x00000000`
  - repeated `0x0c01b0` reads still changed (`r1!=r2/r3`)
- Mode `0x80000001`:
  - readback `ctrl=0x00000001`
  - repeated reads still changed
- Mode `0x00000001`:
  - readback `ctrl=0x00000001`
  - repeated reads still changed
- Restored:
  - `0x0c0004=0x00000001`

### Conclusion
- Bit31 writes did not reveal a usable freeze/snapshot mode through this
  interface.
- Practical next path is pair-aware (lo/hi) 64-bit delta math rather than
  relying on a latch bit that is not behaving as expected in runtime.

## Hi-Lo-Hi + fixed-count size sweep (WAN path, UU)

### Goal
- Re-run fixed-count size sweep using stable free-running pair reads
  (Hi-Lo-Hi) and pair-wise 64-bit split deltas.
- Attempt packet-vs-byte scaling inference for:
  - `0x0c01b0/0x0c01b4`
  - `0x0c01c0/0x0c01c4`
  - `0x0c01d4/0x0c01d8`
  - `0x0c01f0/0x0c01f4`

### Runtime details
- Target toggle: `0x180690` bit10 (`0x00000001 -> 0x00000401`).
- Traffic path: UU on `wan` (`172.16.9.199` with static fake MAC).
- Sizes tested: `64`, `200`, `512`.
- Final run used flood/deadline mode for bounded runtime:
  - `ping -I wan -s <size> -f -c 5000 -w 2 ...`
- Artifact:
  - `/tmp/p10_u64_size_sweep_wan_fast_20260329_164845.log`

### Observation
- Hi-Lo-Hi removed obvious read-tearing artifacts on individual pair reads, but
  resulting control/trial split deltas were still highly coupled and non-linear
  across sizes (large high-word movement in multiple pairs in each window).
- No clean monotonic `64 -> 200 -> 512` ratio emerged for a single isolated
  pair that would confidently map it as a pure byte counter in this runtime.

### Practical conclusion
- Pair-aware math is necessary and now implemented, but with free-running
  internal activity and no confirmed hardware latch mode, this path still does
  not yield definitive per-pair RMON semantic labels.

## One-packet structural diff (raw words, not math deltas)

### Goal
- Test whether `0x0c01xx` behaves like descriptor/mailbox data rather than
  monotonic counters.
- Method:
  - disable LAN/AP interfaces temporarily to reduce external noise.
  - take two snapshots (`1s` apart): `idle_drift_1s`.
  - inject exactly one packet (`wan`, `-s 200`, `-c 1` to fake neighbor).
  - take third snapshot and compare raw word transitions (`after_1pkt`).

### Runtime artifact
- `/tmp/one_pkt_struct_20260329_165649.log`
- snapshots:
  - `/tmp/onepkt_b1_snapshot.txt`
  - `/tmp/onepkt_b2_snapshot.txt`
  - `/tmp/onepkt_after_snapshot.txt`

### Key observation
- `after_1pkt` changed many words across the full `0x0c01b0..0x0c01f4` window
  (not just one monotonic bin), with structural-looking value substitutions.
- Example transitions observed:
  - `0x0c01b0 0x35144c62 -> 0x37146c60`
  - `0x0c01c0 0x31144c60 -> 0x35144c62`
  - `0x0c01c4 0x35140c60 -> 0x31048c20`
  - `0x0c01f4 0x31140c60 -> 0x31040c20`

### Interpretation
- This behavior is consistent with a live metadata/data-pipeline window
  (descriptor/mailbox/ring-like view), not a simple set of independent
  RMON packet/byte counters.

## True-RMON hunt tooling (prepared)

### Why
- With `0x0c01xx` now treated as descriptor/mailbox-like, the next step is to
  locate monotonic packet/byte counters in other MMIO regions.

### Tool added
- `tools/yt921x/rmon_hunt_scan.sh`
  - snapshots candidate ranges at `t0`, `t1` (idle), `t2` (after trial traffic)
  - computes wrap-safe u32 deltas and score:
    - `d_idle = t1 - t0`
    - `d_trial = t2 - t1`
    - `score = d_trial - d_idle`
  - ranks top addresses where trial traffic dominates idle drift.

### Default candidate ranges
- `0x040000:0x200`
- `0x100000:0x200`
- `0x200000:0x200`
- `0x2c0000:0x200`

### Intended runtime invocation
```sh
tools/yt921x/rmon_hunt_scan.sh \
  --ranges '0x040000:0x400,0x100000:0x400,0x200000:0x400,0x2c0000:0x400' \
  --idle-sec 1 \
  --traffic-cmd 'ping -I wan -s 200 -c 1000 172.16.9.199 >/dev/null 2>&1 || true' \
  --top 80
```

### Runtime note
- At time of writing, live SSH path to `192.168.2.1` was unavailable, so this
  scan was prepared but not executed in this session.
