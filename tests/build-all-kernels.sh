#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
built=0

for kernel_dir in /lib/modules/*; do
	kernel=${kernel_dir##*/}
	case $kernel in
		6.[89].*|6.[1-9][0-9].*|[7-9].*|[1-9][0-9].*) ;;
		*) continue ;;
	esac
	[[ -f $kernel_dir/build/Makefile ]] || continue
	for format in rgb565-bbdither xrgb8888 rgb565; do
		echo "Building sm750hdmidrm for $kernel ($format default)"
		make -s -C "$project_dir" KDIR="$kernel_dir/build" \
			SM750_DRM_SCANOUT_DEFAULT="$format" clean all
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
