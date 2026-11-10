# Experimental Qosmio Wi-Fi NSS on CR881x

Status:2026-11-10. Build42 is installed in slot0, with Build41 retained in
slot1. Build42 retains the corrected256MiB allocation profile and enables
matched PAUSE on GMAC1/switch CPU8. Build41 warm and confirmed cold
startup, first5GHz association and forwarded data pass. Build40 results
below remain a comparison baseline. Implementation and validation notes are
committed separately on `nss-experiment`. Automatic initialization and routed IPv4 traffic
through the QCN6122 5GHz STA path pass both warm and user-confirmed cold-boot
checks. A subsequent Build42 internal2.4GHz STA test confirms routed IPv4
acceleration but fails performance acceptance at roughly0.5–0.9Mbps TCP in
the current fixture. A same-image Wi-Fi-NSS-disabled control is also poor at
0.13–0.49Mbps; interference is not independently established. AP mode, Wi-Fi
IPv6 and broader reliability remain open.

## Source and integration

The NSS feed remains pinned to Qosmio nss-packages
`0d970dbf0185e3f53709bd803e8a466598023c57`, using12.2 firmware. Native
qca-nss-dp/core/ECM and the YT921x QinQ adapter remain in place. No Kuncy stmmac
handover service or switch topology is imported.

The ath11k/mac80211 NSS patches come from the Qosmio-derived stack in Kuncy7
`13e8bc914709c4794b3ec7b0421d2038386e697e`. Qosmio reference
`92a2d104145c8d265851c4b388a41bd8e9c21cd9` uses backports6.18.26; this tree
uses7.2. The selected92 patches include IPQ5018/QCN6122 BAR/window, RX-descriptor,
REO, peer-error and cleanup adaptations. Local changes preserve7.2 queue-control
flags and priority restoration, current AHB field ownership, and existing small
host rings. Donor-only AHB userpd/rproc_info relocations are excluded. Exact
import hashes and changed patch identities are in the run's import-manifest.json.

`ATH11K_NSS_SUPPORT` gates patch application and selects the NSS driver and its
Wi-Fi flags. Modules load automatically with `nss_offload=1 frame_mode=2`.
mac80211 compilation receives the staged NSS headers and the same unprefixed
`NSS_FIRMWARE_VERSION_12_2` definition as the driver, preserving shared message
layouts. No runtime arm/start command is needed for the native dataplane.

`ATH11K_MEM_PROFILE_512M` denotes the firmware resource mode1 already used by
this board; physical RAM remains256MiB. The NSS low-memory profile and prior
host ring reductions remain enabled. Build40 still selected the donor512M
allocation setting; Build41 corrects that mismatch as described below. The coherent pools remain512KiB each.
Firmware reservation addresses and sizes are unchanged.

## Build41: corrected256MiB allocation profile

The user caught that Build40 still selected the donor512M allocation profile.
Hzyitc host smallbuffers were present but did not bound the separate NSS
allocations. Build41 selects ATH11K_MEM_PROFILE_256M and
ATH11K_NSS_MEM_PROFILE_256M. The internal512M compatibility define remains only
to preserve the existing firmware-mode1 resource definitions; it is not a
selection of512MiB host buffers.

Actual compiler-flag assertions confirm hzyitc sizes: TX completion2048,
normalRX1024, monitor status512, buffer128 and destination128. TX bookkeeping
is2048 rather than the NSS bitmap patch's8192. Dedicated NSS256 patches skip
unused host TX arrays, cap TX descriptor/extension pools and matching radio
limits at4096, reduceCE5 rings to256, and use private RX/CE fragment caches
with teardown drains. Ordinary host allocation remains alloc_skb under the
existing workaround. Donor512-entry RXDMA reductions were not imported.
Firmware carve-outs, DMA bootstrap and NSS pbuf limits are unchanged.

