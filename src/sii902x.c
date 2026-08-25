// SPDX-License-Identifier: GPL-2.0
/* Minimal SiI9024A TPI setup for the SM750 single-HDMI board. */

#include <linux/device.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/hdmi.h>

#include "ddk750_swi2c.h"
#include "ddk750_hwi2c.h"
#include "ddk750_chip.h"
#include "sii902x.h"

#define SII902X_I2C_ADDR_WRITE		0x72
#define SII902X_TPI_VIDEO_DATA		0x00
#define SII902X_TPI_AVI_OUTPUT_FORMAT	0x0a
#define SII902X_TPI_AVI_OUTPUT_BT709	BIT(4)
#define SII902X_TPI_AVI_INFOFRAME	0x0c
#define SII902X_SYS_CTRL_DATA		0x1a
#define SII902X_SYS_CTRL_LINK_DYNAMIC	BIT(6)
#define SII902X_SYS_CTRL_PWR_DWN	BIT(4)
#define SII902X_SYS_CTRL_AV_MUTE	BIT(3)
#define SII902X_SYS_CTRL_OUTPUT_HDMI	BIT(0)
#define SII902X_SYS_CTRL_DDC_GRANT	BIT(1)
#define SII902X_SYS_CTRL_DDC_REQUEST	BIT(2)
#define SII902X_REG_CHIPID		0x1b
#define SII902X_PWR_STATE_CTRL		0x1e
#define SII902X_PWR_STATE_MASK		GENMASK(1, 0)
#define SII902X_REG_TPI_RQB		0xc7
#define SII902X_COMPAT_TPI_DISABLE	BIT(7)
#define SII902X_COMPAT_VIDEO_BLACK	BIT(5)
#define SII902X_IND_SET_PAGE		0xbc
#define SII902X_IND_OFFSET		0xbd
#define SII902X_IND_VALUE		0xbe
#define SII902X_INT_ENABLE		0x3c
#define SII902X_INT_STATUS		0x3d
#define SII902X_HOTPLUG_EVENT		BIT(0)
#define SII902X_RX_SENSE_EVENT		BIT(1)
#define SII902X_PLUGGED_STATUS		BIT(2)
#define SII902X_RX_SENSE_STATUS		BIT(3)
#define SII902X_LINK_EVENT_MASK		(SII902X_HOTPLUG_EVENT | \
					 SII902X_RX_SENSE_EVENT)
#define SII902X_SYNC_CONFIG		0x60
#define SII902X_SYNC_DETECT		0x61
#define SII902X_SYNC_DETECT_HS_LOW	BIT(0)
#define SII902X_SYNC_DETECT_VS_LOW	BIT(1)
#define SII902X_SYNC_DETECT_INTERLACED	BIT(2)
#define SII902X_DE_GENERATOR		0x63
#define SII902X_HRES_LSB		0x6a
#define SII902X_HRES_MSB		0x6b
#define SII902X_VRES_LSB		0x6c
#define SII902X_VRES_MSB		0x6d
#define EDID_I2C_ADDR_WRITE		0xa0
#define EDID_BLOCK_SIZE			128
#define EDID_MAX_SIZE			(2 * EDID_BLOCK_SIZE)

static bool sii902x_use_hardware_i2c;
static bool sii902x_force_dvi;
static bool sii902x_falling_edge;
static bool sii902x_tpi_totals = true;
static bool sii902x_no_termination;
static bool sii902x_force_termination;
static bool sii902x_clean_init;
static bool sii902x_rom_exact;
static bool sii902x_rom_hdmi;
static bool sii902x_force_black;
static bool sii902x_mode_prepared;

extern int sm750_test_refresh_centihz;

module_param_named(sii9024_dvi, sii902x_force_dvi, bool, 0444);
MODULE_PARM_DESC(sii9024_dvi,
		 "Force DVI signalling over the HDMI connector (default: false)");
module_param_named(sii9024_falling_edge, sii902x_falling_edge, bool, 0444);
MODULE_PARM_DESC(sii9024_falling_edge,
		 "Sample the 24-bit input bus on the falling pixel-clock edge "
		 "(test only, default: false/rising)");
module_param_named(sii9024_tpi_totals, sii902x_tpi_totals, bool, 0444);
MODULE_PARM_DESC(sii9024_tpi_totals,
		 "Program TPI 0x04-0x07 with raster totals as specified by the "
		 "SiI9024A programming guide and board ROM (default: true)");
module_param_named(sii9024_no_termination, sii902x_no_termination, bool,
		   0444);
MODULE_PARM_DESC(sii9024_no_termination,
		 "Disable internal TMDS source termination for an isolated "
		 "output-stage test (default: false)");
module_param_named(sii9024_force_termination, sii902x_force_termination,
		   bool, 0444);
MODULE_PARM_DESC(sii9024_force_termination,
		 "Explicitly enable internal TMDS source termination, including "
		 "in the literal option-ROM path (test only, default: false)");
module_param_named(sii9024_clean_init, sii902x_clean_init, bool, 0444);
MODULE_PARM_DESC(sii9024_clean_init,
		 "Use only documented TPI initialization registers and omit "
		 "inherited board-ROM magic (test only, default: false)");
module_param_named(sii9024_rom_exact, sii902x_rom_exact, bool, 0444);
MODULE_PARM_DESC(sii9024_rom_exact,
		 "Replay the SE-DP750A-HDMI option-ROM initialization order; "
		 "1280x720 is byte-for-byte and selected diagnostic modes replace "
		 "only the TPI timing bytes (test only, default: false)");
module_param_named(sii9024_rom_hdmi, sii902x_rom_hdmi, bool, 0444);
MODULE_PARM_DESC(sii9024_rom_hdmi,
		 "With sii9024_rom_exact, replace only the final DVI enable with "
		 "a valid 720p AVI InfoFrame and HDMI enable (test only)");
module_param_named(sii9024_force_black, sii902x_force_black, bool, 0444);
MODULE_PARM_DESC(sii9024_force_black,
		 "With sii9024_rom_exact, make the transmitter replace input RGB "
		 "with black while retaining clock/sync and TMDS (test only)");

struct sii902x_reg_value {
	unsigned char reg;
	unsigned char value;
};

