#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_file=$project_dir/src/sm750_drm.c

for requirement in \
	'u32 *shadow_source_snapshot;' \
	'bool shadow_source_snapshot_valid;' \
	'module_param(double_shadow, bool, 0444);' \
	'return drm_gem_plane_helper_prepare_fb(&pipe->plane, plane_state);' \
	'.prepare_fb = sm750_shadow_pipe_prepare_fb,' \
	'sdev->dither_source_line[changed_x1] ==' \
	'sdev->dither_source_line[changed_x2 - 1] ==' \
	'memcpy(snapshot + changed_x1,' \
	'sdev->shadow_source_snapshot_valid = false;' \
	'sdev->shadow_source_snapshot_valid = true;' \
	'sdev->shadow_source_snapshot = kvcalloc('; do
	grep -F "$requirement" "$source_file" >/dev/null || {
		echo "Missing shadow source-damage requirement: $requirement" >&2
		exit 1
	}
done

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

echo "Shadow source-difference damage checks passed"