Both radios log the active profile on boot. Warm startup available RAM is
36464KiB(35.61MiB), versus30748KiB on Build40; SUnreclaim falls from24896 to
19480KiB. Main forwarded traffic minimum is20460KiB(19.98MiB), and cleanup
leaves25216KiB(24.63MiB). Build40's final cold-test cleanup had6248KiB, but its
longer test history prevents treating that whole difference as an exact saving.

Warm TCP1stream upload/download received552.33/181.67Mbps;3stream upload and
download received682.17/298.29Mbps. CPU idle was94.8–96.9%, with growing NSS
IPv4/Wi-Fi counters and no new driver failure counters. UDP400Mbps received
379.12Mbps with5.21%loss. Asymmetry/loss remains unresolved; smaller pools do
not establish unchanged peak throughput. Normal wired TCP after cleanup is
936.64Mbps,97.85%CPU idle. Original settings and test routes are restored.

A confirmed ten-second power removal also passed. Both radios activated the
256MiB profile with no kernel warning; available RAM was36648KiB(35.79MiB)
at61.79seconds uptime. The first WPA2 association and forwarded3stream TCP
received625.68Mbps upload and247.20Mbps download,96.4/96.6%CPU idle, with a
minimum26580KiB(25.96MiB) available during the short flow checks. No driver
failure counter increased, but REO invalid-code counters still increased.
After cleanup24768KiB(24.19MiB) was available; the final wired flow received
936.64Mbps and its minimum was23204KiB(22.66MiB). A concurrent health sample
reported23364KiB(22.82MiB); it is not an idle-memory estimate. Original network
settings and host routes are restored, peer servers are stopped and UART is
closed without reset. Long-term AP/client memory coverage remains open.

Compiled contracts, exact source hashes, logs and measured values:
`logs/cr8812dx-wifi-nss-256m-20261110/`.

## First-boot failure and correction

Build39 compiled but its first boot panicked in nss_core_register_subsys_dp.
The NSS DT node lacked `qcom,wlan-dataplane-offload-enabled`: the Wi-Fi feature
was compiled, but nss_hal never initialized wifili handlers or the external-radio
interface pool. The uninitialized allocator returned0, colliding with Ethernet.
The bootloader automatically returned to Build38; its module and networking
were verified before continuing.

Build40 adds that DT flag. It also publishes wifili readiness after handler/pool
initialization and rejects pre-initialization or unrelated-interface registration
before the core duplicate-registration BUG. Image preflight now checks the DT
feature properties and that the readiness guard is compiled. Runtime SoC IDs
are203 for IPQ5018 and215 for QCN6122. These IDs belong to this pinned build.

## Forwarded traffic evidence

Topology: workstation Ethernet/LAN3 → CR881x routing/NAT →5GHz STA → existing
AP → wired AX3000 peer192.168.101.1. Both computers kept their existing
connections and default routes. iperf3 ran on the workstation and peer, not on
the CR881x. Specific temporary routes selected this path.

| Test | Receiver Mbps | CR881x aggregate CPU idle |
|---|---:|---:|
| Warm TCP upload,1stream |557.35|95.73%|
| Warm TCP download,1stream |303.16|95.31%|
| Warm TCP upload,3streams |765.88|96.54%|
| Warm TCP download,3streams |390.07|94.16%|
| Cold TCP upload,3streams |642.57|97.10%|
| Cold TCP download,3streams |201.05|97.40%|

ECM accelerated counts rose during the tests. NSS IPv4 packet counters and
Wi-Fi TX/REO-RX counters grew by hundreds of thousands per flow, confirming
routed Wi-Fi acceleration. Both radio firmware domains remained running with
no kernel warning or NSS driver failure-counter increase. Cold testing included
the first WPA2 association and actual forwarded data after power removal.

Warm UDP400Mbps offered received379.24Mbps with5.1828%loss; a200Mbps run
received198.44Mbps with0.7797%loss. TCP download had substantial retransmits,
and wifili_wbm_src_reo_code_inv increased during some TCP runs. These are open
observations. Neither their cause nor an NSS regression has been isolated.
The saved host-only Build36 results used a router-local traffic generator and
are not a matched forwarded baseline. Radio/peer/transport limits must be
separated before changing ring layouts or claiming a performance improvement.

