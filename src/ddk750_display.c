// SPDX-License-Identifier: GPL-2.0
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#include "ddk750_reg.h"
#include "ddk750_chip.h"
#include "ddk750_display.h"
#include "ddk750_power.h"

#define DISPLAY_TRANSITION_TIMEOUT_US 100000
#define VSYNC_TRANSITION_TIMEOUT_US 50000

static int wait_display_value(unsigned long reg, unsigned long value,
			      unsigned long reserved)
{
	unsigned int elapsed;

	for (elapsed = 0; elapsed < DISPLAY_TRANSITION_TIMEOUT_US;
	     elapsed += 10) {
		if ((peek32(reg) & ~reserved) == (value & ~reserved))
			return 0;
		udelay(10);
	}

	return -ETIMEDOUT;
}

int ddk750_set_display_control(int ctrl, bool enabled)
{
	unsigned long reg, val, reserved;
	int ret;

	if (!ctrl) {
		reg = PANEL_DISPLAY_CTRL;
		/* Bit 11 is live VSYNC status and is not a writable value. */
		reserved = PANEL_DISPLAY_CTRL_RESERVED_MASK |
			PANEL_DISPLAY_CTRL_VSYNC;
	} else {
		reg = CRT_DISPLAY_CTRL;
		reserved = CRT_DISPLAY_CTRL_RESERVED_MASK;
	}

	val = peek32(reg);
	if (enabled) {
		/* The hardware requires timing before plane enable. */
		val |= DISPLAY_CTRL_TIMING;
		poke32(reg, val);
		ret = wait_display_value(reg, val, reserved);
		if (ret)
			return ret;

		val |= DISPLAY_CTRL_PLANE;
		poke32(reg, val);
		return wait_display_value(reg, val, reserved);
	}

	val &= ~DISPLAY_CTRL_PLANE;
	poke32(reg, val);
	/* A plane transition may wait for vertical sync. It must not hang. */
	wait_display_value(reg, val, reserved);

	val &= ~DISPLAY_CTRL_TIMING;
	poke32(reg, val);
	return wait_display_value(reg, val, reserved);
}

static int primary_wait_vertical_sync(int delay)
{
	unsigned int elapsed, status;

	if (!(peek32(PANEL_PLL_CTRL) & PLL_CTRL_POWER) ||
	    !(peek32(PANEL_DISPLAY_CTRL) & DISPLAY_CTRL_TIMING))
		return 0;

	while (delay-- > 0) {
		for (elapsed = 0; elapsed < VSYNC_TRANSITION_TIMEOUT_US;
		     elapsed += 10) {
			status = peek32(SYSTEM_CTRL);
			if (!(status & SYSTEM_CTRL_PANEL_VSYNC_ACTIVE))
				break;
			udelay(10);
		}
		if (elapsed == VSYNC_TRANSITION_TIMEOUT_US)
			return -ETIMEDOUT;

		for (elapsed = 0; elapsed < VSYNC_TRANSITION_TIMEOUT_US;
		     elapsed += 10) {
			status = peek32(SYSTEM_CTRL);
			if (status & SYSTEM_CTRL_PANEL_VSYNC_ACTIVE)
				break;
			udelay(10);
		}
		if (elapsed == VSYNC_TRANSITION_TIMEOUT_US)
			return -ETIMEDOUT;
	}

	return 0;
}

static int set_panel_power_bit(unsigned int bit, bool enabled, int delay)
{
	unsigned int reg = peek32(PANEL_DISPLAY_CTRL);

	reg = enabled ? reg | bit : reg & ~bit;
	poke32(PANEL_DISPLAY_CTRL, reg);
	return primary_wait_vertical_sync(delay);
}

static int sw_panel_power_sequence(bool enabled, int delay)
{
	static const unsigned int enable_bits[] = {
		PANEL_DISPLAY_CTRL_FPVDDEN,
		PANEL_DISPLAY_CTRL_DATA,
		PANEL_DISPLAY_CTRL_VBIASEN,
		PANEL_DISPLAY_CTRL_FPEN,
	};
	static const unsigned int disable_bits[] = {
		PANEL_DISPLAY_CTRL_FPEN,
		PANEL_DISPLAY_CTRL_VBIASEN,
		PANEL_DISPLAY_CTRL_DATA,
		PANEL_DISPLAY_CTRL_FPVDDEN,
	};
	const unsigned int *bits = enabled ? enable_bits : disable_bits;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(enable_bits); i++) {
		/* Vendor sequence delays between stages, not after the last one. */
		ret = set_panel_power_bit(bits[i], enabled,
					  i + 1 < ARRAY_SIZE(enable_bits) ? delay : 0);
		if (ret)
			return ret;
	}

	return 0;
}

int ddk750_set_logical_disp_out(enum disp_output output)
{
	unsigned int reg;
	int ret;

	if (output & PNL_2_USAGE) {
		reg = peek32(PANEL_DISPLAY_CTRL);
		reg &= ~PANEL_DISPLAY_CTRL_SELECT_MASK;
		reg |= (((output & PNL_2_MASK) >> PNL_2_OFFSET) <<
			PANEL_DISPLAY_CTRL_SELECT_SHIFT);
		poke32(PANEL_DISPLAY_CTRL, reg);
	}

	if (output & CRT_2_USAGE) {
		reg = peek32(CRT_DISPLAY_CTRL);
		reg &= ~CRT_DISPLAY_CTRL_SELECT_MASK;
		reg |= (((output & CRT_2_MASK) >> CRT_2_OFFSET) <<
			CRT_DISPLAY_CTRL_SELECT_SHIFT);
		reg &= ~CRT_DISPLAY_CTRL_BLANK;
		poke32(CRT_DISPLAY_CTRL, reg);
	}

	if (output & PRI_TP_USAGE) {
		ret = ddk750_set_display_control(
			0, (output & PRI_TP_MASK) >> PRI_TP_OFFSET);
		if (ret)
			return ret;
	}

	if (output & SEC_TP_USAGE) {
		ret = ddk750_set_display_control(
			1, (output & SEC_TP_MASK) >> SEC_TP_OFFSET);
		if (ret)
			return ret;
	}

	if (output & PNL_SEQ_USAGE) {
		ret = sw_panel_power_sequence((output & PNL_SEQ_MASK) >>
					       PNL_SEQ_OFFSET, 4);
		if (ret)
			return ret;
	}

	if (output & DAC_USAGE)
		set_DAC((output & DAC_MASK) >> DAC_OFFSET);

	if (output & DPMS_USAGE)
		ddk750_set_dpms((output & DPMS_MASK) >> DPMS_OFFSET);

	return 0;
}