static const struct sii902x_reg_value sii9024_legacy_init[] = {
	{ 0x82, 0x25 },
	{ 0x7c, 0x14 },
	{ 0xf5, 0x00 },
};

static int sii902x_write_avi_infoframe(const struct fb_var_screeninfo *var,
				       unsigned int refresh);
static int sii902x_read(unsigned char reg, unsigned char *value);
static int sii902x_write(unsigned char reg, unsigned char value);

bool sm750_sii902x_literal_rom_requested(void)
{
	return sii902x_mode_prepared && sii902x_rom_exact;
}

static int sii902x_indexed_update(unsigned char page, unsigned char offset,
				  unsigned char mask, unsigned char value,
				  unsigned char *result)
{
	unsigned char regval;
	int ret;

	ret = sii902x_write(SII902X_IND_SET_PAGE, page);
	if (ret)
		return ret;
	ret = sii902x_write(SII902X_IND_OFFSET, offset);
	if (ret)
		return ret;
	ret = sii902x_read(SII902X_IND_VALUE, &regval);
	if (ret)
		return ret;
	regval = (regval & ~mask) | (value & mask);
	ret = sii902x_write(SII902X_IND_VALUE, regval);
	if (!ret && result)
		*result = regval;
	return ret;
}

static int sii902x_set_source_termination(bool enable, unsigned char *result)
{
	return sii902x_indexed_update(0x01, 0x82, BIT(0),
				      enable ? BIT(0) : 0, result);
}

/* Vendor Linux and WinCE paths set page 0, register 0x0a bit 3 on HPD. */
static int sii902x_apply_hotplug_workaround(struct device *dev)
{
	unsigned char status, readback;
	int ret;

	ret = sii902x_read(SII902X_INT_STATUS, &status);
	if (ret)
		return ret;
	if (!(status & SII902X_PLUGGED_STATUS)) {
		dev_info(dev,
			 "SiI9024A vendor HPD workaround skipped: HPD is low\n");
		return 0;
	}

	ret = sii902x_indexed_update(0x00, 0x0a, BIT(3), BIT(3),
				      &readback);
	if (!ret)
		dev_info(dev,
			 "SiI9024A vendor HPD workaround applied: page0[0a]=%02x\n",
			 readback);
	return ret;
}

bool sm750_sii902x_black_requested(void)
{
	return sii902x_force_black;
}

int sm750_sii902x_set_black(bool enable, unsigned char *readback)
{
	unsigned char expected = enable ?
		SII902X_COMPAT_TPI_DISABLE | SII902X_COMPAT_VIDEO_BLACK : 0;
	unsigned char value = expected;
	int ret;

	ret = sii902x_write(SII902X_REG_TPI_RQB, value);
	if (ret)
		return ret;
	ret = sii902x_read(SII902X_REG_TPI_RQB, &value);
	if (ret)
		return ret;
	if (readback)
		*readback = value;
	return ((value & (SII902X_COMPAT_TPI_DISABLE |
			  SII902X_COMPAT_VIDEO_BLACK)) != expected) ? -EIO : 0;
}

/* Board-ROM initialization which is independent of the selected video mode. */
static const struct sii902x_reg_value sii9024_board_init[] = {
	{ 0xbc, 0x01 },
	{ 0xbd, 0x03 },
	{ 0xbc, 0x01 },
	{ 0xbd, 0x02 },
	/* A KVM can hold HPD high while switching its TMDS receiver (RSEN). */
	{ SII902X_INT_ENABLE,
	  SII902X_HOTPLUG_EVENT | SII902X_RX_SENSE_EVENT },
	{ 0x0a, 0x00 },
	{ 0x19, 0x00 },
	{ 0x60, 0x00 },
	{ 0x26, 0x30 },
	{ 0x63, 0x00 },
	{ 0x0b, 0x00 },
};

/* Minimal documented setup for a 24-bit RGB bus with external DE/sync. */
static const struct sii902x_reg_value sii9024_clean_init[] = {
	{ SII902X_INT_ENABLE,
	  SII902X_HOTPLUG_EVENT | SII902X_RX_SENSE_EVENT },
	{ 0x0b, 0x00 },	/* Normal RGB input; no YC mux. */
	{ 0x60, 0x00 },	/* Explicit sync/external DE path. */
	{ 0x63, 0x00 },	/* Positive input sync polarity. */
	{ 0x26, 0x00 },	/* Audio interface disabled. */
};

static int sii902x_read(unsigned char reg, unsigned char *value)
{
	if (sii902x_use_hardware_i2c)
		return sm750_hw_i2c_read_reg(SII902X_I2C_ADDR_WRITE,
					       reg, value);
	return sm750_sw_i2c_read_reg_checked(SII902X_I2C_ADDR_WRITE,
					      reg, value) ? -ENXIO : 0;
}

static int sii902x_write(unsigned char reg, unsigned char value)
{
	if (sii902x_use_hardware_i2c)
		return sm750_hw_i2c_write_reg(SII902X_I2C_ADDR_WRITE,
						reg, value);
	return sm750_sw_i2c_write_reg(SII902X_I2C_ADDR_WRITE, reg, value) ?
		-ENXIO : 0;
}

static int sii902x_write_block(unsigned char reg, const unsigned char *data,
			       unsigned int length)
{
	if (sii902x_use_hardware_i2c)
		return sm750_hw_i2c_write_block(SII902X_I2C_ADDR_WRITE, reg,
					       data, length);
	return sm750_sw_i2c_write_block(SII902X_I2C_ADDR_WRITE, reg, data,
					length);
}

/*
 * Read what the transmitter detects on the parallel input pins.  SM750 MMIO
 * readback only proves that its timing generator accepted our values; these
 * counters independently prove what reached the SiI9024A.  Take several
 * samples because the programming guide permits H_RES/V_RES to vary slightly.
 */
