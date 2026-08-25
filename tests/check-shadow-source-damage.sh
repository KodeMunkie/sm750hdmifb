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
	'sdev->dither_source_line[changed_x1] ==' \
	'sdev->dither_source_line[changed_x2 - 1] ==' \
	'memcpy(snapshot + changed_x1,' \
	'sdev->dither_source_line + src_x1, src,' \
	'scale_source = snapshot;' \
	'src_x1 += changed_x1;' \
	'sdev->dither_source_line + changed_x1,' \
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
function changed_span(old, new, count, result, first, last, x) {
	first = 0
	while (first < count && old[first] == new[first])
		first++
	if (first == count)
		return "none"
	last = count
	while (last > first && old[last - 1] == new[last - 1])
		last--
	return first ":" last
}
BEGIN {
	for (x = 0; x < 8; x++) {
		old[x] = x
		new[x] = x
	}
	if (changed_span(old, new, 8) != "none")
		exit 1
	new[3] = 99
	if (changed_span(old, new, 8) != "3:4")
		exit 1
	new[6] = 88
	if (changed_span(old, new, 8) != "3:7")
		exit 1
	new[0] = 77
	new[7] = 66
	if (changed_span(old, new, 8) != "0:8")
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
