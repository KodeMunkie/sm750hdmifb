<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Module parameters

Parameters are read when `sm750hdmidrm` loads and cannot be changed at runtime.
Use `sm750hdmidrm.name=value` on the kernel command line or an `options
sm750hdmidrm ...` line in `/etc/modprobe.d/`. Runtime parameters override
build-time defaults.

## Everyday options

| Parameter | Default | Purpose |
|---|---:|---|
| `scanout_format` | `rgb565-bbdither` | `xrgb8888`, `rgb565`, or dithered `rgb565-bbdither`. The deprecated `rgb565-dither` alias remains accepted. |
| `dither_green_gain` | `94` | Green-channel gain from 0 to 100 for `rgb565-bbdither`. |
| `edid_only` | `1` | Use valid EDID modes instead of the driver catalogue. |
| `softscale_wide` | `0` | Add logical 2464/2560x1080 modes when `edid_only=0`. |
| `sharpen` | `0` | Apply fixed 8% sharpening after horizontal soft scaling. |
| `double_shadow` | `0` | Snapshot source rows and trim reported damage to pixels that really changed. |
| `disable_hardware_cursor` | `0` | Set to `1` to use a software-rendered cursor instead of the 64x64 hardware plane. |
| `disable_dma` | `0` | Set to `1` to force CPU uploads. DMA already falls back permanently after verification failure or timeout. |
| `shadow_dma_min_bytes` | `256` | Smallest aligned upload span sent through DMA1. Valid range is checked at probe. |
| `preferred_width` | `0` | Preferred exposed mode width; use with height and refresh. |
| `preferred_height` | `0` | Preferred exposed mode height. |
| `preferred_refresh` | `0` | Preferred integer refresh rate in hertz. |

## Board wiring

| Parameter | Default | Purpose |
|---|---:|---|
| `dvo_clock_phase` | `-1` | `-1` uses the board option-ROM strap, `0` rising edge, `1` falling edge. |
| `sii9024_scl` | `12` | SM750 GPIO used for SiI9024A software-I2C clock. |
| `sii9024_sda` | `13` | SM750 GPIO used for SiI9024A software-I2C data. |

Changing these can prevent transmitter detection. Defaults match the tested
`SE-DP750A-HDMI`; they are not generic SM750 wiring.

## Transmitter diagnostics

These switches exist to reproduce individual hardware investigations. They are
not normal configuration and may blank or destabilize the link.

| Parameter | Default | Diagnostic effect |
|---|---:|---|
| `sii9024_dvi` | `0` | Force DVI signalling through the HDMI connector. |
| `sii9024_falling_edge` | `0` | Sample the 24-bit input bus on the falling pixel-clock edge. |
| `sii9024_tpi_totals` | `1` | Program TPI raster totals as documented by the programming guide and board ROM. |
| `sii9024_no_termination` | `0` | Disable internal TMDS source termination for an isolated electrical test. |
| `sii9024_force_termination` | `0` | Explicitly enable source termination, including in the ROM replay path. Do not combine with `sii9024_no_termination`. |
| `sii9024_clean_init` | `0` | Omit inherited board-ROM compatibility writes and use only documented TPI initialization. |
| `sii9024_rom_exact` | `0` | Replay the board's 720p option-ROM initialization order. Limited diagnostic modes only. |
| `sii9024_rom_hdmi` | `0` | With `sii9024_rom_exact`, replace the final DVI enable with HDMI and a valid AVI InfoFrame. |
| `sii9024_force_black` | `0` | With `sii9024_rom_exact`, replace incoming RGB with transmitter-generated black while retaining sync/TMDS. |

## Build-time defaults

One package supports every profile. Separate DKMS variants are unnecessary.
To compile different defaults:

```sh
make \
  SM750_DRM_SCANOUT_DEFAULT=rgb565-bbdither \
  SM750_DRM_DEFAULT_EDID_ONLY=1 \
  SM750_DRM_DEFAULT_SOFTSCALE_WIDE=0 \
  SM750_DRM_DEFAULT_SHARPEN=0 \
  SM750_DRM_DEFAULT_DOUBLE_SHADOW=0 \
  SM750_DRM_DEFAULT_DISABLE_HARDWARE_CURSOR=0 \
  SM750_DRM_DEFAULT_DISABLE_DMA=0
```

`SM750_DRM_SCANOUT_DEFAULT` also accepts `xrgb8888` and `rgb565`.
