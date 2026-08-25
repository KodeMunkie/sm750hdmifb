// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/math64.h>

#include "ddk750_reg.h"
#include "ddk750_mode.h"

extern bool sm750_test_reduced_pll_ratio;
extern bool sm750_test_low_vco_pll;
extern int sm750_test_panel_fifo;
#include "ddk750_chip.h"

/*
 * SM750LE only:
 * This function takes care extra registers and bit fields required to set
 * up a mode in SM750LE
 *
 * Explanation about Display Control register:
 * HW only supports 7 predefined pixel clocks, and clock select is
 * in bit 29:27 of Display Control register.
 */
static unsigned long
display_control_adjust_SM750LE(struct mode_parameter *mode_param,
			       unsigned long disp_control)
{
	unsigned long x, y;

	x = mode_param->horizontal_display_end;
	y = mode_param->vertical_display_end;

	/*
	 * SM750LE has to set up the top-left and bottom-right
	 * registers as well.
	 * Note that normal SM750/SM718 only use those two register for
	 * auto-centering mode.
	 */
	poke32(CRT_AUTO_CENTERING_TL, 0);

	poke32(CRT_AUTO_CENTERING_BR,
	       (((y - 1) << CRT_AUTO_CENTERING_BR_BOTTOM_SHIFT) &
		CRT_AUTO_CENTERING_BR_BOTTOM_MASK) |
	       ((x - 1) & CRT_AUTO_CENTERING_BR_RIGHT_MASK));

	/*
	 * Assume common fields in disp_control have been properly set before
	 * calling this function.
	 * This function only sets the extra fields in disp_control.
	 */

	/* Clear bit 29:27 of display control register */
	disp_control &= ~CRT_DISPLAY_CTRL_CLK_MASK;

	/* Set bit 29:27 of display control register for the right clock */
	/* Note that SM750LE only need to supported 7 resolutions. */
	if (x == 800 && y == 600)
		disp_control |= CRT_DISPLAY_CTRL_CLK_PLL41;
	else if (x == 1024 && y == 768)
		disp_control |= CRT_DISPLAY_CTRL_CLK_PLL65;
	else if (x == 1152 && y == 864)
		disp_control |= CRT_DISPLAY_CTRL_CLK_PLL80;
	else if (x == 1280 && y == 768)
		disp_control |= CRT_DISPLAY_CTRL_CLK_PLL80;
	else if (x == 1280 && y == 720)
		disp_control |= CRT_DISPLAY_CTRL_CLK_PLL74;
	else if (x == 1280 && y == 960)
		disp_control |= CRT_DISPLAY_CTRL_CLK_PLL108;
	else if (x == 1280 && y == 1024)
		disp_control |= CRT_DISPLAY_CTRL_CLK_PLL108;
	else /* default to VGA clock */
		disp_control |= CRT_DISPLAY_CTRL_CLK_PLL25;

	/* Set bit 25:24 of display controller */
	disp_control |= (CRT_DISPLAY_CTRL_CRTSELECT | CRT_DISPLAY_CTRL_RGBBIT);

	/* Set bit 14 of display controller */
	disp_control |= DISPLAY_CTRL_CLOCK_PHASE;

	poke32(CRT_DISPLAY_CTRL, disp_control);

	return disp_control;
}

static unsigned long pll_output_clock(const struct pll_value *pll)
{
	u64 clock;

	if (!pll->N)
		return 0;
	clock = (u64)pll->input_freq * pll->M;
	clock = div_u64(clock, pll->N);
	clock >>= pll->OD + pll->POD;
	return clock;
}

/*
 * Preserve the requested line frequency when the integer SM750 PLL cannot
 * synthesize the requested pixel clock. Both supplied newer Linux and WinCE
 * DDKs apply this compensation before programming SM750 mode registers.
 */
