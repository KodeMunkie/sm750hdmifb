/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SM750_SII902X_H_
#define _SM750_SII902X_H_

struct device;
struct fb_var_screeninfo;

int sm750_sii902x_prepare(struct device *dev, bool use_hardware_i2c,
			 unsigned char scl_gpio, unsigned char sda_gpio);
int sm750_sii902x_begin_mode(struct device *dev, bool use_hardware_i2c,
			    unsigned char scl_gpio, unsigned char sda_gpio);
int sm750_sii902x_enable(struct device *dev,
			 const struct fb_var_screeninfo *var,
			 bool use_hardware_i2c,
			 unsigned char scl_gpio,
			 unsigned char sda_gpio);
int sm750_sii902x_read_edid(struct device *dev, unsigned char *edid,
			   unsigned int length);
int sm750_sii902x_disable_link(void);
int sm750_sii902x_shutdown(void);
int sm750_sii902x_get_link_status(bool *connected, bool *receiver,
				 bool *event, bool clear_events);
bool sm750_sii902x_black_requested(void);
int sm750_sii902x_set_black(bool enable, unsigned char *readback);
bool sm750_sii902x_literal_rom_requested(void);

#endif
