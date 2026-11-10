# CR881x NSS memory budget

Status: 2026-11-10. Build42 is installed in slot0; Build41 remains in slot1
as rollback. Build42 retains Build41's experimental Wi-Fi NSS256MiB allocation profile and Build37's
512KiB DMA bootstrap, Build36's host ring reductions and Build38's disabled
slab diagnostics. Firmware carve-outs are unchanged. See
[nss-qosmio-wifi.md](nss-qosmio-wifi.md) for the resumed Wi-Fi work and its
remaining validation gates. Memory changes and their validation notes are
committed separately on `nss-experiment`.

Wi-Fi NSS was paused during the memory investigation. The
read-only Build36 analysis found24.53MiB available and6.04MiB anonymous pages.
Slab40.074MiB reconciles as33.449MiB ordinary cache backing plus6.625MiB
large-kmalloc accounting residual. That residual's owners remain unmeasured.
The remaining14.06MiB kmalloc4k cache is consistent with nominal13MiB of
ath11k RX/monitor/status payload capacity, but capacity arithmetic is not an
allocation-owner census. CE and NSS page-fragment backing remains separate.

Other identified allocations include three2MiB DMA pools,4.969MiB module
backing and a separate2MiB executable allocation inferred to be the BPF JIT
pack. The latter two are already inside the12.031MiB vmalloc page total;
stacks and ECM tables also overlap that view and must not be added again.
There are2577 clock-debug entries, a candidate for a smaller debug tree while
preserving summaries. Potential next savings are up to4.5MiB from a512KiB DMA
bootstrap, about1.9MiB per smaller BPF pack, and roughly2MiB physical from
clock debug metadata. All are unmeasured candidates; allocator growth, pack
occupancy, reclamation and MemAvailable accounting affect the actual result.
That analysis applied no tuning. The subsequent Build37 DMA test is described
below; BPF and clock-debug tuning remain unapplied. Full analysis and caveats:
`logs/cr8812dx-memory-analysis36-20261110/REPORT.md`.

A later read-only memory-island check confirms the2MiB range
0x4ae00000–0x4b000000 is entirely Linux-managed. The three usable physical
ranges total162.8203125MiB after early reservations, matching MemTotal; all
managed pages share Node0 DMA. At inspection the buddy allocator had four free
4MiB blocks, with zero allocation stalls, compaction failures or OOM kills.
Holes limit physical coalescing but no additional stranded Linux capacity was
demonstrated. Details: `logs/cr8812dx-memory-islands-20261110/REPORT.md`.

A refreshed same-session comparison with192.168.101.1 measured48.23MiB
available on AX3000 versus27.50MiB on Build37. Its MemTotal is179.66MiB
versus162.82MiB here. The20.73MiB available gap decomposes into16.84MiB
additional managed memory (16MiB absent NSS reservation plus0.84MiB other
early/kernel difference) and3.89MiB net difference in MemTotal minus
MemAvailable. This is availability arithmetic, not direct NSS ownership.
The peer has neither qca_nss_drv nor ECM loaded, despite its qca_nss_dp/SSDK
Ethernet modules. It uses more anonymous memory and6.5MiB more DMA capacity,
offsetting some of its lower slab footprint. Raw evidence and caveats:
`logs/cr8812dx-ax3000-memory-refresh-20261110/REPORT.md`.

## Build41: hzyitc host buffers plus NSS256 allocations

Build40's first Wi-Fi NSS integration still selected the donor512M allocation
setting. Existing hzyitc host rings remained small, but separate NSS pools and
TX bookkeeping were oversized for this256MiB board. Build41 corrects the device
profile without changing firmware memory regions or mode1 layout.

Actual compiler-flag assertions confirm TX completion2048, RX1024, monitor
status512/buffer128/destination128 and TX bookkeeping2048. The NSS256 profile
skips unused host TX arrays, uses4096 descriptor/extension pools with matching
radio limits, capsCE5 at256 and isolates RX/CE page-fragment caches. Donor512
RXDMA-ring changes were not imported. NSS pbuf watermarks stay unchanged.
Both radios report the active profile on the installed image.

Warm startup36464KiB(35.61MiB) is5716KiB above Build40's30748KiB; SUnreclaim
falls5416KiB. Main Wi-Fi traffic minimum20460KiB(19.98MiB), warm cleanup25216KiB
(24.63MiB). Confirmed cold startup36648KiB(35.79MiB), first-association forwarded
TCP minimum26580KiB(25.96MiB), cleanup24768KiB(24.19MiB). Final wired TCP passes
at936.64Mbps with minimum23204KiB(22.66MiB); a concurrent health sample reports
23364KiB available. These are bounded samples, not a daily-load guarantee.

