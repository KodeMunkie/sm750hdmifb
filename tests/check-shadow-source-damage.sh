#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_file=$project_dir/src/sm750_drm.c

for requirement in \
	'u32 *shadow_source_snapshot;' \
	'u32 shadow_source_width;' \
	'bool shadow_source_snapshot_valid;' \
	'module_param(double_shadow, bool, 0444);' \
	'return drm_gem_plane_helper_prepare_fb(&pipe->plane, plane_state);' \
	'.prepare_fb = sm750_shadow_pipe_prepare_fb,' \
	'#define SM750_DRM_DAMAGE_SPLIT_GAP 64' \
	'if (!src->is_iomem) {' \
	'source_row = (const u32 *)((const u8 *)src->vaddr +' \
	'sm750_next_changed_u32_run(source_row, snapshot,' \
	'sm750_next_changed_u16_run(source, snapshot, count,' \
	'(block_end - x) * sizeof(*source)' \
	'memcpy(snapshot + run_x1, source_row + run_x1,' \
	'sm750_softscale_upload_span(sdev, snapshot, y,' \
	'u16 *rgb565_scanout_snapshot;' \
	'sm750_upload_rgb565_span(sdev,' \
	'bool rgb565_scanout_snapshot_valid;' \
	'#define SM750_DRM_MAX_DAMAGE_RECTS 32' \
	'sm750_damage_lossless_union(' \
	'sm750_damage_add(rects, &rect_count, &damage)' \
	'bool shadow_write_pending;' \
	'sm750_shadow_finish_uploads(sdev);' \
	'if (sdev->shadow_write_pending)' \
	'rect->x2 >= sdev->shadow_source_width' \
	'sdev->shadow_source_snapshot_valid = false;' \
	'sdev->shadow_source_snapshot_valid = true;' \
	'sdev->shadow_source_snapshot = kvcalloc('; do
	grep -F "$requirement" "$source_file" >/dev/null || {
		echo "Missing shadow source-damage requirement: $requirement" >&2
		exit 1
	}
done

for requirement in \
	'struct drm_atomic_helper_damage_iter iter;' \
	'drm_atomic_helper_damage_iter_init(&iter, old_plane_state, state);' \
	'drm_atomic_for_each_plane_damage(&iter, &damage)'; do
	grep -F "$requirement" "$source_file" >/dev/null || {
		echo "Missing individual damage-clip requirement: $requirement" >&2
		exit 1
	}
done

if grep -F 'drm_atomic_helper_damage_merged(old_plane_state' \
	"$source_file" >/dev/null; then
	echo "Damage clips are still being collapsed into a bounding box" >&2
	exit 1
fi

awk '
function damage_runs(old, new, start, end, gap, result, x, run_start, last_changed) {
	result = ""
	x = start
	while (x < end) {
		while (x < end && old[x] == new[x])
			x++
		if (x == end)
			break
		run_start = x
		last_changed = x++
		while (x < end) {
			if (old[x] != new[x]) {
				last_changed = x++
				continue
			}
			if (x - last_changed >= gap)
				break
			x++
		}
		result = result (result == "" ? "" : ",") run_start ":" last_changed + 1
	}
	return result == "" ? "none" : result
}
function chunk_runs(old, new, start, end, gap, result, x, block_end,
		run_start, last_changed, first_changed, candidate) {
	result = ""
	x = start
	while (x < end) {
		while (x < end) {
			block_end = x + gap < end ? x + gap : end
			for (candidate = x; candidate < block_end; candidate++)
				if (old[candidate] != new[candidate])
					break
			if (candidate < block_end) {
				x = candidate
				break
			}
			x = block_end
		}
		if (x == end)
			break
		run_start = x
		last_changed = x++
		while (x < end) {
			block_end = x + gap < end ? x + gap : end
			first_changed = x
			while (first_changed < block_end && old[first_changed] == new[first_changed])
				first_changed++
			if (first_changed == block_end) {
				if (block_end - last_changed > gap)
					break
				x = block_end
				continue
			}
			if (first_changed - last_changed > gap)
				break
			candidate = block_end - 1
			while (old[candidate] == new[candidate])
				candidate--
			last_changed = candidate
			x = block_end
		}
		result = result (result == "" ? "" : ",") run_start ":" last_changed + 1
		x = last_changed + 1
	}
	return result == "" ? "none" : result
}
BEGIN {
	for (x = 0; x < 24; x++) {
		old[x] = x
		new[x] = x
	}
	if (damage_runs(old, new, 0, 24, 4) != "none")
		exit 1
	new[2] = 99
	new[5] = 88
	if (damage_runs(old, new, 0, 24, 4) != "2:6")
		exit 1
	new[16] = 77
	new[17] = 66
	if (damage_runs(old, new, 0, 24, 4) != "2:6,16:18")
		exit 1
	new[8] = 55
	if (damage_runs(old, new, 0, 24, 4) != "2:9,16:18")
		exit 1
	if (chunk_runs(old, new, 0, 24, 4) != damage_runs(old, new, 0, 24, 4))
		exit 1
	srand(750)
	for (test = 0; test < 500; test++) {
		for (x = 0; x < 257; x++) {
			old[x] = int(rand() * 65536)
			new[x] = old[x]
			if (rand() < 0.12)
				new[x] = int(rand() * 65536)
		}
		if (chunk_runs(old, new, 0, 257, 64) != damage_runs(old, new, 0, 257, 64))
			exit 1
	}
}
' /dev/null

awk '
function changed_span(old, new, start, count, result, first, last, x) {
	first = 0
	while (first < count && old[start + first] == new[first])
		first++
	if (first == count)
		return "none"
	last = count
	while (last > first && old[start + last - 1] == new[last - 1])
		last--
	return start + first ":" start + last
}
BEGIN {
	for (x = 0; x < 16; x++)
		old[x] = x
	for (x = 0; x < 6; x++)
		new[x] = old[5 + x]
	if (changed_span(old, new, 5, 6) != "none")
		exit 1
	new[2] = 99
	if (changed_span(old, new, 5, 6) != "7:8")
		exit 1
	new[5] = 88
	if (changed_span(old, new, 5, 6) != "7:11")
		exit 1
}
' /dev/null

echo "Shadow source-difference damage checks passed"
