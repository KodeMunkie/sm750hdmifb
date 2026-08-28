// SPDX-License-Identifier: GPL-2.0-only
/* Damage-stable bbdither 8x8 ordered quantisation for RGB565 scanout. */

#include <linux/errno.h>
#include <linux/minmax.h>

#include "sm750_dither.h"

static const u8 bbdither8[64] = {
	18, 52, 21, 60, 28,  7, 48, 32, 35,  6, 41, 10, 36, 56, 14, 61,
	27, 57, 24, 53, 17, 31, 49,  3, 45, 11, 37,  2, 46,  8, 33, 30,
	16, 50, 22, 59, 25, 54, 20, 58, 40,  5, 42, 12, 39,  4, 43,  9,
	26, 63, 23, 51, 19, 62, 15, 55, 47, 13, 38,  0, 44, 29, 34,  1,
};

int sm750_dither_init(struct sm750_dither *ctx,
		      unsigned int green_gain_percent)
{
	unsigned int sharpen_index;
	unsigned int position;

	if (!ctx || green_gain_percent > 100)
		return -EINVAL;

	for (position = 0; position < 64; position++) {
		unsigned int threshold = bbdither8[position];
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
			ctx->quantise5[position][value] = q5;
			ctx->quantise6_green[position][value] = q6;
		}
	}
	for (sharpen_index = 0;
	     sharpen_index < SM750_DITHER_SHARPEN_LUT_SIZE;
	     sharpen_index++) {
		int detail = (int)sharpen_index - 510;
		int adjustment = detail * 8;

		if (adjustment >= 0)
			adjustment = (adjustment + 50) / 100;
		else
			adjustment = (adjustment - 50) / 100;
		ctx->sharpen8_adjustment[sharpen_index] = adjustment;
	}

	return 0;
}

static __always_inline u16 sm750_dither_pixel(
			      const struct sm750_dither *ctx, u32 pixel,
			      unsigned int position)
{
	position &= 63U;
	unsigned int red =
		ctx->quantise5[position][(pixel >> 16) & 0xffU];
	unsigned int green =
		ctx->quantise6_green[position][(pixel >> 8) & 0xffU];
	unsigned int blue = ctx->quantise5[position][pixel & 0xffU];

	return (red << 11) | (green << 5) | blue;
}

static u16 sm750_rgb565_pixel(u32 pixel)
{
	return (((pixel >> 16) & 0xf8U) << 8) |
		(((pixel >> 8) & 0xfcU) << 3) |
		((pixel & 0xffU) >> 3);
}

void sm750_xrgb8888_to_rgb565(u16 *dst, const u32 *src,
			      unsigned int width)
{
	unsigned int x;

	for (x = 0; x < width; x++)
		dst[x] = sm750_rgb565_pixel(src[x]);
}

static __always_inline u64 sm750_pack_rgb_channels(u32 pixel)
{
	return (pixel & 0xffU) |
		((u64)(pixel & 0x0000ff00U) << 8) |
		((u64)(pixel & 0x00ff0000U) << 16);
}

static __always_inline u32 sm750_weighted_pixel_2(
				  u32 pixel0, unsigned int weight0,
				  u32 pixel1, unsigned int weight1,
				  unsigned int divisor,
				  unsigned int rounding)
{
	u64 channels = sm750_pack_rgb_channels(pixel0) * weight0 +
		sm750_pack_rgb_channels(pixel1) * weight1;
	unsigned int red = (channels >> 32) & 0xffffU;
	unsigned int green = (channels >> 16) & 0xffffU;
	unsigned int blue = channels & 0xffffU;

	return ((red + rounding) / divisor) << 16 |
		((green + rounding) / divisor) << 8 |
		((blue + rounding) / divisor);
}

