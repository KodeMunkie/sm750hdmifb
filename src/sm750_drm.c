// SPDX-License-Identifier: GPL-2.0
#include <linux/aperture.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/version.h>

#ifdef CONFIG_X86
#include <asm/io.h>
#endif

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_fbdev_ttm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
#include <drm/drm_vblank_helper.h>
#endif

#include "ddk750_chip.h"
#include "ddk750_display.h"
#include "ddk750_mode.h"
#include "ddk750_power.h"
#include "ddk750_reg.h"
#include "sii902x.h"
#include "sm750_dither.h"
#include "sm750_drm_mode.h"

#define SM750_DRM_NAME "sm750hdmidrm"
#define SM750_DRM_MAX_WIDTH 2560
#define SM750_DRM_PHYSICAL_MAX_WIDTH 2048
#define SM750_DRM_PLL PRIMARY_PLL
#define SM750_DRM_CONTROLLER 0
#define SM750_DRM_OUTPUT do_LCD1_PRI
#define SM750_DRM_FB_ADDRESS PANEL_FB_ADDRESS
#define SM750_DRM_FB_ADDRESS_STATUS PANEL_FB_ADDRESS_STATUS
#define SM750_DRM_FB_ADDRESS_MASK PANEL_FB_ADDRESS_ADDRESS_MASK
#define SM750_DRM_FB_WIDTH PANEL_FB_WIDTH
#define SM750_DRM_FB_WIDTH_WIDTH_SHIFT PANEL_FB_WIDTH_WIDTH_SHIFT
#define SM750_DRM_FB_WIDTH_WIDTH_MASK PANEL_FB_WIDTH_WIDTH_MASK
#define SM750_DRM_FB_WIDTH_OFFSET_MASK PANEL_FB_WIDTH_OFFSET_MASK
#define SM750_DRM_DISPLAY_CTRL PANEL_DISPLAY_CTRL
#define SM750_DRM_DISPLAY_CTRL_FORMAT PANEL_DISPLAY_CTRL_FORMAT
#define SM750_DRM_DISPLAY_CTRL_FORMAT_16 PANEL_DISPLAY_CTRL_FORMAT_16
#define SM750_DRM_DISPLAY_CTRL_FORMAT_32 PANEL_DISPLAY_CTRL_FORMAT_32
#define SM750_DRM_CURRENT_LINE PANEL_CURRENT_LINE
#define SM750_DRM_CURRENT_LINE_MASK PANEL_CURRENT_LINE_LINE_MASK
#define SM750_DRM_MAX_HEIGHT 1152
#define SII9024_MAX_CLOCK_KHZ 165000
/* Explicit driver modes include the physically tested 2048x1080@75 timing. */
#define SM750_DRM_MAX_CLOCK_KHZ 179000
#define SM750_DRM_LINE_ALIGN 16
#define SM750_DRM_CURSOR_WIDTH 64
#define SM750_DRM_CURSOR_HEIGHT 64
#define SM750_DRM_CURSOR_STRIDE (SM750_DRM_CURSOR_WIDTH * 2 / 8)
#define SM750_DRM_CURSOR_SIZE \
	(SM750_DRM_CURSOR_STRIDE * SM750_DRM_CURSOR_HEIGHT)
#define SM750_DRM_DMA_STAGING_SIZE (2048 * sizeof(u32))
#define SM750_DRM_DMA_TEST_SIZE 256
#define SM750_DRM_DMA_TIMEOUT_US 2000
#define SM750_DRM_DMA_GUARD_WORDS 4
#define SM750_DRM_SHARPEN_PERCENT 8
#define SM750_DRM_HWC_ADDRESS PANEL_HWC_ADDRESS
#define SM750_DRM_HWC_ADDRESS_ENABLE PANEL_HWC_ADDRESS_ENABLE
#define SM750_DRM_HWC_ADDRESS_MASK PANEL_HWC_ADDRESS_ADDRESS_MASK
#define SM750_DRM_HWC_LOCATION PANEL_HWC_LOCATION
#define SM750_DRM_HWC_LOCATION_TOP PANEL_HWC_LOCATION_TOP
#define SM750_DRM_HWC_LOCATION_LEFT PANEL_HWC_LOCATION_LEFT
#define SM750_DRM_HWC_LOCATION_Y_MASK PANEL_HWC_LOCATION_Y_MASK
#define SM750_DRM_HWC_LOCATION_X_MASK PANEL_HWC_LOCATION_X_MASK
#define SM750_DRM_HWC_COLOR_12 PANEL_HWC_COLOR_12
#define SM750_DRM_HWC_COLOR_3 PANEL_HWC_COLOR_3
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
#define SM750_VBLANK_RETRY_NS NSEC_PER_MSEC
#define SM750_VBLANK_MIN_DELAY_NS 50000ULL
#endif

void __iomem *mmio750;
bool sm750_test_reduced_pll_ratio;
bool sm750_test_low_vco_pll;
int sm750_test_panel_fifo = -1;
int sm750_test_refresh_centihz;

static int dvo_clock_phase = -1;
module_param(dvo_clock_phase, int, 0444);
MODULE_PARM_DESC(dvo_clock_phase,
	"DVO clock phase: -1=board option-ROM strap, 0=rising, 1=falling");

static int sii9024_scl = 12;
static int sii9024_sda = 13;
module_param(sii9024_scl, int, 0444);
module_param(sii9024_sda, int, 0444);
MODULE_PARM_DESC(sii9024_scl, "SiI9024A software-I2C SCL GPIO");
MODULE_PARM_DESC(sii9024_sda, "SiI9024A software-I2C SDA GPIO");

static unsigned int preferred_width;
static unsigned int preferred_height;
static unsigned int preferred_refresh;
module_param(preferred_width, uint, 0444);
module_param(preferred_height, uint, 0444);
module_param(preferred_refresh, uint, 0444);
MODULE_PARM_DESC(preferred_width, "Optional preferred mode width in pixels");
MODULE_PARM_DESC(preferred_height, "Optional preferred mode height in lines");
MODULE_PARM_DESC(preferred_refresh, "Optional preferred mode refresh in Hz");

#ifdef SM750_DRM_DEFAULT_XRGB8888
#define SM750_DRM_DEFAULT_SCANOUT_FORMAT "xrgb8888"
#elif defined(SM750_DRM_DEFAULT_RGB565)
#define SM750_DRM_DEFAULT_SCANOUT_FORMAT "rgb565"
#else
#define SM750_DRM_DEFAULT_SCANOUT_FORMAT "rgb565-bbdither"
#endif

#ifndef SM750_DRM_DEFAULT_EDID_ONLY
#define SM750_DRM_DEFAULT_EDID_ONLY 1
#endif
#ifndef SM750_DRM_DEFAULT_SOFTSCALE_WIDE
#define SM750_DRM_DEFAULT_SOFTSCALE_WIDE 0
#endif
#ifndef SM750_DRM_DEFAULT_SHARPEN
#define SM750_DRM_DEFAULT_SHARPEN 0
#endif
#ifndef SM750_DRM_DEFAULT_DOUBLE_SHADOW
#define SM750_DRM_DEFAULT_DOUBLE_SHADOW 0
#endif
#ifndef SM750_DRM_DEFAULT_DISABLE_HARDWARE_CURSOR
#define SM750_DRM_DEFAULT_DISABLE_HARDWARE_CURSOR 0
#endif
#ifndef SM750_DRM_DEFAULT_DISABLE_DMA
#define SM750_DRM_DEFAULT_DISABLE_DMA 0
#endif

static char *scanout_format = SM750_DRM_DEFAULT_SCANOUT_FORMAT;
static unsigned int dither_green_gain = 94;
static bool edid_only = SM750_DRM_DEFAULT_EDID_ONLY;
static bool softscale_wide = SM750_DRM_DEFAULT_SOFTSCALE_WIDE;
static bool sharpen = SM750_DRM_DEFAULT_SHARPEN;
static bool double_shadow = SM750_DRM_DEFAULT_DOUBLE_SHADOW;
static bool disable_hardware_cursor =
	SM750_DRM_DEFAULT_DISABLE_HARDWARE_CURSOR;
static bool disable_dma = SM750_DRM_DEFAULT_DISABLE_DMA;
static unsigned int shadow_dma_min_bytes = 256;
module_param(scanout_format, charp, 0444);
module_param(dither_green_gain, uint, 0444);
module_param(edid_only, bool, 0444);
module_param(softscale_wide, bool, 0444);
module_param(sharpen, bool, 0444);
module_param(double_shadow, bool, 0444);
module_param(disable_hardware_cursor, bool, 0444);
module_param(disable_dma, bool, 0444);
module_param(shadow_dma_min_bytes, uint, 0444);
MODULE_PARM_DESC(scanout_format,
	"Scanout backend: xrgb8888, rgb565, or rgb565-bbdither");
MODULE_PARM_DESC(dither_green_gain,
	"RGB565 bbdither green gain percentage (0-100, default 94)");
MODULE_PARM_DESC(edid_only, "Expose EDID modes instead of the driver catalogue");
MODULE_PARM_DESC(softscale_wide,
	"Enable logical 2464/2560x1080 modes when edid_only=0");
MODULE_PARM_DESC(sharpen, "Enable fixed 8% post-softscale sharpening");
MODULE_PARM_DESC(double_shadow,
	"Enable source snapshots and difference-based damage trimming");
MODULE_PARM_DESC(disable_hardware_cursor,
	"Disable the hardware cursor plane and use software cursor rendering");
MODULE_PARM_DESC(disable_dma, "Force CPU shadow uploads instead of DMA1");
MODULE_PARM_DESC(shadow_dma_min_bytes,
	"Minimum aligned shadow span for DMA (default 256 bytes)");

struct sm750_drm_device {
	struct drm_device drm;
	struct pci_dev *pdev;
	struct drm_simple_display_pipe pipe;
	struct drm_plane cursor_plane;
	struct drm_connector connector;
	struct mutex mode_lock;
	struct mutex shadow_lock;
	struct mutex cursor_lock;
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
	struct hrtimer vblank_timer;
	spinlock_t vblank_lock;
#endif
	void __iomem *regs;
	void __iomem *vram;
	resource_size_t vram_base;
	u32 vram_size;
	u32 scanout_pitch;
	u32 softscale_source_width;
	struct sm750_dither *dither;
	struct sm750_dither_scale_map *dither_scale_map;
	u32 *dither_source_line;
	u32 *shadow_source_snapshot;
	u32 *softscale_output_line;
	u32 *xrgb_output_line;
	u32 *cursor_source;
	u8 *cursor_image;
	void *dma_staging;
	dma_addr_t dma_staging_address;
	u16 *dither_output_line;
	u32 cursor_offset;
	u32 dma_master_base;
	u32 dma_source_address;
	u32 shadow_source_height;
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
	u64 vblank_line_ns;
	u64 vblank_frame_ns;
	u16 vblank_start;
	u16 vblank_vtotal;
	bool vblank_timer_enabled;
#endif
	bool phase_falling;
	bool shadow_scanout;
	bool rgb565;
	bool bbdither;
	bool softscale_active;
	bool shadow_source_snapshot_valid;
	bool hardware_cursor;
	bool shadow_dma_enabled;
	bool shadow_dma_broken;
};

#define to_sm750_drm(drm_dev) container_of(drm_dev, struct sm750_drm_device, drm)
#define connector_to_sm750(conn) \
	container_of(conn, struct sm750_drm_device, connector)
#define pipe_to_sm750(display_pipe) \
	container_of(display_pipe, struct sm750_drm_device, pipe)

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
static u64 sm750_vblank_first_delay_locked(struct sm750_drm_device *sdev)
{
	u32 line;
	u64 lines;

	if (!(peek32(SM750_DRM_DISPLAY_CTRL) & DISPLAY_CTRL_TIMING) ||
	    !sdev->vblank_line_ns || !sdev->vblank_vtotal)
		return SM750_VBLANK_RETRY_NS;

	line = peek32(SM750_DRM_CURRENT_LINE) & SM750_DRM_CURRENT_LINE_MASK;
	if (line >= sdev->vblank_vtotal)
		return SM750_VBLANK_RETRY_NS;

	if (line < sdev->vblank_start)
		lines = sdev->vblank_start - line;
	else
		lines = sdev->vblank_vtotal - line +
			sdev->vblank_start;

	return max_t(u64, lines * sdev->vblank_line_ns,
		     SM750_VBLANK_MIN_DELAY_NS);
}