static int adjust_mode_for_pll(struct mode_parameter *mode,
			       unsigned long actual_clock)
{
	unsigned long blank, front_percent, sync_percent;
	unsigned long front, sync, remaining;

	if (!mode->horizontal_frequency || !actual_clock ||
	    mode->horizontal_total <= mode->horizontal_display_end ||
	    mode->horizontal_sync_start < mode->horizontal_display_end)
		return -EINVAL;

	if (actual_clock == mode->pixel_clock &&
	    mode->horizontal_sync_start - mode->horizontal_display_end > 24) {
		mode->pixel_clock = actual_clock;
		return 0;
	}

	blank = mode->horizontal_total - mode->horizontal_display_end;
	front_percent = DIV_ROUND_CLOSEST_ULL(
		(u64)(mode->horizontal_sync_start -
		      mode->horizontal_display_end) * 100, blank);
	sync_percent = DIV_ROUND_CLOSEST_ULL(
		(u64)mode->horizontal_sync_width * 100, blank);

	mode->pixel_clock = actual_clock;
	mode->horizontal_total = DIV_ROUND_CLOSEST_ULL(
		actual_clock, mode->horizontal_frequency);
	if (mode->horizontal_total <= mode->horizontal_display_end)
		return -ERANGE;

	blank = mode->horizontal_total - mode->horizontal_display_end;
	front = DIV_ROUND_CLOSEST_ULL((u64)blank * front_percent, 100);
	front = max(front, 24UL);
	if (front >= blank)
		return -ERANGE;
	sync = DIV_ROUND_CLOSEST_ULL((u64)blank * sync_percent, 100);
	remaining = blank - front;
	if (remaining <= sync)
		sync = remaining / 2;
	if (!sync)
		return -ERANGE;

	mode->horizontal_sync_start = mode->horizontal_display_end + front;
	mode->horizontal_sync_width = sync;
	mode->horizontal_frequency = DIV_ROUND_CLOSEST_ULL(
		actual_clock, mode->horizontal_total);
	mode->vertical_frequency = DIV_ROUND_CLOSEST_ULL(
		mode->horizontal_frequency, mode->vertical_total);
	return 0;
}

/* only timing related registers will be  programed */
static int program_mode_registers(struct mode_parameter *mode_param,
				  struct pll_value *pll)
{
	int cnt = 0;
	unsigned int tmp, reg;

