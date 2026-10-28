#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
built=0

# Explicit directories also allow unpacked headers to be tested without
# installing or booting those kernels. Build in an isolated source copy when
# the checkout is in a source-only synced directory.
if (($#)); then
	kernel_dirs=("$@")
else
	kernel_dirs=(/lib/modules/*/build)
fi

for kernel_dir in "${kernel_dirs[@]}"; do
	[[ -f $kernel_dir/Makefile ]] || continue
	[[ -f $kernel_dir/include/generated/utsrelease.h ]] || continue
	kernel=$(sed -n 's/^#define UTS_RELEASE "\([^"]*\)"/\1/p' \
		"$kernel_dir/include/generated/utsrelease.h")
	case $kernel in
		6.1[7-9].*|6.[2-9][0-9].*|[7-9].*|[1-9][0-9].*) ;;
		*) continue ;;
	esac
	build_options=("KDIR=$kernel_dir")
	if [[ -n ${CC:-} ]]; then
		build_options+=("CC=$CC")
	fi
	for format in rgb565-bbdither xrgb8888 rgb565; do
		echo "Building sm750hdmidrm for $kernel ($format default)"
		target=all
		if [[ $format == rgb565-bbdither ]]; then
			target=check
		fi
		make -s -C "$project_dir" "${build_options[@]}" clean
		make -s -j "${JOBS:-12}" -C "$project_dir" \
			"${build_options[@]}" SM750_DRM_SCANOUT_DEFAULT="$format" "$target"
		"$project_dir/tests/check-drm-module.sh" \
			"$project_dir/src/sm750hdmidrm.ko"
		case $format in
			xrgb8888) macro=SM750_DRM_DEFAULT_XRGB8888 ;;
			rgb565) macro=SM750_DRM_DEFAULT_RGB565 ;;
			rgb565-bbdither) macro=SM750_DRM_DEFAULT_BBDITHER ;;
		esac
		if [[ $format == rgb565-bbdither ]]; then
			! grep -F -- '-DSM750_DRM_DEFAULT_XRGB8888=1' \
				"$project_dir/src/.sm750_drm.o.cmd" >/dev/null
			! grep -F -- '-DSM750_DRM_DEFAULT_RGB565=1' \
				"$project_dir/src/.sm750_drm.o.cmd" >/dev/null
		else
			grep -F -- "-D${macro}=1" \
				"$project_dir/src/.sm750_drm.o.cmd" >/dev/null
		fi
		((built += 1))
	done
done

((built > 0)) || {
	echo "No supported header-backed kernels were found." >&2
	exit 1
}
echo "Built $built sm750hdmidrm kernel/format combinations"
