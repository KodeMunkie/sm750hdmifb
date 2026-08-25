/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SM750_DRM_MODE_H
#define SM750_DRM_MODE_H

#include <linux/types.h>

struct drm_display_mode;
struct fb_var_screeninfo;

struct sm750_scanout_mode {
	u32 clock_khz;
	u16 hdisplay;
	u16 hsync_start;
	u16 hsync_end;
	u16 htotal;
	u16 vdisplay;
	u16 vsync_start;
	u16 vsync_end;
	u16 vtotal;
	u32 flags;
	u32 pitch;
	u32 offset;
	u8 cpp;
};

int sm750_mode_from_drm(struct sm750_scanout_mode *dst,
			const struct drm_display_mode *src, u32 pitch,
			u32 offset, u8 cpp);
int sm750_mode_from_fb(struct sm750_scanout_mode *dst,
		       const struct fb_var_screeninfo *src, u32 pitch,
		       u32 offset);

#endif
