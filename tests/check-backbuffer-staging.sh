#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_file=$project_dir/src/sm750_drm.c

for requirement in \
	'module_param(backbuffer_staging, bool, 0444);' \
	'#define SM750_DRM_FLIP_TIMEOUT_US 50000' \
	'u32 shadow_upload_offset;' \
	'u32 backbuffer_offset;' \
	'bool backbuffer_ready;' \
	'bool backbuffer_mirror;' \
	'sm750_damage_is_full_frame(' \
	'count == 1 && rects[0].x1 <= 0 && rects[0].y1 <= 0' \
	'sdev->shadow_source_snapshot_valid = false;' \
	'sdev->rgb565_scanout_snapshot_valid = false;' \
	'destination + sdev->shadow_upload_offset, source, size);' \
	'sm750_shadow_finish_uploads(sdev);' \
	'sm750_present_staged_frame(sdev)' \
	'poke32(SM750_DRM_FB_ADDRESS, SM750_DRM_FB_ADDRESS_STATUS |' \
	'readl_poll_timeout(sdev->regs + SM750_DRM_FB_ADDRESS, address,' \
	'sdev->scanout_offset = sdev->backbuffer_offset;' \
	'sdev->backbuffer_offset = old_scanout;'; do
	grep -F "$requirement" "$source_file" >/dev/null || {
		echo "Missing backbuffer staging requirement: $requirement" >&2
		exit 1
	}
done

present_body=$(sed -n '/static bool sm750_present_staged_frame(/,/^}/p' \
	"$source_file")
for requirement in \
	'sdev->backbuffer_ready = false;' \
	'sdev->backbuffer_mirror = true;' \
	'sdev->shadow_source_snapshot_valid = false;' \
	'return false;'; do
	grep -F "$requirement" <<<"$present_body" >/dev/null || {
		echo "Flip timeout does not enter safe mirrored recovery" >&2
		exit 1
	}
done

async_body=$(sed -n '/static void sm750_async_shadow_work(/,/^}/p' \
	"$source_file")
grep -F 'staged && !sm750_present_staged_frame(sdev)' \
	<<<"$async_body" >/dev/null || {
	echo "Asynchronous flip timeout does not redraw the full frame" >&2
	exit 1
}
test "$(grep -Fc 'sm750_shadow_finish_uploads(sdev);' <<<"$async_body")" -ge 2 || {
	echo "Asynchronous flip timeout recovery is not drained" >&2
	exit 1
}

enable_body=$(sed -n '/static void sm750_pipe_enable(/,/^}/p' "$source_file")
for requirement in \
	'frame_size = (u64)pitch * crtc_state->adjusted_mode.vdisplay;' \
	'sdev->backbuffer_offset = ALIGN(sdev->scanout_frame_size,' \
	'sdev->scanout_frame_size <=' \
	'sdev->cursor_offset - sdev->backbuffer_offset;' \
	'sdev->backbuffer_ready = sdev->backbuffer_available;'; do
	grep -F "$requirement" <<<"$enable_body" >/dev/null || {
		echo "Backbuffer reservation is not bounded by available VRAM" >&2
		exit 1
	}
done

# The tested 2048x1080 RGB565 mode needs two 4,423,680-byte buffers and
# remains below the 16 MiB cursor reservation.
awk 'BEGIN {
	pitch = 2048 * 2
	height = 1080
	frame = pitch * height
	back = frame
	vram = 16 * 1024 * 1024
	cursor = vram - 1024
	if (frame != 4423680 || back + frame > cursor)
		exit 1
}' /dev/null

echo "Full-frame backbuffer staging checks passed"