static enum hrtimer_restart sm750_vblank_timer_fn(struct hrtimer *timer)
{
	struct sm750_drm_device *sdev =
		container_of(timer, struct sm750_drm_device, vblank_timer);
	unsigned long flags;

	spin_lock_irqsave(&sdev->vblank_lock, flags);
	if (!sdev->vblank_timer_enabled) {
		spin_unlock_irqrestore(&sdev->vblank_lock, flags);
		return HRTIMER_NORESTART;
	}
	hrtimer_forward_now(timer, ns_to_ktime(sdev->vblank_frame_ns));
	spin_unlock_irqrestore(&sdev->vblank_lock, flags);

	drm_crtc_handle_vblank(&sdev->pipe.crtc);
	return HRTIMER_RESTART;
}

static void sm750_vblank_timer_stop(struct sm750_drm_device *sdev,
				    bool wait)
{
	unsigned long flags;

	spin_lock_irqsave(&sdev->vblank_lock, flags);
	sdev->vblank_timer_enabled = false;
	spin_unlock_irqrestore(&sdev->vblank_lock, flags);
	if (wait)
		hrtimer_cancel(&sdev->vblank_timer);
	else
		hrtimer_try_to_cancel(&sdev->vblank_timer);
}
#endif

static const struct drm_display_mode sm750_standard_modes[] = {
	/* VESA CVT-RBv2: 59.94, 60, 70, 72 and 75 Hz. */
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 21362,
		640, 648, 680, 720, 0, 480, 481, 489, 495, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 21384,
		640, 648, 680, 720, 0, 480, 481, 489, 495, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 24998,
		640, 648, 680, 720, 0, 480, 482, 490, 496, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 25764,
		640, 648, 680, 720, 0, 480, 483, 491, 497, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 26892,
		640, 648, 680, 720, 0, 480, 484, 492, 498, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 32597,
		800, 808, 840, 880, 0, 600, 604, 612, 618, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 32630,
		800, 808, 840, 880, 0, 600, 604, 612, 618, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 38192,
		800, 808, 840, 880, 0, 600, 606, 614, 620, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 39346,
		800, 808, 840, 880, 0, 600, 607, 615, 621, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 41052,
		800, 808, 840, 880, 0, 600, 608, 616, 622, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 52277,
		1024, 1032, 1064, 1104, 0, 768, 776, 784, 790, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
		52329, 1024, 1032, 1064, 1104, 0, 768, 776, 784, 790, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 61360,
		1024, 1032, 1064, 1104, 0, 768, 780, 788, 794, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 63192,
		1024, 1032, 1064, 1104, 0, 768, 781, 789, 795, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 65908,
		1024, 1032, 1064, 1104, 0, 768, 782, 790, 796, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1152x864", DRM_MODE_TYPE_DRIVER, 65649,
		1152, 1160, 1192, 1232, 0, 864, 875, 883, 889, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1152x864", DRM_MODE_TYPE_DRIVER, 65714,
		1152, 1160, 1192, 1232, 0, 864, 875, 883, 889, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1152x864", DRM_MODE_TYPE_DRIVER, 77012,
		1152, 1160, 1192, 1232, 0, 864, 879, 887, 893, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1152x864", DRM_MODE_TYPE_DRIVER, 79301,
		1152, 1160, 1192, 1232, 0, 864, 880, 888, 894, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1152x864", DRM_MODE_TYPE_DRIVER, 82697,
		1152, 1160, 1192, 1232, 0, 864, 881, 889, 895, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 60405,
		1280, 1288, 1320, 1360, 0, 720, 727, 735, 741, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 60465,
		1280, 1288, 1320, 1360, 0, 720, 727, 735, 741, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 70828,
		1280, 1288, 1320, 1360, 0, 720, 730, 738, 744, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 72950,
		1280, 1288, 1320, 1360, 0, 720, 731, 739, 745, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 76092,
		1280, 1288, 1320, 1360, 0, 720, 732, 740, 746, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x800", DRM_MODE_TYPE_DRIVER, 67089,
		1280, 1288, 1320, 1360, 0, 800, 809, 817, 823, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x800", DRM_MODE_TYPE_DRIVER, 67156,
		1280, 1288, 1320, 1360, 0, 800, 809, 817, 823, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x800", DRM_MODE_TYPE_DRIVER, 78730,
		1280, 1288, 1320, 1360, 0, 800, 813, 821, 827, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x800", DRM_MODE_TYPE_DRIVER, 81077,
		1280, 1288, 1320, 1360, 0, 800, 814, 822, 828, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x800", DRM_MODE_TYPE_DRIVER, 84558,
		1280, 1288, 1320, 1360, 0, 800, 815, 823, 829, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x1024", DRM_MODE_TYPE_DRIVER, 85920,
		1280, 1288, 1320, 1360, 0, 1024, 1040, 1048, 1054, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x1024", DRM_MODE_TYPE_DRIVER, 86006,
		1280, 1288, 1320, 1360, 0, 1024, 1040, 1048, 1054, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x1024", DRM_MODE_TYPE_DRIVER, 100816,
		1280, 1288, 1320, 1360, 0, 1024, 1045, 1053, 1059, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x1024", DRM_MODE_TYPE_DRIVER, 103795,
		1280, 1288, 1320, 1360, 0, 1024, 1046, 1054, 1060, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1280x1024", DRM_MODE_TYPE_DRIVER, 108221,
		1280, 1288, 1320, 1360, 0, 1024, 1047, 1055, 1061, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1366x768", DRM_MODE_TYPE_DRIVER, 68471,
		1366, 1374, 1406, 1446, 0, 768, 776, 784, 790, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1366x768", DRM_MODE_TYPE_DRIVER, 68540,
		1366, 1374, 1406, 1446, 0, 768, 776, 784, 790, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1366x768", DRM_MODE_TYPE_DRIVER, 80368,
		1366, 1374, 1406, 1446, 0, 768, 780, 788, 794, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1366x768", DRM_MODE_TYPE_DRIVER, 82769,
		1366, 1374, 1406, 1446, 0, 768, 781, 789, 795, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1366x768", DRM_MODE_TYPE_DRIVER, 86326,
		1366, 1374, 1406, 1446, 0, 768, 782, 790, 796, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1600x900", DRM_MODE_TYPE_DRIVER, 93247,
		1600, 1608, 1640, 1680, 0, 900, 912, 920, 926, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1600x900", DRM_MODE_TYPE_DRIVER, 93340,
		1600, 1608, 1640, 1680, 0, 900, 912, 920, 926, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1600x900", DRM_MODE_TYPE_DRIVER, 109367,
		1600, 1608, 1640, 1680, 0, 900, 916, 924, 930, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1600x900", DRM_MODE_TYPE_DRIVER, 112613,
		1600, 1608, 1640, 1680, 0, 900, 917, 925, 931, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1600x900", DRM_MODE_TYPE_DRIVER, 117558,
		1600, 1608, 1640, 1680, 0, 900, 919, 927, 933, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1680x1050", DRM_MODE_TYPE_DRIVER, 113934,
		1680, 1688, 1720, 1760, 0, 1050, 1066, 1074, 1080, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1680x1050", DRM_MODE_TYPE_DRIVER, 114048,
		1680, 1688, 1720, 1760, 0, 1050, 1066, 1074, 1080, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1680x1050", DRM_MODE_TYPE_DRIVER, 133672,
		1680, 1688, 1720, 1760, 0, 1050, 1071, 1079, 1085, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1680x1050", DRM_MODE_TYPE_DRIVER, 137617,
		1680, 1688, 1720, 1760, 0, 1050, 1072, 1080, 1086, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1680x1050", DRM_MODE_TYPE_DRIVER, 143616,
		1680, 1688, 1720, 1760, 0, 1050, 1074, 1082, 1088, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
};