	if (pll->clock_type == SECONDARY_PLL) {
		/* programe secondary pixel clock */
		poke32(CRT_PLL_CTRL, sm750_format_pll_reg(pll));

		tmp = ((mode_param->horizontal_total - 1) <<
		       CRT_HORIZONTAL_TOTAL_TOTAL_SHIFT) &
		     CRT_HORIZONTAL_TOTAL_TOTAL_MASK;
		tmp |= (mode_param->horizontal_display_end - 1) &
		      CRT_HORIZONTAL_TOTAL_DISPLAY_END_MASK;

		poke32(CRT_HORIZONTAL_TOTAL, tmp);

		tmp = (mode_param->horizontal_sync_width <<
		       CRT_HORIZONTAL_SYNC_WIDTH_SHIFT) &
		     CRT_HORIZONTAL_SYNC_WIDTH_MASK;
		tmp |= (mode_param->horizontal_sync_start - 1) &
		      CRT_HORIZONTAL_SYNC_START_MASK;

		poke32(CRT_HORIZONTAL_SYNC, tmp);

		tmp = ((mode_param->vertical_total - 1) <<
		       CRT_VERTICAL_TOTAL_TOTAL_SHIFT) &
		     CRT_VERTICAL_TOTAL_TOTAL_MASK;
		tmp |= (mode_param->vertical_display_end - 1) &
		      CRT_VERTICAL_TOTAL_DISPLAY_END_MASK;

		poke32(CRT_VERTICAL_TOTAL, tmp);

		tmp = ((mode_param->vertical_sync_height <<
		       CRT_VERTICAL_SYNC_HEIGHT_SHIFT)) &
		     CRT_VERTICAL_SYNC_HEIGHT_MASK;
		tmp |= (mode_param->vertical_sync_start - 1) &
		      CRT_VERTICAL_SYNC_START_MASK;

		poke32(CRT_VERTICAL_SYNC, tmp);

		/* Stage polarity while the normal SM750 controller remains off. */
		tmp = 0;
		if (mode_param->vertical_sync_polarity)
			tmp |= DISPLAY_CTRL_VSYNC_PHASE;
		if (mode_param->horizontal_sync_polarity)
			tmp |= DISPLAY_CTRL_HSYNC_PHASE;

		if (sm750_get_chip_type() == SM750LE) {
			/* The LE uses a different output-enable path. */
			tmp |= DISPLAY_CTRL_TIMING | DISPLAY_CTRL_PLANE;
			display_control_adjust_SM750LE(mode_param, tmp);
		} else {
			reg = peek32(CRT_DISPLAY_CTRL) &
				~(DISPLAY_CTRL_VSYNC_PHASE |
				  DISPLAY_CTRL_HSYNC_PHASE |
				  DISPLAY_CTRL_TIMING | DISPLAY_CTRL_PLANE);

			poke32(CRT_DISPLAY_CTRL, tmp | reg);
		}

	} else if (pll->clock_type == PRIMARY_PLL) {
		unsigned int reserved;

		poke32(PANEL_PLL_CTRL, sm750_format_pll_reg(pll));

		reg = ((mode_param->horizontal_total - 1) <<
			PANEL_HORIZONTAL_TOTAL_TOTAL_SHIFT) &
			PANEL_HORIZONTAL_TOTAL_TOTAL_MASK;
		reg |= ((mode_param->horizontal_display_end - 1) &
			PANEL_HORIZONTAL_TOTAL_DISPLAY_END_MASK);
		poke32(PANEL_HORIZONTAL_TOTAL, reg);

		poke32(PANEL_HORIZONTAL_SYNC,
		       ((mode_param->horizontal_sync_width <<
			 PANEL_HORIZONTAL_SYNC_WIDTH_SHIFT) &
			PANEL_HORIZONTAL_SYNC_WIDTH_MASK) |
		       ((mode_param->horizontal_sync_start - 1) &
			PANEL_HORIZONTAL_SYNC_START_MASK));

		poke32(PANEL_VERTICAL_TOTAL,
		       (((mode_param->vertical_total - 1) <<
			 PANEL_VERTICAL_TOTAL_TOTAL_SHIFT) &
			PANEL_VERTICAL_TOTAL_TOTAL_MASK) |
		       ((mode_param->vertical_display_end - 1) &
			PANEL_VERTICAL_TOTAL_DISPLAY_END_MASK));

		poke32(PANEL_VERTICAL_SYNC,
		       ((mode_param->vertical_sync_height <<
			 PANEL_VERTICAL_SYNC_HEIGHT_SHIFT) &
			PANEL_VERTICAL_SYNC_HEIGHT_MASK) |
		       ((mode_param->vertical_sync_start - 1) &
			PANEL_VERTICAL_SYNC_START_MASK));

		/* Timing and plane are enabled later, in the required order. */
		tmp = 0;
		if (mode_param->vertical_sync_polarity)
			tmp |= DISPLAY_CTRL_VSYNC_PHASE;
		if (mode_param->horizontal_sync_polarity)
			tmp |= DISPLAY_CTRL_HSYNC_PHASE;
		if (mode_param->clock_phase_polarity)
			tmp |= DISPLAY_CTRL_CLOCK_PHASE;

		reserved = PANEL_DISPLAY_CTRL_RESERVED_MASK |
			PANEL_DISPLAY_CTRL_VSYNC;

		reg = (peek32(PANEL_DISPLAY_CTRL) & ~reserved) &
			~(DISPLAY_CTRL_CLOCK_PHASE | DISPLAY_CTRL_VSYNC_PHASE |
			  DISPLAY_CTRL_HSYNC_PHASE | DISPLAY_CTRL_TIMING |
			  DISPLAY_CTRL_PLANE);
		if (sm750_test_panel_fifo >= 0) {
			reg &= ~PANEL_DISPLAY_CTRL_FIFO;
			switch (sm750_test_panel_fifo) {
			case 1:
				reg |= PANEL_DISPLAY_CTRL_FIFO_1;
				break;
			case 3:
				reg |= PANEL_DISPLAY_CTRL_FIFO_3;
				break;
			case 7:
				reg |= PANEL_DISPLAY_CTRL_FIFO_7;
				break;
			case 11:
				reg |= PANEL_DISPLAY_CTRL_FIFO_11;
				break;
			}
			pr_info("panel FIFO test: request at %u empty entries\n",
				sm750_test_panel_fifo);
		}

		/*
		 * May a hardware bug or just my test chip (not confirmed).
		 * PANEL_DISPLAY_CTRL register seems requiring few writes
		 * before a value can be successfully written in.
		 * Added some masks to mask out the reserved bits.
		 * Note: This problem happens by design. The hardware will wait
		 *       for the next vertical sync to turn on/off the plane.
		 */
		poke32(PANEL_DISPLAY_CTRL, tmp | reg);

		while ((peek32(PANEL_DISPLAY_CTRL) & ~reserved) !=
			(tmp | reg)) {
			cnt++;
			if (cnt > 1000)
				return -ETIMEDOUT;
			poke32(PANEL_DISPLAY_CTRL, tmp | reg);
			udelay(10);
		}
	}