Startup available RAM was30.03MiB warm and29.91MiB cold. Main warm traffic
minimum was15.41MiB; the follow-up minimum18.34MiB; cold traffic minimum20.67MiB.
These snapshots differ in allocation/cache state. All three atomic DMA pools
stayed512KiB, with no sampled growth. Memory headroom under sustained AP/client
load remains open.

Wired NSS remained functional before and after the Wi-Fi tests: the warm final
LAN→WAN run received936.60Mbps with98.66%CPU idle and no new driver errors.
The final cold-run result is saved separately in the evidence directory.
Temporary router settings, workstation routes and peer servers were restored.
No Wi-Fi client is left configured by the tests.

## Remaining acceptance

A same-image Build41 probe-time NSS-on/off diagnostic is complete. TCP download
received281.80/313.11Mbps with NSS for1/3streams, versus76.68/115.87Mbps without
it. CPU idle was97.88/97.07% with NSS and28.24/1.91% without it. Upload received
535.23Mbps versus382.79Mbps. This confirms the benefit of acceleration in this
fixture; it does not isolate the remaining receive-loss cause. Software-path
CPU pressure and RX overflow, RF variation and profile-specific resource paths
limit equal-load comparisons. An alternating repeated control is still open.

Per-run NSS invalid-REO and host FrameOOR deltas matched exactly with NSS enabled;
both are derived from firmware diagnostics. Equality does not independently
prove descriptor corruption, exact packet disposition or ABI correctness. FCS
and FrameOOR also increased with NSS disabled. Smaller TCP MSS did not improve
download. These observations do not justify changes to the accepted wired
YT921x/QinQ path or firmware reservations.

The sustained reverse UDP tests did not demonstrate routing acceleration. The
restored ECM setting accel_delay_pkts=1 explicitly waits for traffic in both
directions; it is not a numeric one-packet threshold. A bidirectional fixture
using the same UDP tuple and per-flow ECM/NSS evidence is needed. Wi-Fi firmware
packet handling and NSS IPv4 routing acceleration are distinct stages.

DeepSeek Free, Agy and verified-model interactive Qoder returned bounded reviews;
InferX returned a quota error. Suggestions remain hypotheses, with unsupported
claims rejected in the primary disposition. Normal NSS parameters and network
configuration were restored, temporary client/host route and boot guard removed,
and all per-case peer servers stopped. WAN DHCP works. The final Windows TCP5201
connection timed out, so a new wired throughput pass is not claimed here.
Evidence and review dispositions: logs/cr8812dx-wifi-download-20261110/REPORT.md.

A subsequent same-radio comparison now verifies software-forwarded and
NSS-routed UDP separately while Wi-Fi NSS stays enabled. Fresh flow tuples,
two live ECM samples per run and NSS packet growth prove the actual paths.
All software flows have ECM mode0; accelerated flows have mode2. Temporarily
bypassing the classifier delay selects the latter; baseline policy1 is restored
after every case and at cleanup. No radio restart or firmware change is involved.

Three repeats at50/75/100Mbps show zero software-path loss. Accelerated loss
is0% at50Mbps,0-0.06344% at75Mbps and0.05318-0.13436% at100Mbps. Two200Mbps
repeats have zero software loss versus0.40715-0.76270% accelerated loss, with
about97%CPU idle under acceleration. Workstation UDP receive-buffer errors
remain zero through these tests. At300Mbps, software loss is0-0.04496% versus
1.68648-2.74849% accelerated;400 workstation receive-buffer errors near the
test transitions make that comparison partly endpoint-confounded.

These results shift the lead toward behavior associated with routed NSS
acceleration. They do not identify the loss stage or exclude Ethernet/switch
burst handling. FrameOOR still fails to account for flow loss: one accelerated
100Mbps run loses38packets with zero OOR, while a loss-free software75Mbps run
has76OOR. Next trace receive, NSS forwarding, QinQ/Ethernet egress and switch
queues using synchronized counters and packet sequence evidence before tuning
buffers. All router settings, classifier policy, test device and host route
are restored; all case servers stopped. Evidence and26 verified cases:
logs/cr8812dx-wifi-udp-paths-20261110/REPORT.md.

