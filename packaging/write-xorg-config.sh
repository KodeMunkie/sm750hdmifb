#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

output=${1:?usage: write-xorg-config.sh OUTPUT [PCI_BDF]}
bdf=${2:-}

if [[ -z $bdf ]]; then
	matches=()
	for device in /sys/bus/pci/devices/*; do
		[[ $(<"$device/vendor") == 0x126f ]] || continue
		[[ $(<"$device/device") == 0x0750 ]] || continue
		matches+=("${device##*/}")
	done
	if ((${#matches[@]} != 1)); then
		echo "Expected exactly one SM750 (126f:0750), found ${#matches[@]}." >&2
		exit 1
	fi
	bdf=${matches[0]}
fi

[[ $bdf =~ ^([[:xdigit:]]{4}):([[:xdigit:]]{2}):([[:xdigit:]]{2})\.([0-7])$ ]] || {
	echo "Invalid PCI BDF: $bdf" >&2
	exit 2
}
domain=$((16#${BASH_REMATCH[1]}))
bus=$((16#${BASH_REMATCH[2]}))
slot=$((16#${BASH_REMATCH[3]}))
function=${BASH_REMATCH[4]}
if ((domain)); then
	xorg_busid="PCI:${bus}@${domain}:${slot}:${function}"
else
	xorg_busid="PCI:${bus}:${slot}:${function}"
fi

output_dir=$(dirname "$output")
[[ -d $output_dir ]] || install -d -m 0755 "$output_dir"
temporary=$(mktemp "${output}.XXXXXX")
trap 'rm -f -- "$temporary"' EXIT
cat >"$temporary" <<EOF
# SPDX-License-Identifier: GPL-2.0-only
# Generated from the detected SM750 PCI device.
Section "ServerFlags"
    Option "AutoAddGPU" "false"
EndSection

Section "ServerLayout"
    Identifier "SM750 HDMI Layout"
    Screen 0 "SM750 HDMI Screen"
EndSection

Section "Device"
    Identifier "SM750 HDMI Device"
    Driver "modesetting"
    BusID "$xorg_busid"
    Option "kmsdev" "/dev/dri/by-path/pci-${bdf}-card"
    Option "AccelMethod" "none"
    Option "ShadowFB" "true"
EndSection

Section "Monitor"
    Identifier "SM750 HDMI Monitor"
EndSection

Section "Screen"
    Identifier "SM750 HDMI Screen"
    Device "SM750 HDMI Device"
    Monitor "SM750 HDMI Monitor"
    DefaultDepth 24
    SubSection "Display"
        Depth 24
    EndSubSection
EndSection
EOF
install -m 0644 "$temporary" "$output"
