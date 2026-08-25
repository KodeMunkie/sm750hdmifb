// SPDX-License-Identifier: GPL-2.0-only
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbdither-rgb565.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
			__FILE__, __LINE__, #condition); \
		return EXIT_FAILURE; \
	} \
} while (0)

static unsigned int expand5(unsigned int value)
{
	return (value * 255 + 15) / 31;
}

static unsigned int expand6(unsigned int value)
{
	return (value * 255 + 31) / 63;
}

static int test_matrix(void)
{
	uint64_t seen = 0;
	unsigned int x, y;

	for (y = 0; y < 8; y++)
		for (x = 0; x < 8; x++) {
			unsigned int rank = sm750_bbdither_threshold(x, y);

			CHECK(rank < 64);
			CHECK(!(seen & (UINT64_C(1) << rank)));
			seen |= UINT64_C(1) << rank;
		}
	CHECK(seen == UINT64_MAX);
	return EXIT_SUCCESS;
}

static int test_lookup_tables(void)
{
	struct sm750_bbdither_rgb565 ctx;
	unsigned int threshold;

	CHECK(sm750_bbdither_rgb565_init(NULL, 100));
	CHECK(sm750_bbdither_rgb565_init(&ctx, 101));
	CHECK(!sm750_bbdither_rgb565_init(&ctx, 100));
	for (threshold = 0; threshold < 64; threshold++) {
		unsigned int value;

		for (value = 0; value < 256; value++) {
			unsigned int scaled5 = value * 31;
			unsigned int scaled6 = value * 63;
			unsigned int q5 = scaled5 / 255;
			unsigned int q6 = scaled6 / 255;
			unsigned int midpoint = (2 * threshold + 1) * 255;

			if ((scaled5 % 255) * 128 > midpoint && q5 < 31)
				q5++;
			if ((scaled6 % 255) * 128 > midpoint && q6 < 63)
				q6++;
			CHECK(ctx.quantise5[threshold][value] == q5);
			CHECK(ctx.quantise6_green[threshold][value] == q6);
		}
	}
	return EXIT_SUCCESS;
}

static int test_endpoints_and_neutrals(void)
{
	struct sm750_bbdither_rgb565 ctx;
	uint32_t source[64];
	uint16_t output[64];
	unsigned int value;

	CHECK(!sm750_bbdither_rgb565_init(&ctx, 100));
	memset(source, 0, sizeof(source));
	sm750_bbdither_xrgb8888_to_rgb565(&ctx, output, 8, source, 8,
					0, 0, 8, 8);
	for (value = 0; value < 64; value++)
		CHECK(output[value] == 0);

	for (value = 0; value < 64; value++)
		source[value] = UINT32_C(0x00ffffff);
	sm750_bbdither_xrgb8888_to_rgb565(&ctx, output, 8, source, 8,
					0, 0, 8, 8);
	for (value = 0; value < 64; value++)
		CHECK(output[value] == UINT16_C(0xffff));

	for (value = 1; value < 255; value++) {
		unsigned int i, red = 0, green = 0, blue = 0;

		for (i = 0; i < 64; i++)
			source[i] = value * UINT32_C(0x00010101);
		sm750_bbdither_xrgb8888_to_rgb565(&ctx, output, 8, source, 8,
						0, 0, 8, 8);
		for (i = 0; i < 64; i++) {
			red += expand5((output[i] >> 11) & 31U);
			green += expand6((output[i] >> 5) & 63U);
			blue += expand5(output[i] & 31U);
		}
		CHECK(red == blue);
		CHECK(abs((int)red - (int)green) <= 64);
	}
	return EXIT_SUCCESS;
}

static int test_green_gain_direction(void)
{
	struct sm750_bbdither_rgb565 full, reduced;
	unsigned int threshold, value, lower = 0;

	CHECK(!sm750_bbdither_rgb565_init(&full, 100));
	CHECK(!sm750_bbdither_rgb565_init(&reduced, 94));
	for (threshold = 0; threshold < 64; threshold++)
		for (value = 0; value < 256; value++) {
			CHECK(reduced.quantise6_green[threshold][value] <=
			      full.quantise6_green[threshold][value]);
			if (reduced.quantise6_green[threshold][value] <
			    full.quantise6_green[threshold][value])
				lower++;
		}
	CHECK(lower > 0);
	return EXIT_SUCCESS;
}

static int test_damage_phase(void)
{
	enum { WIDTH = 9, HEIGHT = 7 };
	struct sm750_bbdither_rgb565 ctx;
	uint32_t source[WIDTH * HEIGHT];
	uint16_t full[WIDTH * HEIGHT], crop[5 * 4];
	unsigned int x, y;

	CHECK(!sm750_bbdither_rgb565_init(&ctx, 95));
	for (y = 0; y < HEIGHT; y++)
		for (x = 0; x < WIDTH; x++)
			source[y * WIDTH + x] =
				(((x * 29U + y * 7U) & 255U) << 16) |
				(((x * 11U + y * 37U) & 255U) << 8) |
				((x * 43U + y * 5U) & 255U);
	sm750_bbdither_xrgb8888_to_rgb565(&ctx, full, WIDTH, source, WIDTH,
					0, 0, WIDTH, HEIGHT);
	sm750_bbdither_xrgb8888_to_rgb565(&ctx, crop, 5, source + WIDTH + 2,
					WIDTH, 2, 1, 5, 4);
	for (y = 0; y < 4; y++)
		for (x = 0; x < 5; x++)
			CHECK(crop[y * 5 + x] ==
			      full[(y + 1) * WIDTH + x + 2]);
	return EXIT_SUCCESS;
}

int main(void)
{
	if (test_matrix() || test_lookup_tables() ||
	    test_endpoints_and_neutrals() || test_green_gain_direction() ||
	    test_damage_phase())
		return EXIT_FAILURE;
	puts("bbdither RGB565 checks passed");
	return EXIT_SUCCESS;
}