A targeted pool check repeated eight verified200/300Mbps UDP cases while
sampling NSS N2H and wifili counters. Accelerated loss reproduced at
0.38268-1.04399% for200Mbps and2.04690-2.20226% for300Mbps. Across128 samples
spanning130.15seconds, default-pbuf free count stayed at least7816/9984 and
OCM free count1231/1500; payload free count stayed at least1614. Lifetime
payload/default-pbuf allocation failures remained5, with zero new failures.
N2H queue0drops stayed96. Wi-Fi RX/TX/extension descriptor allocation failures,
no-buffer, RXDMA descriptor-unavailable, TCL-full and queue-limit counters
remained0; host NSS buffer-allocation errors also stayed0. Gauge minima are
sampled, but cumulative failure counters also had zero deltas after post-flow
settling. These observations do not support exhaustion of the reported pools.
The workstation recorded60UDP receive-buffer errors across this new suite,
which limits assigning every lost packet to the router. No memory limits were
changed, and the test configuration and route are restored. Evidence:
logs/cr8812dx-nss-pool-check-20261110/REPORT.md.

Read-only switch-boundary counters subsequently narrow the accelerated loss
to the CPU-ingress-to-LAN3-egress path. At200Mbps, two accelerated controls
show SoC MAC TX161028/161059, switch CPU8 RX161025/161059, and switch LAN3
TX159692/159754: switch deficits1333/1305 versus iperf loss1245/1209. The
software control shows CPU RX161066 and LAN TX161066, with zero iperf loss.
Counter windows include omitted/startup traffic and read overhead, so the
counts are not expected to reconcile exactly with the measured iperf interval.
Ingress CRC/oversize/discard counters do not increase. Egress queue pressure
between the2.5Gbps CPU link and1Gbps LAN is now the leading mechanism, while
the exact internal drop reason remains unproven. Workstation UDP error counters
stay unchanged across the monitored controls.

Requested sender pacing100/1000/10000us does not provide a decisive result:
short startup captures show similar actual spacing rather than the requested
100-fold burst separation. The captures confirm the peer sends over wiredLAN
to192.168.101.116; its Wi-Fi default route does not carry this test flow. The
earlier temporary inference of a second Wi-Fi hop is rejected by packet evidence.
In Build41, switch CPU port8 had PAUSE disabled and its fixed-link lacked the
pause properties present on secondary CPU port4. This identified matched
CPU-trunk flow control as the next candidate; that diagnostic changed no
link, buffer or switch forwarding setting.
All test configuration is restored. Evidence and source-review disposition:
logs/cr8812dx-nss-burst-check-20261110/REPORT.md.

Register-map recheck confirms the MAC identities and MIB addressing. Vendor
API total TX is a histogram sum; raw `+0x9c` matches that sum in the new
64-ping cross-check, but the earlier high-rate snapshots lack an independent
TX histogram. Future localization should record both. The buffer controls
were already documented and programmed at `0x2801d0`, `0x281000`, `0x301000`
and `0x303000`; the README's broad unknown-`0x34xx`-BMU statement was stale
and is corrected. Live readback matches stock-derived setup. Primary CPU8
uses ordinary per-MAC buffer values while secondary CPU4 receives a special
constant; neither that distinction nor disabled PAUSE proves the loss cause.
Group admission indices must not be equated to MAC ports without evidence.
No hardware setting changed. Evidence:
logs/cr8812dx-yt921x-map-audit-20261110/REPORT.md.

Build42 adds only `pause` and `asym-pause` to the two NSS CPU-trunk fixed
links, GMAC1 and switch CPU8. Rootfs/modules, 256MiB Wi-Fi buffers, reservations
and switch admission thresholds are unchanged. Live switch CTRL0x5fc,
STATUS0x1fc and SERDES0x27c confirm enabled pause, and both endpoints log
RX/TX flow control.

