# Upstream sync, 2026-11-07

Historical merge report. The checks and running-image identity below are
those at merge time. Later Slice18 closes reload cleanup, Slice20 accepts the
standard customer-QinQ matrix, and subsequent images run Linux6.18.60. See
[current acceptance status](nss-qosmio-dsa-slices.md#current-acceptance-status)
for installed native31 and the remaining tests.

Merged upstream `4765b657625ec5bbda74b44400465fe043802b30` into the local
NSS development/release source at `8125a0a2f9`. The merge includes229 upstream
commits and advances the selected kernel from6.18.55 to6.18.60.

## Integration decisions

Upstream now supplies the YT921x/YT922x driver and tagger backports. Their
source names collide with the enhanced driver copied through the local kernel
overlay before patches are applied. Both implementations are retained:

- The local entrypoint and shared header become `yt921x_enhanced.c/.h`.
  Its module remains `yt921x_driver.ko`; parameters and debugfs root remain
  unchanged. The local CR881x DTS selects `motorcomm,yt9215-enhanced` and the
  enhanced MDIO driver name is distinct from the upstream driver.
- The enhanced legacy tagger becomes `tag_yt921x_enhanced.c/.ko`, selected by
  `NET_DSA_TAG_YT921X_ENHANCED`. Its private packet header stays unchanged.
  Upstream retains `tag_yt921x.c/.ko` and its separate registration.
- The old830-series wiring and762/763 registration patches are replaced by
  one late968 wiring patch after the upstream Motorcomm series. The enhanced
  composite driver keeps `NET_DSA_YT921X`; upstream uses `NET_DSA_YT92XX`.
- Upstream's kernel tag registry uses31 for MXL8021Q,32 for YT921x,33/34 for
  YT922x variants and35 for QCA EtherType. Enhanced 802.1Q, QinQ and legacy
  transports use36,37 and38. These are kernel tagger registration IDs, not
  packet TPIDs, NSS interface numbers, or customer/port-ID VLAN values.
  Drivers, taggers and kernel must be built and installed together.

The MXL and QCA patch conflicts resolve to upstream's bytes. No upstream
Motorcomm driver support is removed. Source-extracting tests follow the new
local source names. Existing application profiles and NSS feed pin are retained.

## Checks

- Downloaded kernel6.18.60 matches the SHA256 recorded in the tree.
- The complete generic backport, pending, hack and qualcommax patch sequence
  applies with both overlays and no rejected hunks. Kernel tag values and
  Kconfig declarations are unique.
- Both upstream and enhanced DSA drivers and their taggers compile as AArch64
  objects. The YT922x four-byte tagger shares `tag_yt922x.o`.
- NSS core initialization/removal, data-plane integration, native IPQ5018 GMAC,
  QinQ controller and ECM mapping compile against the patched6.18.60 headers.
- Actual-C transport, VLAN/FID, MST, RMA, standard-TX and MTU tests pass.
- Native-NSS and standard-DSA profile selections are checked using the merged
  Kconfig and existing development feed metadata, without changing either
  working configuration. Global recursive-dependency diagnostics match the
  pre-merge baseline; the older main-checkout metadata needs normal build
  regeneration. Ordinary CR881x, native-NSS CR881x and Redmi AX3000 DTS files
  preprocess and compile to DTBs.
- Shell syntax passes for19 changed scripts using their declared interpreter.

These are patch, source and object-compilation checks. No complete firmware
image was built or flashed, and there is no new hardware acceptance claim.
At this merge checkpoint, the NSS reload-cleanup and customer-QinQ runtime
gates from Slice17 remained open, and running build21 still used6.18.55.

Ignored evidence: `logs/upstream-merge-20261107/`. A bounded Qoder source review
is saved under `logs/agents/20261107-134922-135b1079/`; primary review and actual
patch/compiler results determine acceptance. Original reference stashes and
helper draft archives are preserved.