static void sii902x_log_input_sync(struct device *dev,
				   const struct fb_var_screeninfo *var)
{
	unsigned int expected_h, expected_v, hres[3], vres[3];
	unsigned char detect[3], raw[4], config, generator;
	unsigned int sample, i;
	int ret;

	expected_h = var->xres + var->left_margin + var->right_margin +
		     var->hsync_len;
	expected_v = var->yres + var->upper_margin + var->lower_margin +
		     var->vsync_len;

	/* Allow more than one complete frame before sampling the counters. */
	msleep(40);
	for (sample = 0; sample < ARRAY_SIZE(hres); sample++) {
		ret = sii902x_read(SII902X_SYNC_DETECT, &detect[sample]);
		if (ret)
			goto read_failed;
		for (i = 0; i < ARRAY_SIZE(raw); i++) {
			ret = sii902x_read(SII902X_HRES_LSB + i, &raw[i]);
			if (ret)
				goto read_failed;
		}
		hres[sample] = raw[0] | ((raw[1] & 0x3f) << 8);
		vres[sample] = raw[2] | ((raw[3] & 0x0f) << 8);
		if (sample + 1 < ARRAY_SIZE(hres))
			msleep(20);
	}
	ret = sii902x_read(SII902X_SYNC_CONFIG, &config);
	if (ret)
		goto read_failed;
	ret = sii902x_read(SII902X_DE_GENERATOR, &generator);
	if (ret)
		goto read_failed;

	dev_info(dev,
		 "SiI9024A input sync measured: requested=%ux%u, H=%u/%u/%u, V=%u/%u/%u, detect=%02x/%02x/%02x (H=%s V=%s %s), sync-config=%02x DE-generator=%02x\n",
		 expected_h, expected_v,
		 hres[0], hres[1], hres[2], vres[0], vres[1], vres[2],
		 detect[0], detect[1], detect[2],
		 detect[2] & SII902X_SYNC_DETECT_HS_LOW ? "negative" : "positive",
		 detect[2] & SII902X_SYNC_DETECT_VS_LOW ? "negative" : "positive",
		 detect[2] & SII902X_SYNC_DETECT_INTERLACED ?
		 "interlaced" : "progressive", config, generator);
	if (hres[2] != expected_h || vres[2] != expected_v)
		dev_warn(dev,
			 "SiI9024A input raster differs from requested timing\n");
	return;

read_failed:
	dev_warn(dev, "SiI9024A input sync readback failed: %d\n", ret);
}

static int sii902x_verify_tpi_and_clear_events(struct device *dev,
					       unsigned char chipid[3])
{
	unsigned char status;
	int i, ret;

	for (i = 0; i < 3; i++) {
		ret = sii902x_read(SII902X_REG_CHIPID + i, &chipid[i]);
		if (ret) {
			dev_err(dev, "SiI9024A identity read failed: %d\n", ret);
			return ret;
		}
	}
	if (chipid[0] != 0xb0) {
		dev_err(dev, "SiI902x unexpected chip ID %02x\n", chipid[0]);
		return -ENODEV;
	}

	/* Clear only the latched W1C event bits; pin-state bits are read-only. */
	ret = sii902x_read(SII902X_INT_STATUS, &status);
	if (ret)
		return ret;
	status &= SII902X_LINK_EVENT_MASK;
	return status ? sii902x_write(SII902X_INT_STATUS, status) : 0;
}

/*
 * Exact routine at option-ROM offsets 0x1317..0x1428 (version 1.00.59,
 * 2019-05-14).  Keep this deliberately separate from the general mode path:
 * it is a diagnostic control which reproduces the card vendor's known boot
 * sequence, including byte-at-a-time writes and the otherwise omitted
 * dynamic link-integrity bit in system-control register 0x1a.
 */