Throughput asymmetry, UDP receiver loss and REO error counters remain open.
See [Wi-Fi NSS results](nss-qosmio-wifi.md) and
`logs/cr8812dx-wifi-nss-256m-20261110/RESULT.json`. During these tests, Build38
was retained in slot0 and Build41 ran slot1. Build42 has since replaced slot0,
retaining the same memory profile; Build41 is the fallback. Both warm/cold
test setups were restored.

## Build38: slab diagnostics disabled

The CR881x NSS seed now explicitly disables KERNEL_SLUB_DEBUG and
KERNEL_SLUB_DEBUG_ON, matching the old saved OpenWrt settings. The resolved
kernel config also drops STACKDEPOT, its frame-count setting and STACKTRACE.
No other final kernel setting changed. debugfs, cgroups/MEMCG, NSS/ECM, the
firmware maps, DMA settings and the ath11k module remain unchanged. Detailed
slab diagnostics, /proc/slabinfo and /proc/PID/stack are consequently absent;
aggregate slab totals remain available in /proc/meminfo.

At60.89seconds uptime, MemTotal remained166728KiB and MemAvailable measured
30988KiB (30.26MiB), within Build37's30972–31188KiB startup range. Slab was
40788KiB and SUnreclaim33680KiB, respectively248KiB and252KiB below Build37's
initial startup snapshot. Against its separate warm-boot snapshot these deltas
were192KiB and196KiB. These are different boots, so this is a small observed
slab reduction rather than an exact allocation-owner measurement. There is no
clear MemAvailable gain beyond baseline variation. The image shrank20480bytes.

TCP received936.48/936.37Mbps with96.84/97.35%CPU idle. The900Mbps-offered UDP
run received891.29Mbps with0.9678%loss; one repeat received899.94Mbps with
0.000778%loss. Neither run increased NSS failure/drop counters. Minimum sampled
available RAM during the main traffic suite was26060KiB (25.45MiB). DMA pools
remained512KiB each, without sampled growth. Final available RAM was28228KiB
(27.57MiB), with no kernel warning or configuration change. Both radio firmware
domains and all eight QinQ NSS nodes were running. Wi-Fi NSS remains paused.

Build38 was installed by sysupgrade into slot0. Build37's raw slot1 hash was
verified again after the tests. No cold power-cycle was performed for Build38.
Detailed build/config proof, backups, traffic, memory and UART evidence:
`logs/cr8812dx-memory-sluboff-20261110/`.

## Build37:512KiB DMA bootstrap

The NSS-specific DTS overrides coherent_pool=2M with512K. The ordinary CR881x
DTS retains2M. Patch0828 and the Build36 ath11k settings are unchanged. The
compiled device tree's reserved-region reg bytes match the baseline, MemTotal
remains166728KiB and the ath11k module is byte-identical to Build36.

Patch0829 adds root-readable pool_used_dma/dma32/kernel snapshots to the
existing dma_pools debugfs directory. Each reads gen_pool_size minus
gen_pool_avail, clamped to zero if growth races the reads. Allocation, free
and resize behavior is unchanged. These are current snapshots, not lifetime
or boot peaks.

After sysupgrade and a subsequent normal warm reboot, all three pools stayed
at524288bytes each:1.5MiB total instead of6MiB. This measures4.5MiB less pinned
capacity. Both radio firmware domains and all eight NSS nodes started without
kernel warnings. Warm-boot available RAM was30.25MiB. A user-confirmed ten-second
power removal also passed: new boot ID, both radio firmware domains and NSS
running, no kernel warning, unchanged configuration,1.5MiB DMA capacity and
30.42MiB available. A further cold-boot TCP run reached936.65Mbps with98.06%CPU
idle, minimum26.50MiB available and no new failure/drop counters. All pools
still measured512KiB. No pool growth occurred across these startup/load checks.

Wired TCP measured936.66/936.52Mbps, and UDP at900Mbps offered received
899.50Mbps with0.05585%receiver loss. No NSS drop/allocation-failure counter
increased. Minimum available RAM was25.36MiB during40seconds of sampling.
Pool capacity never grew; all sampled usage values were zero. This does not
establish a zero boot/lifetime peak or validate AP/client-load behavior.
Wi-Fi NSS remains paused. Exact image, build, backups, checks and logs are in
`logs/cr8812dx-memory-dma512-20261110/`.

