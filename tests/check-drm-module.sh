#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail

module=${1:?usage: check-drm-module.sh MODULE.ko}

test -s "$module"
test "$(modinfo -F name "$module")" = sm750hdmidrm
test "$(modinfo -F license "$module")" = "GPL v2"
modinfo -F alias "$module" | grep 'v0000126Fd00000750' >/dev/null
modinfo -F description "$module" | grep 'DRM' >/dev/null
modinfo -F parm "$module" | grep '^preferred_width:' >/dev/null
modinfo -F parm "$module" | grep '^preferred_height:' >/dev/null
modinfo -F parm "$module" | grep '^preferred_refresh:' >/dev/null
modinfo -F parm "$module" | grep '^scanout_format:' >/dev/null
modinfo -F parm "$module" | grep '^dither_green_gain:' >/dev/null
for parameter in edid_only softscale_wide sharpen double_shadow \
		disable_hardware_cursor disable_dma; do
	modinfo -F parm "$module" | grep "^${parameter}:" >/dev/null
done
readelf -h "$module" | grep 'ELF64' >/dev/null
if strings "$module" | grep -x 'sm750fb' >/dev/null; then
	echo "DRM module contains the stock module name as an identity." >&2
	exit 1
fi
undefined=$(nm -u "$module")
for symbol in drm_crtc_arm_vblank_event drm_crtc_vblank_get \
		drm_crtc_vblank_off drm_crtc_vblank_on \
		drm_vblank_init; do
	grep -E "[[:space:]]${symbol}$" <<<"$undefined" >/dev/null || {
		echo "DRM module lacks required vblank symbol: $symbol" >&2
		exit 1
	}
done
for symbol in drm_gem_shmem_dumb_create \
		drm_gem_simple_kms_begin_shadow_fb_access \
		drm_gem_simple_kms_end_shadow_fb_access; do
	grep -E "[[:space:]]${symbol}$" <<<"$undefined" >/dev/null || {
		echo "DRM module lacks required RGB565 shadow symbol: $symbol" >&2
		exit 1
	}
done
if [[ $(modinfo -F vermagic "$module") == 7.* ]]; then
	for symbol in drm_crtc_vblank_helper_enable_vblank_timer \
			drm_crtc_vblank_helper_disable_vblank_timer \
			drm_crtc_vblank_helper_get_vblank_timestamp_from_timer; do
		grep -E "[[:space:]]${symbol}$" <<<"$undefined" >/dev/null || {
			echo "DRM 7.x module lacks core vblank helper: $symbol" >&2
			exit 1
		}
	done
else
	grep -E '[[:space:]]drm_crtc_handle_vblank$' <<<"$undefined" \
		>/dev/null || {
		echo "DRM pre-7.0 module lacks private timer handler." >&2
		exit 1
	}
fi
if grep -E 'request(_threaded)?_irq' <<<"$undefined" >/dev/null; then
	echo "DRM module unexpectedly depends on unvalidated hardware IRQ setup." >&2
	exit 1
fi
echo "DRM module metadata checks passed: $module"
