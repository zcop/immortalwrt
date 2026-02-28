#!/usr/bin/env bash
set -euo pipefail

TOPDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_FILE="${TOPDIR}/.config"
VERSION_MK="${TOPDIR}/include/version.mk"

if [[ ! -f "${CONFIG_FILE}" ]]; then
	echo "Error: ${CONFIG_FILE} does not exist."
	exit 1
fi

if [[ ! -f "${VERSION_MK}" ]]; then
	echo "Error: ${VERSION_MK} does not exist."
	exit 1
fi

FIELDS=(
	VERSION_DIST
	VERSION_NUMBER
	VERSION_CODE
	VERSION_REPO
	VERSION_HOME_URL
	VERSION_MANUFACTURER
	VERSION_MANUFACTURER_URL
	VERSION_BUG_URL
	VERSION_SUPPORT_URL
	VERSION_FIRMWARE_URL
	VERSION_PRODUCT
	VERSION_HWREV
)

declare -A VALUES
declare -A DEFAULTS
VERSIONOPT_ENABLED=0

escape_kconfig_string() {
	local value="${1-}"
	value="${value//\\/\\\\}"
	value="${value//\"/\\\"}"
	printf "%s" "${value}"
}

remove_symbol_lines() {
	local sym="$1"
	sed -i \
		-e "/^CONFIG_${sym}=.*/d" \
		-e "/^# CONFIG_${sym} is not set$/d" \
		"${CONFIG_FILE}"
}

set_symbol_string() {
	local sym="$1"
	local val="$2"
	remove_symbol_lines "${sym}"
	printf "CONFIG_%s=\"%s\"\n" "${sym}" "$(escape_kconfig_string "${val}")" >> "${CONFIG_FILE}"
}

set_symbol_not_set() {
	local sym="$1"
	remove_symbol_lines "${sym}"
	printf "# CONFIG_%s is not set\n" "${sym}" >> "${CONFIG_FILE}"
}

get_config_string() {
	local sym="$1"
	local line
	line="$(grep -E "^CONFIG_${sym}=" "${CONFIG_FILE}" | tail -n1 || true)"
	if [[ -z "${line}" ]]; then
		printf ""
		return
	fi

	line="${line#CONFIG_${sym}=}"
	if [[ "${line}" == \"*\" ]]; then
		line="${line:1:${#line}-2}"
		line="${line//\\\"/\"}"
		line="${line//\\\\/\\}"
	fi

	printf "%s" "${line}"
}

get_default_from_version_mk() {
	local sym="$1"
	awk -v s="${sym}" '
		$0 ~ ("^" s ":=\\$\\(if \\$\\(" s "\\),\\$\\(" s "\\),") {
			line = $0
			sub("^" s ":=\\$\\(if \\$\\(" s "\\),\\$\\(" s "\\),", "", line)
			sub("\\)$", "", line)
			print line
			exit
		}
	' "${VERSION_MK}"
}

load_state() {
	if grep -q '^CONFIG_VERSIONOPT=y$' "${CONFIG_FILE}"; then
		VERSIONOPT_ENABLED=1
	else
		VERSIONOPT_ENABLED=0
	fi

	local sym
	for sym in "${FIELDS[@]}"; do
		VALUES["${sym}"]="$(get_config_string "${sym}")"
		DEFAULTS["${sym}"]="$(get_default_from_version_mk "${sym}")"
		if [[ -z "${DEFAULTS[${sym}]}" ]]; then
			DEFAULTS["${sym}"]="<kconfig default>"
		fi
	done
}

print_menu() {
	if [[ -t 1 ]]; then
		clear
	fi

	echo "OpenWrt Version Config UI"
	echo "Topdir: ${TOPDIR}"
	echo "Config: ${CONFIG_FILE}"
	echo
	if [[ "${VERSIONOPT_ENABLED}" -eq 1 ]]; then
		echo "VERSIONOPT: enabled"
	else
		echo "VERSIONOPT: disabled (values below are ignored until enabled)"
	fi
	echo

	local i=1
	local sym current default
	for sym in "${FIELDS[@]}"; do
		current="${VALUES[${sym}]}"
		default="${DEFAULTS[${sym}]}"
		if [[ -n "${current}" ]]; then
			printf "%2d) %-24s = %s\n" "${i}" "${sym}" "${current}"
		else
			printf "%2d) %-24s = <unset> (default: %s)\n" "${i}" "${sym}" "${default}"
		fi
		i=$((i + 1))
	done

	echo
	echo "t) Toggle VERSIONOPT"
	echo "s) Save"
	echo "q) Quit without saving"
}

edit_field() {
	local idx="$1"
	local sym="${FIELDS[$((idx - 1))]}"
	local current="${VALUES[${sym}]}"
	local default="${DEFAULTS[${sym}]}"
	local input

	echo
	echo "Editing ${sym}"
	if [[ -n "${current}" ]]; then
		echo "Current: ${current}"
	else
		echo "Current: <unset>"
	fi
	echo "Default: ${default}"
	echo "Leave empty to unset and use default."
	read -r -p "New value: " input
	VALUES["${sym}"]="${input}"
}

save_state() {
	if [[ "${VERSIONOPT_ENABLED}" -eq 1 ]]; then
		remove_symbol_lines "VERSIONOPT"
		printf "CONFIG_VERSIONOPT=y\n" >> "${CONFIG_FILE}"
	else
		set_symbol_not_set "VERSIONOPT"
	fi

	local sym
	for sym in "${FIELDS[@]}"; do
		if [[ "${VERSIONOPT_ENABLED}" -eq 1 && -n "${VALUES[${sym}]}" ]]; then
			set_symbol_string "${sym}" "${VALUES[${sym}]}"
		else
			remove_symbol_lines "${sym}"
		fi
	done

	echo
	echo "Saved to ${CONFIG_FILE}"
	read -r -p "Run 'make defconfig' now? [Y/n]: " ans
	if [[ -z "${ans}" || "${ans}" =~ ^[Yy]$ ]]; then
		(
			cd "${TOPDIR}"
			make defconfig
		)
	fi
}

main_loop() {
	load_state

	while true; do
		print_menu
		echo
		read -r -p "Choice: " choice
		case "${choice}" in
			t|T)
				if [[ "${VERSIONOPT_ENABLED}" -eq 1 ]]; then
					VERSIONOPT_ENABLED=0
				else
					VERSIONOPT_ENABLED=1
				fi
				;;
			s|S)
				save_state
				echo "Done."
				exit 0
				;;
			q|Q)
				echo "Canceled. No changes saved."
				exit 0
				;;
			''|*[!0-9]*)
				echo "Invalid choice."
				read -r -p "Press Enter to continue..." _
				;;
			*)
				if (( choice < 1 || choice > ${#FIELDS[@]} )); then
					echo "Invalid index."
					read -r -p "Press Enter to continue..." _
				else
					edit_field "${choice}"
				fi
				;;
		esac
	done
}

main_loop
