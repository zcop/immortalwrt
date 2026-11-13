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
the upgrade hook. It must be bootstrapped with the exact validated hook, or the
first transition still uses the old behavior. A new image alone cannot replace
code already running in the old system's upgrade process.

The offline production-script harness covers both slots, keep/reset, known
board/layout guards, archive placement, files left intact until reboot and
failures before boot-target handoff. Hardware acceptance remains separate from
these fixtures. Shared userdata also means the slots are not independent data
backups; save the configuration and shared volume before destructive tests.

For a manual reset, use `factoryreset -y -r`. The built-in sync/reboot stays in
the already loaded process after deleting overlay files. Launching another
dynamically linked program after deletion can fail with stale inode handles.
