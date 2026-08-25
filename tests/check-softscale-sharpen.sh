#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

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