Six accelerated200/300Mbps UDP runs with pause enabled show zero switch
CPU-ingress-to-LAN3-egress deficit, independently verified with TX histograms
and raw total. Switch TX PAUSE counts rise880-5440per run. Disabling only
switch TX PAUSE in the same boot reproduces deficits1642/4284 and end-to-end
loss0.84517/2.01361% at200/300Mbps. Restoring the two registers returns both
deficits to zero. This identifies missing CPU-trunk backpressure as a cause
of the prior switch loss; the exact queue admission rule and a dedicated
per-port SRAM bank are not established. Four software controls also have
zero switch deficit. Residual endpoint loss0-0.07603% in accelerated
pause-enabled runs remains partly host-buffer/RF/path-confounded.

TCP download now receives545.78Mbps single-stream and666.14Mbps with3streams,
CPU96.54/97.04%idle; both have confirmed NSS TCP flows and zero switch
deficit. These are single runs in this RF fixture. Native wired NSS passes
at936.69Mbps and98.25%CPU idle after restoration. No Wi-Fi pool was enlarged.
Normal classifier policy, config hashes, host route and temporary STA are
restored. Build42 stays installed in slot0; inactive Build41 slot1's raw
hash matches its pre-test backup exactly. No commit/push.

Observability correction: SoC eth1 data-path packet totals are usable, but
its FAL-backed extended MIB counters, including rx_pause, remain zero with
NSS_DP_SKIP_SSDK_FAL=1; they cannot validate received PAUSE. The switch MIB,
live link status and reversible on/off/on test supply the evidence.
Build42 multiport head-of-line effects, sustained mixed loads and cold boot
remain unvalidated. Evidence: logs/cr8812dx-nss-pause-20261110/REPORT.md.

## Build42: internal2.4GHz STA data test

ZC2s is on channel13/2472MHz. The router's original US domain disables that
channel, so it did not appear in router scans until the user confirmed the
physical country as Vietnam. A user-authorized workstation scan found the
SSID without changing its ZC5s connection. The temporary router client used
VN, the same locally supplied credential as ZC5s,20MHz, and the internal
IPQ5018 radio at `c000000.wifi`/`phy0-sta0`.

| TCP test | Receiver Mbps | Router CPU idle |
| --- | ---: | ---: |
| Upload,1stream |0.577|97.30%|
| Upload,3streams |0.499|97.14%|
| Download,1stream |0.786|97.71%|
| Download,3streams |0.917|97.81%|

Each actual TCP data stream, excluding its control socket, has two ECM
samples in mode2. NSS IPv4 TX grows1920/3698/2702/3184packets respectively;
internal-radio wifili RX/TX counts also grow, while external-radio counters
stay unchanged. Thus internal2.4GHz STA offload is demonstrated, but these
speeds do not pass practical performance acceptance.

A1Mbps reverse UDP comparison verifies software routing in mode0 and NSS
routing in mode2. Receiver rates are0.930/0.907Mbps, with iperf-reported loss
6.28/5.04%. The latter window also has534workstation global UDP receive-buffer
errors; sender/receiver measurement windows differ, so do not treat those
percentages as precise router-drop fractions. Software routing still uses
Wi-Fi NSS reception; it is not a Wi-Fi-NSS-disabled control. A5Mbps software
attempt times out, with temporary peer SSH unreachable; it is not accepted
as a throughput result. Its acceleration policy and owned peer server are
restored afterward. The planned5Mbps accelerated case was not run.

Completed cases have zero measured switch CPU8-RX-to-LAN3-TX deficit. There
are no new reported pbuf/payload/descriptor allocation failures or kernel
warnings. FCS, BA-duplicate and FrameOOR counters increase on the internal
radio. Its survey reports97.99%busy over129.91seconds, with strong station
signal and varying HE rates; this is a driver observation, not independent
proof of RF interference. Radio/AP interoperability, the Wi-Fi NSS receive
path and endpoint effects remain unresolved. No buffer, firmware or switch
queue tuning was attempted. No cold2.4GHz association or AP-mode test passed.