static int sii902x_run_rom_720p_sequence(struct device *dev,
					 const struct fb_var_screeninfo *var)
{
	/* begin_control() has already replayed the compatible-map preamble. */
	static const struct sii902x_reg_value sequence[] = {
		{ 0xbc, 0x01 }, { 0xbd, 0x03 },
		{ 0xbc, 0x01 }, { 0xbd, 0x02 },
		{ 0x3c, 0x01 }, { 0x08, 0x70 },
		{ 0x00, 0x01 }, { 0x01, 0x1d },
		{ 0x02, 0x70 }, { 0x03, 0x17 },
		{ 0x04, 0x72 }, { 0x05, 0x06 },
		{ 0x06, 0xee }, { 0x07, 0x02 },
		{ 0x08, 0x70 }, { 0x1a, 0x11 },
		{ 0x09, 0x00 }, { 0x0a, 0x10 },
		{ 0x19, 0x00 }, { 0x1a, 0x10 },
		{ 0x60, 0x00 }, { 0x1e, 0x00 },
		{ 0x26, 0x30 }, { 0x63, 0x00 },
		{ 0x0b, 0x00 }, { 0x1a, 0x40 },
	};
	struct pll_value pll = {
		.clock_type = PRIMARY_PLL,
		.input_freq = DEFAULT_INPUT_CLOCK,
	};
	unsigned char control, status, video[10];
	unsigned char termination = 0xff;
	unsigned int htotal, vtotal, requested_hz, actual_hz;
	unsigned int i, pixel_10khz, refresh_centihz;
	bool override_geometry;
	int ret;

	if (!((var->xres == 1280 && var->yres == 720) ||
	      (var->xres == 1024 && var->yres == 768))) {
		dev_err(dev,
			"option-ROM ordered test supports 1280x720 or 1024x768, not %ux%u\n",
			var->xres, var->yres);
		return -EINVAL;
	}
	override_geometry = sm750_test_refresh_centihz || var->xres != 1280 ||
		var->yres != 720;
	htotal = var->xres + var->left_margin + var->right_margin +
		var->hsync_len;
	vtotal = var->yres + var->upper_margin + var->lower_margin +
		var->vsync_len;
	requested_hz = DIV_ROUND_CLOSEST_ULL(1000000000000ULL,
						 var->pixclock);
	actual_hz = sm750_calc_pll_value(requested_hz, &pll);
	pixel_10khz = DIV_ROUND_CLOSEST(actual_hz, 10000);
	refresh_centihz = DIV_ROUND_CLOSEST_ULL(
		(u64)actual_hz * 100, (u64)htotal * vtotal);
	if (override_geometry) {
		dev_info(dev,
			 "%s ROM timing override uses quantized PLL: requested=%u Hz, actual=%u Hz (M=%lu N=%lu OD=%lu POD=%lu), TPI=%u.%02u Hz/%u.%02u MHz\n",
			 sm750_test_refresh_centihz ? "fractional" : "standard-mode",
			 requested_hz, actual_hz, pll.M, pll.N, pll.OD, pll.POD,
			 refresh_centihz / 100, refresh_centihz % 100,
			 pixel_10khz / 100, pixel_10khz % 100);
	}

	for (i = 0; i < ARRAY_SIZE(sequence); i++) {
		unsigned char value = sequence[i].value;

		/*
		 * The ROM's legacy 0x82=0x25 write selects the x1 clock path;
		 * it is not the indexed page-1 source-termination control.  Apply
		 * the requested electrical experiment immediately before the final
		 * TMDS power-up write, preserving the literal path by default.
		 */
		if (i == ARRAY_SIZE(sequence) - 1 &&
		    (sii902x_force_termination || sii902x_no_termination)) {
			if (sii902x_force_termination && sii902x_no_termination)
				return -EINVAL;
			ret = sii902x_set_source_termination(
				sii902x_force_termination, &termination);
			if (ret)
				return ret;
		}
		/* Keep TMDS powered down until the HDMI AVI packet is staged. */
		if (sii902x_rom_hdmi && i == ARRAY_SIZE(sequence) - 1)
			break;
		if (i == ARRAY_SIZE(sequence) - 1) {
			ret = sii902x_apply_hotplug_workaround(dev);
			if (ret)
				return ret;
		}
		/*
		 * Keep the ordinary 720p path byte-for-byte identical to the option
		 * ROM, including its nominal CEA clock/rate metadata (74.25 MHz and
		 * 60.00 Hz).  The SM750 PLL's small quantisation error is present when
		 * the ROM runs too, so reporting 74.28 MHz/60.02 Hz here was not an
		 * exact replay.  Only diagnostic geometry or refresh tests replace
		 * the ROM's timing bytes with calculated values.
		 */
		switch (sequence[i].reg) {
		case 0x00:
			if (override_geometry)
				value = pixel_10khz;
			break;
		case 0x01:
			if (override_geometry)
				value = pixel_10khz >> 8;
			break;
		case 0x02:
			if (override_geometry)
				value = refresh_centihz;
			break;
		case 0x03:
			if (override_geometry)
				value = refresh_centihz >> 8;
			break;
		case 0x04:
			if (override_geometry)
				value = var->xres + var->left_margin +
					var->right_margin + var->hsync_len;
			break;
		case 0x05:
			if (override_geometry)
				value = (var->xres + var->left_margin +
					 var->right_margin + var->hsync_len) >> 8;
			break;
		case 0x06:
			if (override_geometry)
				value = var->yres + var->upper_margin +
					var->lower_margin + var->vsync_len;
			break;
		case 0x07:
			if (override_geometry)
				value = (var->yres + var->upper_margin +
					 var->lower_margin + var->vsync_len) >> 8;
			break;
		}
		ret = sii902x_write(sequence[i].reg, value);
		if (ret) {
			dev_err(dev,
				"option-ROM sequence failed at step %u (%02x=%02x): %d\n",
				 i, sequence[i].reg, value, ret);
			return ret;
		}
	}
	if (sii902x_rom_hdmi) {
		ret = sii902x_write_avi_infoframe(
			var, DIV_ROUND_CLOSEST(refresh_centihz, 100));
		if (ret)
			return ret;
		ret = sii902x_write(SII902X_SYS_CTRL_DATA,
				     SII902X_SYS_CTRL_LINK_DYNAMIC |
				     SII902X_SYS_CTRL_OUTPUT_HDMI);
		if (ret)
			return ret;
	}
	/*
	 * The final ROM write (or HDMI override above) powers TMDS up.  That
	 * transition resets pixel repetition, so TPI 0x08 must always be the
	 * first post-power-up write; the edge experiment changes only bit 4.
	 */
	ret = sii902x_write(SII902X_TPI_VIDEO_DATA + 8,
			     sii902x_falling_edge ? 0x60 : 0x70);
	if (ret)
		return ret;
	sii902x_log_input_sync(dev, var);

	for (i = 0; i < ARRAY_SIZE(video); i++) {
		ret = sii902x_read(SII902X_TPI_VIDEO_DATA + i, &video[i]);
		if (ret)
			return ret;
	}
	ret = sii902x_read(SII902X_SYS_CTRL_DATA, &control);
	if (ret)
		return ret;
	ret = sii902x_read(SII902X_INT_STATUS, &status);
	if (ret)
		return ret;

	dev_info(dev,
		 "SiI9024A option-ROM sequence%s%s%s: video %*ph, system-control=%02x (dynamic=%u power-down=%u mute=%u mode=%s), HPD=%u RSEN=%u status=%02x, source-termination=%s%s, black-control=%s\n",
		 sii902x_rom_hdmi ? " with HDMI AVI override" : "",
		 sii902x_falling_edge ? " with falling-edge override" : "",
		 sii902x_force_black ? " with transmitter-black override" : "",
		 (int)sizeof(video), video, control,
		 !!(control & SII902X_SYS_CTRL_LINK_DYNAMIC),
		 !!(control & SII902X_SYS_CTRL_PWR_DWN),
		 !!(control & SII902X_SYS_CTRL_AV_MUTE),
		 control & SII902X_SYS_CTRL_OUTPUT_HDMI ? "HDMI" : "DVI",
		 !!(status & SII902X_PLUGGED_STATUS),
		 !!(status & SII902X_RX_SENSE_STATUS), status,
		 termination == 0xff ? "ROM/default" :
		 (termination & BIT(0) ? "enabled" : "disabled"),
		 termination == 0xff ? "" : " (explicit)",
		 sii902x_force_black ? "scheduled-after-live-video" : "off");
	return 0;
}

int sm750_sii902x_disable_link(void)
{
	unsigned char value;
	int ret;

	sii902x_mode_prepared = false;
	ret = sii902x_read(SII902X_SYS_CTRL_DATA, &value);
	if (ret)
		return ret;
	value &= ~(SII902X_SYS_CTRL_DDC_REQUEST |
		   SII902X_SYS_CTRL_DDC_GRANT);
	value |= SII902X_SYS_CTRL_PWR_DWN | SII902X_SYS_CTRL_AV_MUTE;
	ret = sii902x_write(SII902X_SYS_CTRL_DATA, value);
	if (!ret)
		msleep(20);
	return ret;
}

