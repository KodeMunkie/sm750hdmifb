// SPDX-License-Identifier: GPL-2.0-only
#include <stdint.h>
#include <stdio.h>

#define WIDTH_77 2464
#define WIDTH_5 2560
#define OUTPUT_WIDTH 2048

struct scale_77_state {
	unsigned int source_x;
	unsigned int phase;
};

static uint32_t packed_weighted_2(uint32_t p0, unsigned int w0,
				  uint32_t p1, unsigned int w1,
				  unsigned int divisor,
				  unsigned int rounding)
{
	uint64_t rb = (uint64_t)(p0 & 0x00ff00ffU) * w0 +
		(uint64_t)(p1 & 0x00ff00ffU) * w1;
	unsigned int green = ((p0 >> 8) & 0xffU) * w0 +
		((p1 >> 8) & 0xffU) * w1;

	return ((((rb >> 16) & 0xffffU) + rounding) / divisor) << 16 |
		((green + rounding) / divisor) << 8 |
		(((rb & 0xffffU) + rounding) / divisor);
}

static uint32_t packed_weighted_3(uint32_t p0, unsigned int w0,
				  uint32_t p1, unsigned int w1,
				  uint32_t p2, unsigned int w2,
				  unsigned int divisor,
				  unsigned int rounding)
{
	uint64_t rb = (uint64_t)(p0 & 0x00ff00ffU) * w0 +
		(uint64_t)(p1 & 0x00ff00ffU) * w1 +
		(uint64_t)(p2 & 0x00ff00ffU) * w2;
	unsigned int green = ((p0 >> 8) & 0xffU) * w0 +
		((p1 >> 8) & 0xffU) * w1 + ((p2 >> 8) & 0xffU) * w2;

	return ((((rb >> 16) & 0xffffU) + rounding) / divisor) << 16 |
		((green + rounding) / divisor) << 8 |
		(((rb & 0xffffU) + rounding) / divisor);
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
	unsigned int phase = state->phase;
	unsigned int sx = state->source_x;
	unsigned int w0 = 64U - phase;
	unsigned int w1 = 13U + phase < 64U ? 13U + phase : 64U;
	unsigned int w2 = phase > 51U ? phase - 51U : 0U;
	uint32_t pixel = packed_weighted_3(src[sx], w0, src[sx + 1], w1,
			w2 ? src[sx + 2] : 0, w2, 77, 38);

	state->source_x = sx + 1;
	state->phase = phase + 13;
	if (state->phase >= 64) {
		state->phase -= 64;
		state->source_x++;
	}
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

int main(void)
{
	uint32_t source_77[WIDTH_77];
	uint32_t source_5[WIDTH_5];
	int sharpen_table[1021];
	struct scale_77_state state = { 0, 0 };
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
		unsigned int phase = x & 3U;
		unsigned int sx = (x >> 2) * 5U + phase;
		uint32_t optimized_5 = packed_weighted_2(source_5[sx],
			4U - phase, source_5[sx + 1], 1U + phase, 5, 2);

		if (optimized != reference_77(source_77, x) ||
		    optimized_5 != reference_5(source_5, x))
			return 1;
		if (state.source_x != ((x + 1) * 77U >> 6) ||
		    state.phase != ((x + 1) * 13U & 63U))
			return 1;
	}
	for (detail = -510; detail <= 510; detail++) {
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
