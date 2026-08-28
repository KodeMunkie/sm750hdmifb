/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SM750_DITHER_H
#define SM750_DITHER_H

#include <linux/reciprocal_div.h>
#include <linux/types.h>

#define SM750_DITHER_SCALE_MAX_SAMPLES 2048
#define SM750_DITHER_SCALE_MAX_TAPS 3
#define SM750_DITHER_SHARPEN_LUT_SIZE 1024

struct sm750_dither {
	u8 quantise5[64][256];
	u8 quantise6_green[64][256];
	s8 sharpen8_adjustment[SM750_DITHER_SHARPEN_LUT_SIZE];
};

struct sm750_dither_scale_sample {
	u16 first;
	u16 weight[SM750_DITHER_SCALE_MAX_TAPS];
	u8 taps;
};

struct sm750_dither_scale_map {
	u32 src_width;
	u32 dst_width;
	struct reciprocal_value divisor;
	struct sm750_dither_scale_sample
		sample[SM750_DITHER_SCALE_MAX_SAMPLES];
};

int sm750_dither_init(struct sm750_dither *ctx,
		      unsigned int green_gain_percent);
void sm750_xrgb8888_to_rgb565(u16 *dst, const u32 *src,
			      unsigned int width);
void sm750_dither_xrgb8888_to_rgb565(const struct sm750_dither *ctx,
				     u16 *dst, size_t dst_stride,
				     const u32 *src, size_t src_stride,
				     unsigned int x_origin,
				     unsigned int y_origin,
				     unsigned int width,
				     unsigned int height);
void sm750_dither_scale_5_to_4_xrgb8888_to_rgb565(
				     const struct sm750_dither *ctx,
				     u16 *dst, const u32 *src,
				     unsigned int y_origin,
				     unsigned int dst_x1,
				     unsigned int dst_x2);
void sm750_dither_scale_5_to_4_sharpen_xrgb8888_to_rgb565(
				     const struct sm750_dither *ctx,
				     u16 *dst, const u32 *src,
				     unsigned int y_origin,
				     unsigned int dst_x1,
				     unsigned int dst_x2);
void sm750_dither_scale_77_to_64_xrgb8888_to_rgb565(
				     const struct sm750_dither *ctx,
				     u16 *dst, const u32 *src,
				     unsigned int y_origin,
				     unsigned int dst_x1,
				     unsigned int dst_x2);
int sm750_dither_scale_map_init(struct sm750_dither_scale_map *map,
				unsigned int src_width,
				unsigned int dst_width);
void sm750_scale_xrgb8888(u32 *dst, const u32 *src,
			 const struct sm750_dither_scale_map *map,
			 unsigned int src_width,
			 unsigned int dst_x1,
			 unsigned int dst_x2);
void sm750_sharpen_xrgb8888(u32 *dst, const u32 *src,
			   unsigned int dst_x1,
			   unsigned int dst_x2,
			   unsigned int sharpen_percent);
void sm750_dither_scale_xrgb8888_to_rgb565(
				     const struct sm750_dither *ctx,
				     u16 *dst, const u32 *src,
				     unsigned int y_origin,
				     const struct sm750_dither_scale_map *map,
				     unsigned int dst_x1,
				     unsigned int dst_x2);
void sm750_dither_scale_sharpen_xrgb8888_to_rgb565(
					     const struct sm750_dither *ctx,
					     u16 *dst, u32 *scratch,
				     const u32 *src,
				     unsigned int y_origin,
				     const struct sm750_dither_scale_map *map,
				     unsigned int src_width,
				     unsigned int dst_x1,
				     unsigned int dst_x2);
void sm750_dither_scale_77_to_64_sharpen_xrgb8888_to_rgb565(
				     const struct sm750_dither *ctx,
				     u16 *dst, const u32 *src,
				     unsigned int y_origin,
				     unsigned int dst_x1,
				     unsigned int dst_x2);

#endif