	return 0;
}

int ddk750_set_mode_timing(struct mode_parameter *parm, enum clock_type clock,
			   bool compensate_pll)
{
	struct pll_value pll = { 0 };
	unsigned long actual_clock;
	int ret;

	if (sm750_test_panel_fifo != -1 && sm750_test_panel_fifo != 1 &&
	    sm750_test_panel_fifo != 3 && sm750_test_panel_fifo != 7 &&
	    sm750_test_panel_fifo != 11) {
		pr_err("invalid panel FIFO threshold %d; use -1, 1, 3, 7, or 11\n",
		       sm750_test_panel_fifo);
		return -EINVAL;
	}

	pll.input_freq = DEFAULT_INPUT_CLOCK;
	pll.clock_type = clock;

	actual_clock = sm750_calc_pll_value(parm->pixel_clock, &pll);
	if (!actual_clock)
		return -ERANGE;
	if (sm750_test_reduced_pll_ratio && sm750_test_low_vco_pll) {
		pr_err("reduced-ratio and low-VCO PLL tests are mutually exclusive\n");
		return -EINVAL;
	}
	if ((sm750_test_reduced_pll_ratio || sm750_test_low_vco_pll) &&
	    clock == PRIMARY_PLL) {
		/*
		 * 249/12 and 83/4 are exactly equal.  Retain the same VCO and
		 * output-divider values while tripling the phase-detector rate.
		 * Refuse to apply this diagnostic to any other calculated mode.
		 */
		if (pll.M != 249 || pll.N != 12 || pll.OD != 2 || pll.POD != 0) {
			pr_err("reduced PLL ratio requested for unexpected tuple M=%lu N=%lu OD=%lu POD=%lu\n",
			       pll.M, pll.N, pll.OD, pll.POD);
			return -EINVAL;
		}
		pll.M = 83;
		if (sm750_test_reduced_pll_ratio) {
			pll.N = 4;
			pr_info("panel PLL reduced-ratio test: M=83 N=4 OD=2 POD=0 (frequency unchanged)\n");
		} else {
			pll.N = 8;
			pll.OD = 1;
			pr_info("panel PLL low-VCO test: M=83 N=8 OD=1 POD=0 (frequency unchanged)\n");
		}
	}
	actual_clock = pll_output_clock(&pll);
	if (compensate_pll) {
		ret = adjust_mode_for_pll(parm, actual_clock);
		if (ret)
			return ret;
	} else {
		/* Literal ROM diagnostics retain their exact programmed totals. */
		parm->pixel_clock = actual_clock;
	}
	if (sm750_get_chip_type() == SM750LE) {
		/* set graphic mode via IO method */
		outb_p(0x88, 0x3d4);
		outb_p(0x06, 0x3d5);
	}
	return program_mode_registers(parm, &pll);
}