int sm750_sii902x_shutdown(void)
{
	int bus_ret = 0;
	int ret;

	ret = sm750_sii902x_disable_link();
	/*
	 * Module replacement otherwise leaves the bit-banged host bus in an
	 * inherited electrical state.  End with a bus-clear/STOP and both GPIOs
	 * released so the next probe can enter TPI mode reliably.
	 */
	if (!sii902x_use_hardware_i2c)
		bus_ret = sm750_sw_i2c_recover_bus();

	return ret ? ret : bus_ret;
}

int sm750_sii902x_get_link_status(bool *connected, bool *receiver,
				 bool *event, bool clear_events)
{
	unsigned char value;
	int ret;

	if (!connected || !receiver || !event)
		return -EINVAL;
	ret = sii902x_read(SII902X_INT_STATUS, &value);
	if (ret)
		return ret;
	*connected = !!(value & SII902X_PLUGGED_STATUS);
	*receiver = !!(value & SII902X_RX_SENSE_STATUS);
	*event = !!(value & SII902X_LINK_EVENT_MASK);
	if (!clear_events)
		return 0;
	/* Register 0x3d uses write-one-to-clear for latched event bits. */
	value &= SII902X_LINK_EVENT_MASK;
	return value ? sii902x_write(SII902X_INT_STATUS, value) : 0;
}

/*
 * HPD means that DDC is available; it does not mean that a receiver is
 * connected to the TMDS pairs.  Conversely, the KC-KVM8201 exposes its TMDS
 * receiver (RSEN) without asserting HPD.  A forced mode does not need DDC, so
 * require receiver sense alone to remain asserted for 100 ms before normal
 * video is unmuted and retain HPD as diagnostic state.
 */
static int sii902x_wait_receiver_ready(struct device *dev,
				       unsigned char *status)
{
	unsigned int elapsed, stable = 0;
	unsigned char value = 0;
	int ret;

	for (elapsed = 0; elapsed < 2000; elapsed += 25) {
		ret = sii902x_read(SII902X_INT_STATUS, &value);
		if (ret)
			return ret;

		if (value & SII902X_RX_SENSE_STATUS) {
			stable += 25;
			if (stable >= 100)
				break;
		} else {
			stable = 0;
		}
		msleep(25);
	}

	*status = value;
	if (stable >= 100)
		dev_info(dev,
			 "SiI9024A receiver ready after %u ms: HPD=%u RSEN=1 status=%02x\n",
			 elapsed + 25,
			 !!(value & SII902X_PLUGGED_STATUS), value);
	else
		dev_warn(dev,
			 "SiI9024A receiver not ready after %u ms: HPD=%u RSEN=%u status=%02x; enabling for compatibility\n",
			 elapsed,
			 !!(value & SII902X_PLUGGED_STATUS),
			 !!(value & SII902X_RX_SENSE_STATUS), value);

	return 0;
}

static unsigned char sii902x_cea_vic(const struct fb_var_screeninfo *var,
				     unsigned int refresh)
{
	unsigned int htotal = var->xres + var->left_margin +
		var->right_margin + var->hsync_len;
	unsigned int vtotal = var->yres + var->upper_margin +
		var->lower_margin + var->vsync_len;

	if (refresh < 59 || refresh > 61)
		return 0;

	/* A VIC asserts the complete CEA raster, not just active geometry. */
	if (var->xres == 1920 && var->yres == 1080 &&
	    htotal == 2200 && vtotal == 1125)
		return 16;
	if (var->xres == 1280 && var->yres == 720 &&
	    htotal == 1650 && vtotal == 750)
		return 4;
	if (var->xres == 720 && var->yres == 480 &&
	    htotal == 858 && vtotal == 525)
		return 2;
	if (var->xres == 640 && var->yres == 480 &&
	    htotal == 800 && vtotal == 525)
		return 1;

	return 0;
}

static int sii902x_write_avi_infoframe(const struct fb_var_screeninfo *var,
				       unsigned int refresh)
{
	struct hdmi_avi_infoframe frame;
	unsigned char packed[HDMI_INFOFRAME_SIZE(AVI)];
	ssize_t length;
	unsigned int payload_offset = HDMI_INFOFRAME_HEADER_SIZE - 1;

	hdmi_avi_infoframe_init(&frame);
	frame.colorspace = HDMI_COLORSPACE_RGB;
	frame.scan_mode = HDMI_SCAN_MODE_NONE;
	frame.quantization_range = HDMI_QUANTIZATION_RANGE_DEFAULT;
	frame.video_code = sii902x_cea_vic(var, refresh);

	if ((u64)var->xres * 3 == (u64)var->yres * 4) {
		frame.picture_aspect = HDMI_PICTURE_ASPECT_4_3;
		frame.active_aspect = HDMI_ACTIVE_ASPECT_PICTURE;
	} else if ((u64)var->xres * 9 == (u64)var->yres * 16) {
		frame.picture_aspect = HDMI_PICTURE_ASPECT_16_9;
		frame.active_aspect = HDMI_ACTIVE_ASPECT_PICTURE;
	} else {
		/*
		 * AVI version 2 only encodes 4:3 and 16:9 in its two-bit
		 * picture-aspect field.  The kernel helper correctly rejects
		 * HDMI_PICTURE_ASPECT_64_27 here, despite that value being useful
		 * to newer DRM metadata APIs.  Custom PC timings retain their
		 * geometry from the video timing and advertise no AVI aspect.
		 */
		frame.picture_aspect = HDMI_PICTURE_ASPECT_NONE;
	}

	length = hdmi_avi_infoframe_pack(&frame, packed, sizeof(packed));
	if (length < 0) {
		pr_err("SiI9024A AVI InfoFrame rejected for %ux%u: %zd\n",
		       var->xres, var->yres, length);
		return length;
	}

	/*
	 * The TPI window takes the checksum followed by the 13-byte payload.
	 * Keep this in one I2C transaction: writing the final 0x19 byte atomically
	 * latches the staged output format and complete AVI InfoFrame.
	 */
	return sii902x_write_block(SII902X_TPI_AVI_INFOFRAME,
				   packed + payload_offset,
				   length - payload_offset);
}

static int sii902x_init_control_bus(struct device *dev,
				   bool use_hardware_i2c,
				   unsigned char scl_gpio,
				   unsigned char sda_gpio)
{
	int ret;

	sii902x_use_hardware_i2c = use_hardware_i2c;
	ret = use_hardware_i2c ? sm750_hw_i2c_init() :
		sm750_sw_i2c_init(scl_gpio, sda_gpio);
	if (ret) {
		dev_err(dev, "SiI902x control bus is stuck on GPIO %u/%u: %d\n",
			scl_gpio, sda_gpio, ret);
		return ret;
	}
	usleep_range(1000, 2000);
	return 0;
}

