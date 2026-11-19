#!/bin/sh

set -eu

TOPDIR="${TOPDIR:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"

apply_one_patch() {
	local repo="$1"
	local patch="$2"

	if git -C "$repo" apply --check "$patch" >/dev/null 2>&1; then
		echo "Applying $(basename "$patch") -> $repo"
		git -C "$repo" apply "$patch"
	elif git -C "$repo" apply -R --check "$patch" >/dev/null 2>&1; then
		echo "Already applied: $(basename "$patch")"
	else
		echo "Patch does not apply cleanly: $patch" >&2
		return 1
	fi
}

apply_patch_dir() {
	local repo="$1"
	local patch_dir="$2"

	[ -d "$patch_dir" ] || return 0

	if [ -f "$patch_dir/series" ]; then
		sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$patch_dir/series" |
		while IFS= read -r patch; do
			case "$patch" in
				*[!A-Za-z0-9._-]*|.*) echo "Invalid patch name: $patch" >&2; return 1 ;;
			esac
			apply_one_patch "$repo" "$patch_dir/$patch" || return 1
		done
	else
		for patch in "$patch_dir"/*.patch; do
			[ -f "$patch" ] || continue
			apply_one_patch "$repo" "$patch" || return 1
		done
	fi
}

link_nss_source_patches() {
	local repo="$TOPDIR/feeds/nss"
	local source_dir="$TOPDIR/tools/nss/patches/qosmio"
	local package source target previous

	for package in qca-nss-drv qca-nss-clients qca-nss-ecm; do
		set -- "$source_dir/$package/"*.patch
		[ -f "$1" ] || {
			echo "Missing repository NSS patches: $source_dir/$package" >&2
			return 1
		}
	done

	# Preserve conflicting user edits, including files left by the old installer.
	for source in "$source_dir"/*/*.patch; do
		[ -f "$source" ] || continue
		package="$(basename "$(dirname "$source")")"
		target="$repo/$package/patches/$(basename "$source")"
		if [ -e "$target" ] || [ -L "$target" ]; then
			[ "$(readlink "$target" 2>/dev/null || :)" = "$source" ] && continue
			cmp -s "$source" "$target" || {
				echo "Conflicting local NSS patch: $target" >&2
				return 1
			}
		fi
	done

	for source in "$source_dir"/*/*.patch; do
		[ -f "$source" ] || continue
		package="$(basename "$(dirname "$source")")"
		mkdir -p "$repo/$package/patches"
		target="$repo/$package/patches/$(basename "$source")"
		[ "$(readlink "$target" 2>/dev/null || :)" = "$source" ] || ln -sf "$source" "$target"
	done
	# Withdraw only obsolete symlinks owned by this installer.
	for package in qca-nss-drv qca-nss-clients qca-nss-ecm; do
		for target in "$repo/$package/patches/"*.patch; do
			[ -L "$target" ] || continue
			previous="$(readlink "$target")"
			case "$previous" in
				"$source_dir/$package/"*.patch) [ -e "$previous" ] || rm "$target" ;;
			esac
		done
	done
}

apply_feed() {
	local name="$1"
	local patch_dir="$TOPDIR/patches/feeds-$name"
	local repo="$TOPDIR/feeds/$name"
	local migration="$patch_dir/migrations/ipq50xx-firmware-default.patch"
	local expected actual

	case "$name" in ''|*[!A-Za-z0-9_-]*) echo "Invalid feed name: $name" >&2; return 1 ;; esac
	[ -d "$patch_dir" ] || return 0
	if [ -f "$patch_dir/revision" ]; then
		expected="$(cat "$patch_dir/revision")"
		actual="$(git -C "$repo" rev-parse HEAD)"
		[ "$actual" = "$expected" ] || {
			echo "Unexpected $name feed revision: $actual (expected $expected)" >&2
			return 1
		}
	fi
	# Migrate the previous NSS default before checking the current series.
	if [ "$name" = nss ] && git -C "$repo" apply --check "$migration" >/dev/null 2>&1; then
		apply_one_patch "$repo" "$migration"
	fi
	apply_patch_dir "$repo" "$patch_dir"
	[ "$name" != nss ] || link_nss_source_patches
}

if [ "$#" -gt 0 ]; then
	for feed in "$@"; do apply_feed "$feed"; done
else
	apply_feed packages
	apply_feed luci
	[ ! -d "$TOPDIR/feeds/nss" ] || apply_feed nss
fi

echo "Local feed patches are up to date."
