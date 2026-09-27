#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_file=$project_dir/src/sm750_drm.c

for requirement in \
	'module_param(enable_dma, bool, 0444);' \
	'module_param(disable_dma, bool, 0444);' \
	'#define SM750_DRM_DEFAULT_ENABLE_DMA 1' \
	'static unsigned int shadow_dma_min_bytes = 4096;' \
	'dma_set_mask_and_coherent(&sdev->pdev->dev, DMA_BIT_MASK(31))' \
	'dmam_alloc_coherent(' \
	'#define SM750_DRM_DMA_BATCH_ROW_SIZE (2048 * sizeof(u16))' \
	'#define SM750_DRM_DMA_BATCH_ROWS 128' \
	'#define SM750_DRM_DMA_STAGING_BUFFERS 2' \
	'(SM750_DRM_DMA_BATCH_ROWS * SM750_DRM_DMA_BATCH_ROW_SIZE)' \
	'void *dma_staging[SM750_DRM_DMA_STAGING_BUFFERS];' \
	'size_t dma_pending_size;' \
	'size_t dma_active_size;' \
	'bool dma_active;' \
	'module_param(dma_batch_rows, uint, 0644);' \
	'READ_ONCE(dma_batch_rows), 1, SM750_DRM_DMA_BATCH_ROWS);' \
	'size_t dma_batch_limit;' \
	'destination == sdev->dma_pending_destination +' \
	'sdev->dma_pending_size + size <= sdev->dma_batch_limit' \
	'if (size == SM750_DRM_DMA_BATCH_ROW_SIZE)' \
	'sm750_dma_submit_pending(sdev);' \
	'sm750_dma_complete_active(sdev);' \
	'sdev->dma_fill_index ^= 1;' \
	'readl_poll_timeout_atomic(' \
	'control & DMA_ABORT_INTERRUPT_INT_1, 1,' \
	'control & ~DMA_ABORT_INTERRUPT_INT_1);' \
	'((size - sizeof(u32)) & DMA_1_SIZE_CONTROL_SIZE_MASK)' \
	'#define SM750_DRM_DMA_GUARD_WORDS 4' \
	'0x51a70000U + i' \
	'0xa75e0000U + i' \
	'DMA_ABORT_INTERRUPT_ABORT_1' \
	'devm_request_irq(&sdev->pdev->dev, sdev->pdev->irq,' \
	'IRQF_SHARED' \
	'wait_for_completion_timeout(&sdev->dma_completion,' \
	'DMA1 IRQ was not delivered; reverting to completion polling' \
	'sm750_dma_irq_disable(sdev);' \
	'DMA1 shadow uploads enabled after off-screen transfer verification' \
	'sdev->shadow_dma_enabled = false;' \
	'if (enable_dma && !disable_dma) {' \
	'sm750_shadow_upload(sdev,' \
	'dst_x1 &= ~1U;' \
	'dst_x2 = min(ALIGN(dst_x2, 2), 2048U);'; do
	grep -F "$requirement" "$source_file" >/dev/null || {
		echo "Missing shadow DMA requirement: $requirement" >&2
		exit 1
	}
done

start_body=$(sed -n '/static int sm750_dma_start(/,/^}/p' "$source_file")
for requirement in 'poke32(PCI_MASTER_BASE' 'poke32(DMA_1_SOURCE' \
		'sdev->dma_active = true;' 'return 0;'; do
	grep -F "$requirement" <<<"$start_body" >/dev/null || {
		echo "DMA start does not select and launch a staging buffer" >&2
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

awk 'BEGIN {
	rows = 8
	row = 4096
	staging = rows * row
	fill = 0
	active = -1
	pending_destination = 0
	pending_size = row
	next_destination = pending_destination + pending_size
	if (row != 4096 || next_destination != 4096 ||
	    staging != 32768 || pending_size + row > staging)
		exit 1
	for (batch_row = 1; batch_row < rows; batch_row++) {
		if (next_destination != pending_destination + pending_size)
			exit 1
		pending_size += row
		next_destination += row
	}
	if (pending_size != staging)
		exit 1
	# Submitting a full buffer makes it active and changes the CPU fill buffer.
	active = fill
	fill = 1 - fill
	if (active != 0 || fill != 1)
		exit 1
	# The next batch is built in buffer 1 while DMA reads buffer 0.
	if (fill == active)
		exit 1
}' /dev/null

echo "Shadow DMA safety checks passed"
