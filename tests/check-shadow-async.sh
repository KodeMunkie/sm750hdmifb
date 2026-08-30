#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_file=$project_dir/src/sm750_drm.c

for requirement in \
	'#define SM750_DRM_DEFAULT_ASYNC_UPDATES 1' \
	'module_param(async_updates, bool, 0444);' \
	'alloc_ordered_workqueue(' \
	'WQ_HIGHPRI | WQ_MEM_RECLAIM' \
	'queue_work(sdev->shadow_workqueue, &sdev->shadow_work);' \
	'cancel_work_sync(&sdev->shadow_work);' \
	'u32 *async_source;' \
	'struct drm_rect async_damage[SM750_DRM_MAX_DAMAGE_RECTS];' \
	'sm750_async_copy_rect_locked(' \
	'sm750_damage_add_bounded(' \
	'sm750_async_queue_damage(sdev, state, rects,' \
	'sm750_shadow_source_rect_locked(sdev, NULL,' \
	'sm750_async_quiesce(sdev);'; do
	grep -F "$requirement" "$source_file" >/dev/null || {
		echo "Missing asynchronous shadow requirement: $requirement" >&2
		exit 1
	}
done

# The commit callback may copy damage to normal RAM, but conversion and VRAM
# upload must remain exclusively on the worker or explicit fallback path.
update_body=$(sed -n '/static void sm750_pipe_update(/,/^}/p' "$source_file")
grep -F 'sm750_async_queue_damage(sdev, state, rects,' \
	<<<"$update_body" >/dev/null
grep -F 'if (rect_count) {' <<<"$update_body" >/dev/null

awk 'BEGIN {
	max = 4
	count = 0
	for (frame = 1; frame <= 100; frame++) {
		# Repeated drag updates collapse into one latest bounding region rather
		# than growing a frame-by-frame FIFO.
		x1 = frame
		x2 = frame + 300
		if (count == 0) {
			left = x1
			right = x2
			count = 1
		} else {
			if (x1 < left)
				left = x1
			if (x2 > right)
				right = x2
		}
		if (count > max)
			exit 1
	}
	if (count != 1 || left != 1 || right != 400)
		exit 1
}' /dev/null

echo "Asynchronous shadow mailbox checks passed"