static __always_inline u32 sm750_weighted_pixel_3(
				  u32 pixel0, unsigned int weight0,
				  u32 pixel1, unsigned int weight1,
				  u32 pixel2, unsigned int weight2,
				  unsigned int divisor,
				  unsigned int rounding)
{
	u64 channels = sm750_pack_rgb_channels(pixel0) * weight0 +
		sm750_pack_rgb_channels(pixel1) * weight1 +
		sm750_pack_rgb_channels(pixel2) * weight2;
	unsigned int red = (channels >> 32) & 0xffffU;
	unsigned int green = (channels >> 16) & 0xffffU;
	unsigned int blue = channels & 0xffffU;

	return ((red + rounding) / divisor) << 16 |
		((green + rounding) / divisor) << 8 |
		((blue + rounding) / divisor);
}

struct sm750_scale_5_state {
	unsigned int position;
};

static __always_inline void sm750_scale_5_state_init(
				     struct sm750_scale_5_state *state,
				     unsigned int x)
{
	state->position = x * 5U;
}

static __always_inline u32 sm750_scale_5_next(
			      const u32 *src,
			      struct sm750_scale_5_state *state)
{
	unsigned int position = state->position;
	unsigned int phase = position & 3U;
	unsigned int source_x = position >> 2;
	unsigned int first_weight = 4U - phase;
	unsigned int second_weight = 1U + phase;
	u32 pixel = sm750_weighted_pixel_2(src[source_x], first_weight,
			src[source_x + 1], second_weight, 5, 2);

	state->position = position + 5U;
	return pixel;
}

static __always_inline u32 sm750_scale_5_to_4_pixel(const u32 *src,
						    unsigned int x)
{
	struct sm750_scale_5_state state;

	sm750_scale_5_state_init(&state, x);
	return sm750_scale_5_next(src, &state);
}

/* 2464:2048 reduces exactly to 77:64. */
struct sm750_scale_77_state {
	unsigned int position;
};

static __always_inline void sm750_scale_77_state_init(
				      struct sm750_scale_77_state *state,
				      unsigned int x)
{
	state->position = x * 77U;
}

static __always_inline u32 sm750_scale_77_next(
			       const u32 *src,
			       struct sm750_scale_77_state *state)
{
	unsigned int position = state->position;
	unsigned int phase = position & 63U;
	unsigned int source_x = position >> 6;
	unsigned int weight0 = 64U - phase;
	unsigned int weight1 = min(13U + phase, 64U);
	unsigned int weight2 = phase > 51U ? phase - 51U : 0U;
	u32 pixel;

	pixel = sm750_weighted_pixel_3(src[source_x], weight0,
			src[source_x + 1], weight1,
			weight2 ? src[source_x + 2] : 0, weight2, 77, 38);
	state->position = position + 77U;
	return pixel;
}

static __always_inline u32 sm750_scale_77_to_64_pixel(const u32 *src,
						       unsigned int x)
{
	struct sm750_scale_77_state state;

	sm750_scale_77_state_init(&state, x);
	return sm750_scale_77_next(src, &state);
}

void sm750_dither_xrgb8888_to_rgb565(const struct sm750_dither *ctx,
				     u16 *dst, size_t dst_stride,
				     const u32 *src, size_t src_stride,
				     unsigned int x_origin,
				     unsigned int y_origin,
				     unsigned int width,
				     unsigned int height)
{
	unsigned int y;

	for (y = 0; y < height; y++) {
		unsigned int dither_row = ((y_origin + y) & 7U) << 3;
		unsigned int x;

		for (x = 0; x < width; x++) {
			u32 pixel = src[x];
			unsigned int position = dither_row |
				((x_origin + x) & 7U);

			dst[x] = sm750_dither_pixel(ctx, pixel, position);
		}
		src += src_stride;
		dst += dst_stride;
	}
}

void sm750_dither_scale_5_to_4_xrgb8888_to_rgb565(
				     const struct sm750_dither *ctx,
				     u16 *dst, const u32 *src,
				     unsigned int y_origin,
				     unsigned int dst_x1,
				     unsigned int dst_x2)
{
	unsigned int dither_row = (y_origin & 7U) << 3;
	struct sm750_scale_5_state state;
	unsigned int x;

	sm750_scale_5_state_init(&state, dst_x1);
	for (x = dst_x1; x < dst_x2; x++) {
		u32 pixel = sm750_scale_5_next(src, &state);

		dst[x] = sm750_dither_pixel(ctx, pixel,
					     dither_row | (x & 7U));
	}
}

