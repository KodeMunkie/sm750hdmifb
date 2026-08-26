#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

awk 'BEGIN {
	for (x = 0; x < 2048; x++) {
		phase = x % 4
		source_x = int(x / 4) * 5 + phase
		first_weight = 4 - phase
		second_weight = 1 + phase
		if (source_x < 0 || source_x + 1 >= 2560 ||
		    first_weight + second_weight != 5)
			exit 1
		coverage[source_x] += first_weight
		coverage[source_x + 1] += second_weight
	}
	for (x = 0; x < 2560; x++)
		if (!coverage[x])
			exit 1

	# The fixed 77:64 scaler must exactly match the generic 2464:2048
	# area weights after their common factor of 32 is removed.
	for (x = 0; x < 2048; x++) {
		start = x * 2464
		end = (x + 1) * 2464
		first = int(start / 2048)
		phase = (x * 13) % 64
		fixed_first = int(x * 77 / 64)
		weight0 = 64 - phase
		weight1 = 13 + phase
		if (weight1 > 64)
			weight1 = 64
		weight2 = phase > 51 ? phase - 51 : 0
		if (first != fixed_first || weight0 + weight1 + weight2 != 77)
			exit 1
		for (tap = 0; tap < 3; tap++) {
			source_x = first + tap
			left = source_x * 2048
			if (start > left)
				left = start
			right = (source_x + 1) * 2048
			if (end < right)
				right = end
			generic_weight = right > left ? (right - left) / 32 : 0
			fixed_weight = tap == 0 ? weight0 : (tap == 1 ? weight1 : weight2)
			if (generic_weight != fixed_weight)
				exit 1
		}
	}

	scale_widths[0] = 2464
	for (width_index = 0; width_index < 1; width_index++) {
		src_width = scale_widths[width_index]
		delete coverage_general
		for (x = 0; x < 2048; x++) {
			start = x * src_width
			end = (x + 1) * src_width
			first = int(start / 2048)
			last = int((end - 1) / 2048)
			total_weight = 0
			for (source_x = first; source_x <= last; source_x++) {
				left = source_x * 2048
				if (start > left)
					left = start
				right = (source_x + 1) * 2048
				if (end < right)
					right = end
				weight = right - left
				coverage_general[source_x] += weight
				total_weight += weight
				damage_x1 = int(source_x * 2048 / src_width)
				damage_x2 = int(((source_x + 1) * 2048 + src_width - 1) / src_width)
				if (x < damage_x1 || x >= damage_x2)
					exit 1
			}
			if (total_weight != src_width)
				exit 1
		}
		for (x = 0; x < src_width; x++)
			if (!coverage_general[x])
				exit 1

		# A partial update needs one output pixel for the scale filter and
		# another when sharpening consumes neighbouring scaled pixels.
		for (source_x = 0; source_x < src_width; source_x++) {
			damage_x1 = int(source_x * 2048 / src_width)
			damage_x2 = int(((source_x + 1) * 2048 + src_width - 1) / src_width)
			filter_x1 = damage_x1 > 0 ? damage_x1 - 1 : 0
			filter_x2 = damage_x2 < 2048 ? damage_x2 + 1 : 2048
			sharp_x1 = filter_x1 > 0 ? filter_x1 - 1 : 0
			sharp_x2 = filter_x2 < 2048 ? filter_x2 + 1 : 2048
			if (filter_x1 > damage_x1 || filter_x2 < damage_x2 ||
			    sharp_x1 > filter_x1 || sharp_x2 < filter_x2)
				exit 1
			if (damage_x1 > 0 && filter_x1 != damage_x1 - 1)
				exit 1
			if (damage_x2 < 2048 && filter_x2 != damage_x2 + 1)
				exit 1
			if (filter_x1 > 0 && sharp_x1 != filter_x1 - 1)
				exit 1
			if (filter_x2 < 2048 && sharp_x2 != filter_x2 + 1)
				exit 1
		}
	}
	print "Softscale 2560/2464-to-2048 mapping checks passed"
}'
