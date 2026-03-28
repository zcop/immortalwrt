#!/bin/sh

set -eu

TOPDIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

apply_patch_dir() {
	local repo="$1"
	local patch_dir="$2"

	[ -d "$patch_dir" ] || return 0

	for patch in "$patch_dir"/*.patch; do
		[ -f "$patch" ] || continue

		if git -C "$repo" apply --check "$patch" >/dev/null 2>&1; then
			echo "Applying $(basename "$patch") -> $repo"
			git -C "$repo" apply "$patch"
		elif git -C "$repo" apply -R --check "$patch" >/dev/null 2>&1; then
			echo "Already applied: $(basename "$patch")"
		else
			echo "Patch does not apply cleanly: $patch" >&2
			return 1
		fi
	done
}

apply_patch_dir "$TOPDIR/feeds/packages" "$TOPDIR/patches/feeds-packages"
apply_patch_dir "$TOPDIR/feeds/luci" "$TOPDIR/patches/feeds-luci"

echo "Local feed patches are up to date."
