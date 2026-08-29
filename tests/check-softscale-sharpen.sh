#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_file=$project_dir/src/sm750_drm.c
dither_file=$project_dir/src/sm750_dither.c
dither_header=$project_dir/src/sm750_dither.h

for requirement in \
	'sm750_dither_scale_77_to_64_sharpen_xrgb8888_to_rgb565(' \
	'if (sdev->softscale_source_width == 2464)' \
	'SM750_DITHER_SHARPEN_LUT_SIZE 1024' \
	'static __always_inline u32 sm750_scale_77_next(' \
	'static __always_inline u32 sm750_scale_5_next(' \
	'sm750_pack_rgb_channels(' \
	'sm750_weighted_pixel_3(' \
	'sm750_scale_77_next(' \
	'sm750_dither_scale_5_to_4_sharpen_xrgb8888_to_rgb565(' \
	'sm750_scale_5_next(' \
	'sm750_sharpen_dither_pixel_8(' \
	'SM750_RUN_SHARPEN_DITHER_8(sm750_scale_5_next(src, &state));' \
	'SM750_RUN_SHARPEN_DITHER_8(sm750_scale_77_next(src, &state));' \
	'SM750_EMIT_SHARPEN_DITHER_PIXEL(next_pixel, 0U);' \
	'SM750_EMIT_SHARPEN_DITHER_PIXEL(next_pixel, 7U);'; do
	grep -F "$requirement" "$source_file" "$dither_file" \
		"$dither_header" >/dev/null || {
		echo "Missing fused 2464 sharpen requirement: $requirement" >&2
		exit 1
	}
done

fused_body=$(sed -n \
	'/^void sm750_dither_scale_77_to_64_sharpen_xrgb8888_to_rgb565(/,/^}/p' \
	"$dither_file")
if grep -F 'scratch' <<<"$fused_body" >/dev/null; then
	echo "Fused 2464 sharpen path still uses an intermediate scratch line" >&2
	exit 1
fi

awk 'function sharpen(left, center, right, percent, detail, adjustment, value) {
	detail = 2 * center - left - right
	adjustment = detail * percent
	if (adjustment >= 0)
		adjustment = int((adjustment + 50) / 100)
	else
		adjustment = int((adjustment - 50) / 100)
	value = center + adjustment
	if (value < 0)
		value = 0
	if (value > 255)
		value = 255
	return value
}
BEGIN {
	for (value = 0; value < 256; value++)
		if (sharpen(value, value, value, 8) != value)
			exit 1
	if (sharpen(0, 128, 128, 8) != 138)
		exit 1
	if (sharpen(128, 128, 0, 8) != 138)
		exit 1
	if (sharpen(255, 0, 255, 8) != 0)
		exit 1
	if (sharpen(0, 255, 0, 8) != 255)
		exit 1
	print "Softscale 8% contrast sharpening checks passed"
}'