static int sii902x_begin_control(struct device *dev, bool use_hardware_i2c,
				 unsigned char scl_gpio,
				 unsigned char sda_gpio,
				 unsigned char chipid[3])
{
	unsigned char control, compat = 0xff;
	unsigned int i;
	int ret;

	sii902x_mode_prepared = false;
	ret = sii902x_init_control_bus(dev, use_hardware_i2c, scl_gpio,
				       sda_gpio);
	if (ret)
		return ret;

	/*
	 * Select the compatible map and black output before touching either the
	 * transmitter clock path or the SM750 raster. The legacy defaults must be
	 * written before C7=0 enters TPI mode, matching both ROM and vendor code.
	 */
	ret = sm750_sii902x_set_black(true, &compat);
	if (ret && !use_hardware_i2c) {
		ret = sm750_sw_i2c_recover_bus();
		if (!ret) {
			usleep_range(1000, 2000);
			ret = sm750_sii902x_set_black(true, &compat);
		}
	}
	if (ret)
		goto no_device;

	if (sii902x_rom_exact || !sii902x_clean_init) {
		for (i = 0; i < ARRAY_SIZE(sii9024_legacy_init); i++) {
			ret = sii902x_write(sii9024_legacy_init[i].reg,
					     sii9024_legacy_init[i].value);
			if (ret)
				return ret;
		}
	}

	ret = sii902x_write(SII902X_REG_TPI_RQB, 0x00);
	if (ret)
		return ret;
	ret = sii902x_verify_tpi_and_clear_events(dev, chipid);
	if (ret)
		return ret;

	ret = sii902x_read(SII902X_SYS_CTRL_DATA, &control);
	if (ret)
		return ret;
	control &= ~(SII902X_SYS_CTRL_DDC_REQUEST |
		     SII902X_SYS_CTRL_DDC_GRANT);
	control |= SII902X_SYS_CTRL_PWR_DWN | SII902X_SYS_CTRL_AV_MUTE;
	ret = sii902x_write(SII902X_SYS_CTRL_DATA, control);
	if (ret)
		return ret;

	/* TPI programming is specified in full-operation D0, not D2. */
	ret = sii902x_read(SII902X_PWR_STATE_CTRL, &control);
	if (ret)
		return ret;
	ret = sii902x_write(SII902X_PWR_STATE_CTRL,
			    control & ~SII902X_PWR_STATE_MASK);
	if (ret)
		return ret;

	/* Resolution changes require at least 128 ms with TMDS powered down. */
	msleep(150);
	sii902x_mode_prepared = true;
	dev_info(dev,
		 "SiI9024A quiesced before source mode change: compatible=%02x, profile=%s\n",
		 compat, sii902x_clean_init ? "documented-only" : "board-ROM");
	return 0;

no_device:
	dev_err(dev, "SiI9024A did not acknowledge on GPIO %u/%u\n",
		scl_gpio, sda_gpio);
	return ret;
}

int sm750_sii902x_prepare(struct device *dev, bool use_hardware_i2c,
			 unsigned char scl_gpio, unsigned char sda_gpio)
{
	unsigned char chipid[3];

	return sii902x_begin_control(dev, use_hardware_i2c, scl_gpio,
				      sda_gpio, chipid);
}

int sm750_sii902x_begin_mode(struct device *dev, bool use_hardware_i2c,
			    unsigned char scl_gpio, unsigned char sda_gpio)
{
	unsigned char chipid[3];

	return sii902x_begin_control(dev, use_hardware_i2c, scl_gpio,
				      sda_gpio, chipid);
}

