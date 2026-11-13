. /lib/functions.sh

mi_dualboot_check_image() {
	local ret=0

	if ! nand_do_platform_check "$(board_name)" "$1"; then
		v "Unsupported upgrade image for $(board_name)"
		ret=1
	fi

	local mtd="$( grep -oE 'ubi.mtd=[a-zA-Z0-9\-\_]*' /proc/cmdline | cut -d'=' -f2 )"
	if [[ "${mtd}" != "rootfs" ]] && [[ "${mtd}" != "rootfs_1" ]]; then
		v "Unable to determine UBIPART: ubi.mtd=${mtd}"
		ret=1
	fi

	if ! fw_printenv >/dev/null; then
		v "Fail to read U-Boot env"
		ret=1
	fi

	return ${ret}
}

# Return 2 for boards/layouts using ordinary slot-local rootfs_data. Never
# format the shared partition here: a failed attach must stop the upgrade.
mi_dualboot_prepare_userdata() (
	case "$(board_name)" in
	xiaomi,cr881x|redmi,ax3000) ;;
	*) return 2 ;;
	esac

	local mtdnum="$(find_mtd_index overlay)"
	[ -n "$mtdnum" ] || return 2
	[ "$(rootfs_type)" = tmpfs ] || {
		v "Shared userdata preparation requires the upgrade RAMFS"
		return 1
	}
	[ -z "$UPGRADE_BACKUP" ] || [ -s "$UPGRADE_BACKUP" ] || {
		v "Configuration backup is missing or empty"
		return 1
	}

	local ubidev="$(nand_find_ubi overlay)"
	if [ -z "$ubidev" ]; then
		ubiattach -m "$mtdnum" || return 1
		ubidev="$(nand_find_ubi overlay)"
	fi
	[ -n "$ubidev" ] || return 1
	local ubivol="$(nand_find_volume "$ubidev" userdata)"
	if [ -z "$ubivol" ]; then
		ubimkvol "/dev/$ubidev" -N userdata -m || return 1
		ubivol="$(nand_find_volume "$ubidev" userdata)"
	fi
	[ -n "$ubivol" ] || return 1

	local mp=/tmp/mi-userdata mounted=0
	mkdir "$mp" || return 1
	trap '[ "$mounted" = 0 ] || umount "$mp"; rmdir "$mp"' 0
	# switch_to_ramfs has remounted the previous overlay read-only. Reuse the
	# same UBIFS instance, without erasing files referenced by the old root.
	mount -t ubifs -o ro "/dev/$ubivol" "$mp" || return 1
	mounted=1
	mount -o remount,rw "$mp" || return 1

	# Pending-state cleanup preserves sysupgrade.tgz, then preinit restores it.
	# Remove stale archives in both locations used by fstools' fopivot().
	rm -f "$mp/sysupgrade.tgz" "$mp/upper/sysupgrade.tgz" || return 1
	if [ -n "$UPGRADE_BACKUP" ]; then
		rm -f "$mp/sysupgrade.tgz.next" || return 1
		cp "$UPGRADE_BACKUP" "$mp/sysupgrade.tgz.next" || return 1
		mv -fT "$mp/sysupgrade.tgz.next" "$mp/sysupgrade.tgz" || return 1
	fi
	# FS_STATE_PENDING=1; libfstools/mount_overlay() clears the old overlay
	# before selecting its upper directory, retaining only the backup archive.
	rm -f "$mp/.fs_state.next" || return 1
	ln -s 1 "$mp/.fs_state.next" || return 1
	mv -fT "$mp/.fs_state.next" "$mp/.fs_state" || return 1
	[ "$(readlink "$mp/.fs_state")" = 1 ] || return 1
	sync || return 1
	umount "$mp" || return 1
	mounted=0
	rmdir "$mp" || return 1
	trap - 0
	v "Shared userdata prepared for next-boot reset/config restore"
)

mi_dualboot_do_upgrade() {
	mkdir -p /var/lock
	fw_printenv >/dev/null || return 1

	local cmd="$(identify_if_gzip "$1")cat"

	# Determine UBIPART
	local mtd="$( grep -oE 'ubi.mtd=[a-zA-Z0-9\-\_]*' /proc/cmdline | cut -d'=' -f2 )"
	case "${mtd}" in
		rootfs)
			CI_UBIPART="rootfs_1"
			local current=0
			;;
		rootfs_1)
			CI_UBIPART="rootfs"
			local current=1
			;;
		*)
			v "Unable to determine UBIPART: ubi.mtd=$mtd"
			return 1
			;;
	esac

	local mtdnum="$( find_mtd_index "${CI_UBIPART}" )"
	v "Flashing to ${CI_UBIPART}(mtd${mtdnum})"
	nand_do_flash_file "$1" "$cmd" || return 1
	sync

	# Check to avoid the bug of the vendor U-Boot
	local ubidev="$( nand_find_ubi "${CI_UBIPART}" )"
	if [ -z "$( nand_find_volume "${ubidev}" "kernel" )" ]; then
		v "\"kernel\" volume doesn't exist, which causes a bug of the vendor U-Boot."
		v "When try to boot this system, U-Boot will always set flag_try_sys1_failed=0 even if this is the sysem 2"
		return 1
	fi

	# Boot mounts shared userdata on these boards, not this slot's rootfs_data.
	# Prepare it only after the new kernel is verified, before boot flags change.
	local userdata_status=0
	mi_dualboot_prepare_userdata || userdata_status=$?
	case "$userdata_status" in
	0) ;;
	2)
		if [ -n "$UPGRADE_BACKUP" ]; then
			nand_restore_config "$UPGRADE_BACKUP" || return 1
		fi
		;;
	*) return 1 ;;
	esac

	# Clean the failed flag. So we can boot to them.
	fw_setenv flag_try_sys1_failed 0 || return 1
	fw_setenv flag_try_sys2_failed 0 || return 1

	# Tell u-boot that the current is able to boot.
	fw_setenv flag_last_success ${current} || return 1

	# Tell u-boot to try to boot the other system.
	# If it failed, it will clean this flag.
	fw_setenv flag_ota_reboot 1 || return 1

	# When this flag is set to 0 and the `flag_ota_reboot` is 1,
	# the u-boot will roll back to `flag_last_success`.
	fw_setenv flag_boot_success || return 1
}