## Build36: additional ath11k buffer reductions

The optional `ATH11K_SMALLBUFFERS` build setting adapts
[hzyitc's implementation](https://github.com/hzyitc/openwrt-redmi-ax3000/commit/d99e7803ff034f11a60e1adc3095940ae5b1bba5).
It reduces TX completion entries from4096 to2048 and monitor-buffer entries
from1288 to128. Normal RX1024, monitor-status512 and monitor-destination128
stay unchanged. The option defaults off and is enabled in the CR881x NSS
configuration seed. Existing regular/AHB packages remain in use, with no
package-provider or dependency changes. Firmware reservations, firmware memory
mode and the existing alloc_skb workaround are unchanged.

The first build was rejected because kernel6.18 ignored EXTRA_CFLAGS for module
compilation, producing the same image as Build35. The corrected build uses
CFLAGS_MODULE. Its saved dp/dp_rx compiler commands include the define, and the
new module extracted from the sysupgrade image matches the built rootfs. The
installed module hash was checked after boot.

First live snapshots showed SUnreclaim falling from43984 to34020KiB:9.73MiB
less. MemAvailable rose from18504 to25896KiB:7.22MiB more. These are different
boots, so changes in other allocations/caches prevent equating slab savings
with the whole available-memory difference. Both radio firmware domains and
all eight NSS interfaces started, with no kernel warning. The short wired
test reached930.94Mbps,97.63%CPU idle, no new drop/failure counters and a
minimum21.29MiB available. A later user-confirmed cold boot also passed, with
26036KiB (25.43MiB) available,33976KiB SUnreclaim, both radio firmware domains
running, all eight NSS interfaces present and no kernel warning. Original
network configuration and ECM policy were unchanged; no Wi-Fi test interface
remained. Build35 was retained in slot1 during this test; Build37 subsequently
replaced that slot, and Build36 was then retained in slot0; Build38 has since replaced it, with
Build37 retained in slot1.

A temporary5GHz/80MHz STA joined the existing ZC5s AP while both computers
kept their existing connections. It installed no default route or DNS. Two
rate-limited TCP streams reached199.83Mbps TX and172.72Mbps RX with zero TCP
retransmits and minimum20.47MiB available. Both firmware domains stayed running
with no kernel warnings. Because both endpoints used the shared AP, this is
client-load validation, not an independent peak-throughput benchmark. The
original router configuration hashes and host route were verified after
restoration; the temporary server/runtime stopped. AP mode and2.4GHz traffic
remain unvalidated on these smaller rings.

A further unrestricted three-stream test used the existing iperf3 on
192.168.101.1. The MikroTik route to its subnet is directly connected over
sfp1, and the peer sees the MikroTik's192.168.101.116 address on its1Gbps LAN.
Thus this path has one Wi-Fi link, without the workstation's second Wi-Fi hop.
An exact server route on the CR881x selected its STA interface while WAN stayed
the default route. Receiver results were501.59Mbps TX and475.92Mbps RX, with
275/31TCP retransmits respectively. Router aggregate CPU idle was27.58%, and
minimum available RAM was19.05MiB. No kernel warning or firmware-domain loss
occurred. The traffic generator ran on the CR881x itself, so this does not prove
the forwarding ceiling or unchanged performance versus Build35. Wi-Fi NSS is
disabled. Even with Wi-Fi NSS, TCP generation/reception local to the router
would still use the host CPU; offload benefit needs a forwarded flow between
external endpoints. The observed PHY rates varied from648.5Mbps before the
runs to1200.9Mbps TX/960.7Mbps RX afterwards. These snapshots do not isolate
the roughly500Mbps bottleneck. Temporary
routes/configuration, binaries, peer server and SSH session were restored or
removed afterwards. The MikroTik configuration was read only.

Bounded third-party review attempts produced no verdict: InferX hit quota and
the alternate provider could not read the scoped inputs. Primary review and
actual compiler/image/runtime checks are the acceptance evidence. No external
review approval is claimed. Exact sources and results are retained under
`logs/cr8812dx-memory-smallbuffers-20261110/`.

## Physical RAM and Linux allocations

Physical RAM is256MiB. Fixed NOMAP regions reserve16MiB for NSS,48MiB for
WCSS and10MiB for secure/boot/SMEM uses. Kernel and early reservations take
19.1796875MiB more, leaving MemTotal162.8203125MiB. The physical ledger adds
up exactly. Runtime buffers, slab, DMA pools and applications live inside
MemTotal; they must not be subtracted from physical RAM again.

Build34 samples show SUnreclaim about43MiB and only10–15MiB MemAvailable.
Processes use about6MiB anonymous memory. Process count/RSS cannot explain
the rest: RSS also repeats shared mappings, and excludes kernel/firmware costs.
NSS held2330SKBs in an inspected snapshot. A previous estimate multiplied this
by a4KiB data object plus256-byte header; that estimate is withdrawn. The
Qosmio driver calls dev_alloc_skb(), and this kernel uses shared page fragments
for its1984-byte request under GFP_ATOMIC. Buffer count therefore cannot be
converted directly into4KiB slab objects or physical residency. Reused buffers
may also have different backing. Exact NSS page ownership is still unmeasured.
ECM tables independently account for1.875MiB of vmalloc-backed physical pages.
Neither estimate assigns the entire27.27MiB slab difference against a live
AX3000 running6.12 without NSS.

Build34 compiled full slab counts out. Build35 enables KERNEL_SLUB_DEBUG and
keeps SLUB_DEBUG_ON off, exposing the cache census. It shows roughly23MiB in
kmalloc4K objects. About6.75MiB of the Slab total is outside that cache table;
large kmalloc requests are accounted to SUnreclaim by mm/slub.c but have no
ordinary slab-cache row. Neither process totals nor a single cache table close
the ownership ledger by themselves.

A controlled Build34 unload returned44.98MiB of MemAvailable when ath11k_ahb
was removed, including24.93MiB less SUnreclaim; the48MiB WCSS reservation stayed.
Build35's repeated unload released21.81MiB from kmalloc4K caches and1.84MiB from
SKB-header caches, with about25MiB total SUnreclaim returned. This attributes a
large host-memory cost to ath11k even with no AP interface configured. Removing
NSS afterwards returned only about2MiB; firmware-held payloads may remain, so
this is not a total NSS footprint or a proof of reclaim on unload.

The Build35 hot-reload test did not pass: Q6 startup timed out and reported an
unbalanced IRQ enable. A normal reboot restored core,pd1,pd2 to running and
cleared the warning state. This failure is preserved, not accepted as an
optimization or attributed to GFP_KERNEL without a baseline A/B.

## Confirmed per-CPU page-cache cost

Linux6.18 keeps free pages on per-CPU lists. With min_free_kbytes16384, this
board permits2560pages (10MiB) per CPU by default. These pages are free but do
not enter NR_FREE_PAGES until returned to the shared buddy allocator. The
live page-cache counts and source accounting both confirm that distinction.

A bounded live test changed only vm.percpu_pagelist_high_fraction from0 to100:

| State | MemAvailable MiB | PCP pages MiB | SUnreclaim MiB |
| --- | ---: | ---: | ---: |
| Default, settled |9.23|14.92|42.80|
| Cap100, first sample |23.41|0.91|42.80|
| Cap100, second sample |23.10|1.04|42.80|
| Cap100 plus temporary8MiB tmpfs allocation |14.89|0.99|42.80|
| Allocation released |23.27|0.97|42.82|

The allocation succeeded and was removed. This initial experiment restored0;
Build35 subsequently installed the persistent100 default. The16MiB emergency watermark, NSS pools,
firmware regions, boot ID and saved network configuration were unchanged.
This proves about14MiB can be returned to the shared allocator in this sample;
the later wired throughput checks and bounded Build36 5GHz STA load passed.
AP mode,2.4GHz traffic and long-term stability remain untested.

The new95-cr881x-pcp-memory UCI-default script installs cap100 only on
xiaomi,cr881x and only if no explicit PCP sysctl already exists. S11sysctl
loads the persisted default on every boot. Standard sysctl precedence applies:
use /etc/sysctl.conf (loaded last) or a later-numbered sysctl.d drop-in to
change it;0 restores automatic limits. No daemon is added. The cap is1% of
managed pages shared across CPUs; the allocation batch and emergency watermark
stay unchanged.

## Q6 firmware-loading allocation fix

The local MPD loader requests its temporary coherent DMA buffers using bare
GFP_DMA. dma_alloc_attrs strips zone flags, leaving no reclaim permission,
which sends them through the atomic pool. The same function calls sleepable
firmware-loading APIs. Patch0828 changes just those two flags toGFP_KERNEL;
the DMA API still chooses addresses within the device mask. Sizes, firmware
addresses and secure-loader arguments are unchanged. Pre-existing error-path
cleanup issues are outside this patch.

Build34's coherent_pool=2M creates three pools, with live capacity8MiB after
one grows. They do not shrink. Build35 keeps2M and both radios boot; total pool capacity is now6MiB.
This confirms2MiB less pinned capacity after warm and cold boots. Build35
kept2M while validating the allocation fix. The subsequent Build37 experiment
reduces it to512K and measures capacity/usage as described above. Startup and
traffic acceptance are recorded separately from the earlier expected saving.

## Build35 boot and wired traffic acceptance

A user-confirmed ten-second power removal produced boot ID
`b4b33433-5081-459e-a3c0-24c2c21edf00`. Both radio firmware domains started,
NSS created all eight QinQ interfaces automatically, and the current kernel log
has no warning, call trace, allocation failure or Q6 startup failure. PCP100,
min_free_kbytes16384 and all three2MiB DMA pools persisted. The four saved
network/wireless/firewall/DHCP hashes and UCI state are unchanged. This validates
firmware startup, not Wi-Fi client operation; no AP or STA is configured.

Before that power cycle, routed tests measured:

| Test | Receiver Mbps | Loss | Router CPU idle |
| --- | ---: | ---: | ---: |
| TCP LAN to WAN |936.58|—|98.41%|
| TCP WAN to LAN |936.59|—|98.32%|
| UDP LAN to WAN,900Mbps offered |899.98|0packets|98.36%|
| UDP WAN to LAN,900Mbps offered, temporary delay2 |898.96|3packets,0.00037%|99.16%|

Every accelerated run showed substantial NSS packet-counter growth and no new
allocation-failure or drop counter. The55-second sampler recorded a minimum
MemAvailable of15.93MiB. Idle samples after reboot were roughly18–21MiB.
These are short wired tests, not a daily-use or Wi-Fi load guarantee.

The first reverse UDP run stayed on the software path and lost29.58% at a
900Mbps offer. ECM's existing accel_delay_pkts=1 policy requires observed
traffic in both directions. Its initial client probe was unconfirmed in
conntrack and skipped, leaving ECM with only server-to-client packets; the
default and DSCP classifiers therefore denied acceleration. A guarded test
temporarily used2, which waits for two packets, and confirmed the accelerated
receive path above. The original1 setting was restored and also survived cold
boot. Persistent ECM policy was not changed. The software run increased N2H
queue drops but did not increase allocation-failure counters; it is not counted
as a successful accelerated test.

After the cold boot, a further short TCP run reached936.59Mbps with762983
additional NSS packets and98.50%CPU idle. Its memory sampler stayed above
17.20MiB available, and no failure/drop counter increased. The final idle
snapshot had17.70MiB available. The source fingerprints still match the tested
image. UART capture was closed without resetting the device.

Raw results, before/after counters, memory samples, policy restoration proof
and cold-boot checks are under the evidence directory below.

## Comparable published evidence

- [ADCDS validation](https://github.com/ADCDS/openwrt-xiaomi-ax3000t-rd03v2/blob/main/VALIDATION.md)
  reports a256MiB IPQ5018 build going from roughly17–21MB available to about42MB
  idle after a combination of PCP capping and Wi-Fi allocation changes. The
  published PCP default is100. Our experiment isolates PCP capping alone.
- [Kuncy7 IPQ50xx notes](https://github.com/kuncy7/openwrt-nss-edma/blob/c3po-tag-8021q/README.ipq50xx.md#256-mb-boards)
  document the MPD GFP_DMA problem and a512K coherent pool paired with its fix,
  smaller ath11k pools and an8MiB NSS region on256MiB boards. Its EX511 test
  reported about20MB available after Wi-Fi load. Those changes cannot all be
  copied as one RAM number or validated on the CR881x by citation alone.
- [hzyitc QSDK report155](https://github.com/hzyitc/openwrt-redmi-ax3000/issues/155)
  describes kernel5.4/QSDK11.5 onCR8809, with SUnreclaim58–66MB and a proposed
  ECM startup-order workaround. This is a user diagnosis, not a confirmed
  maintainer root cause or a controlled comparison with our6.18/Qosmio stack.
  The old releases are32-bit ARM; allocator footprints differ from ARM64.

Firmware carve-outs remain16MiB NSS and48MiB WCSS. Published8MiB NSS success
is a candidate for a separate firmware-specific validation, not permission to
reclaim addresses that may still be used on this build. Wi-Fi NSS is still a
separate workstream.

Evidence is retained under logs/cr8812dx-memory-attribution-20261110/, including
raw snapshots, the PCP experiment, reference URLs/hashes, review dispositions
and Build34 rollback artifact. The memory source changes are committed
separately from this evidence summary.