int sm750_sii902x_enable(struct device *dev,
			 const struct fb_var_screeninfo *var,
			 bool use_hardware_i2c,
			 unsigned char scl_gpio,
			 unsigned char sda_gpio)
{
	unsigned int htotal, vtotal, refresh;
	unsigned int pixel_khz, pll_khz, pixel_10khz, refresh_centihz;
	struct pll_value pll = {
		.clock_type = PRIMARY_PLL,
		.input_freq = DEFAULT_INPUT_CLOCK,
	};
	unsigned char chipid[3];
	unsigned char video[10];
	unsigned char readback[10], value, termination, output_format, avi_last;
	const struct sii902x_reg_value *init_sequence;
	unsigned int init_count;
	int i, ret;

	if (!sii902x_mode_prepared)
		return -EPIPE;
	if (use_hardware_i2c != sii902x_use_hardware_i2c)
		return -EINVAL;
	(void)scl_gpio;
	(void)sda_gpio;
	sii902x_mode_prepared = false;

	ret = sii902x_verify_tpi_and_clear_events(dev, chipid);
	if (ret)
		return ret;

	if (sii902x_rom_exact) {
		return sii902x_run_rom_720p_sequence(dev, var);
	}

	if (sii902x_clean_init) {
		init_sequence = sii9024_clean_init;
		init_count = ARRAY_SIZE(sii9024_clean_init);
	} else {
		init_sequence = sii9024_board_init;
		init_count = ARRAY_SIZE(sii9024_board_init);
	}
	for (i = 0; i < init_count; i++) {
		ret = sii902x_write(init_sequence[i].reg,
				     init_sequence[i].value);
		if (ret)
			return ret;
	}
	dev_info(dev, "SiI9024A initialization profile: %s\n",
		 sii902x_clean_init ? "documented-only" : "board-ROM");

	ret = sii902x_apply_hotplug_workaround(dev);
	if (ret)
		return ret;

	/* Configure the SiI9022A/SiI9024A TMDS source termination. */
	ret = sii902x_set_source_termination(!sii902x_no_termination,
					    &termination);
	if (ret)
		return ret;

	pixel_khz = PICOS2KHZ(var->pixclock);
	pll_khz = DIV_ROUND_CLOSEST(
		sm750_calc_pll_value(pixel_khz * 1000, &pll), 1000);
	htotal = var->xres + var->left_margin + var->right_margin +
		 var->hsync_len;
	vtotal = var->yres + var->upper_margin + var->lower_margin +
		 var->vsync_len;
	pixel_10khz = DIV_ROUND_CLOSEST(pll_khz, 10);
	refresh_centihz = htotal && vtotal ?
		DIV_ROUND_CLOSEST_ULL((u64)pll_khz * 100000,
				      (u64)htotal * vtotal) : 0;
	refresh = DIV_ROUND_CLOSEST(refresh_centihz, 100);

	video[0] = pixel_10khz;
	video[1] = pixel_10khz >> 8;
	/*
	 * The board option ROM proves the TPI units and geometry unambiguously:
	 * 0x02-0x03 is vertical frequency in 0.01 Hz (6000 for 60.00 Hz), and
	 * 0x04-0x07 contains complete raster totals (1650x750 for CEA 720p),
	 * not the 1280x720 active area.  A tolerant display may recover when
	 * these metadata fields are wrong; a KVM/repeater need not.
	 */
	video[2] = refresh_centihz;
	video[3] = refresh_centihz >> 8;
	video[4] = sii902x_tpi_totals ? htotal : var->xres;
	video[5] = (sii902x_tpi_totals ? htotal : var->xres) >> 8;
	video[6] = sii902x_tpi_totals ? vtotal : var->yres;
	video[7] = (sii902x_tpi_totals ? vtotal : var->yres) >> 8;
	/*
	 * Preserve the board's rising-edge default, but permit an isolated
	 * falling-edge experiment.  A wrong parallel sampling edge can produce
	 * a marginal TMDS stream which a tolerant display decodes while a KVM's
	 * receiver refuses to lock.  Bit 4 is the TPI input-bus rising-edge bit;
	 * every other bus characteristic remains identical between the tests.
	 */
	video[8] = sii902x_falling_edge ? 0x60 : 0x70;
	video[9] = 0x00; /* RGB input, automatic quantization range. */

	ret = sii902x_write_block(SII902X_TPI_VIDEO_DATA, video,
				  ARRAY_SIZE(video));
	if (ret)
		return ret;
	dev_info(dev,
		 "SiI9024A TPI video block: %*ph (pixel=%u.%02u MHz, rate=%u Hz, %s=%ux%u, input edge=%s)\n",
		 (int)sizeof(video), video, pll_khz / 1000,
		 pll_khz % 1000 / 10, refresh,
		 sii902x_tpi_totals ? "totals" : "active",
		 sii902x_tpi_totals ? htotal : var->xres,
		 sii902x_tpi_totals ? vtotal : var->yres,
		 sii902x_falling_edge ? "falling" : "rising");
	/* The board ROM selects BT.709 for its 720p mode (TPI 0x0a=0x10). */
	ret = sii902x_write(SII902X_TPI_AVI_OUTPUT_FORMAT,
			    var->yres >= 720 ?
			    SII902X_TPI_AVI_OUTPUT_BT709 : 0x00);
	if (ret)
		return ret;

	if (sii902x_force_dvi) {
		/*
		 * TPI 0x09-0x0a are staged registers.  The programming guide
		 * requires a write to the last AVI byte (0x19) to latch them even
		 * in DVI mode; without this, a previous mode's format survives.
		 */
		ret = sii902x_write(0x19, 0x00);
		if (ret)
			return ret;
	} else {
		ret = sii902x_write_avi_infoframe(var, refresh);
		if (ret)
			return ret;
	}

	ret = sii902x_read(SII902X_SYS_CTRL_DATA, &value);
	if (ret)
		return ret;
	value &= ~SII902X_SYS_CTRL_PWR_DWN;
	value |= SII902X_SYS_CTRL_LINK_DYNAMIC;
	if (sii902x_force_dvi)
		value &= ~SII902X_SYS_CTRL_OUTPUT_HDMI;
	else
		value |= SII902X_SYS_CTRL_OUTPUT_HDMI;
	/* Start the TMDS clock while video remains muted in either mode. */
	value |= SII902X_SYS_CTRL_AV_MUTE;
	/*
	 * SiI-PR-1032 requires TPI 0x08 to be written after 0x1a[4]
	 * transitions from 1 (TMDS powered down) to 0 (TMDS active).  The
	 * transition resets the pixel-repetition field.  Restore it immediately:
	 * exposing the reset value to a KVM before repairing it can make the KVM
	 * reject the stream even though a directly connected monitor reacquires.
	 */
	ret = sii902x_write(SII902X_SYS_CTRL_DATA, value);
	if (ret)
		return ret;
	ret = sii902x_write(SII902X_TPI_VIDEO_DATA + 8, video[8]);
	if (ret)
		return ret;
	sii902x_log_input_sync(dev, var);
	for (i = 0; i < ARRAY_SIZE(readback); i++) {
		ret = sii902x_read(SII902X_TPI_VIDEO_DATA + i, &readback[i]);
		if (ret)
			return ret;
	}
	ret = sii902x_read(SII902X_TPI_AVI_OUTPUT_FORMAT, &output_format);
	if (ret)
		return ret;
	ret = sii902x_read(0x19, &avi_last);
	if (ret)
		return ret;
	dev_info(dev,
		 "SiI9024A TPI readback: video %*ph, output-format=%02x AVI-last=%02x\n",
		 (int)sizeof(readback), readback, output_format, avi_last);
	if (memcmp(video, readback, sizeof(video)))
		dev_warn(dev, "SiI9024A video timing readback mismatch\n");

	ret = sii902x_wait_receiver_ready(dev, &value);
	if (ret)
		return ret;
	ret = sii902x_read(SII902X_SYS_CTRL_DATA, &value);
	if (ret)
		return ret;
	value &= ~SII902X_SYS_CTRL_AV_MUTE;
	ret = sii902x_write(SII902X_SYS_CTRL_DATA, value);
	if (ret)
		return ret;

	usleep_range(1000, 2000);
	ret = sii902x_read(SII902X_INT_STATUS, &value);
	if (ret)
		return ret;
	dev_info(dev,
		 "SiI9024A enabled: id %02x:%02x:%02x, %ux%u@%u, %s, HPD %s, RSEN %s, status %02x; TPI %u.%02u Hz, %u.%02u MHz, PLL %u.%02u MHz, totals %ux%u, termination %02x\n",
		 chipid[0], chipid[1], chipid[2],
		 var->xres, var->yres, refresh,
		 sii902x_force_dvi ? "DVI" : "HDMI",
		 value & SII902X_PLUGGED_STATUS ? "connected" : "disconnected",
		 value & SII902X_RX_SENSE_STATUS ? "detected" : "absent", value,
		 refresh_centihz / 100, refresh_centihz % 100,
		 pixel_khz / 1000, pixel_khz % 1000 / 10,
		 pll_khz / 1000, pll_khz % 1000 / 10,
		 htotal, vtotal, termination);
	return 0;
}

