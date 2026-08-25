// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <xf86drm.h>

#define SAMPLE_COUNT 120

static double seconds_between(const struct timespec *start,
			      const struct timespec *end)
{
	return end->tv_sec - start->tv_sec +
	       (end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

int main(int argc, char **argv)
{
	const char *device;
	drmVBlank vblank;
	struct timespec start;
	struct timespec end;
	double expected;
	double elapsed;
	double measured;
	int fd;
	int i;

	if (argc != 3) {
		fprintf(stderr, "usage: %s DRM_CARD EXPECTED_HZ\n", argv[0]);
		return 2;
	}
	device = argv[1];
	expected = strtod(argv[2], NULL);
	if (expected < 20.0 || expected > 250.0) {
		fprintf(stderr, "invalid expected refresh: %s\n", argv[2]);
		return 2;
	}

	fd = open(device, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", device, strerror(errno));
		return 1;
	}

	if (clock_gettime(CLOCK_MONOTONIC, &start)) {
		perror("clock_gettime");
		close(fd);
		return 1;
	}
	for (i = 0; i < SAMPLE_COUNT; i++) {
		memset(&vblank, 0, sizeof(vblank));
		vblank.request.type = DRM_VBLANK_RELATIVE;
		vblank.request.sequence = 1;
		if (drmWaitVBlank(fd, &vblank)) {
			fprintf(stderr, "DRM_IOCTL_WAIT_VBLANK: %s\n",
				strerror(errno));
			close(fd);
			return 1;
		}
	}
	if (clock_gettime(CLOCK_MONOTONIC, &end)) {
		perror("clock_gettime");
		close(fd);
		return 1;
	}
	close(fd);

	elapsed = seconds_between(&start, &end);
	measured = SAMPLE_COUNT / elapsed;
	printf("Measured %.2f DRM vblanks/s over %d events (expected %.2f)\n",
	       measured, SAMPLE_COUNT, expected);
	if (fabs(measured - expected) > expected * 0.05) {
		fprintf(stderr, "vblank rate is outside the 5%% tolerance\n");
		return 1;
	}
	return 0;
}
