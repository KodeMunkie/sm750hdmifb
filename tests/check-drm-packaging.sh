#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
builder=$project_dir/build-package.sh
dkms_config=$project_dir/dkms.conf

bash -n "$builder" "$project_dir/packaging/write-xorg-config.sh"
sh -n "$project_dir/packaging/drm-postinst" \
	"$project_dir/packaging/drm-prerm" \
	"$project_dir/packaging/drm-postrm"
grep -F 'PACKAGE_VERSION="0.5.2"' "$dkms_config" >/dev/null
grep -F 'BUILT_MODULE_NAME[0]="sm750hdmidrm"' "$dkms_config" >/dev/null
grep -F 'readonly package=sm750hdmifb' "$builder" >/dev/null
grep -F 'readonly deb="${dist_dir}/${package}_${version}_${arch}.deb"' \
	"$builder" >/dev/null
grep -F 'Maintainer: Benjamin Brown' "$builder" >/dev/null
grep -F 'SM750_DRM_SCANOUT_DEFAULT' "$project_dir/src/Makefile" >/dev/null
grep -F 'SM750_DRM_DEFAULT_SCANOUT_FORMAT "rgb565-bbdither"' \
	"$project_dir/src/sm750_drm.c" >/dev/null
grep -F 'SM750_DRM_DEFAULT_SCANOUT_FORMAT "xrgb8888"' \
	"$project_dir/src/sm750_drm.c" >/dev/null
grep -F 'SM750_DRM_DEFAULT_SCANOUT_FORMAT "rgb565"' \
	"$project_dir/src/sm750_drm.c" >/dev/null
! grep -F 'sm750hdmifb.o' "$project_dir/src/Makefile" >/dev/null
! grep -F 'sm750fb' "$project_dir/dkms.conf" >/dev/null

echo "Unified DRM package checks passed"
