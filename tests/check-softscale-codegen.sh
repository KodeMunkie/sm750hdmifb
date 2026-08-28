#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

object=${1:-src/sm750_dither.o}

[[ -f $object ]] || {
	echo "Missing dither object: $object" >&2
	exit 1
}

for symbol in \
	sm750_dither_scale_77_to_64_xrgb8888_to_rgb565 \
	sm750_dither_scale_77_to_64_sharpen_xrgb8888_to_rgb565 \
	sm750_dither_scale_5_to_4_xrgb8888_to_rgb565 \
	sm750_dither_scale_5_to_4_sharpen_xrgb8888_to_rgb565; do
	body=$(objdump -dr --no-show-raw-insn --disassemble="$symbol" "$object")
	grep -F "<$symbol>" <<<"$body" >/dev/null || {
		echo "Missing generated softscale function: $symbol" >&2
		exit 1
	}
	if grep -E 'R_[^[:space:]]+[[:space:]]+(sm750_scale_(77|5)|__ubsan_handle_out_of_bounds)' \
		<<<"$body" >/dev/null; then
		echo "Hot softscale function contains a helper or bounds-handler call: $symbol" >&2
		exit 1
	fi
done

echo "Softscale generated-code checks passed"