void sm750_dither_scale_77_to_64_xrgb8888_to_rgb565(
				     const struct sm750_dither *ctx,
				     u16 *dst, const u32 *src,
				     unsigned int y_origin,
				     unsigned int dst_x1,
				     unsigned int dst_x2)
{
	unsigned int dither_row = (y_origin & 7U) << 3;
	struct sm750_scale_77_state state;
	unsigned int x;

	sm750_scale_77_state_init(&state, dst_x1);
	for (x = dst_x1; x < dst_x2; x++) {
		u32 pixel = sm750_scale_77_next(src, &state);

		dst[x] = sm750_dither_pixel(ctx, pixel,
					     dither_row | (x & 7U));
	}
}

int sm750_dither_scale_map_init(struct sm750_dither_scale_map *map,
				unsigned int src_width,
				unsigned int dst_width)
{
	unsigned int x;

	if (!map || !src_width || !dst_width ||
	    dst_width > SM750_DITHER_SCALE_MAX_SAMPLES)
		return -EINVAL;

	map->src_width = src_width;
	map->dst_width = dst_width;
	map->divisor = reciprocal_value(src_width);
	for (x = 0; x < dst_width; x++) {
		struct sm750_dither_scale_sample *sample = &map->sample[x];
		unsigned int start = x * src_width;
		unsigned int end = (x + 1) * src_width;
		unsigned int first = start / dst_width;
		unsigned int last = (end - 1) / dst_width;
		unsigned int source_x;

		sample->first = first;
		sample->taps = last - first + 1;
		if (sample->taps > SM750_DITHER_SCALE_MAX_TAPS)
			return -EINVAL;
		for (source_x = first; source_x <= last; source_x++) {
			unsigned int source_start = source_x * dst_width;
			unsigned int source_end = (source_x + 1) * dst_width;
			unsigned int left = max(start, source_start);
			unsigned int right = min(end, source_end);

			sample->weight[source_x - first] = right - left;
		}
	}

	return 0;
}

static u32 sm750_scale_map_pixel(const u32 *src,
				 const struct sm750_dither_scale_map *map,
				 unsigned int x)
{
	const struct sm750_dither_scale_sample *sample = &map->sample[x];
	unsigned int red_sum = 0;
	unsigned int green_sum = 0;
	unsigned int blue_sum = 0;
	unsigned int tap;

	for (tap = 0; tap < sample->taps; tap++) {
		unsigned int weight = sample->weight[tap];
		u32 pixel = src[sample->first + tap];

		red_sum += ((pixel >> 16) & 0xffU) * weight;
		green_sum += ((pixel >> 8) & 0xffU) * weight;
		blue_sum += (pixel & 0xffU) * weight;
	}

	return reciprocal_divide(red_sum + map->src_width / 2,
				 map->divisor) << 16 |
		reciprocal_divide(green_sum + map->src_width / 2,
				  map->divisor) << 8 |
		reciprocal_divide(blue_sum + map->src_width / 2,
				  map->divisor);
}

void sm750_scale_xrgb8888(u32 *dst, const u32 *src,
			 const struct sm750_dither_scale_map *map,
			 unsigned int src_width,
			 unsigned int dst_x1,
			 unsigned int dst_x2)
{
	unsigned int x;

	if (src_width == 2464) {
		struct sm750_scale_77_state state;

		sm750_scale_77_state_init(&state, dst_x1);
		for (x = dst_x1; x < dst_x2; x++)
			dst[x] = sm750_scale_77_next(src, &state);
		return;
	}
	if (src_width == 2560) {
		struct sm750_scale_5_state state;

		sm750_scale_5_state_init(&state, dst_x1);
		for (x = dst_x1; x < dst_x2; x++)
			dst[x] = sm750_scale_5_next(src, &state);
		return;
	}
	for (x = dst_x1; x < dst_x2; x++)
		dst[x] = sm750_scale_map_pixel(src, map, x);
}

