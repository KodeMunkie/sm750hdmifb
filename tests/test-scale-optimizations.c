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

static unsigned int sharpen_channel_8(unsigned int left,
				      unsigned int center,
				      unsigned int right)
{
	int detail = 2 * (int)center - (int)left - (int)right;
	int adjustment = detail * 8;
	int value;

	adjustment = adjustment >= 0 ?
		(adjustment + 50) / 100 : (adjustment - 50) / 100;
	value = (int)center + adjustment;
	return value < 0 ? 0U : value > 255 ? 255U : (unsigned int)value;
}

static uint32_t sharpen_pixel_8(uint32_t left, uint32_t center,
				uint32_t right)
{
	return sharpen_channel_8(left >> 16 & 0xffU, center >> 16 & 0xffU,
			right >> 16 & 0xffU) << 16 |
		sharpen_channel_8(left >> 8 & 0xffU, center >> 8 & 0xffU,
			right >> 8 & 0xffU) << 8 |
		sharpen_channel_8(left & 0xffU, center & 0xffU,
			right & 0xffU);
}

static int check_unrolled_77_span(const uint32_t *src,
				  unsigned int start, unsigned int end)
{
	uint32_t actual[OUTPUT_WIDTH];
	struct scale_77_state state = { start * 77U };
	uint32_t center = optimized_77(src, &state);
	uint32_t left = start ? reference_77(src, start - 1) : center;
	uint32_t right = start + 1 < OUTPUT_WIDTH ?
		optimized_77(src, &state) : center;
	unsigned int x = start;
	unsigned int i;

#define EMIT_MODEL_77(phase) do { \
	actual[x] = sharpen_pixel_8(left, center, right) | \
		(((phase) & 7U) << 24); \
	left = center; \
	center = right; \
	x++; \
	right = x + 1 < OUTPUT_WIDTH ? optimized_77(src, &state) : center; \
} while (0)
	while (x < end && (x & 7U))
		EMIT_MODEL_77(x);
	while (x + 8 <= end) {
		EMIT_MODEL_77(0U);
		EMIT_MODEL_77(1U);
		EMIT_MODEL_77(2U);
		EMIT_MODEL_77(3U);
		EMIT_MODEL_77(4U);
		EMIT_MODEL_77(5U);
		EMIT_MODEL_77(6U);
		EMIT_MODEL_77(7U);
	}
	while (x < end)
		EMIT_MODEL_77(x);
#undef EMIT_MODEL_77

	for (i = start; i < end; i++) {
		uint32_t expected_center = reference_77(src, i);
		uint32_t expected_left = i ? reference_77(src, i - 1) :
			expected_center;
		uint32_t expected_right = i + 1 < OUTPUT_WIDTH ?
			reference_77(src, i + 1) : expected_center;
		uint32_t expected = sharpen_pixel_8(expected_left,
			expected_center, expected_right) | ((i & 7U) << 24);

		if (actual[i] != expected)
			return 1;
	}
	return 0;
}

static int check_unrolled_5_span(const uint32_t *src,
				 unsigned int start, unsigned int end)
{
	uint32_t actual[OUTPUT_WIDTH];
	struct scale_5_state state = { start * 5U };
	uint32_t center = optimized_5(src, &state);
	uint32_t left = start ? reference_5(src, start - 1) : center;
	uint32_t right = start + 1 < OUTPUT_WIDTH ?
		optimized_5(src, &state) : center;
	unsigned int x = start;
	unsigned int i;

#define EMIT_MODEL_5(phase) do { \
	actual[x] = sharpen_pixel_8(left, center, right) | \
		(((phase) & 7U) << 24); \
	left = center; \
	center = right; \
	x++; \
	right = x + 1 < OUTPUT_WIDTH ? optimized_5(src, &state) : center; \
} while (0)
	while (x < end && (x & 7U))
		EMIT_MODEL_5(x);
	while (x + 8 <= end) {
		EMIT_MODEL_5(0U);
		EMIT_MODEL_5(1U);
		EMIT_MODEL_5(2U);
		EMIT_MODEL_5(3U);
		EMIT_MODEL_5(4U);
		EMIT_MODEL_5(5U);
		EMIT_MODEL_5(6U);
		EMIT_MODEL_5(7U);
	}
	while (x < end)
		EMIT_MODEL_5(x);
#undef EMIT_MODEL_5

	for (i = start; i < end; i++) {
		uint32_t expected_center = reference_5(src, i);
		uint32_t expected_left = i ? reference_5(src, i - 1) :
			expected_center;
		uint32_t expected_right = i + 1 < OUTPUT_WIDTH ?
			reference_5(src, i + 1) : expected_center;
		uint32_t expected = sharpen_pixel_8(expected_left,
			expected_center, expected_right) | ((i & 7U) << 24);

		if (actual[i] != expected)
			return 1;
	}
	return 0;
}

int main(void)
{
	static const unsigned int spans[][2] = {
		{ 0, OUTPUT_WIDTH }, { 3, 7 }, { 5, 2041 },
		{ 1023, 1034 }, { 2040, OUTPUT_WIDTH },
	};
	uint32_t source_77[WIDTH_77];
	uint32_t source_5[WIDTH_5];
	int sharpen_table[1024];
	struct scale_77_state state = { 0 };
	struct scale_5_state state_5 = { 0 };
	uint32_t seed = 0x750750U;
	unsigned int x;
	unsigned int span;
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
	for (span = 0; span < sizeof(spans) / sizeof(spans[0]); span++)
		if (check_unrolled_77_span(source_77, spans[span][0],
					     spans[span][1]) ||
		    check_unrolled_5_span(source_5, spans[span][0],
					    spans[span][1]))
			return 1;
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
	puts("Packed scaler, unrolled span and fixed-sharpen checks passed");
	return 0;
}
