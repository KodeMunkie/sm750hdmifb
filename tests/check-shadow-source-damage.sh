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
	'if (!memcmp(source_row + src_x1, snapshot + src_x1,' \
	'while (x < src_x2 && source_row[x] == snapshot[x])' \
	'if (x - last_changed >=' \
	'SM750_DRM_DAMAGE_SPLIT_GAP)' \
	'memcpy(snapshot + run_x1, source_row + run_x1,' \
	'sm750_softscale_upload_span(sdev, snapshot, y,' \
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
