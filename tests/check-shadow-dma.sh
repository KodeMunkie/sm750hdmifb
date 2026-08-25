#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_file=$project_dir/src/sm750_drm.c

for requirement in \
	'module_param(disable_dma, bool, 0444);' \
	'dma_set_mask_and_coherent(&sdev->pdev->dev, DMA_BIT_MASK(31))' \
	'dmam_alloc_coherent(&sdev->pdev->dev,' \
	'readl_poll_timeout_atomic(sdev->regs + DMA_ABORT_INTERRUPT,' \
	'control, control & DMA_ABORT_INTERRUPT_INT_1, 1,' \
	'control & ~DMA_ABORT_INTERRUPT_INT_1);' \
	'((size - sizeof(u32)) & DMA_1_SIZE_CONTROL_SIZE_MASK)' \
	'#define SM750_DRM_DMA_GUARD_WORDS 4' \
	'0x51a70000U + i' \
	'0xa75e0000U + i' \
	'DMA_ABORT_INTERRUPT_ABORT_1' \
	'DMA1 shadow uploads enabled after off-screen transfer verification' \
	'sdev->shadow_dma_enabled = false;' \
	'if (!disable_dma) {' \
	'sm750_shadow_upload(sdev,' \
	'dst_x1 &= ~1U;' \
	'dst_x2 = min(ALIGN(dst_x2, 2), 2048U);'; do
	grep -F "$requirement" "$source_file" >/dev/null || {
		echo "Missing shadow DMA requirement: $requirement" >&2
		exit 1
	}
done

awk 'BEGIN {
	for (x1 = 0; x1 < 2048; x1++) {
		for (width = 1; width <= 16 && x1 + width <= 2048; width++) {
			x2 = x1 + width
			aligned_x1 = x1 - (x1 % 2)
			aligned_x2 = x2 + (x2 % 2)
			if (aligned_x2 > 2048)
				aligned_x2 = 2048
			destination = aligned_x1 * 2
			size = (aligned_x2 - aligned_x1) * 2
			if (destination % 4 || size % 4 ||
			    aligned_x1 > x1 || aligned_x2 < x2)
				exit 1
		}
	}
}' /dev/null

echo "Shadow DMA safety checks passed"
