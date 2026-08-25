// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/string.h>

#include <drm/drm_modes.h>

#include "sm750_drm_mode.h"

static int sm750_mode_validate(const struct sm750_scanout_mode *mode)
{
	if (!mode->clock_khz || !mode->hdisplay || !mode->vdisplay ||
	    mode->hsync_start < mode->hdisplay ||
	    mode->hsync_end <= mode->hsync_start ||
	    mode->htotal < mode->hsync_end ||
	    mode->vsync_start < mode->vdisplay ||
	    mode->vsync_end <= mode->vsync_start ||
	    mode->vtotal < mode->vsync_end ||
	    (mode->cpp != 2 && mode->cpp != 4) ||
	    mode->pitch < mode->hdisplay * mode->cpp)
		return -EINVAL;

	return 0;
}

int sm750_mode_from_drm(struct sm750_scanout_mode *dst,
			const struct drm_display_mode *src, u32 pitch,
			u32 offset, u8 cpp)
{
	memset(dst, 0, sizeof(*dst));
	dst->clock_khz = src->clock;
	dst->hdisplay = src->hdisplay;
	dst->hsync_start = src->hsync_start;
	dst->hsync_end = src->hsync_end;
	dst->htotal = src->htotal;
	dst->vdisplay = src->vdisplay;
	dst->vsync_start = src->vsync_start;
	dst->vsync_end = src->vsync_end;
	dst->vtotal = src->vtotal;
	dst->flags = src->flags;
	dst->pitch = pitch;
	dst->offset = offset;
	dst->cpp = cpp;

	return sm750_mode_validate(dst);
}

int sm750_mode_from_fb(struct sm750_scanout_mode *dst,
		       const struct fb_var_screeninfo *src, u32 pitch,
		       u32 offset)
{
	memset(dst, 0, sizeof(*dst));
	if (!src->pixclock)
		return -EINVAL;
	dst->clock_khz = PICOS2KHZ(src->pixclock);
	dst->hdisplay = src->xres;
	dst->hsync_start = src->xres + src->right_margin;
	dst->hsync_end = dst->hsync_start + src->hsync_len;
	dst->htotal = src->xres + src->right_margin + src->hsync_len +
			src->left_margin;
	dst->vdisplay = src->yres;
	dst->vsync_start = src->yres + src->lower_margin;
	dst->vsync_end = dst->vsync_start + src->vsync_len;
	dst->vtotal = src->yres + src->lower_margin + src->vsync_len +
			src->upper_margin;
	if (src->sync & FB_SYNC_HOR_HIGH_ACT)
		dst->flags |= DRM_MODE_FLAG_PHSYNC;
	else
		dst->flags |= DRM_MODE_FLAG_NHSYNC;
	if (src->sync & FB_SYNC_VERT_HIGH_ACT)
		dst->flags |= DRM_MODE_FLAG_PVSYNC;
	else
		dst->flags |= DRM_MODE_FLAG_NVSYNC;
	dst->pitch = pitch;
	dst->offset = offset;
	dst->cpp = src->bits_per_pixel / 8;

	return sm750_mode_validate(dst);
}
