// SPDX-License-Identifier: GPL-2.0-only
#include <stdint.h>
#include <stdio.h>

#define WIDTH_77 2464
#define WIDTH_5 2560
#define OUTPUT_WIDTH 2048

static uint64_t pack_rgb_channels(uint32_t pixel)
{
	return (pixel & 0xffU) |
		((uint64_t)(pixel & 0x0000ff00U) << 8) |
		((uint64_t)(pixel & 0x00ff0000U) << 16);
}

struct scale_77_state {
	unsigned int position;
};

struct scale_5_state {
	unsigned int position;
};

static uint32_t packed_weighted_2(uint32_t p0, unsigned int w0,
				  uint32_t p1, unsigned int w1,
				  unsigned int divisor,
				  unsigned int rounding)
{
	uint64_t channels = pack_rgb_channels(p0) * w0 +
		pack_rgb_channels(p1) * w1;

	return ((((channels >> 32) & 0xffffU) + rounding) / divisor) << 16 |
		((((channels >> 16) & 0xffffU) + rounding) / divisor) << 8 |
		(((channels & 0xffffU) + rounding) / divisor);
}

static uint32_t packed_weighted_3(uint32_t p0, unsigned int w0,
				  uint32_t p1, unsigned int w1,
				  uint32_t p2, unsigned int w2,
				  unsigned int divisor,
				  unsigned int rounding)
{
	uint64_t channels = pack_rgb_channels(p0) * w0 +
		pack_rgb_channels(p1) * w1 + pack_rgb_channels(p2) * w2;

	return ((((channels >> 32) & 0xffffU) + rounding) / divisor) << 16 |
		((((channels >> 16) & 0xffffU) + rounding) / divisor) << 8 |
		(((channels & 0xffffU) + rounding) / divisor);
}

static uint32_t reference_77(const uint32_t *src, unsigned int x)
{
	unsigned int phase = (x * 13U) & 63U;
	unsigned int sx = (x * 77U) >> 6;
	unsigned int w0 = 64U - phase;
	unsigned int w1 = 13U + phase < 64U ? 13U + phase : 64U;
	unsigned int w2 = phase > 51U ? phase - 51U : 0U;
	unsigned int red = (((src[sx] >> 16) & 0xffU) * w0 +
		((src[sx + 1] >> 16) & 0xffU) * w1 +
		((w2 ? src[sx + 2] : 0) >> 16 & 0xffU) * w2 + 38) / 77;
	unsigned int green = (((src[sx] >> 8) & 0xffU) * w0 +
		((src[sx + 1] >> 8) & 0xffU) * w1 +
		((w2 ? src[sx + 2] : 0) >> 8 & 0xffU) * w2 + 38) / 77;
	unsigned int blue = ((src[sx] & 0xffU) * w0 +
		(src[sx + 1] & 0xffU) * w1 +
		(w2 ? src[sx + 2] & 0xffU : 0) * w2 + 38) / 77;

	return red << 16 | green << 8 | blue;
}

static uint32_t optimized_77(const uint32_t *src,
			     struct scale_77_state *state)
{
	unsigned int position = state->position;
	unsigned int phase = position & 63U;
	unsigned int sx = position >> 6;
	unsigned int w0 = 64U - phase;
	unsigned int w1 = 13U + phase < 64U ? 13U + phase : 64U;
	unsigned int w2 = phase > 51U ? phase - 51U : 0U;
	uint32_t pixel = packed_weighted_3(src[sx], w0, src[sx + 1], w1,
			w2 ? src[sx + 2] : 0, w2, 77, 38);

	state->position = position + 77U;
	return pixel;
}

static uint32_t reference_5(const uint32_t *src, unsigned int x)
{
	unsigned int phase = x & 3U;
	unsigned int sx = (x >> 2) * 5U + phase;
	unsigned int w0 = 4U - phase;
	unsigned int w1 = 1U + phase;
	unsigned int red = (((src[sx] >> 16) & 0xffU) * w0 +
		((src[sx + 1] >> 16) & 0xffU) * w1 + 2) / 5;
	unsigned int green = (((src[sx] >> 8) & 0xffU) * w0 +
		((src[sx + 1] >> 8) & 0xffU) * w1 + 2) / 5;
	unsigned int blue = ((src[sx] & 0xffU) * w0 +
		(src[sx + 1] & 0xffU) * w1 + 2) / 5;

	return red << 16 | green << 8 | blue;
}

static uint32_t optimized_5(const uint32_t *src, struct scale_5_state *state)
{
	unsigned int position = state->position;
	unsigned int phase = position & 3U;
	unsigned int sx = position >> 2;
	uint32_t pixel = packed_weighted_2(src[sx], 4U - phase,
			src[sx + 1], 1U + phase, 5, 2);

	state->position = position + 5U;
	return pixel;
}

int main(void)
{
	uint32_t source_77[WIDTH_77];
	uint32_t source_5[WIDTH_5];
	int sharpen_table[1024];
	struct scale_77_state state = { 0 };
	struct scale_5_state state_5 = { 0 };
	uint32_t seed = 0x750750U;
	unsigned int x;
	int detail;

	for (x = 0; x < WIDTH_5; x++) {
		seed = seed * 1664525U + 1013904223U;
		source_5[x] = seed & 0x00ffffffU;
		if (x < WIDTH_77)
			source_77[x] = source_5[x];
	}
	for (x = 0; x < OUTPUT_WIDTH; x++) {
		uint32_t optimized = optimized_77(source_77, &state);
		uint32_t pixel_5 = optimized_5(source_5, &state_5);

		if (optimized != reference_77(source_77, x) ||
		    pixel_5 != reference_5(source_5, x))
			return 1;
		if (state.position != (x + 1) * 77U)
			return 1;
		if (state_5.position != (x + 1) * 5U)
			return 1;
	}
	for (detail = -510; detail <= 513; detail++) {
		int adjustment = detail * 8;
		int magnitude = detail < 0 ? -detail * 8 : detail * 8;
		int reference = magnitude / 100 +
			(magnitude % 100 >= 50 ? 1 : 0);

		sharpen_table[detail + 510] = adjustment >= 0 ?
			(adjustment + 50) / 100 : (adjustment - 50) / 100;
		if (detail < 0)
			reference = -reference;
		if (sharpen_table[detail + 510] != reference)
			return 1;
	}
	puts("Packed scaler and fixed-sharpen equivalence checks passed");
	return 0;
}