static const struct drm_display_mode sm750_custom_modes[] = {
	/* 2048x1080 CVT-RBv2 timings explicitly enabled by driver policy. */
	{ DRM_MODE("2048x1080", DRM_MODE_TYPE_DRIVER, 118210,
		2048, 2056, 2088, 2128, 0, 1080, 1097, 1105, 1111, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x1080", DRM_MODE_TYPE_DRIVER, 141710,
		2048, 2056, 2088, 2128, 0, 1080, 1097, 1105, 1111, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x1080", DRM_MODE_TYPE_DRIVER, 141852,
		2048, 2056, 2088, 2128, 0, 1080, 1097, 1105, 1111, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x1080", DRM_MODE_TYPE_DRIVER, 166239,
		2048, 2056, 2088, 2128, 0, 1080, 1102, 1110, 1116, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x1080", DRM_MODE_TYPE_DRIVER, 171142,
		2048, 2056, 2088, 2128, 0, 1080, 1103, 1111, 1117, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x1080", DRM_MODE_TYPE_DRIVER, 178592,
		2048, 2056, 2088, 2128, 0, 1080, 1105, 1113, 1119, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* CTA-861 50/59.94/60 Hz, then VESA CVT-RBv2. */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500,
		1920, 2448, 2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148352,
		1920, 2008, 2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500,
		1920, 2008, 2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 156240,
		1920, 1928, 1960, 2000, 0, 1080, 1102, 1110, 1116, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 160848,
		1920, 1928, 1960, 2000, 0, 1080, 1103, 1111, 1117, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1920x810", DRM_MODE_TYPE_DRIVER, 99860,
		1920, 1928, 1960, 2000, 0, 810, 819, 827, 833, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1920x810", DRM_MODE_TYPE_DRIVER, 99959,
		1920, 1928, 1960, 2000, 0, 810, 819, 827, 833, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1920x810", DRM_MODE_TYPE_DRIVER, 117180,
		1920, 1928, 1960, 2000, 0, 810, 823, 831, 837, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1920x810", DRM_MODE_TYPE_DRIVER, 120672,
		1920, 1928, 1960, 2000, 0, 810, 824, 832, 838, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("1920x810", DRM_MODE_TYPE_DRIVER, 125849,
		1920, 1928, 1960, 2000, 0, 810, 825, 833, 839, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x864", DRM_MODE_TYPE_DRIVER, 113394,
		2048, 2056, 2088, 2128, 0, 864, 875, 883, 889, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x864", DRM_MODE_TYPE_DRIVER, 113507,
		2048, 2056, 2088, 2128, 0, 864, 875, 883, 889, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x864", DRM_MODE_TYPE_DRIVER, 133021,
		2048, 2056, 2088, 2128, 0, 864, 879, 887, 893, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x864", DRM_MODE_TYPE_DRIVER, 136975,
		2048, 2056, 2088, 2128, 0, 864, 880, 888, 894, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x864", DRM_MODE_TYPE_DRIVER, 142842,
		2048, 2056, 2088, 2128, 0, 864, 881, 889, 895, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x1024", DRM_MODE_TYPE_DRIVER, 134440,
		2048, 2056, 2088, 2128, 0, 1024, 1040, 1048, 1054, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x1024", DRM_MODE_TYPE_DRIVER, 134574,
		2048, 2056, 2088, 2128, 0, 1024, 1040, 1048, 1054, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x1024", DRM_MODE_TYPE_DRIVER, 157748,
		2048, 2056, 2088, 2128, 0, 1024, 1045, 1053, 1059, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x1024", DRM_MODE_TYPE_DRIVER, 162408,
		2048, 2056, 2088, 2128, 0, 1024, 1046, 1054, 1060, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x1152", DRM_MODE_TYPE_DRIVER, 151149,
		2048, 2056, 2088, 2128, 0, 1152, 1171, 1179, 1185, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2048x1152", DRM_MODE_TYPE_DRIVER, 151300,
		2048, 2056, 2088, 2128, 0, 1152, 1171, 1179, 1185, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
};

static const struct drm_display_mode sm750_safe_1920_modes[] = {
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500,
		1920, 2448, 2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148352,
		1920, 2008, 2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500,
		1920, 2008, 2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
};

static const struct drm_display_mode sm750_wide_modes[] = {
	/* Logical modes; hardware output is substituted with 2048x1080. */
	{ DRM_MODE("2464x1080", DRM_MODE_TYPE_DRIVER, 145763,
		2464, 2512, 2544, 2624, 0, 1080, 1097, 1105, 1111, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2464x1080", DRM_MODE_TYPE_DRIVER, 174741,
		2464, 2512, 2544, 2624, 0, 1080, 1097, 1105, 1111, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2464x1080", DRM_MODE_TYPE_DRIVER, 174915,
		2464, 2512, 2544, 2624, 0, 1080, 1097, 1105, 1111, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2464x1080", DRM_MODE_TYPE_DRIVER, 204986,
		2464, 2512, 2544, 2624, 0, 1080, 1102, 1110, 1116, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2464x1080", DRM_MODE_TYPE_DRIVER, 211032,
		2464, 2512, 2544, 2624, 0, 1080, 1103, 1111, 1117, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2464x1080", DRM_MODE_TYPE_DRIVER, 220219,
		2464, 2512, 2544, 2624, 0, 1080, 1105, 1113, 1119, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2560x1080", DRM_MODE_TYPE_DRIVER, 151096,
		2560, 2608, 2640, 2720, 0, 1080, 1097, 1105, 1111, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2560x1080", DRM_MODE_TYPE_DRIVER, 181134,
		2560, 2608, 2640, 2720, 0, 1080, 1097, 1105, 1111, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2560x1080", DRM_MODE_TYPE_DRIVER, 181315,
		2560, 2608, 2640, 2720, 0, 1080, 1097, 1105, 1111, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2560x1080", DRM_MODE_TYPE_DRIVER, 212486,
		2560, 2608, 2640, 2720, 0, 1080, 1102, 1110, 1116, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2560x1080", DRM_MODE_TYPE_DRIVER, 218753,
		2560, 2608, 2640, 2720, 0, 1080, 1103, 1111, 1117, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	{ DRM_MODE("2560x1080", DRM_MODE_TYPE_DRIVER, 228275,
		2560, 2608, 2640, 2720, 0, 1080, 1105, 1113, 1119, 0,
		DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
};

static bool sm750_mode_is_softscaled(const struct drm_display_mode *mode)
{
	return !edid_only && softscale_wide &&
		(mode->hdisplay == 2464 || mode->hdisplay == 2560) &&
		mode->vdisplay == 1080 &&
		(drm_mode_vrefresh(mode) == 50 ||
		 drm_mode_vrefresh(mode) == 60 ||
		 drm_mode_vrefresh(mode) == 70 ||
		 drm_mode_vrefresh(mode) == 72 ||
		 drm_mode_vrefresh(mode) == 75);
}

static int sm750_softscale_output_mode(const struct drm_display_mode *logical,
				       struct drm_display_mode *output)
{
	unsigned int refresh;

	*output = *logical;
	if (!sm750_mode_is_softscaled(logical))
		return 0;

	refresh = drm_mode_vrefresh(logical);
	output->hdisplay = 2048;
	output->hsync_start = 2056;
	output->hsync_end = 2088;
	output->htotal = 2128;
	if (refresh == 50) {
		output->clock = 118210;
		output->vsync_start = 1097;
		output->vsync_end = 1105;
		output->vtotal = 1111;
	} else if (logical->clock == 174741 || logical->clock == 181134) {
		output->clock = 141710;
		output->vsync_start = 1097;
		output->vsync_end = 1105;
		output->vtotal = 1111;
	} else if (refresh == 60) {
		output->clock = 141852;
		output->vsync_start = 1097;
		output->vsync_end = 1105;
		output->vtotal = 1111;
	} else if (refresh == 70) {
		output->clock = 166239;
		output->vsync_start = 1102;
		output->vsync_end = 1110;
		output->vtotal = 1116;
	} else if (refresh == 72) {
		output->clock = 171142;
		output->vsync_start = 1103;
		output->vsync_end = 1111;
		output->vtotal = 1117;
	} else if (refresh == 75) {
		output->clock = 178592;
		output->vsync_start = 1105;
		output->vsync_end = 1113;
		output->vtotal = 1119;
	} else {
		return -EINVAL;
	}
	drm_mode_set_name(output);
	return 1;
}

static int sm750_add_modes(struct drm_connector *connector,
			   const struct drm_display_mode *modes,
			   size_t count, bool clear_preferred)
{
	struct drm_display_mode *mode;
	size_t i;
	int added = 0;

	for (i = 0; i < count; i++) {
		mode = drm_mode_duplicate(connector->dev, &modes[i]);
		if (!mode)
			continue;
		if (clear_preferred)
			mode->type &= ~DRM_MODE_TYPE_PREFERRED;
		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
		added++;
	}

	return added;
}

static void sm750_apply_preferred_mode(struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *preferred = NULL;
	unsigned int supplied;

	supplied = !!preferred_width + !!preferred_height + !!preferred_refresh;
	if (!supplied)
		return;
	if (supplied != 3) {
		drm_warn(connector->dev,
			 "preferred_width, preferred_height and preferred_refresh must all be set\n");
		return;
	}

	list_for_each_entry(mode, &connector->probed_modes, head) {
		if (mode->hdisplay == preferred_width &&
		    mode->vdisplay == preferred_height &&
		    drm_mode_vrefresh(mode) == preferred_refresh)
			preferred = mode;
	}
	if (!preferred) {
		drm_warn(connector->dev,
			 "requested preferred mode %ux%u@%u is unavailable\n",
			 preferred_width, preferred_height, preferred_refresh);
		return;
	}

	list_for_each_entry(mode, &connector->probed_modes, head)
		mode->type &= ~DRM_MODE_TYPE_PREFERRED;
	preferred->type |= DRM_MODE_TYPE_PREFERRED;
	drm_info(connector->dev, "preferred mode overridden to %ux%u@%u\n",
		 preferred_width, preferred_height, preferred_refresh);
}

static struct edid *sm750_connector_read_edid(struct drm_connector *connector)
{
	struct sm750_drm_device *sdev = connector_to_sm750(connector);
	struct edid *edid;
	u8 checksum;
	unsigned int i;
	int ret;

	edid = kmalloc(256, GFP_KERNEL);
	if (!edid)
		return NULL;
	mutex_lock(&sdev->mode_lock);
	ret = sm750_sii902x_read_edid(&sdev->pdev->dev, (u8 *)edid, 256);
	mutex_unlock(&sdev->mode_lock);
	if (ret) {
		kfree(edid);
		drm_connector_update_edid_property(connector, NULL);
		drm_dbg_kms(connector->dev,
			    "monitor identity EDID unavailable: %d\n", ret);
		return NULL;
	}

	/* The transmitter helper reads at most one extension. */
	if (edid->extensions > 1) {
		edid->extensions = 1;
		edid->checksum = 0;
		checksum = 0;
		for (i = 0; i < 128; i++)
			checksum += ((u8 *)edid)[i];
		edid->checksum = -checksum;
	}

	ret = drm_connector_update_edid_property(connector, edid);
	if (ret)
		drm_warn(connector->dev,
			 "failed to publish monitor identity EDID: %d\n", ret);
	return edid;
}

static bool sm750_connector_has_resolution(struct drm_connector *connector,
					   unsigned int width,
					   unsigned int height)
{
	struct drm_display_mode *mode;

	list_for_each_entry(mode, &connector->probed_modes, head)
		if (mode->hdisplay == width && mode->vdisplay == height)
			return true;
	return false;
}

static int sm750_connector_get_modes(struct drm_connector *connector)
{
	struct edid *edid;
	int count = 0;

	edid = sm750_connector_read_edid(connector);
	if (edid_only && edid)
		count = drm_add_edid_modes(connector, edid);

	if (edid_only && count > 0) {
		if (softscale_wide &&
		    !sm750_connector_has_resolution(connector, 1920, 1080))
			count += sm750_add_modes(connector, sm750_safe_1920_modes,
				ARRAY_SIZE(sm750_safe_1920_modes), true);
	} else {
		count = sm750_add_modes(connector, sm750_standard_modes,
				ARRAY_SIZE(sm750_standard_modes), false);
		if (!edid_only) {
			count += sm750_add_modes(connector, sm750_custom_modes,
				ARRAY_SIZE(sm750_custom_modes), true);
			if (softscale_wide)
				count += sm750_add_modes(connector, sm750_wide_modes,
					ARRAY_SIZE(sm750_wide_modes), true);
		}
	}
	kfree(edid);
	sm750_apply_preferred_mode(connector);
	return count;
}

static enum drm_connector_status
sm750_connector_detect(struct drm_connector *connector, bool force)
{
	struct sm750_drm_device *sdev = connector_to_sm750(connector);
	bool connected = false;
	bool receiver = false;
	bool event = false;
	int ret;

	mutex_lock(&sdev->mode_lock);
	ret = sm750_sii902x_get_link_status(&connected, &receiver, &event, false);
	mutex_unlock(&sdev->mode_lock);
	if (ret)
		return connector_status_unknown;

	return (connected || receiver) ? connector_status_connected :
		connector_status_disconnected;
}

static bool sm750_mode_has_same_timing(const struct drm_display_mode *mode,
				       const struct drm_display_mode *candidate)
{
	return mode->clock == candidate->clock &&
		mode->hdisplay == candidate->hdisplay &&
		mode->hsync_start == candidate->hsync_start &&
		mode->hsync_end == candidate->hsync_end &&
		mode->htotal == candidate->htotal &&
		mode->vdisplay == candidate->vdisplay &&
		mode->vsync_start == candidate->vsync_start &&
		mode->vsync_end == candidate->vsync_end &&
		mode->vtotal == candidate->vtotal &&
		mode->flags == candidate->flags;
}

static bool sm750_mode_is_catalogued(const struct drm_display_mode *mode)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(sm750_standard_modes); i++)
		if (sm750_mode_has_same_timing(mode, &sm750_standard_modes[i]))
			return true;

	for (i = 0; i < ARRAY_SIZE(sm750_custom_modes); i++)
		if (sm750_mode_has_same_timing(mode, &sm750_custom_modes[i]))
			return true;
	for (i = 0; i < ARRAY_SIZE(sm750_safe_1920_modes); i++)
		if (sm750_mode_has_same_timing(mode, &sm750_safe_1920_modes[i]))
			return true;
	if (softscale_wide)
		for (i = 0; i < ARRAY_SIZE(sm750_wide_modes); i++)
			if (sm750_mode_has_same_timing(mode, &sm750_wide_modes[i]))
				return true;

	return false;
}

static enum drm_mode_status
sm750_mode_valid(const struct drm_display_mode *mode, u32 vram_size)
{
	struct drm_display_mode output_mode;
	const struct drm_display_mode *hardware_mode = mode;
	struct pll_value pll = {
		.clock_type = SM750_DRM_PLL,
		.input_freq = DEFAULT_INPUT_CLOCK,
	};
	u64 size;
	int ret;

	if (mode->flags & (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN))
		return MODE_NO_INTERLACE;
	if (!edid_only && !sm750_mode_is_catalogued(mode))
		return MODE_BAD;
	ret = sm750_softscale_output_mode(mode, &output_mode);
	if (ret < 0)
		return MODE_BAD;
	if (ret > 0)
		hardware_mode = &output_mode;
	if (!hardware_mode->clock ||
	    hardware_mode->clock > SM750_DRM_MAX_CLOCK_KHZ)
		return MODE_CLOCK_HIGH;
	if (hardware_mode->hdisplay > SM750_DRM_PHYSICAL_MAX_WIDTH)
		return MODE_BAD_HVALUE;
	if (hardware_mode->vdisplay > SM750_DRM_MAX_HEIGHT)
		return MODE_BAD_VVALUE;
	if (hardware_mode->hdisplay > hardware_mode->hsync_start ||
	    hardware_mode->hsync_start >= hardware_mode->hsync_end ||
	    hardware_mode->hsync_end > hardware_mode->htotal)
		return MODE_H_ILLEGAL;
	if (hardware_mode->vdisplay > hardware_mode->vsync_start ||
	    hardware_mode->vsync_start >= hardware_mode->vsync_end ||
	    hardware_mode->vsync_end > hardware_mode->vtotal)
		return MODE_V_ILLEGAL;
	if (!sm750_calc_pll_value(hardware_mode->clock * 1000, &pll))
		return MODE_CLOCK_RANGE;

	size = (u64)ALIGN(mode->hdisplay * 4, SM750_DRM_LINE_ALIGN) *
		mode->vdisplay;
	if (size > vram_size)
		return MODE_MEM;

	return MODE_OK;
}

static enum drm_mode_status
sm750_connector_mode_valid(struct drm_connector *connector,
			   const struct drm_display_mode *mode)
{
	return sm750_mode_valid(mode, connector_to_sm750(connector)->vram_size);
}

static const struct drm_connector_helper_funcs sm750_connector_helper_funcs = {
	.get_modes = sm750_connector_get_modes,
	.mode_valid = sm750_connector_mode_valid,
};

static const struct drm_connector_funcs sm750_connector_funcs = {
	.detect = sm750_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void sm750_mode_to_fb_var(struct fb_var_screeninfo *var,
				 const struct drm_display_mode *mode,
				 unsigned int bits_per_pixel)
{
	memset(var, 0, sizeof(*var));
	var->xres = mode->hdisplay;
	var->xres_virtual = mode->hdisplay;
	var->yres = mode->vdisplay;
	var->yres_virtual = mode->vdisplay;
	var->bits_per_pixel = bits_per_pixel;
	var->pixclock = div64_u64(1000000000ULL + mode->clock / 2,
				 mode->clock);
	var->right_margin = mode->hsync_start - mode->hdisplay;
	var->hsync_len = mode->hsync_end - mode->hsync_start;
	var->left_margin = mode->htotal - mode->hsync_end;
	var->lower_margin = mode->vsync_start - mode->vdisplay;
	var->vsync_len = mode->vsync_end - mode->vsync_start;
	var->upper_margin = mode->vtotal - mode->vsync_end;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		var->sync |= FB_SYNC_HOR_HIGH_ACT;
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		var->sync |= FB_SYNC_VERT_HIGH_ACT;
}

static int sm750_program_mode(struct sm750_drm_device *sdev,
			      const struct drm_display_mode *drm_mode,
			      u32 pitch, u32 offset, u8 cpp)
{
	struct drm_display_mode output_mode;
	struct sm750_scanout_mode mode;
	struct mode_parameter timing;
	struct fb_var_screeninfo var;
	u32 control;
	u32 line_width;
	u32 width;
	int ret;

	ret = sm750_softscale_output_mode(drm_mode, &output_mode);
	if (ret < 0)
		return ret;
	ret = sm750_mode_from_drm(&mode, &output_mode, pitch, offset, cpp);
	if (ret)
		return ret;

	memset(&timing, 0, sizeof(timing));
	timing.pixel_clock = mode.clock_khz * 1000UL;
	timing.horizontal_display_end = mode.hdisplay;
	timing.horizontal_sync_start = mode.hsync_start;
	timing.horizontal_sync_width = mode.hsync_end - mode.hsync_start;
	timing.horizontal_total = mode.htotal;
	timing.vertical_display_end = mode.vdisplay;
	timing.vertical_sync_start = mode.vsync_start;
	timing.vertical_sync_height = mode.vsync_end - mode.vsync_start;
	timing.vertical_total = mode.vtotal;
	timing.horizontal_sync_polarity =
		(mode.flags & DRM_MODE_FLAG_PHSYNC) ? POS : NEG;
	timing.vertical_sync_polarity =
		(mode.flags & DRM_MODE_FLAG_PVSYNC) ? POS : NEG;
	timing.clock_phase_polarity = sdev->phase_falling ? NEG : POS;
	timing.horizontal_frequency = DIV_ROUND_CLOSEST_ULL(
		timing.pixel_clock, timing.horizontal_total);
	timing.vertical_frequency = DIV_ROUND_CLOSEST_ULL(
		timing.horizontal_frequency, timing.vertical_total);

	sm750_mode_to_fb_var(&var, &output_mode, cpp * 8);
	ret = sm750_sii902x_begin_mode(&sdev->pdev->dev, false,
				       sii9024_scl, sii9024_sda);
	if (ret)
		return ret;
	ret = ddk750_set_display_control(SM750_DRM_CONTROLLER, false);
	if (ret)
		return ret;
	ret = ddk750_set_mode_timing(&timing, SM750_DRM_PLL, true);
	if (ret)
		return ret;

	poke32(SM750_DRM_FB_ADDRESS, SM750_DRM_FB_ADDRESS_STATUS |
	       (offset & SM750_DRM_FB_ADDRESS_MASK));
	line_width = ALIGN(mode.hdisplay * mode.cpp, SM750_DRM_LINE_ALIGN);
	width = ((line_width << SM750_DRM_FB_WIDTH_WIDTH_SHIFT) &
		 SM750_DRM_FB_WIDTH_WIDTH_MASK) |
		(pitch & SM750_DRM_FB_WIDTH_OFFSET_MASK);
	poke32(SM750_DRM_FB_WIDTH, width);
	poke32(PANEL_WINDOW_WIDTH,
	       ((mode.hdisplay - 1) << PANEL_WINDOW_WIDTH_WIDTH_SHIFT) &
	       PANEL_WINDOW_WIDTH_WIDTH_MASK);
	poke32(PANEL_WINDOW_HEIGHT,
	       ((mode.vdisplay - 1) << PANEL_WINDOW_HEIGHT_HEIGHT_SHIFT) &
	       PANEL_WINDOW_HEIGHT_HEIGHT_MASK);
	poke32(PANEL_PLANE_TL, 0);
	poke32(PANEL_PLANE_BR,
	       ((mode.vdisplay - 1) << PANEL_PLANE_BR_BOTTOM_SHIFT) |
	       ((mode.hdisplay - 1) & PANEL_PLANE_BR_RIGHT_MASK));

	control = peek32(SM750_DRM_DISPLAY_CTRL);
	control &= ~SM750_DRM_DISPLAY_CTRL_FORMAT;
	control |= cpp == 2 ? SM750_DRM_DISPLAY_CTRL_FORMAT_16 :
		SM750_DRM_DISPLAY_CTRL_FORMAT_32;
	poke32(SM750_DRM_DISPLAY_CTRL, control);
	ret = ddk750_set_logical_disp_out(SM750_DRM_OUTPUT);
	if (ret)
		return ret;
	return sm750_sii902x_enable(&sdev->pdev->dev, &var, false,
				    sii9024_scl, sii9024_sda);
}

static void sm750_dma_abort(struct sm750_drm_device *sdev)
{
	u32 control = peek32(DMA_ABORT_INTERRUPT);

	poke32(DMA_ABORT_INTERRUPT, control | DMA_ABORT_INTERRUPT_ABORT_1);
	udelay(1);
	poke32(DMA_ABORT_INTERRUPT,
		(control & ~DMA_ABORT_INTERRUPT_ABORT_1) &
		~DMA_ABORT_INTERRUPT_INT_1);
}

static int sm750_dma_transfer(struct sm750_drm_device *sdev,
			      u32 destination, size_t size)
{
	u32 control;
	int ret;

	if (!size || size > SM750_DRM_DMA_STAGING_SIZE ||
	    !IS_ALIGNED(size, sizeof(u32)) ||
	    !IS_ALIGNED(destination, sizeof(u32)) ||
	    destination + size > sdev->vram_size)
		return -EINVAL;

	control = peek32(DMA_ABORT_INTERRUPT);
	control &= ~(DMA_ABORT_INTERRUPT_ABORT_1 |
		     DMA_ABORT_INTERRUPT_INT_1);
	poke32(DMA_ABORT_INTERRUPT, control);
	poke32(PCI_MASTER_BASE,
		sdev->dma_master_base & PCI_MASTER_BASE_ADDRESS_MASK);
	poke32(DMA_1_SOURCE, sdev->dma_source_address);
	poke32(DMA_1_DESTINATION,
		destination & DMA_1_DESTINATION_ADDRESS_MASK);
	dma_wmb();
	poke32(DMA_1_SIZE_CONTROL,
		DMA_1_SIZE_CONTROL_STATUS |
		((size - sizeof(u32)) & DMA_1_SIZE_CONTROL_SIZE_MASK));
	ret = readl_poll_timeout_atomic(sdev->regs + DMA_ABORT_INTERRUPT,
		control, control & DMA_ABORT_INTERRUPT_INT_1, 1,
		SM750_DRM_DMA_TIMEOUT_US);
	if (!ret) {
		/* A zero write acknowledges the completed channel-1 interrupt. */
		poke32(DMA_ABORT_INTERRUPT,
			control & ~DMA_ABORT_INTERRUPT_INT_1);
		return 0;
	} else {
		drm_err(&sdev->drm,
			"DMA1 shadow upload timed out; using CPU copies from now on\n");
	}

	sm750_dma_abort(sdev);
	sm750_enable_dma(0);
	sdev->shadow_dma_broken = true;
	sdev->shadow_dma_enabled = false;
	return ret;
}

static void sm750_shadow_upload(struct sm750_drm_device *sdev,
				u32 destination, const void *source,
				size_t size)
{
	if (sdev->shadow_dma_enabled && size >= shadow_dma_min_bytes &&
	    IS_ALIGNED(destination, sizeof(u32)) &&
	    IS_ALIGNED(size, sizeof(u32))) {
		memcpy(sdev->dma_staging, source, size);
		if (!sm750_dma_transfer(sdev, destination, size))
			return;
	}
	memcpy_toio(sdev->vram + destination, source, size);
}

static int sm750_dma_init(struct sm750_drm_device *sdev)
{
	dma_addr_t address;
	u32 test_destination;
	u32 expected;
	unsigned int i;
	int ret;

	ret = dma_set_mask_and_coherent(&sdev->pdev->dev, DMA_BIT_MASK(31));
	if (ret)
		return ret;
	sdev->dma_staging = dmam_alloc_coherent(&sdev->pdev->dev,
		SM750_DRM_DMA_STAGING_SIZE, &sdev->dma_staging_address,
		GFP_KERNEL);
	if (!sdev->dma_staging)
		return -ENOMEM;
	address = sdev->dma_staging_address;
	if (address > DMA_BIT_MASK(31) || !IS_ALIGNED(address, sizeof(u32)) ||
	    (address & GENMASK_ULL(22, 0)) + SM750_DRM_DMA_STAGING_SIZE >
		BIT_ULL(23))
		return -ERANGE;

	/* Keep DMA address bits 25:23 zero and select their 8 MiB window here. */
	sdev->dma_master_base = (address >> 23) &
		PCI_MASTER_BASE_ADDRESS_MASK;
	sdev->dma_source_address = DMA_1_SOURCE_ADDRESS_EXT |
		(address & GENMASK_ULL(22, 2));
	sm750_enable_dma(1);
	sm750_dma_abort(sdev);

	test_destination = sdev->cursor_offset - SM750_DRM_DMA_TEST_SIZE -
		SM750_DRM_DMA_GUARD_WORDS * sizeof(u32);
	for (i = 0; i < SM750_DRM_DMA_GUARD_WORDS; i++) {
		writel(0x51a70000U + i,
		       sdev->vram + test_destination - sizeof(u32) *
		       (SM750_DRM_DMA_GUARD_WORDS - i));
		writel(0xa75e0000U + i,
		       sdev->vram + test_destination +
		       SM750_DRM_DMA_TEST_SIZE + sizeof(u32) * i);
	}
	for (i = 0; i < SM750_DRM_DMA_TEST_SIZE; i++)
		((u8 *)sdev->dma_staging)[i] = (i * 73U + 19U) & 0xff;
	ret = sm750_dma_transfer(sdev, test_destination,
				 SM750_DRM_DMA_TEST_SIZE);
	if (!ret) {
		for (i = 0; i < SM750_DRM_DMA_TEST_SIZE; i++) {
			if (readb(sdev->vram + test_destination + i) !=
			    (((i * 73U) + 19U) & 0xff)) {
				ret = -EIO;
				break;
			}
		}
	}
	if (!ret) {
		for (i = 0; i < SM750_DRM_DMA_GUARD_WORDS; i++) {
			expected = 0x51a70000U + i;
			if (readl(sdev->vram + test_destination - sizeof(u32) *
				  (SM750_DRM_DMA_GUARD_WORDS - i)) != expected) {
				ret = -EOVERFLOW;
				break;
			}
			expected = 0xa75e0000U + i;
			if (readl(sdev->vram + test_destination +
				  SM750_DRM_DMA_TEST_SIZE + sizeof(u32) * i) !=
			    expected) {
				ret = -EOVERFLOW;
				break;
			}
		}
	}
	memset_io(sdev->vram + test_destination -
		  SM750_DRM_DMA_GUARD_WORDS * sizeof(u32), 0,
		  SM750_DRM_DMA_TEST_SIZE +
		  2 * SM750_DRM_DMA_GUARD_WORDS * sizeof(u32));
	if (ret) {
		sm750_dma_abort(sdev);
		sm750_enable_dma(0);
		sdev->shadow_dma_enabled = false;
		return ret;
	}

	sdev->shadow_dma_broken = false;
	sdev->shadow_dma_enabled = true;
	drm_info(&sdev->drm,
		 "DMA1 shadow uploads enabled after off-screen transfer verification (minimum %u bytes)\n",
		 shadow_dma_min_bytes);
	return 0;
}

static void sm750_dma_stop(void *data)
{
	struct sm750_drm_device *sdev = data;

	sm750_dma_abort(sdev);
	sm750_enable_dma(0);
	sdev->shadow_dma_enabled = false;
}

static void sm750_shadow_rect(struct sm750_drm_device *sdev,
			      struct drm_plane_state *plane_state,
			      const struct drm_rect *rect)
{
	struct drm_shadow_plane_state *shadow =
		to_drm_shadow_plane_state(plane_state);
	const struct iosys_map *src = &shadow->data[0];
	unsigned int width = drm_rect_width(rect);
	unsigned int y1;
	unsigned int y2;
	unsigned int y;

	if (!width || drm_rect_height(rect) <= 0 || iosys_map_is_null(src))
		return;
	y1 = clamp_t(int, rect->y1, 0, sdev->shadow_source_height);
	y2 = clamp_t(int, rect->y2, 0, sdev->shadow_source_height);
	if (y1 >= y2)
		return;

	mutex_lock(&sdev->shadow_lock);
	for (y = y1; y < y2; y++) {
		if (sdev->softscale_active) {
			u32 *snapshot = double_shadow ?
				sdev->shadow_source_snapshot +
				(size_t)y * SM750_DRM_MAX_WIDTH : NULL;
			unsigned int src_x1 = clamp_t(int, rect->x1, 0,
					     sdev->softscale_source_width);
			unsigned int src_x2 = clamp_t(int, rect->x2, 0,
					     sdev->softscale_source_width);
			unsigned int changed_x1;
			unsigned int changed_x2;
			unsigned int dst_x1;
			unsigned int dst_x2;
			size_t src_offset =
				(size_t)y * plane_state->fb->pitches[0];
			size_t dst_offset = (size_t)y * sdev->scanout_pitch;

			if (src_x1 >= src_x2)
				continue;
			iosys_map_memcpy_from(sdev->dither_source_line, src,
					      src_offset,
					      sdev->softscale_source_width * sizeof(u32));

			changed_x1 = src_x1;
			changed_x2 = src_x2;
			if (snapshot && sdev->shadow_source_snapshot_valid) {
				while (changed_x1 < changed_x2 &&
				       sdev->dither_source_line[changed_x1] ==
				       snapshot[changed_x1])
					changed_x1++;
				if (changed_x1 == changed_x2)
					continue;
				while (changed_x2 > changed_x1 &&
				       sdev->dither_source_line[changed_x2 - 1] ==
				       snapshot[changed_x2 - 1])
					changed_x2--;
			}
			if (snapshot)
				memcpy(snapshot + changed_x1,
				       sdev->dither_source_line + changed_x1,
				       (changed_x2 - changed_x1) * sizeof(u32));

			dst_x1 = (u64)changed_x1 * 2048 /
				sdev->softscale_source_width;
			dst_x2 = DIV_ROUND_UP_ULL((u64)changed_x2 * 2048,
						   sdev->softscale_source_width);
			/* Include output pixels whose scale filter touches the change. */
			if (dst_x1)
				dst_x1--;
			if (dst_x2 < 2048)
				dst_x2++;
			if (sharpen) {
				if (dst_x1)
					dst_x1--;
				if (dst_x2 < 2048)
					dst_x2++;
			}
			if (sdev->shadow_dma_enabled && sdev->rgb565) {
				dst_x1 &= ~1U;
				dst_x2 = min(ALIGN(dst_x2, 2), 2048U);
			}
			if (dst_x1 >= dst_x2)
				continue;
			if (!sdev->rgb565) {
				unsigned int calc_x1 = dst_x1 ? dst_x1 - 1 : 0;
				unsigned int calc_x2 = min(dst_x2 + 1, 2048U);
				u32 *output = sdev->softscale_output_line;

				sm750_scale_xrgb8888(sdev->softscale_output_line,
					sdev->dither_source_line,
					sdev->dither_scale_map,
					sdev->softscale_source_width,
					calc_x1, calc_x2);
				if (sharpen) {
					sm750_sharpen_xrgb8888(sdev->xrgb_output_line,
						sdev->softscale_output_line,
						dst_x1, dst_x2,
						SM750_DRM_SHARPEN_PERCENT);
					output = sdev->xrgb_output_line;
				}
				sm750_shadow_upload(sdev,
					dst_offset + dst_x1 * sizeof(u32),
					output + dst_x1,
					(dst_x2 - dst_x1) * sizeof(u32));
				continue;
			}
			if (!sdev->bbdither) {
				unsigned int calc_x1 = dst_x1 ? dst_x1 - 1 : 0;
				unsigned int calc_x2 = min(dst_x2 + 1, 2048U);
				u32 *output = sdev->softscale_output_line;

				sm750_scale_xrgb8888(sdev->softscale_output_line,
					sdev->dither_source_line,
					sdev->dither_scale_map,
					sdev->softscale_source_width,
					calc_x1, calc_x2);
				if (sharpen) {
					sm750_sharpen_xrgb8888(sdev->xrgb_output_line,
						sdev->softscale_output_line,
						dst_x1, dst_x2,
						SM750_DRM_SHARPEN_PERCENT);
					output = sdev->xrgb_output_line;
				}
				sm750_xrgb8888_to_rgb565(
					sdev->dither_output_line + dst_x1,
					output + dst_x1, dst_x2 - dst_x1);
			} else if (sharpen)
				sm750_dither_scale_sharpen_xrgb8888_to_rgb565(
					sdev->dither, sdev->dither_output_line,
					sdev->softscale_output_line,
					sdev->dither_source_line, y,
					sdev->dither_scale_map,
					sdev->softscale_source_width,
					dst_x1, dst_x2,
					SM750_DRM_SHARPEN_PERCENT);
			else if (sdev->softscale_source_width == 2560)
				sm750_dither_scale_5_to_4_xrgb8888_to_rgb565(
					sdev->dither, sdev->dither_output_line,
					sdev->dither_source_line, y,
					dst_x1, dst_x2);
			else
				sm750_dither_scale_xrgb8888_to_rgb565(
					sdev->dither, sdev->dither_output_line,
					sdev->dither_source_line, y,
					sdev->dither_scale_map, dst_x1, dst_x2);
			sm750_shadow_upload(sdev,
				dst_offset + dst_x1 * sizeof(u16),
				sdev->dither_output_line + dst_x1,
				(dst_x2 - dst_x1) * sizeof(u16));
			continue;
		}
		size_t src_offset = (size_t)y * plane_state->fb->pitches[0] +
			(size_t)rect->x1 * sizeof(u32);
		size_t dst_offset = (size_t)y * sdev->scanout_pitch +
			(size_t)rect->x1 *
			(sdev->rgb565 ? sizeof(u16) : sizeof(u32));

		iosys_map_memcpy_from(sdev->dither_source_line, src,
				      src_offset, width * sizeof(u32));
		if (!sdev->rgb565) {
			sm750_shadow_upload(sdev, dst_offset,
				sdev->dither_source_line, width * sizeof(u32));
			continue;
		}
		if (sdev->bbdither)
			sm750_dither_xrgb8888_to_rgb565(sdev->dither,
					sdev->dither_output_line, width,
					sdev->dither_source_line, width,
					rect->x1, y, width, 1);
		else
			sm750_xrgb8888_to_rgb565(sdev->dither_output_line,
					sdev->dither_source_line, width);
		sm750_shadow_upload(sdev, dst_offset, sdev->dither_output_line,
				width * sizeof(u16));
	}
	if (double_shadow && !sdev->shadow_source_snapshot_valid &&
	    sdev->softscale_active &&
	    rect->x1 <= 0 && rect->y1 <= 0 &&
	    rect->x2 >= sdev->softscale_source_width &&
	    rect->y2 >= sdev->shadow_source_height)
		sdev->shadow_source_snapshot_valid = true;
	wmb();
	mutex_unlock(&sdev->shadow_lock);
}

static void sm750_cursor_disable_locked(struct sm750_drm_device *sdev)
{
	poke32(SM750_DRM_HWC_ADDRESS, 0);
}

static u16 sm750_cursor_rgb565(u8 red, u8 green, u8 blue)
{
	return ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
}

static void sm750_cursor_unpremultiply(u32 pixel, u8 *alpha, u8 *red,
				       u8 *green, u8 *blue)
{
	u32 a = pixel >> 24;
	u32 r = (pixel >> 16) & 0xff;
	u32 g = (pixel >> 8) & 0xff;
	u32 b = pixel & 0xff;

	if (a && a < 255) {
		r = min(255U, DIV_ROUND_CLOSEST(r * 255, a));
		g = min(255U, DIV_ROUND_CLOSEST(g * 255, a));
		b = min(255U, DIV_ROUND_CLOSEST(b * 255, a));
	}
	*alpha = a;
	*red = r;
	*green = g;
	*blue = b;
}

static void sm750_cursor_palette(struct sm750_drm_device *sdev,
				 unsigned int width, unsigned int height,
				 u16 palette[3])
{
	u64 red_sum[3] = { 0 };
	u64 green_sum[3] = { 0 };
	u64 blue_sum[3] = { 0 };
	u64 weight[3] = { 0 };
	unsigned int x;
	unsigned int y;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			u8 alpha;
			u8 red;
			u8 green;
			u8 blue;
			u32 luma;
			u32 bin;

			sm750_cursor_unpremultiply(
				sdev->cursor_source[y * width + x],
				&alpha, &red, &green, &blue);
			if (!alpha)
				continue;
			luma = (77U * red + 150U * green + 29U * blue) >> 8;
			bin = min(luma / 86, 2U);
			red_sum[bin] += (u64)red * alpha;
			green_sum[bin] += (u64)green * alpha;
			blue_sum[bin] += (u64)blue * alpha;
			weight[bin] += alpha;
		}
	}

	palette[0] = 0x0000;
	palette[1] = 0x7bef;
	palette[2] = 0xffff;
	for (x = 0; x < ARRAY_SIZE(weight); x++) {
		if (weight[x])
			palette[x] = sm750_cursor_rgb565(
				DIV_ROUND_CLOSEST_ULL(red_sum[x], weight[x]),
				DIV_ROUND_CLOSEST_ULL(green_sum[x], weight[x]),
				DIV_ROUND_CLOSEST_ULL(blue_sum[x], weight[x]));
	}
}

static unsigned int sm750_cursor_nearest_color(u8 red, u8 green, u8 blue,
					       const u16 palette[3])
{
	unsigned int nearest = 0;
	u32 nearest_distance = U32_MAX;
	unsigned int i;

	for (i = 0; i < 3; i++) {
		int palette_red = ((palette[i] >> 11) & 0x1f) * 255 / 31;
		int palette_green = ((palette[i] >> 5) & 0x3f) * 255 / 63;
		int palette_blue = (palette[i] & 0x1f) * 255 / 31;
		int red_delta = (int)red - palette_red;
		int green_delta = (int)green - palette_green;
		int blue_delta = (int)blue - palette_blue;
		u32 distance = red_delta * red_delta +
			green_delta * green_delta + blue_delta * blue_delta;

		if (distance < nearest_distance) {
			nearest = i;
			nearest_distance = distance;
		}
	}
	return nearest + 1;
}

static void sm750_cursor_encode(struct sm750_drm_device *sdev,
				unsigned int source_width,
				unsigned int source_height,
				unsigned int output_width,
				u16 palette[3])
{
	static const u8 alpha_dither[16] = {
		0, 8, 2, 10,
		12, 4, 14, 6,
		3, 11, 1, 9,
		15, 7, 13, 5,
	};
	unsigned int x;
	unsigned int y;

	memset(sdev->cursor_image, 0, SM750_DRM_CURSOR_SIZE);
	for (y = 0; y < source_height; y++) {
		for (x = 0; x < output_width; x++) {
			unsigned int source_x = min_t(u64,
				((u64)(2 * x + 1) * source_width) /
				(2 * output_width), source_width - 1);
			u32 pixel = sdev->cursor_source[y * source_width + source_x];
			u8 alpha;
			u8 red;
			u8 green;
			u8 blue;
			u8 value;

			sm750_cursor_unpremultiply(pixel, &alpha, &red, &green, &blue);
			if (alpha <= alpha_dither[((y & 3) << 2) | (x & 3)] * 16)
				continue;
			value = sm750_cursor_nearest_color(red, green, blue, palette);
			sdev->cursor_image[y * SM750_DRM_CURSOR_STRIDE + x / 4] |=
				value << ((x & 3) * 2);
		}
	}
}

static int sm750_cursor_atomic_check(struct drm_plane *plane,
				     struct drm_atomic_state *atomic_state)
{
	struct drm_plane_state *state =
		drm_atomic_get_new_plane_state(atomic_state, plane);
	struct drm_crtc_state *crtc_state;
	int ret;

	if (!state->crtc || !state->fb)
		return 0;
	crtc_state = drm_atomic_get_new_crtc_state(atomic_state, state->crtc);
	ret = drm_atomic_helper_check_plane_state(state, crtc_state,
			DRM_PLANE_NO_SCALING, DRM_PLANE_NO_SCALING, true, true);
	if (ret || !state->visible)
		return ret;
	if ((state->src.x1 | state->src.y1 | state->src.x2 | state->src.y2) &
	    0xffff)
		return -EINVAL;
	if (drm_rect_width(&state->src) > (SM750_DRM_CURSOR_WIDTH << 16) ||
	    drm_rect_height(&state->src) > (SM750_DRM_CURSOR_HEIGHT << 16))
		return -EINVAL;
	return 0;
}

static void sm750_cursor_atomic_update(struct drm_plane *plane,
				       struct drm_atomic_state *atomic_state)
{
	struct sm750_drm_device *sdev =
		container_of(plane, struct sm750_drm_device, cursor_plane);
	struct drm_plane_state *state =
		drm_atomic_get_new_plane_state(atomic_state, plane);
	struct drm_shadow_plane_state *shadow;
	const struct iosys_map *source;
	unsigned int source_x;
	unsigned int source_y;
	unsigned int source_width;
	unsigned int source_height;
	unsigned int output_width;
	unsigned int y;
	u16 palette[3];
	int physical_x;
	int physical_y;
	u32 location = 0;

	mutex_lock(&sdev->cursor_lock);
	if (!state || !state->fb || !state->visible) {
		sm750_cursor_disable_locked(sdev);
		goto unlock;
	}
	shadow = to_drm_shadow_plane_state(state);
	source = &shadow->data[0];
	if (iosys_map_is_null(source)) {
		sm750_cursor_disable_locked(sdev);
		goto unlock;
	}
	source_x = state->src.x1 >> 16;
	source_y = state->src.y1 >> 16;
	source_width = drm_rect_width(&state->src) >> 16;
	source_height = drm_rect_height(&state->src) >> 16;
	if (!source_width || !source_height) {
		sm750_cursor_disable_locked(sdev);
		goto unlock;
	}
	for (y = 0; y < source_height; y++) {
		size_t offset = (size_t)(source_y + y) * state->fb->pitches[0] +
			(size_t)source_x * sizeof(u32);

		iosys_map_memcpy_from(sdev->cursor_source + y * source_width,
				      source, offset, source_width * sizeof(u32));
	}
	output_width = source_width;
	physical_x = state->dst.x1;
	if (sdev->softscale_active) {
		output_width = max_t(u64, 1,
			DIV_ROUND_UP_ULL((u64)source_width * 2048,
					 sdev->softscale_source_width));
		physical_x = div_s64((s64)physical_x * 2048,
				       sdev->softscale_source_width);
	}
	output_width = min(output_width, (unsigned int)SM750_DRM_CURSOR_WIDTH);
	physical_y = state->dst.y1;
	if (physical_x >= 2048 || physical_y >= sdev->shadow_source_height ||
	    physical_x + output_width <= 0 ||
	    physical_y + source_height <= 0) {
		sm750_cursor_disable_locked(sdev);
		goto unlock;
	}

	sm750_cursor_palette(sdev, source_width, source_height, palette);
	sm750_cursor_encode(sdev, source_width, source_height, output_width,
			    palette);
	memcpy_toio(sdev->vram + sdev->cursor_offset, sdev->cursor_image,
		    SM750_DRM_CURSOR_SIZE);
	poke32(SM750_DRM_HWC_COLOR_12, (u32)palette[1] << 16 | palette[0]);
	poke32(SM750_DRM_HWC_COLOR_3, palette[2]);
	if (physical_x < 0) {
		location |= SM750_DRM_HWC_LOCATION_LEFT;
		physical_x = -physical_x;
	}
	if (physical_y < 0) {
		location |= SM750_DRM_HWC_LOCATION_TOP;
		physical_y = -physical_y;
	}
	location |= ((physical_y << 16) & SM750_DRM_HWC_LOCATION_Y_MASK) |
		(physical_x & SM750_DRM_HWC_LOCATION_X_MASK);
	poke32(SM750_DRM_HWC_LOCATION, location);
	wmb();
	poke32(SM750_DRM_HWC_ADDRESS,
		SM750_DRM_HWC_ADDRESS_ENABLE |
		(sdev->cursor_offset & SM750_DRM_HWC_ADDRESS_MASK));
unlock:
	mutex_unlock(&sdev->cursor_lock);
}

static void sm750_cursor_atomic_disable(struct drm_plane *plane,
					struct drm_atomic_state *atomic_state)
{
	struct sm750_drm_device *sdev =
		container_of(plane, struct sm750_drm_device, cursor_plane);

	mutex_lock(&sdev->cursor_lock);
	sm750_cursor_disable_locked(sdev);
	mutex_unlock(&sdev->cursor_lock);
}

static const struct drm_plane_funcs sm750_cursor_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	DRM_GEM_SHADOW_PLANE_FUNCS,
};

static const struct drm_plane_helper_funcs sm750_cursor_plane_helper_funcs = {
	.prepare_fb = drm_gem_plane_helper_prepare_fb,
	.atomic_check = sm750_cursor_atomic_check,
	.atomic_update = sm750_cursor_atomic_update,
	.atomic_disable = sm750_cursor_atomic_disable,
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
};

static int sm750_pipe_prepare_fb(struct drm_simple_display_pipe *pipe,
				 struct drm_plane_state *plane_state)
{
	return drm_gem_vram_plane_helper_prepare_fb(&pipe->plane, plane_state);
}

static void sm750_pipe_cleanup_fb(struct drm_simple_display_pipe *pipe,
				  struct drm_plane_state *plane_state)
{
	drm_gem_vram_plane_helper_cleanup_fb(&pipe->plane, plane_state);
}

static int sm750_shadow_pipe_prepare_fb(struct drm_simple_display_pipe *pipe,
					struct drm_plane_state *plane_state)
{
	return drm_gem_plane_helper_prepare_fb(&pipe->plane, plane_state);
}

static int sm750_pipe_check(struct drm_simple_display_pipe *pipe,
			    struct drm_plane_state *plane_state,
			    struct drm_crtc_state *crtc_state)
{
	struct sm750_drm_device *sdev = pipe_to_sm750(pipe);

	if (plane_state->fb && !sdev->shadow_scanout) {
		if (plane_state->fb->pitches[0] > SM750_DRM_FB_WIDTH_OFFSET_MASK ||
		    !IS_ALIGNED(plane_state->fb->pitches[0],
				SM750_DRM_LINE_ALIGN))
			return -EINVAL;
	}
	if (crtc_state->enable &&
	    sm750_mode_valid(&crtc_state->adjusted_mode, sdev->vram_size) != MODE_OK)
		return -EINVAL;

	return 0;
}

static int sm750_scanout_offset(struct drm_framebuffer *fb, u32 *offset)
{
	struct drm_gem_vram_object *gbo;
	s64 value;

	gbo = drm_gem_vram_of_gem(drm_gem_fb_get_obj(fb, 0));
	value = drm_gem_vram_offset(gbo);
	if (value < 0 || value > U32_MAX)
		return -EINVAL;
	*offset = value;
	return 0;
}

static void sm750_arm_vblank_event(struct drm_simple_display_pipe *pipe)
{
	struct drm_crtc_state *state = pipe->crtc.state;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	if (!state->event)
		return;
	spin_lock_irqsave(&pipe->crtc.dev->event_lock, flags);
	event = state->event;
	state->event = NULL;
	if (!drm_crtc_vblank_get(&pipe->crtc))
		drm_crtc_arm_vblank_event(&pipe->crtc, event);
	else
		drm_crtc_send_vblank_event(&pipe->crtc, event);
	spin_unlock_irqrestore(&pipe->crtc.dev->event_lock, flags);
}

static void sm750_send_vblank_event(struct drm_simple_display_pipe *pipe)
{
	struct drm_crtc_state *state = pipe->crtc.state;
	unsigned long flags;

	if (!state->event)
		return;
	spin_lock_irqsave(&pipe->crtc.dev->event_lock, flags);
	drm_crtc_send_vblank_event(&pipe->crtc, state->event);
	state->event = NULL;
	spin_unlock_irqrestore(&pipe->crtc.dev->event_lock, flags);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
static int sm750_pipe_enable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct sm750_drm_device *sdev = pipe_to_sm750(pipe);
	const struct drm_display_mode *mode = &pipe->crtc.mode;
	unsigned long flags;
	u64 delay_ns;

	if (!mode->clock || !mode->htotal ||
	    mode->vdisplay >= mode->vtotal)
		return -EINVAL;

	spin_lock_irqsave(&sdev->vblank_lock, flags);
	sdev->vblank_line_ns = DIV_ROUND_CLOSEST_ULL(
		(u64)NSEC_PER_SEC * mode->htotal, (u64)mode->clock * 1000);
	sdev->vblank_frame_ns = sdev->vblank_line_ns * mode->vtotal;
	sdev->vblank_start = mode->vdisplay;
	sdev->vblank_vtotal = mode->vtotal;
	sdev->vblank_timer_enabled = true;
	delay_ns = sm750_vblank_first_delay_locked(sdev);
	spin_unlock_irqrestore(&sdev->vblank_lock, flags);

	hrtimer_start(&sdev->vblank_timer, ns_to_ktime(delay_ns),
		      HRTIMER_MODE_REL);
	return 0;
}

static void sm750_pipe_disable_vblank(struct drm_simple_display_pipe *pipe)
{
	sm750_vblank_timer_stop(pipe_to_sm750(pipe), false);
}
#endif

static void sm750_pipe_enable(struct drm_simple_display_pipe *pipe,
			      struct drm_crtc_state *crtc_state,
			      struct drm_plane_state *plane_state)
{
	struct sm750_drm_device *sdev = pipe_to_sm750(pipe);
	struct drm_rect damage;
	u32 pitch;
	u32 offset;
	u8 cpp;
	int ret;

	if (sdev->shadow_scanout) {
		sdev->softscale_active = sm750_mode_is_softscaled(
			&crtc_state->adjusted_mode);
		sdev->softscale_source_width = sdev->softscale_active ?
			crtc_state->adjusted_mode.hdisplay : 0;
		sdev->shadow_source_height = crtc_state->adjusted_mode.vdisplay;
		sdev->shadow_source_snapshot_valid = false;
		if (sdev->softscale_active &&
		    sdev->softscale_source_width != 2560) {
			ret = sm750_dither_scale_map_init(sdev->dither_scale_map,
					sdev->softscale_source_width, 2048);
			if (ret)
				goto fail;
		}
		cpp = sdev->rgb565 ? sizeof(u16) : sizeof(u32);
		pitch = ALIGN((sdev->softscale_active ? 2048 :
			crtc_state->adjusted_mode.hdisplay) * cpp,
			      SM750_DRM_LINE_ALIGN);
		sdev->scanout_pitch = pitch;
		offset = 0;
		damage.x1 = 0;
		damage.y1 = 0;
		damage.x2 = crtc_state->adjusted_mode.hdisplay;
		damage.y2 = crtc_state->adjusted_mode.vdisplay;
		sm750_shadow_rect(sdev, plane_state, &damage);
	} else {
		ret = sm750_scanout_offset(plane_state->fb, &offset);
		if (ret)
			goto fail;
		pitch = plane_state->fb->pitches[0];
		cpp = sizeof(u32);
	}
	mutex_lock(&sdev->mode_lock);
	ret = sm750_program_mode(sdev, &crtc_state->adjusted_mode,
				 pitch, offset, cpp);
	mutex_unlock(&sdev->mode_lock);
	if (!ret) {
		drm_crtc_vblank_on(&pipe->crtc);
		sm750_arm_vblank_event(pipe);
		return;
	}
fail:
	sdev->softscale_active = false;
	sdev->softscale_source_width = 0;
	sdev->shadow_source_height = 0;
	sdev->shadow_source_snapshot_valid = false;
	drm_err(&sdev->drm, "failed to enable HDMI mode: %d\n", ret);
}

static void sm750_pipe_update(struct drm_simple_display_pipe *pipe,
			      struct drm_plane_state *old_plane_state)
{
	struct sm750_drm_device *sdev = pipe_to_sm750(pipe);
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_rect damage;
	u32 offset;

	if (sdev->shadow_scanout &&
	    drm_atomic_helper_damage_merged(old_plane_state, state, &damage)) {
		sm750_shadow_rect(sdev, state, &damage);
	} else if (!sdev->shadow_scanout && state->fb &&
		 !sm750_scanout_offset(state->fb, &offset))
		poke32(SM750_DRM_FB_ADDRESS, SM750_DRM_FB_ADDRESS_STATUS |
		       (offset & SM750_DRM_FB_ADDRESS_MASK));
	if (!pipe->crtc.state->mode_changed)
		sm750_arm_vblank_event(pipe);
}

static void sm750_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct sm750_drm_device *sdev = pipe_to_sm750(pipe);

	drm_crtc_vblank_off(&pipe->crtc);
	sm750_send_vblank_event(pipe);
	if (sdev->hardware_cursor) {
		mutex_lock(&sdev->cursor_lock);
		sm750_cursor_disable_locked(sdev);
		mutex_unlock(&sdev->cursor_lock);
	}
	mutex_lock(&sdev->mode_lock);
	sm750_sii902x_disable_link();
	ddk750_set_display_control(SM750_DRM_CONTROLLER, false);
	mutex_unlock(&sdev->mode_lock);
	sdev->softscale_active = false;
	sdev->softscale_source_width = 0;
	sdev->shadow_source_height = 0;
	sdev->shadow_source_snapshot_valid = false;
}

static enum drm_mode_status
sm750_pipe_mode_valid(struct drm_simple_display_pipe *pipe,
			      const struct drm_display_mode *mode)
{
	return sm750_mode_valid(mode, pipe_to_sm750(pipe)->vram_size);
}

static const struct drm_simple_display_pipe_funcs sm750_pipe_funcs = {
	.mode_valid = sm750_pipe_mode_valid,
	.enable = sm750_pipe_enable,
	.disable = sm750_pipe_disable,
	.check = sm750_pipe_check,
	.update = sm750_pipe_update,
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
	.enable_vblank = sm750_pipe_enable_vblank,
	.disable_vblank = sm750_pipe_disable_vblank,
#endif
	.prepare_fb = sm750_pipe_prepare_fb,
	.cleanup_fb = sm750_pipe_cleanup_fb,
};

static const struct drm_simple_display_pipe_funcs sm750_shadow_pipe_funcs = {
	.mode_valid = sm750_pipe_mode_valid,
	.enable = sm750_pipe_enable,
	.disable = sm750_pipe_disable,
	.check = sm750_pipe_check,
	.update = sm750_pipe_update,
	.prepare_fb = sm750_shadow_pipe_prepare_fb,
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
	.enable_vblank = sm750_pipe_enable_vblank,
	.disable_vblank = sm750_pipe_disable_vblank,
#endif
	DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
static const struct drm_crtc_funcs sm750_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	DRM_CRTC_VBLANK_TIMER_FUNCS,
};
#endif

static const struct drm_mode_config_funcs sm750_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const struct drm_mode_config_funcs sm750_shadow_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_FOPS(sm750_drm_fops);

static int sm750_dumb_create(struct drm_file *file, struct drm_device *drm,
			     struct drm_mode_create_dumb *args)
{
	return drm_gem_vram_fill_create_dumb(file, drm, 0,
					     SM750_DRM_LINE_ALIGN, args);
}

static const struct drm_driver sm750_drm_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &sm750_drm_fops,
	.debugfs_init = drm_vram_mm_debugfs_init,
	.dumb_create = sm750_dumb_create,
	.dumb_map_offset = drm_gem_ttm_dumb_map_offset,
	DRM_FBDEV_TTM_DRIVER_OPS,
	.name = SM750_DRM_NAME,
	.desc = "SM750G10 SiI9024A HDMI DRM driver",
	.major = 0,
	.minor = 1,
};

static const struct drm_driver sm750_shadow_drm_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &sm750_drm_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,
	.name = SM750_DRM_NAME,
	.desc = "SM750G10 SiI9024A HDMI DRM shadow driver",
	.major = 0,
	.minor = 1,
};

