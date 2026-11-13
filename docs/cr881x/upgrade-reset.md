# Xiaomi shared userdata during sysupgrade

CR881x and this tree's Redmi AX3000 support prefer the `userdata` UBI volume in
the MTD partition named `overlay`. This is shared by both firmware slots. It is
separate from either slot's `rootfs_data`; resetting the latter does not reset
the settings actually used at boot.

The Xiaomi dual-slot upgrade now prepares the shared volume after the target
image is written and its kernel volume is verified, before changing boot flags.
The helper requires the upgrade RAMFS and discovers the partition/volume by
name. It attaches existing UBI without formatting it. Layouts without a shared
overlay retain the normal slot-local backup path. Storage or backup errors stop
the handoff instead of being silently ignored.

The helper does not erase a live overlay or truncate a mounted UBI volume. It
uses fstools' existing `.fs_state -> 1` (`FS_STATE_PENDING`) marker. At the next
boot, `mount_overlay()` clears the old overlay before using its upper directory.
For keep-settings upgrades, the selected backup is placed at the shared volume's
root as `sysupgrade.tgz`; fstools preserves that archive and preinit restores it.
For reset upgrades, stale root/upper backup archives are removed and no archive
is restored. Files from manually installed packages do not survive merely
because the two slots share userdata.

The script and its `ln`/`readlink` helpers are copied into RAMFS. When upgrading
from an image that predates this fix, the currently running image still supplies
the upgrade hook. Obtaining the corrected cleanup on that first transition
requires updating that running hook; a new image alone cannot replace code
already running in the old upgrade process. Without that update, old userdata
can remain even with Keep settings off. This does not inherently require a
reset to use NSS: the beta's one-shot default disables conflicting Linux flow
offload while preserving compatible settings. See the
[r3 old-release migration test](nss-beta-20261113-r3.md). Reset is optional for
users who want clean defaults or need to resolve incompatible retained settings.

The offline production-script harness covers both slots, keep/reset, known
board/layout guards, archive placement, files left intact until reboot and
failures before boot-target handoff. Hardware acceptance remains separate from
these fixtures. Shared userdata also means the slots are not independent data
backups; save the configuration and shared volume before destructive tests.

For a manual reset, use `factoryreset -y -r`. The built-in sync/reboot stays in
the already loaded process after deleting overlay files. Launching another
dynamically linked program after deletion can fail with stale inode handles.

## CR8812DX acceptance, 2026-11-13

NSS beta r2 (`00e457b50d6`, kernel 6.18.60) passed a slot1-to-slot0
`sysupgrade -n` transition with the corrected old-image hook bootstrapped
temporarily. UART logged shared userdata preparation followed by next-boot
overlay initialization. A planted `/etc` marker and changed hostname were
removed, and the NSS firewall defaults applied without a manual fix.

The same image then passed `factoryreset -y -r`: a second marker and hostname
change disappeared and the router rebooted without a power cycle. The old
processes emitted stale inode messages during the manual reset's shutdown;
the built-in reboot completed and the new boot had no stale inode errors.
The sysupgrade helper itself leaves overlay files intact until the next boot.

After each reset, the six Zcop/LuCI pages loaded and routed NSS reached about
936 Mbps in each direction with no additional payload allocation failures.
The fallback firmware slot, calibration and bdata hashes stayed unchanged.
The full [beta acceptance record](nss-beta-20261113.md) identifies the image
and local evidence. Keep-settings upgrades, the opposite slot transition and
Redmi AX3000 remain offline-tested rather than hardware-validated here.
