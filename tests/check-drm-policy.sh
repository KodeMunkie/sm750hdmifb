#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

source_file=${1:-src/sm750_drm.c}
required=(
	640x480 800x600 1024x768 1152x864 1280x720 1280x800
	1280x1024 1366x768 1600x900 1680x1050 1920x1080 1920x810
	2048x864 2048x1024 2048x1080 2048x1152 2464x1080 2560x1080
)

for mode in "${required[@]}"; do
	grep -F "DRM_MODE(\"$mode\"" "$source_file" >/dev/null || {
		echo "Missing required DRM mode: $mode" >&2
		exit 1
	}
done
for mode in 640x480 800x600 1024x768 1152x864 1280x720 1280x800 \
		1280x1024 1366x768 1600x900 1680x1050 1920x810 2048x864; do
	test "$(grep -Fc "DRM_MODE(\"$mode\"" "$source_file")" -eq 5 || {
		echo "Incomplete five-rate mode set: $mode" >&2
		exit 1
	}
done
for entry in '1920x1080 8' '2048x1024 4' '2048x1080 6' \
		'2048x1152 2' '2464x1080 6' '2560x1080 6'; do
	mode=${entry% *}
	expected=${entry#* }
	test "$(grep -Fc "DRM_MODE(\"$mode\"" "$source_file")" -eq "$expected" || {
		echo "Unexpected hardware-limited mode count: $mode" >&2
		exit 1
	}
done
grep -F 'DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED' "$source_file" \
	>/dev/null
grep -F 'DRM_FORMAT_XRGB8888' "$source_file" >/dev/null
grep -F '#define SM750_DRM_MAX_WIDTH 2560' "$source_file" >/dev/null
grep -F '#define SM750_DRM_PHYSICAL_MAX_WIDTH 2048' "$source_file" >/dev/null
grep -F '#define SM750_DRM_MAX_HEIGHT 1152' "$source_file" >/dev/null
grep -F 'static const struct drm_display_mode sm750_standard_modes[]' \
	"$source_file" >/dev/null
for clock in 148352 148500 156240 160848 \
	99860 99959 117180 120672 125849 \
	113394 113507 133021 136975 142842 \
	134440 134574 157748 162408 151149 151300; do
	grep -F "DRM_MODE_TYPE_DRIVER, $clock," "$source_file" >/dev/null || {
		echo "Missing required standards-derived clock: $clock kHz" >&2
		exit 1
	}
done
if grep -F 'DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 167850' \
	"$source_file" >/dev/null ||
	grep -F 'DRM_MODE("2048x1024", DRM_MODE_TYPE_DRIVER, 169335' \
	"$source_file" >/dev/null ||
	grep -E 'DRM_MODE\("2048x1152".*(177411|182633|190562)' \
	"$source_file" >/dev/null; then
	echo "Out-of-spec high-resolution refresh mode is present." >&2
	exit 1
fi
grep -F 'DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC' "$source_file" \
	>/dev/null
grep -F 'drm->mode_config.prefer_shadow = 1;' "$source_file" >/dev/null
for requirement in \
	'module_param(scanout_format, charp, 0444);' \
	'"rgb565-bbdither"' \
	'"rgb565"' \
	'DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,' \
	'drm_gem_fb_create_with_dirty' \
	'drm_atomic_for_each_plane_damage(&iter, &damage)' \
	'drm_plane_enable_fb_damage_clips(&sdev->pipe.plane)' \
		'PANEL_DISPLAY_CTRL_FORMAT_16' \
		'sm750_xrgb8888_to_rgb565' \
		'sm750_dither_xrgb8888_to_rgb565' \
		'sm750_shadow_upload(sdev,' \
		'memcpy_toio(sdev->vram + destination, source, size);'; do
	grep -F "$requirement" "$source_file" >/dev/null || {
		echo "Missing RGB565 dither requirement: $requirement" >&2
		exit 1
	}
done
if grep -F 'crtc_state->no_vblank = true;' "$source_file" >/dev/null; then
	echo "DRM source disables vblank event pacing." >&2
	exit 1
fi
grep -F 'if (!edid_only && !sm750_mode_is_catalogued(mode))' \
	"$source_file" >/dev/null