static int sm750_hw_init(struct sm750_drm_device *sdev, bool reset_memory)
{
	struct initchip_param init = {
		.power_mode = 0,
		.chip_clock = 290,
		.mem_clock = 290,
		.master_clock = 96,
		.set_all_eng_off = 1,
		.reset_memory = reset_memory,
	};
	u32 value;

	mmio750 = sdev->regs;
	sm750_set_chip_type(sdev->pdev->device, sdev->pdev->revision);
	sdev->vram_size = ddk750_get_vm_size();
	if (!sdev->vram_size || sdev->vram_size > pci_resource_len(sdev->pdev, 0))
		return -ENODEV;
	ddk750_init_hw(&init);

	value = peek32(MISC_CTRL) | MISC_CTRL_DAC_POWER_OFF;
	poke32(MISC_CTRL, value);
	value = peek32(PANEL_DISPLAY_CTRL) &
		~(PANEL_DISPLAY_CTRL_DUAL_DISPLAY |
		  PANEL_DISPLAY_CTRL_DOUBLE_PIXEL);
	poke32(PANEL_DISPLAY_CTRL, value);
	return 0;
}

static int sm750_modeset_init(struct sm750_drm_device *sdev)
{
	static const u32 formats[] = { DRM_FORMAT_XRGB8888 };
	static const u32 cursor_formats[] = { DRM_FORMAT_ARGB8888 };
	struct drm_device *drm = &sdev->drm;
	const struct drm_simple_display_pipe_funcs *pipe_funcs;
	int ret;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;
	drm->mode_config.funcs = sdev->shadow_scanout ?
		&sm750_shadow_mode_config_funcs : &sm750_mode_config_funcs;
	drm->mode_config.min_width = 320;
	drm->mode_config.min_height = 200;
	drm->mode_config.max_width = SM750_DRM_MAX_WIDTH;
	drm->mode_config.max_height = SM750_DRM_MAX_HEIGHT;
	drm->mode_config.preferred_depth = 24;
	drm->mode_config.prefer_shadow = 1;

	ret = drm_connector_init(drm, &sdev->connector, &sm750_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret)
		return ret;
	drm_connector_helper_add(&sdev->connector,
				 &sm750_connector_helper_funcs);
	sdev->connector.polled = DRM_CONNECTOR_POLL_CONNECT |
				 DRM_CONNECTOR_POLL_DISCONNECT;

	pipe_funcs = sdev->shadow_scanout ? &sm750_shadow_pipe_funcs :
		&sm750_pipe_funcs;
	ret = drm_simple_display_pipe_init(drm, &sdev->pipe, pipe_funcs,
					   formats, ARRAY_SIZE(formats), NULL,
					   &sdev->connector);
	if (ret)
		return ret;
	if (sdev->shadow_scanout)
		drm_plane_enable_fb_damage_clips(&sdev->pipe.plane);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
	sdev->pipe.crtc.funcs = &sm750_crtc_funcs;
#endif
	if (sdev->hardware_cursor) {
		ret = drm_universal_plane_init(drm, &sdev->cursor_plane,
				drm_crtc_mask(&sdev->pipe.crtc),
				&sm750_cursor_plane_funcs, cursor_formats,
				ARRAY_SIZE(cursor_formats), NULL,
				DRM_PLANE_TYPE_CURSOR, "sm750-cursor");
		if (ret)
			return ret;
		drm_plane_helper_add(&sdev->cursor_plane,
				     &sm750_cursor_plane_helper_funcs);
		sdev->pipe.crtc.cursor = &sdev->cursor_plane;
		drm->mode_config.cursor_width = SM750_DRM_CURSOR_WIDTH;
		drm->mode_config.cursor_height = SM750_DRM_CURSOR_HEIGHT;
	}
	ret = drm_vblank_init(drm, 1);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);
	return 0;
}