void sm750_dither_scale_xrgb8888_to_rgb565(
				     const struct sm750_dither *ctx,
				     u16 *dst, const u32 *src,
				     unsigned int y_origin,
				     const struct sm750_dither_scale_map *map,
				     unsigned int dst_x1,
				     unsigned int dst_x2)
{
	unsigned int dither_row = (y_origin & 7U) << 3;
	unsigned int x;

	for (x = dst_x1; x < dst_x2; x++) {
		dst[x] = sm750_dither_pixel(ctx, sm750_scale_map_pixel(src, map, x),
					     dither_row | (x & 7U));
	}
}

static u8 sm750_sharpen_channel(unsigned int left, unsigned int center,
				unsigned int right, unsigned int percent)
{
	int detail = 2 * (int)center - (int)left - (int)right;
	int adjustment = detail * (int)percent;

	if (adjustment >= 0)
		adjustment = (adjustment + 50) / 100;
	else
		adjustment = (adjustment - 50) / 100;
	return clamp_t(int, (int)center + adjustment, 0, 255);
}

static __always_inline u8 sm750_sharpen_channel_8(
				  const struct sm750_dither *ctx,
				  u8 left, u8 center, u8 right)
{
	const s8 *adjustment = ctx->sharpen8_adjustment;
	int detail = 2 * (int)center - (int)left - (int)right;
	unsigned int index = (detail + 510) &
		(SM750_DITHER_SHARPEN_LUT_SIZE - 1);

	return clamp_t(int,
		(int)center + adjustment[index], 0, 255);
}

void sm750_dither_scale_5_to_4_sharpen_xrgb8888_to_rgb565(
				     const struct sm750_dither *ctx,
				     u16 *dst, const u32 *src,
				     unsigned int y_origin,
				     unsigned int dst_x1,
				     unsigned int dst_x2)
{
	unsigned int dither_row = (y_origin & 7U) << 3;
	struct sm750_scale_5_state state;
	u32 left;
	u32 center;
	u32 right;
	unsigned int x;

	if (dst_x1 >= dst_x2)
		return;
	sm750_scale_5_state_init(&state, dst_x1);
	center = sm750_scale_5_next(src, &state);
	left = dst_x1 ? sm750_scale_5_to_4_pixel(src, dst_x1 - 1) : center;
	right = dst_x1 + 1 < SM750_DITHER_SCALE_MAX_SAMPLES ?
		sm750_scale_5_next(src, &state) : center;

	for (x = dst_x1; x < dst_x2; x++) {
		u32 pixel =
			sm750_sharpen_channel_8(ctx, (left >> 16) & 0xffU,
				(center >> 16) & 0xffU, (right >> 16) & 0xffU) << 16 |
			sm750_sharpen_channel_8(ctx, (left >> 8) & 0xffU,
				(center >> 8) & 0xffU, (right >> 8) & 0xffU) << 8 |
			sm750_sharpen_channel_8(ctx, left & 0xffU,
				center & 0xffU, right & 0xffU);

		dst[x] = sm750_dither_pixel(ctx, pixel,
					     dither_row | (x & 7U));
		left = center;
		center = right;
		right = x + 2 < SM750_DITHER_SCALE_MAX_SAMPLES ?
			sm750_scale_5_next(src, &state) : center;
	}
}

