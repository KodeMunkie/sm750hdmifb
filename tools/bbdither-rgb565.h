// SPDX-License-Identifier: GPL-2.0-only
/* Damage-stable bbdither 8x8 ordered dithering for RGB565 scanout. */
#ifndef SM750_BBDITHER_RGB565_H
#define SM750_BBDITHER_RGB565_H

#include <stddef.h>
#include <stdint.h>

struct sm750_bbdither_rgb565 {
	uint8_t quantise5[64][256];
	uint8_t quantise6_green[64][256];
};

/* green_gain_percent is 94 for the validated SM750 daily profile. */
int sm750_bbdither_rgb565_init(struct sm750_bbdither_rgb565 *ctx,
			   unsigned int green_gain_percent);

/* Return the 0..63 rank at an absolute screen coordinate. */
unsigned int sm750_bbdither_threshold(unsigned int x, unsigned int y);

/*
 * Convert XRGB8888 pixels (numeric 0x00RRGGBB) to RGB565. Strides are in
 * pixels. Absolute origins keep phase stable across damage rectangles.
 */
void sm750_bbdither_xrgb8888_to_rgb565(
		const struct sm750_bbdither_rgb565 *ctx,
		uint16_t *restrict dst, size_t dst_stride,
		const uint32_t *restrict src, size_t src_stride,
		unsigned int x_origin, unsigned int y_origin,
		unsigned int width, unsigned int height);

#endif
