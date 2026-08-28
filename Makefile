# SPDX-License-Identifier: GPL-2.0-only
KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all clean check check-all-kernels module-info package

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/src modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/src clean
	$(RM) tests/check-vblank tests/test-bbdither-rgb565 \
		tests/test-scale-optimizations

check: all tests/check-vblank tests/test-bbdither-rgb565 \
		tests/test-scale-optimizations
	./tests/check-drm-module.sh src/sm750hdmidrm.ko
	./tests/check-drm-policy.sh
	./tests/check-drm-xorg-config.sh
	./tests/check-drm-packaging.sh
	./tests/check-softscale-map.sh
	./tests/check-softscale-sharpen.sh
	./tests/check-hardware-cursor.sh
	./tests/check-shadow-dma.sh
	./tests/check-shadow-source-damage.sh
	./tests/test-bbdither-rgb565
	./tests/test-scale-optimizations

check-all-kernels:
	./tests/build-all-kernels.sh

module-info: all
	/sbin/modinfo src/sm750hdmidrm.ko

tests/check-vblank: tests/check-vblank.c
	$(CC) -O2 -Wall -Wextra -Werror $$(pkg-config --cflags libdrm) \
		-o $@ $< $$(pkg-config --libs libdrm) -lm

tests/test-bbdither-rgb565: tests/test-bbdither-rgb565.c tools/bbdither-rgb565.c tools/bbdither-rgb565.h
	$(CC) -O3 -Wall -Wextra -Werror -Itools -o $@ tests/test-bbdither-rgb565.c tools/bbdither-rgb565.c

tests/test-scale-optimizations: tests/test-scale-optimizations.c
	$(CC) -O3 -Wall -Wextra -Werror -o $@ $<

package:
	./build-package.sh