void sm750_dither_scale_77_to_64_sharpen_xrgb8888_to_rgb565(
				     const struct sm750_dither *ctx,
				     u16 *dst, const u32 *src,
				     unsigned int y_origin,
				     unsigned int dst_x1,
				     unsigned int dst_x2)
{
	unsigned int dither_row = (y_origin & 7U) << 3;
	struct sm750_scale_77_state state;
	u32 left;
	u32 center;
	u32 right;
	unsigned int x;

	if (dst_x1 >= dst_x2)
		return;
	sm750_scale_77_state_init(&state, dst_x1);
	center = sm750_scale_77_next(src, &state);
	left = dst_x1 ? sm750_scale_77_to_64_pixel(src, dst_x1 - 1) : center;
	right = dst_x1 + 1 < SM750_DITHER_SCALE_MAX_SAMPLES ?
		sm750_scale_77_next(src, &state) : center;

	for (x = dst_x1; x < dst_x2; x++) {
		u32 pixel =
			sm750_sharpen_channel_8(ctx, (left >> 16) & 0xffU,
				(center >> 16) & 0xffU, (right >> 16) & 0xffU) << 16 |
			sm750_sharpen_channel_8(ctx, (left >> 8) & 0xffU,
				(center >> 8) & 0xffU, (right >> 8) & 0xffU) << 8 |
			sm750_sharpen_channel_8(ctx, left & 0xffU,
				center & 0xffU, right & 0xffU);

		dst[x] = sm750_dither_pixel(ctx, pixel,
					     dither_row | (x & 7U));
		left = center;
		center = right;
		right = x + 2 < SM750_DITHER_SCALE_MAX_SAMPLES ?
			sm750_scale_77_next(src, &state) : center;
	}
}

void sm750_sharpen_xrgb8888(u32 *dst, const u32 *src,
			   unsigned int dst_x1,
			   unsigned int dst_x2,
			   unsigned int sharpen_percent)
{
	unsigned int x;

	for (x = dst_x1; x < dst_x2; x++) {
		u32 left = src[x ? x - 1 : x];
		u32 center = src[x];
		u32 right = src[x + 1 < SM750_DITHER_SCALE_MAX_SAMPLES ?
				x + 1 : x];

		dst[x] =
			sm750_sharpen_channel((left >> 16) & 0xffU,
				(center >> 16) & 0xffU, (right >> 16) & 0xffU,
				sharpen_percent) << 16 |
			sm750_sharpen_channel((left >> 8) & 0xffU,
				(center >> 8) & 0xffU, (right >> 8) & 0xffU,
				sharpen_percent) << 8 |
			sm750_sharpen_channel(left & 0xffU, center & 0xffU,
				right & 0xffU, sharpen_percent);
	}
}

void sm750_dither_scale_sharpen_xrgb8888_to_rgb565(
				     const struct sm750_dither *ctx,
				     u16 *dst, u32 *scratch,
				     const u32 *src,
				     unsigned int y_origin,
				     const struct sm750_dither_scale_map *map,
				     unsigned int src_width,
				     unsigned int dst_x1,
				     unsigned int dst_x2)
{
	unsigned int dither_row = (y_origin & 7U) << 3;
	unsigned int calc_x1 = dst_x1 ? dst_x1 - 1 : 0;
	unsigned int calc_x2 = min(dst_x2 + 1,
				   (unsigned int)SM750_DITHER_SCALE_MAX_SAMPLES);
	unsigned int x;

	sm750_scale_xrgb8888(scratch, src, map, src_width, calc_x1, calc_x2);

	for (x = dst_x1; x < dst_x2; x++) {
		u32 left = scratch[x ? x - 1 : x];
		u32 center = scratch[x];
		u32 right = scratch[x + 1 < SM750_DITHER_SCALE_MAX_SAMPLES ?
				    x + 1 : x];
		u32 pixel =
			sm750_sharpen_channel_8(ctx, (left >> 16) & 0xffU,
				(center >> 16) & 0xffU, (right >> 16) & 0xffU) << 16 |
			sm750_sharpen_channel_8(ctx, (left >> 8) & 0xffU,
				(center >> 8) & 0xffU, (right >> 8) & 0xffU) << 8 |
			sm750_sharpen_channel_8(ctx, left & 0xffU,
				center & 0xffU, right & 0xffU);

		dst[x] = sm750_dither_pixel(ctx, pixel,
					     dither_row | (x & 7U));
	}
}
