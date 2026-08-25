#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_file=$project_dir/src/sm750_drm.c

for requirement in \
	'#define SM750_DRM_CURSOR_WIDTH 64' \
	'#define SM750_DRM_CURSOR_HEIGHT 64' \
	'module_param(disable_hardware_cursor, bool, 0444);' \
	'sdev->hardware_cursor = !disable_hardware_cursor;' \
	'struct drm_plane cursor_plane;' \
	'DRM_FORMAT_ARGB8888' \
	'DRM_PLANE_TYPE_CURSOR' \
	'DRM_GEM_SHADOW_PLANE_FUNCS' \
	'DRM_GEM_SHADOW_PLANE_HELPER_FUNCS' \
	'sdev->pipe.crtc.cursor = &sdev->cursor_plane;' \
	'drm->mode_config.cursor_width = SM750_DRM_CURSOR_WIDTH;' \
	'sm750_cursor_unpremultiply' \
	'sm750_cursor_palette' \
	'sm750_cursor_encode' \
	'bool cursor_image_valid;' \
	'bool rebuild_image;' \
	'state->fb != old_state->fb' \
	'state->fb_damage_clips != old_state->fb_damage_clips' \
	'output_width != sdev->cursor_encoded_width' \
	'if (rebuild_image) {' \
	'value << ((x & 3) * 2)' \
	'SM750_DRM_HWC_ADDRESS_ENABLE |' \
	'64x64 hardware cursor plane enabled with ARGB palette conversion'; do
	grep -F "$requirement" "$source_file" >/dev/null || {
		echo "Missing hardware-cursor requirement: $requirement" >&2
		exit 1
	}
done

awk '
function div_up(value, divisor) {
	return int((value + divisor - 1) / divisor)
}
BEGIN {
	packed = 1 * 4 + 2 * 16 + 3 * 64
	if (packed != 228)
		exit 1
	if (div_up(64 * 2048, 2464) != 54)
		exit 1
	if (int(1232 * 2048 / 2464) != 1024)
		exit 1
	if (248 * 256 + 252 * 8 + int(255 / 8) != 65535)
		exit 1
}
' /dev/null

echo "Hardware cursor plane checks passed"