int sm750_sii902x_read_edid(struct device *dev, unsigned char *edid,
			   unsigned int length)
{
	unsigned char saved, value, checksum, int_status = 0xff;
	unsigned int blocks_read = 1, extension_count, i;
	bool cta = false, hdmi = false;
	int ret, last_read_ret = 0, release_ret = -ETIMEDOUT;

	if (!edid || length < EDID_BLOCK_SIZE || sii902x_use_hardware_i2c)
		return -EINVAL;
	memset(edid, 0, length);

	ret = sii902x_read(SII902X_SYS_CTRL_DATA, &saved);
	if (ret)
		return ret;
	sii902x_read(0x3d, &int_status);
	dev_info(dev, "DDC request: system-control=%02x interrupt-status=%02x\n",
		 saved, int_status);

	/* Request, wait for grant, then explicitly lock host ownership. */
	value = saved | SII902X_SYS_CTRL_DDC_REQUEST;
	ret = sii902x_write(SII902X_SYS_CTRL_DATA, value);
	if (ret)
		return ret;
	for (i = 0; i < 50; i++) {
		ret = sii902x_read(SII902X_SYS_CTRL_DATA, &value);
		last_read_ret = ret;
		if (!ret && (value & SII902X_SYS_CTRL_DDC_GRANT))
			break;
		msleep(20);
	}
	if (i == 50) {
		dev_err(dev,
			"DDC grant timeout: last-read=%d system-control=%02x\n",
			last_read_ret, value);
		ret = -ETIMEDOUT;
		goto release;
	}
	dev_info(dev, "DDC granted: system-control=%02x\n", value);

	/* Preserve all unrelated system-control bits while closing the switch. */
	ret = sii902x_write(SII902X_SYS_CTRL_DATA, value);
	if (ret)
		goto release;
	usleep_range(1000, 1500);

	/*
	 * Keep the base block and CTA extension in one sequential E-DDC read.
	 * Some active KVM EDID proxies ACK the initial address only once per DDC
	 * ownership grant and NACK a second address phase at offset 0x80.
	 */
	ret = sm750_sw_i2c_read_block(EDID_I2C_ADDR_WRITE, 0, edid,
				      length >= EDID_MAX_SIZE ?
				      EDID_MAX_SIZE : EDID_BLOCK_SIZE);
	if (ret) {
		dev_err(dev, "EDID sequential transaction failed: %d\n", ret);
		if (sm750_sw_i2c_recover_bus())
			dev_err(dev, "DDC lines remained busy after bus clear\n");
	}

release:
	/*
	 * TPI reads are invalid while DDC is granted. Restore the saved value
	 * blindly first, exactly as required by the SiI902x reference code.
	 */
	value = saved & ~(SII902X_SYS_CTRL_DDC_REQUEST |
			  SII902X_SYS_CTRL_DDC_GRANT);
	for (i = 0; i < 50; i++) {
		udelay(30);
		if (sii902x_write(SII902X_SYS_CTRL_DATA, value)) {
			msleep(1);
			continue;
		}
		if (!sii902x_read(SII902X_SYS_CTRL_DATA, &saved) &&
		    !(saved & (SII902X_SYS_CTRL_DDC_REQUEST |
			       SII902X_SYS_CTRL_DDC_GRANT))) {
			release_ret = 0;
			break;
		}
		msleep(1);
	}
	if (release_ret) {
		dev_err(dev, "failed to release SiI9024A DDC bus\n");
		return release_ret;
	}
	if (ret)
		return ret;

	checksum = 0;
	for (i = 0; i < EDID_BLOCK_SIZE; i++)
		checksum += edid[i];
	if (memcmp(edid, "\x00\xff\xff\xff\xff\xff\xff\x00", 8) || checksum) {
		dev_err(dev, "invalid EDID header or checksum\n");
		return -EBADMSG;
	}

	extension_count = edid[126];
	if (extension_count && length >= EDID_MAX_SIZE)
		blocks_read = 2;
	if (blocks_read == 2) {
		unsigned char *extension = edid + EDID_BLOCK_SIZE;
		unsigned int data_end;

		checksum = 0;
		for (i = 0; i < EDID_BLOCK_SIZE; i++)
			checksum += extension[i];
		if (checksum) {
			dev_err(dev, "invalid EDID extension checksum\n");
			return -EBADMSG;
		}

		cta = extension[0] == 0x02;
		data_end = extension[2];
		if (cta && data_end >= 4 && data_end <= 127) {
			for (i = 4; i < data_end;) {
				unsigned int tag = extension[i] >> 5;
				unsigned int block_length = extension[i] & 0x1f;
				unsigned int next = i + 1 + block_length;

				if (next > data_end)
					break;
				if (tag == 3 && block_length >= 3 &&
				    ((extension[i + 1] == 0x03 &&
				      extension[i + 2] == 0x0c &&
				      extension[i + 3] == 0x00) ||
				     (extension[i + 1] == 0xd8 &&
				      extension[i + 2] == 0x5d &&
				      extension[i + 3] == 0xc4)))
					hdmi = true;
				i = next;
			}
		}
	}

	dev_info(dev,
		 "read valid %u-byte EDID over SiI9024A DDC: %u extension%s advertised, first extension %s, HDMI vendor block %s%s\n",
		 blocks_read * EDID_BLOCK_SIZE, extension_count,
		 extension_count == 1 ? "" : "s",
		 blocks_read == 2 ? (cta ? "CTA" : "non-CTA") : "not read",
		 hdmi ? "present" : "absent",
		 extension_count > 1 ? "; later extensions not read" : "");
	print_hex_dump_debug("sm750hdmifb EDID: ", DUMP_PREFIX_OFFSET,
			     16, 1, edid, blocks_read * EDID_BLOCK_SIZE, false);
	return 0;
}
