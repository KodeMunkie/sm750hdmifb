#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
generator=$project_dir/packaging/write-xorg-config.sh
config=$(mktemp /tmp/sm750hdmidrm-xorg.XXXXXX.conf)
trap 'rm -f -- "$config"' EXIT

"$generator" "$config" 0000:01:02.0
grep -F 'Option "AutoAddGPU" "false"' "$config" >/dev/null
grep -F 'Driver "modesetting"' "$config" >/dev/null
grep -F 'BusID "PCI:1:2:0"' "$config" >/dev/null
grep -F 'Option "kmsdev" "/dev/dri/by-path/pci-0000:01:02.0-card"' \
	"$config" >/dev/null
grep -F 'Option "AccelMethod" "none"' "$config" >/dev/null
grep -F 'Option "ShadowFB" "true"' "$config" >/dev/null
if grep -E 'Option "(SWcursor|DoubleShadow)" "true"' "$config" \
		>/dev/null ||
	grep -E 'Option "(SWcursor|DoubleShadow)" "true"' \
		"$project_dir/packaging/20-sm750hdmidrm.conf" >/dev/null; then
	echo "Xorg configuration overrides a kernel-controlled feature." >&2
	exit 1
fi
if grep -F 'Driver "nvidia"' "$config" >/dev/null; then
	echo "Generated Xorg configuration enables an NVIDIA display driver." >&2
	exit 1
fi
! grep -F 'sm750hdmidrm-test' "$project_dir/packaging/drm-postinst" >/dev/null
! grep -F 'systemctl' "$project_dir/packaging/drm-postinst" >/dev/null

"$generator" "$config" 0001:0a:1f.7
grep -F 'BusID "PCI:10@1:31:7"' "$config" >/dev/null
grep -F 'pci-0001:0a:1f.7-card' "$config" >/dev/null

echo "DRM Xorg isolation checks passed"