static void sm750_release_regions(void *data)
{
	struct pci_dev *pdev = data;

	pci_release_regions(pdev);
}

static void sm750_unmap_regs(void *data)
{
	struct sm750_drm_device *sdev = data;

	if (mmio750 == sdev->regs)
		mmio750 = NULL;
	pci_iounmap(sdev->pdev, sdev->regs);
}

static void sm750_unmap_vram(void *data)
{
	struct sm750_drm_device *sdev = data;

	pci_iounmap(sdev->pdev, sdev->vram);
}

static void sm750_free_shadow_source_snapshot(void *data)
{
	kvfree(data);
}

static int sm750_pci_probe(struct pci_dev *pdev,
			   const struct pci_device_id *id)
{
	const struct drm_driver *driver;
	struct sm750_drm_device *sdev;
	bool use_shadow;
	bool use_rgb565;
	bool use_bbdither;
	int ret;

	if (!strcmp(scanout_format, "xrgb8888")) {
		use_rgb565 = false;
		use_bbdither = false;
	} else if (!strcmp(scanout_format, "rgb565")) {
		use_rgb565 = true;
		use_bbdither = false;
	} else if (!strcmp(scanout_format, "rgb565-bbdither") ||
		   !strcmp(scanout_format, "rgb565-dither")) {
		use_rgb565 = true;
		use_bbdither = true;
	} else {
		return dev_err_probe(&pdev->dev, -EINVAL,
			"invalid scanout_format '%s'\n", scanout_format);
	}
	use_shadow = use_rgb565 || softscale_wide || double_shadow ||
		!disable_hardware_cursor;
	if (use_bbdither && dither_green_gain > 100)
		return dev_err_probe(&pdev->dev, -EINVAL,
			"dither_green_gain must be between 0 and 100\n");
	if (!disable_dma && use_shadow &&
	    (shadow_dma_min_bytes < sizeof(u32) ||
	     shadow_dma_min_bytes > SM750_DRM_DMA_STAGING_SIZE ||
	     !IS_ALIGNED(shadow_dma_min_bytes, sizeof(u32))))
		return dev_err_probe(&pdev->dev, -EINVAL,
			"shadow_dma_min_bytes must be a 4-byte aligned value from 4 to %zu\n",
			(size_t)SM750_DRM_DMA_STAGING_SIZE);
	driver = use_shadow ? &sm750_shadow_drm_driver : &sm750_drm_driver;

	ret = aperture_remove_conflicting_pci_devices(pdev, SM750_DRM_NAME);
	if (ret)
		return ret;
	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;
	ret = pci_request_regions(pdev, SM750_DRM_NAME);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(&pdev->dev, sm750_release_regions, pdev);
	if (ret)
		return ret;
	pci_set_master(pdev);

	sdev = devm_drm_dev_alloc(&pdev->dev, driver,
				  struct sm750_drm_device, drm);
	if (IS_ERR(sdev))
		return PTR_ERR(sdev);
	sdev->pdev = pdev;
	sdev->shadow_scanout = use_shadow;
	sdev->rgb565 = use_rgb565;
	sdev->bbdither = use_bbdither;
	sdev->hardware_cursor = !disable_hardware_cursor;
	sdev->vram_base = pci_resource_start(pdev, 0);
	mutex_init(&sdev->mode_lock);
	mutex_init(&sdev->shadow_lock);
	mutex_init(&sdev->cursor_lock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
	spin_lock_init(&sdev->vblank_lock);
	hrtimer_setup(&sdev->vblank_timer, sm750_vblank_timer_fn,
		      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
#endif

	sdev->regs = pci_iomap(pdev, 1, min_t(resource_size_t,
					      pci_resource_len(pdev, 1), SZ_2M));
	if (!sdev->regs)
		return -ENOMEM;
	ret = devm_add_action_or_reset(&pdev->dev, sm750_unmap_regs, sdev);
	if (ret)
		return ret;

	ret = sm750_hw_init(sdev, true);
	if (ret)
		return ret;
	if (sdev->shadow_scanout) {
		sdev->vram = pci_iomap_wc(pdev, 0, sdev->vram_size);
		if (!sdev->vram)
			return -ENOMEM;
		ret = devm_add_action_or_reset(&pdev->dev, sm750_unmap_vram,
					       sdev);
		if (ret)
			return ret;
		sdev->dither_scale_map = devm_kzalloc(&pdev->dev,
			sizeof(*sdev->dither_scale_map), GFP_KERNEL);
		sdev->dither_source_line = devm_kmalloc_array(&pdev->dev,
			SM750_DRM_MAX_WIDTH, sizeof(*sdev->dither_source_line),
			GFP_KERNEL);
		if (double_shadow)
			sdev->shadow_source_snapshot = kvcalloc(
				(size_t)SM750_DRM_MAX_WIDTH * SM750_DRM_MAX_HEIGHT,
				sizeof(*sdev->shadow_source_snapshot), GFP_KERNEL);
		sdev->softscale_output_line = devm_kmalloc_array(&pdev->dev,
			SM750_DITHER_SCALE_MAX_SAMPLES,
			sizeof(*sdev->softscale_output_line), GFP_KERNEL);
		sdev->xrgb_output_line = devm_kmalloc_array(&pdev->dev,
			SM750_DITHER_SCALE_MAX_SAMPLES,
			sizeof(*sdev->xrgb_output_line),
			GFP_KERNEL);
		if (sdev->hardware_cursor) {
			sdev->cursor_source = devm_kmalloc_array(&pdev->dev,
				SM750_DRM_CURSOR_WIDTH * SM750_DRM_CURSOR_HEIGHT,
				sizeof(*sdev->cursor_source), GFP_KERNEL);
			sdev->cursor_image = devm_kzalloc(&pdev->dev,
				SM750_DRM_CURSOR_SIZE, GFP_KERNEL);
		}
		if (!sdev->dither_scale_map || !sdev->dither_source_line ||
		    (double_shadow && !sdev->shadow_source_snapshot) ||
		    !sdev->softscale_output_line || !sdev->xrgb_output_line ||
		    (sdev->hardware_cursor &&
		     (!sdev->cursor_source || !sdev->cursor_image)))
			return -ENOMEM;
		if (sdev->shadow_source_snapshot) {
			ret = devm_add_action_or_reset(&pdev->dev,
					sm750_free_shadow_source_snapshot,
					sdev->shadow_source_snapshot);
			if (ret)
				return ret;
		}
		if (sdev->hardware_cursor &&
		    sdev->vram_size < SM750_DRM_CURSOR_SIZE)
			return -ENOSPC;
		sdev->cursor_offset = ALIGN_DOWN(
			sdev->vram_size -
			(sdev->hardware_cursor ? SM750_DRM_CURSOR_SIZE : 0), 16);
		if (sdev->hardware_cursor) {
			memset_io(sdev->vram + sdev->cursor_offset, 0,
				  SM750_DRM_CURSOR_SIZE);
			sm750_cursor_disable_locked(sdev);
		}
		if (sdev->rgb565) {
			sdev->dither_output_line = devm_kmalloc_array(&pdev->dev,
				SM750_DRM_MAX_WIDTH,
				sizeof(*sdev->dither_output_line), GFP_KERNEL);
			if (!sdev->dither_output_line)
				return -ENOMEM;
		}
		if (sdev->bbdither) {
			sdev->dither = devm_kzalloc(&pdev->dev,
				sizeof(*sdev->dither), GFP_KERNEL);
			if (!sdev->dither)
				return -ENOMEM;
			ret = sm750_dither_init(sdev->dither, dither_green_gain);
			if (ret)
				return ret;
		}
		if (!disable_dma) {
			ret = sm750_dma_init(sdev);
			if (ret) {
				drm_warn(&sdev->drm,
					 "DMA1 verification failed (%d); retaining CPU shadow uploads\n",
					 ret);
			} else {
				ret = devm_add_action_or_reset(&pdev->dev,
						sm750_dma_stop, sdev);
				if (ret)
					return ret;
			}
		}
	}
#ifdef CONFIG_X86
	if (dvo_clock_phase < 0) {
		u8 saved_index = inb(0x3c4);

		outb(0x95, 0x3c4);
		sdev->phase_falling = !!(inb(0x3c5) & BIT(1));
		outb(saved_index, 0x3c4);
	} else
#endif
		sdev->phase_falling = dvo_clock_phase > 0;

	if (!sdev->shadow_scanout) {
		ret = drmm_vram_helper_init(&sdev->drm, sdev->vram_base,
					    sdev->vram_size);
		if (ret)
			return ret;
	}
	ret = sm750_modeset_init(sdev);
	if (ret)
		return ret;

	mutex_lock(&sdev->mode_lock);
	ret = sm750_sii902x_prepare(&pdev->dev, false,
				    sii9024_scl, sii9024_sda);
	mutex_unlock(&sdev->mode_lock);
	if (ret)
		return ret;

	pci_set_drvdata(pdev, sdev);
	ret = drm_dev_register(&sdev->drm, 0);
	if (ret)
		return ret;
	drm_kms_helper_poll_init(&sdev->drm);
	drm_client_setup(&sdev->drm, drm_format_info(DRM_FORMAT_XRGB8888));
	drm_info(&sdev->drm,
		 "registered SM750 HDMI DRM device with %u MiB VRAM (%s, primary controller)\n",
		 sdev->vram_size >> 20,
		 sdev->bbdither ? "RGB565 bbdither" :
		 (sdev->rgb565 ? "RGB565" :
		  (sdev->shadow_scanout ? "XRGB8888 shadow" :
		   "XRGB8888 direct")));
	if (!strcmp(scanout_format, "rgb565-dither"))
		drm_info(&sdev->drm,
			 "scanout_format=rgb565-dither is deprecated; use rgb565-bbdither\n");
	if (softscale_wide && !edid_only) {
		drm_info(&sdev->drm,
			 "logical 2464/2560x1080 softscale to 2048x1080 enabled\n");
		if (sharpen)
			drm_info(&sdev->drm,
				 "softscale horizontal contrast sharpening enabled at 8%%\n");
	} else if (sharpen) {
		drm_info(&sdev->drm,
			 "sharpen=1 ignored because wide softscale modes are inactive\n");
	}
	if (sdev->hardware_cursor)
		drm_info(&sdev->drm,
			 "64x64 hardware cursor plane enabled with ARGB palette conversion\n");
	return 0;
}

static void sm750_pci_remove(struct pci_dev *pdev)
{
	struct sm750_drm_device *sdev = pci_get_drvdata(pdev);

	drm_kms_helper_poll_fini(&sdev->drm);
	drm_dev_unplug(&sdev->drm);
	drm_atomic_helper_shutdown(&sdev->drm);
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
	sm750_vblank_timer_stop(sdev, true);
#endif
	mutex_lock(&sdev->mode_lock);
	sm750_sii902x_shutdown();
	mutex_unlock(&sdev->mode_lock);
}

static void sm750_pci_shutdown(struct pci_dev *pdev)
{
	struct sm750_drm_device *sdev = pci_get_drvdata(pdev);

	if (sdev)
		drm_atomic_helper_shutdown(&sdev->drm);
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
	if (sdev)
		sm750_vblank_timer_stop(sdev, true);
#endif
}

static int sm750_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct sm750_drm_device *sdev = pci_get_drvdata(pdev);
	int ret;

	ret = drm_mode_config_helper_suspend(&sdev->drm);
	if (ret)
		return ret;
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
	sm750_vblank_timer_stop(sdev, true);
#endif
	mutex_lock(&sdev->mode_lock);
	sm750_sii902x_shutdown();
	mutex_unlock(&sdev->mode_lock);
	return 0;
}

static int sm750_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct sm750_drm_device *sdev = pci_get_drvdata(pdev);
	int ret;

	ret = sm750_hw_init(sdev, false);
	if (ret)
		return ret;
	mutex_lock(&sdev->mode_lock);
	ret = sm750_sii902x_prepare(&pdev->dev, false,
				    sii9024_scl, sii9024_sda);
	mutex_unlock(&sdev->mode_lock);
	if (ret)
		return ret;
	return drm_mode_config_helper_resume(&sdev->drm);
}

static DEFINE_SIMPLE_DEV_PM_OPS(sm750_pm_ops, sm750_pm_suspend,
				 sm750_pm_resume);

static const struct pci_device_id sm750_pci_ids[] = {
	{ PCI_DEVICE(0x126f, 0x0750) },
	{ }
};
MODULE_DEVICE_TABLE(pci, sm750_pci_ids);

static struct pci_driver sm750_pci_driver = {
	.name = SM750_DRM_NAME,
	.id_table = sm750_pci_ids,
	.probe = sm750_pci_probe,
	.remove = sm750_pci_remove,
	.shutdown = sm750_pci_shutdown,
	.driver.pm = pm_sleep_ptr(&sm750_pm_ops),
};
module_pci_driver(sm750_pci_driver);

MODULE_AUTHOR("Benjamin Brown");
MODULE_DESCRIPTION("Atomic DRM/KMS driver for SM750G10 SiI9024A HDMI boards");
MODULE_LICENSE("GPL v2");