grep -F 'line_width = ALIGN(mode.hdisplay * mode.cpp, SM750_DRM_LINE_ALIGN);' \
	"$source_file" >/dev/null
grep -F 'drm_gem_vram_fill_create_dumb(file, drm, 0,' \
	"$source_file" >/dev/null
grep -F '.dumb_create = sm750_dumb_create,' "$source_file" >/dev/null
grep -F '!IS_ALIGNED(plane_state->fb->pitches[0],' "$source_file" >/dev/null
grep -F 'sm750_sii902x_read_edid(&sdev->pdev->dev' "$source_file" >/dev/null
grep -F 'drm_connector_update_edid_property(connector, edid);' \
	"$source_file" >/dev/null
grep -F 'count = drm_add_edid_modes(connector, edid);' \
	"$source_file" >/dev/null
for requirement in \
	'if (sdev->connected_edid_valid && sdev->monitor_disconnected &&' \
	'memcmp(sdev->connected_edid, edid, SM750_DRM_EDID_SIZE)' \
	'sm750_remember_disconnected_mode(sdev, connector);' \
	'sm750_select_reconnected_monitor_mode(connector);' \
	'sm750_catalog_supports_edid_mode(sdev, mode)' \
	'sdev->hotplug_preferred_valid = true;' \
	'"hotplug preferred mode %ux%u@%u is unavailable\n"'; do
	grep -F "$requirement" "$source_file" >/dev/null || {
		echo "Missing EDID hotplug policy: $requirement" >&2
		exit 1
	}
done
for parameter in edid_only softscale_wide sharpen double_shadow \
		disable_hardware_cursor enable_dma disable_dma; do
	grep -F "module_param($parameter, bool, 0444);" "$source_file" \
		>/dev/null || {
		echo "Missing runtime policy parameter: $parameter" >&2
		exit 1
	}
done
grep -F 'module_param(preferred_width, uint, 0444);' "$source_file" >/dev/null
grep -F 'module_param(preferred_height, uint, 0444);' "$source_file" >/dev/null
grep -F 'module_param(preferred_refresh, uint, 0444);' "$source_file" >/dev/null
for requirement in \
	'drm_vblank_init(drm, 1);' \
	'#include <drm/drm_vblank_helper.h>' \
	'DRM_CRTC_VBLANK_TIMER_FUNCS,' \
	'sdev->pipe.crtc.funcs = &sm750_crtc_funcs;' \
	'PANEL_CURRENT_LINE' \
	'drm_crtc_vblank_get(&pipe->crtc)' \
	'drm_crtc_handle_vblank(&sdev->pipe.crtc);' \
	'drm_crtc_arm_vblank_event(&pipe->crtc, event);' \
	'drm_crtc_vblank_on(&pipe->crtc);' \
	'.enable_vblank = sm750_pipe_enable_vblank,' \
	'.disable_vblank = sm750_pipe_disable_vblank,' \
	'hrtimer_try_to_cancel(&sdev->vblank_timer);' \
	'hrtimer_cancel(&sdev->vblank_timer);'; do
	grep -F "$requirement" "$source_file" >/dev/null || {
		echo "Missing DRM vblank requirement: $requirement" >&2
		exit 1
	}
done
if ! grep -F '#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)' \
	"$source_file" >/dev/null ||
	! grep -F '#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)' \
	"$source_file" >/dev/null; then
	echo "DRM source lacks versioned core-timer/fallback selection." >&2
	exit 1
fi
if grep -E 'request(_threaded)?_irq' "$source_file" >/dev/null; then
	echo "DRM source unexpectedly enables unvalidated hardware IRQ handling." >&2
	exit 1
fi
if grep -F '#define SM750_DRM_MAX_WIDTH 1920' "$source_file" >/dev/null; then
	echo "Initial build was accidentally capped at 1920 pixels." >&2
	exit 1
fi
for excluded in 2432x1080 2496x1080 2540x1080 \
		SM750_DRM_SECONDARY_SCANOUT; do
	if grep -F "$excluded" "$source_file" >/dev/null; then
		echo "Excluded experimental path remains: $excluded" >&2
		exit 1
	fi
done
echo "DRM mode policy checks passed"
