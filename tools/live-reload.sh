#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

readonly script_path=$(readlink -f "$0")
readonly project_dir=$(CDPATH= cd -- "$(dirname -- "$script_path")/.." && pwd)
readonly module=$project_dir/src/sm750hdmidrm.ko
readonly unit=sm750hdmidrm-live-reload
readonly result=/run/sm750hdmidrm-live-reload.result

find_device() {
	local device
	local -a matches=()

	for device in /sys/bus/pci/devices/*; do
		[[ $(<"$device/vendor") == 0x126f ]] || continue
		[[ $(<"$device/device") == 0x0750 ]] || continue
		matches+=("$device")
	done
	((${#matches[@]} == 1)) || {
		echo "Expected one SM750, found ${#matches[@]}." >&2
		return 1
	}
	printf '%s\n' "${matches[0]}"
}

stop_display_manager() {
	local attempt state

	systemctl stop --no-block display-manager.service
	for attempt in {1..100}; do
		state=$(systemctl show display-manager.service \
			--property=ActiveState --value 2>/dev/null || true)
		[[ $state == inactive || $state == failed ]] && return 0
		sleep 0.1
	done
	return 1
}

recover_installed_module() {
	local device=$1

	set +e
	[[ -L $device/driver ]] && \
		printf '%s\n' "${device##*/}" >"$device/driver/unbind"
	rmmod sm750hdmidrm 2>/dev/null
	modprobe sm750hdmidrm
	systemctl start display-manager.service
	printf 'FAILED: candidate activation failed; installed module restored\n' \
		>"$result"
}

activate() {
	local profile=${1:-current}
	local device driver name parameter value
	local -a parameters=()

	device=$(find_device)
	for parameter in /sys/module/sm750hdmidrm/parameters/*; do
		[[ -f $parameter ]] || continue
		name=${parameter##*/}
		value=$(<"$parameter")
		[[ $value == '(null)' ]] && continue
		if [[ $profile != current &&
		      $name =~ ^(enable_dma|disable_dma|shadow_dma_min_bytes)$ ]]; then
			continue
		fi
		parameters+=("$name=$value")
	done
	case $profile in
		disable-dma)
			parameters+=(enable_dma=N disable_dma=N \
				shadow_dma_min_bytes=4096)
			;;
		enable-dma)
			parameters+=(enable_dma=Y disable_dma=N \
				shadow_dma_min_bytes=4096)
			;;
	esac

	trap 'recover_installed_module "$device"' ERR
	stop_display_manager
	[[ -L $device/driver ]] && \
		printf '%s\n' "${device##*/}" >"$device/driver/unbind"
	rmmod sm750hdmidrm
	insmod "$module" "${parameters[@]}"
	udevadm settle --timeout=10
	driver=$(basename "$(readlink "$device/driver")")
	[[ $driver == sm750hdmidrm ]]
	systemctl start display-manager.service
	trap - ERR
	printf 'PASS: candidate module active; profile=%s\n' "$profile" >"$result"
}

case ${1:-start} in
	start)
		profile=${2:-current}
		[[ $profile == current || $profile == disable-dma ||
		   $profile == enable-dma ]] || {
			echo "usage: $0 [start] [disable-dma|enable-dma]" >&2
			exit 2
		}
		((EUID == 0)) && {
			echo "Run this command as your desktop user, without sudo." >&2
			exit 2
		}
		make -C "$project_dir" clean check
		exec sudo "$script_path" launch "$profile"
		;;
	launch)
		profile=${2:-current}
		[[ $profile == current || $profile == disable-dma ||
		   $profile == enable-dma ]] || exit 2
		((EUID == 0)) || exec sudo "$script_path" launch "$profile"
		rm -f -- "$result"
		systemctl reset-failed "$unit.service" 2>/dev/null || true
		systemd-run --unit="$unit" --collect --no-block \
			--property=Type=oneshot "$script_path" activate "$profile"
		echo "Live reload started; your graphical session will restart."
		echo "Result: $result"
		;;
	activate)
		((EUID == 0)) || exit 2
		activate "${2:-current}"
		;;
	*)
		echo "usage: $0" >&2
		exit 2
		;;
esac