Original config hashes, regulatory domains, classifier mode2/delay1 and both
computers' stable connections/routes are restored; the temporary client,
reader and exact peer route are removed. All owned peer servers were stopped.
The WAN DHCP log shows missing lease/discovery state predating this test;
a final wired regression check needs its Windows test endpoint restored.
Evidence: `logs/cr8812dx-wifi24-nss-20261110/`.

### Same-image2.4GHz control with Wi-Fi NSS disabled

Build42 was rebooted with only `nss_offload=0`; `frame_mode=2`, firmware,
buffers, reservations and switch PAUSE settings were unchanged. Wired NSS
and all8QinQ nodes stayed initialized. The temporary STA used the same ZC2s
BSSID, channel13,20MHz and VN domain. Peer management used the workstation's
existing Wi-Fi connection; traffic remained bound to workstation Ethernet
through the CR881x STA. Neither computer's connection/default route changed.

| TCP test | Earlier Wi-Fi NSS on, Mbps | Wi-Fi NSS off, Mbps |
| --- | ---: | ---: |
| Upload,1stream |0.577|0.491|
| Upload,3streams |0.499|0.395|
| Download,1stream |0.786|0.262|
| Download,3streams |0.917|0.131|

Both live ECM samples for every actual TCP data stream show no acceleration;
NSS IPv4 TX does not grow and WIFILI statistics are absent with offload disabled.
Router CPU remains97.11–97.72%idle. Across the four TCP windows, internal-radio
FCS rises30and FrameOOR1366. These are radio-wide diagnostics, not independent
per-flow drop accounting. All five completed TCP/UDP windows have zero measured
switch CPU8-RX-to-LAN3-TX deficit, with no sampled ingress discard/CRC increase.
Initial gateway ARP fails despite association; one bounded reconnect restores
replies with33%loss and482–1483ms latency before throughput testing.

A1Mbps UDP download receives0.953Mbps and reports zero loss, but the workstation
global RcvbufErrors rises665and sender/receiver intervals differ15.10/8.00seconds.
The earlier NSS-on comparisons also had interval/accounting limits. This does
not establish loss-free delivery or an NSS-specific UDP regression.

The severe TCP limitation and FCS/FrameOOR growth persist without Wi-Fi NSS.
They cannot be attributed solely to active Wi-Fi firmware offload. Compiled
NSS patches, host small rings and wired NSS remain present in this control;
shared host-driver resource paths are not isolated. A shared
radio/firmware/AP issue, RF conditions and endpoint effects remain candidates;
strong beacon signal and near-full driver survey busy time do not independently
prove or exclude interference. This sequential control does not clear NSS of
every contribution and does not pass2.4GHz performance acceptance.

Wi-Fi NSS and both256MiB profiles are restored on the same image after reboot,
with original config hashes/regulatory domains, no test STA or peer route,
all owned peer servers stopped and the temporary boot guard removed. No new
kernel warning is detected. WAN remains1Gbps with DHCP pending as before the
test; a new wired throughput pass is not claimed.
Evidence: `logs/cr8812dx-wifi24-control-20261110/REPORT.md`.

AP mode, internal2.4GHz performance/cold recovery, Wi-Fi IPv6, multiple
clients, customer-VLAN/bridge combinations, recovery/churn and sustained load
remain unvalidated. Mesh is not enabled in the profile. Existing wired DSA/QinQ
acceptance stays distinct from this new Wi-Fi matrix.

Evidence: `logs/cr8812dx-wifi-nss-20261110/`, including build39/40 manifests,
artifact-preflight40.json, failure39-analysis.json, post40/, cold40/, forwarded40/,
forwarded40-followup/, forwarded40-cold/, restoration proofs and UART capture.
The bounded external review attempts returned a timeout and provider quota;
no third-party approval is claimed. Primary source/compiler/image/device checks
are the acceptance evidence.
