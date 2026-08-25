// SPDX-License-Identifier: GPL-2.0-only
/*
 * High-throughput XRGB8888 to RGB565 ordered-dither conversion.
 *
 * Copyright (C) 2026 Benjamin Brown
 *
 * The 8x8 rank map was produced by deterministic offline optimisation of
 * progressive spatial dispersion and expanded-RGB565 colour error.
 */

#include "bbdither-rgb565.h"

static const uint8_t bbdither8[64] = {
	18,52,21,60,28, 7,48,32, 35, 6,41,10,36,56,14,61,
	27,57,24,53,17,31,49, 3, 45,11,37, 2,46, 8,33,30,
	16,50,22,59,25,54,20,58, 40, 5,42,12,39, 4,43, 9,
	26,63,23,51,19,62,15,55, 47,13,38, 0,44,29,34, 1,
};

int sm750_bbdither_rgb565_init(struct sm750_bbdither_rgb565 *ctx,
			   unsigned int green_gain_percent)
{
	unsigned int threshold;

	if (!ctx || green_gain_percent > 100)
		return -1;

	for (threshold = 0; threshold < 64; threshold++) {
		unsigned int value;

		for (value = 0; value < 256; value++) {
			unsigned int divisor6 = 255 * 100;
			unsigned int scaled5 = value * 31;
			unsigned int scaled6 = value * 63 * green_gain_percent;
			unsigned int q5 = scaled5 / 255;
			unsigned int q6 = scaled6 / divisor6;
			unsigned int rem5 = scaled5 % 255;
			unsigned int rem6 = scaled6 % divisor6;
			unsigned int midpoint5 = (2 * threshold + 1) * 255;
			unsigned int midpoint6 =
				(2 * threshold + 1) * divisor6;

			if (rem5 * 128 > midpoint5 && q5 < 31)
				q5++;
			if (rem6 * 128 > midpoint6 && q6 < 63)
				q6++;
			ctx->quantise5[threshold][value] = (uint8_t)q5;
			ctx->quantise6_green[threshold][value] = (uint8_t)q6;
		}
	}
	return 0;
}

unsigned int sm750_bbdither_threshold(unsigned int x, unsigned int y)
{
	return bbdither8[((y & 7U) << 3) | (x & 7U)];
}

void sm750_bbdither_xrgb8888_to_rgb565(
		const struct sm750_bbdither_rgb565 *ctx,
		uint16_t *restrict dst, size_t dst_stride,
		const uint32_t *restrict src, size_t src_stride,
		unsigned int x_origin, unsigned int y_origin,
		unsigned int width, unsigned int height)
{
	unsigned int y;

	for (y = 0; y < height; y++) {
		const uint8_t *matrix = &bbdither8[((y_origin + y) & 7U) << 3];
		unsigned int x;

		for (x = 0; x < width; x++) {
			uint32_t pixel = src[x];
			unsigned int threshold = matrix[(x_origin + x) & 7U];
			unsigned int red =
				ctx->quantise5[threshold][(pixel >> 16) & 0xffU];
			unsigned int green =
				ctx->quantise6_green[threshold][(pixel >> 8) & 0xffU];
			unsigned int blue =
				ctx->quantise5[threshold][pixel & 0xffU];
			dst[x] = (uint16_t)((red << 11) | (green << 5) | blue);
		}
		src += src_stride;
		dst += dst_stride;
	}
}
