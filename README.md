<!-- SPDX-License-Identifier: GPL-2.0-only -->
# SM750 HDMI DRM Driver

**Push this card to real 2048-wide output or a software-scaled 2560x1080
ultrawide desktop, with bandwidth-saving colour conversion and update
optimisations for smoother performance.**

Linux display driver for the single-HDMI `SE-DP750A-HDMI` PCIe card.

> **Experimental:** this driver is for one specific SM750 board. Optional modes
> can exceed published GPU or monitor clock limits and may produce no signal,
> distortion, or an unstable display. Keep SSH or another recovery route
> available when trying non-EDID modes.

## Is my card supported?

The tested board is sold or marked as `SE-DP750A-HDMI` and has all of these:

- Silicon Motion `SM750G10-AC`, revision A1
- PCI ID `126f:0750`
- Silicon Image/Lattice `SiI9024ACNU` HDMI transmitter
- 16 MiB display memory
- One HDMI output

The PCI ID alone is not enough. Other SM750 cards may use VGA, a different
transmitter, or different GPIO wiring and are not currently supported.

## Install

Ubuntu 24.04 and Linux Mint 22 users can build a DKMS package locally:

```sh
sudo apt update
sudo apt install build-essential dkms linux-headers-$(uname -r) libdrm-dev git
git clone https://github.com/KodeMunkie/sm750hdmifb.git
cd sm750hdmifb
make check
./build-package.sh
sudo apt install ./dist/sm750hdmifb_0.5.6_all.deb
sudo reboot
```

The reboot matters: it lets the package blacklist Linux's old `sm750fb` driver
before it can claim the card. The package is named `sm750hdmifb`; the kernel
module is `sm750hdmidrm.ko` so it cannot be confused with `sm750fb.ko`.

### Kernel compatibility

The driver is intended for **Linux 6.17 and newer**. DKMS deliberately refuses
older kernels because the required DRM interfaces are not supported by this
project. The source has compatibility paths for Linux 6.17 through the 6.x
series and for Linux 7.0 onward. It currently builds and passes the full test
suite against 6.17 and 7.0 Ubuntu kernels; the manual CI workflow also builds
against Ubuntu 24.04's 6.17 kernel headers.

There is no fixed upper version cap, but future kernels can change internal DRM
APIs. Because this is an out-of-tree driver, a new kernel version may require a
source update even though it falls within the intended `6.17+` range.

Prebuilt packages, when available, are published under
[GitHub Releases](https://github.com/KodeMunkie/sm750hdmifb/releases).
Building locally is recommended because DKMS compiles against the installed
kernel headers.

## Normal use

By default the driver:

- Uses monitor EDID modes
- Uses dithered 16-bit RGB565 scanout
- Uses a hardware cursor
- Coalesces updates on a worker
- Uploads eight-row batches with DMA
- Falls back safely if DMA fails

Applications still render in 32-bit colour. Immediately before upload, the
driver converts changed screen regions to RGB565 and applies KodeMunkie's
ordered dither with a 94% green-channel correction. This is the default because
the SM750 framebuffer is reached over a PCIe 1.1 x1 link. At the higher
resolutions that link cannot provide responsive full-screen 32-bit updates:
XRGB8888 sends four bytes per output pixel, while RGB565 sends two. Dithered
RGB565 therefore halves device-bound pixel traffic while preserving much of
the apparent colour detail.

Common kernel or GRUB options are:

The table omits the common `sm750hdmidrm.` prefix to keep it readable. For
example, enter `enable_dma=0` as `sm750hdmidrm.enable_dma=0` in GRUB.

| Option and default | When to specify it | Effect and tradeoff |
|---|---|---|
| `scanout_format=rgb565-bbdither`<br>Default: `rgb565-bbdither` | No need; this is the recommended default | Dithered 16-bit scanout with green correction |
| `scanout_format=xrgb8888`<br>Default: `rgb565-bbdither` | Only for 32-bit scanout | Disables RGB565, dither and green correction; doubles upload traffic and can lag at high resolutions |
| `scanout_format=rgb565`<br>Default: `rgb565-bbdither` | Only for plain RGB565 | Disables the dither and green correction, reducing colour quality |
| `dither_green_gain=94`<br>Default: `94` | No need; correction is on by default | Enables the tuned green correction while dithering |
| `dither_green_gain=100`<br>Default: `94` | To turn green correction off | Keeps the dither but may produce a greener colour balance |
| `enable_dma=1`<br>Default: `1` | No need; DMA is on by default | Verified eight-row DMA uploads with automatic CPU fallback |
| `enable_dma=0`<br>Default: `1` | To disable DMA | Forces CPU uploads, usually with lower update performance |
| `disable_hardware_cursor=1`<br>Default: `0` | For the software cursor fallback | Disables the hardware cursor and may reduce cursor responsiveness |
| `edid_only=0`<br>Default: `1` | Required for the driver catalogue and ultrawide modes | **DANGEROUS: STOPS EDID RESTRICTING MODES AND CLOCKS THE MONITOR MAY NOT SUPPORT** |
| `softscale_wide=1`<br>Default: `0` | Required for 2464x1080 and 2560x1080; also set `edid_only=0` | **DANGEROUS / EXPERIMENTAL: ENABLES WIDE COMPRESSION AND REQUIRES MONITOR STRETCHING** |
| `sharpen=1`<br>Default: `0` | Recommended with ultrawide modes | Adds fixed 8% contrast sharpening after compression |
| `double_shadow=1`<br>Default: `0` | Recommended to avoid redundant uploads | Adds source and output comparison snapshots; skips unchanged pixels but uses more system memory. This is not front/back page flipping |
| `async_updates=1`<br>Default: `1` | No need; enabled by default | Keeps only the latest pending update instead of queuing stale frames |

Add options to the existing `GRUB_CMDLINE_LINUX_DEFAULT` value in
`/etc/default/grub`, then apply them with:

```sh
sudo update-grub
sudo reboot
```

Every option and its default is listed in
[Module parameters](docs/PARAMETERS.md).

## Ultrawide scaling

The device is normally described as supporting up to 1920 pixels horizontally.
With `edid_only=0`, the driver also exposes these real 2048-wide hardware
modes:

| Desktop and HDMI mode | Refresh rates | Output and risk |
|---|---|---|
| `2048x864` | 59.94, 60, 70, 72, 75 Hz | Native or monitor-scaled. **EXPERIMENTAL: NOT RESTRICTED BY EDID** |
| `2048x1024` | 59.94, 60, 70, 72 Hz | Native or monitor-scaled. **EXPERIMENTAL: NOT RESTRICTED BY EDID** |
| `2048x1080` | 50, 59.94, 60, 70, 72, 75 Hz | Native or monitor-scaled. **EXPERIMENTAL: HIGH REFRESH MAY EXCEED SPECIFICATION** |
| `2048x1152` | 59.94, 60 Hz | Native or monitor-scaled. **EXPERIMENTAL: NOT RESTRICTED BY EDID** |

`softscale_wide=1` adds two wider logical desktops. Both require a physical
2560x1080 ultrawide monitor with **FULL WIDESCREEN STRETCH ENABLED** in its
on-screen menu:

| Logical desktop | Physical monitor | Width ratio | HDMI and monitor path |
|---|---|---|---|
| `2464x1080` | `2560x1080` | `77:64`<br>16.9% compression | `2048x1080 -> 2560x1080` stretch |
| `2560x1080` | `2560x1080` | `5:4`<br>20% compression | `2048x1080 -> 2560x1080` stretch |

| Mode | Refresh rates | Guidance |
|---|---|---|
| `2464x1080` | 50, 59.94, 60, 70, 72, 75 Hz | **RECOMMENDED:** more detail and better responsiveness; enable `sharpen=1` |
| `2560x1080` | 50, 59.94, 60, 70, 72, 75 Hz | Less detail and lower performance because more width is compressed; see [the reasoning](#why-2464x1080-is-recommended) |

For both modes, the HDMI signal is stretched from `2048x1080` to the physical
`2560x1080` pixels of a 2K ultrawide monitor.

**2048 pixels is the highest real width this card can produce in hardware.**
The SM750 primary graphics plane has an 11-bit right-edge field, so its physical
scanout width cannot exceed 2048 pixels. This is a hardware constraint;
reducing the height does not release more horizontal bits. The 2464 and 2560
modes create wider workspaces in software, but the card still outputs only a
2048-pixel-wide signal.

An ultrawide monitor with its full-width or full-widescreen stretch option
enabled is required for the intended result. The complete path is:

```text
logical desktop -> driver compression -> 2048x1080 HDMI -> monitor stretch to 2560x1080 physical pixels
```

The monitor's stretch setting spatially expands the compressed 2048-pixel HDMI
image across all 2560 physical panel pixels. It is not recovering lost source
data, but it restores the intended ultrawide screen coverage and approximately
restores the intended aspect ratio. What looks "correct" is subjective because
the 2464 mode deliberately retains a small aspect difference.

### Why 2464x1080 is recommended

For `2560x1080`, the driver performs an optimized 5:4 reduction from 2560 to
2048 pixels, a 20% horizontal compression. A 2560-wide monitor then expands the
2048-pixel signal by 25% back across its panel. This provides a true
2560x1080-sized workspace, but some fine horizontal detail is necessarily
combined during the first step and more source pixels must be rendered and
processed.

For a physical 2560x1080 monitor, `2464x1080` is the recommended,
higher-performance and often sharper mode. It starts with
3.75% fewer desktop pixels and compresses width by only 16.9% before sending the
same 2048x1080 signal. The monitor still expands that signal across 2560 panel
pixels, so less source detail was discarded by the driver, but the final image
is about 3.9% wider than its logical geometry. This slight aspect distortion is
generally difficult to notice and is the tradeoff for improved responsiveness
and perceived sharpness. Enabling `sm750hdmidrm.sharpen=1` is recommended.

Both wide modes can apply a small fixed 8% contrast sharpen after compression.
It restores some edge definition lost to filtering; it does not recreate
discarded pixels. The dither and green correction are applied during the final
RGB565 conversion before the 2048-wide scanout is uploaded.

This tested profile enables the driver mode catalogue and wide scaling:

> **DANGER: THIS PROFILE DISABLES EDID MODE RESTRICTIONS AND EXPOSES
> EXPERIMENTAL OR ABOVE-SPECIFICATION CLOCKS. A LISTED MODE MAY BLANK OR
> DESTABILIZE THE DISPLAY OR EXCEED A GPU, TRANSMITTER OR MONITOR LIMIT.**

```text
sm750hdmidrm.edid_only=0 sm750hdmidrm.softscale_wide=1 sm750hdmidrm.sharpen=1 sm750hdmidrm.scanout_format=rgb565-bbdither sm750hdmidrm.double_shadow=1 sm750hdmidrm.enable_dma=1
```

Some catalogue refresh rates run the SM750 DVO path or the monitor outside
published limits. A mode being listed does not guarantee that every monitor,
cable, KVM, or adapter will accept it. See
[Modes and clock risks](docs/MODES.md) before experimenting.

## Custom desktop dither

`rgb565-bbdither` uses an 8x8 ordered dither conceived and tuned by KodeMunkie
specifically for desktop use on this card. It includes an adjustable
green-channel correction and anchors the pattern to screen coordinates, so
small updates do not make the pattern crawl or leave mismatched patches.

## Screenshots

The three images below visualise the complete ultrawide conversion process.
Click any preview to open the original image. The logical desktop and physical
photo are shown at 50% page width. The compressed preview is shown at 41.56%,
preserving the `2048 / 2464` width reduction instead of stretching both digital
stages to the same preview width.

### 1. Logical desktop

<a href="docs/images/desktop-2464x1080-logical.png"><img src="docs/images/desktop-2464x1080-logical.png" alt="Original 2464x1080 logical desktop" width="50%"></a>

The driver receives this original `2464x1080` logical desktop.

### 2. Compressed HDMI scanout

<a href="docs/images/desktop-2048x1080-scanout.png"><img src="docs/images/desktop-2048x1080-scanout.png" alt="The 2464x1080 desktop compressed to the SM750's 2048x1080 hardware scanout" width="41.56%"></a>

The image above deliberately looks horizontally squashed. It reproduces the
`2048x1080` HDMI scanout generated by compressing the `2464x1080` logical
desktop to the SM750's hardware-width limit.

### 3. Physical monitor result

<a href="docs/images/desktop-2560x1080-physical.jpg"><img src="docs/images/desktop-2560x1080-physical.jpg" alt="The monitor stretching the SM750 scanout across its physical ultrawide panel" width="50%"></a>

The photographed result fills the monitor's `2560x1080` physical pixels at
75 Hz, using the driver's 16-bit `rgb565-bbdither` dithered scanout.

The monitor's **FULL WIDESCREEN STRETCH** option expands that signal across its
physical `2560x1080` panel. The complete path is therefore:

*Logical desktop -> SM750 hardware scanout -> physical panel, stretched by the
monitor.*

## Troubleshooting

Check binding and recent driver messages:

```sh
lspci -nnk -d 126f:0750
journalctl -k -b | grep -iE 'sm750|sii902'
```

If an experimental mode gives no signal, boot an older kernel entry or remove
the added `sm750hdmidrm.*` GRUB options from recovery access. Testing and live
reload guidance is in [Testing and recovery](docs/TESTING.md).

## Developers

- [Hardware and capability comparison](docs/HARDWARE.md)
- [Module parameters](docs/PARAMETERS.md)
- [Modes and clock risks](docs/MODES.md)
- [Provenance and licensing audit](docs/PROVENANCE.md)
- [Testing and recovery](docs/TESTING.md)
- [Manual CI build](https://github.com/KodeMunkie/sm750hdmifb/actions/workflows/build.yml)

The official SM750 specification is known to be wrong for at least one
hardware-observed partial-update boundary. Empirically verified workarounds are
documented in the source and must not be removed solely because an ideal model
or specification says they are unnecessary.

## Licence and disclosure

The project is **GPL-2.0-only**. DDK-derived files came from Linux's GPL-2.0
staging `sm750fb` driver. No proprietary Silicon Motion driver, binary object,
firmware blob, or closed library is included or linked. See the
[provenance audit](docs/PROVENANCE.md) for details.

This project was created through extensive AI-assisted or "vibe coding". I have
specified and physically tested the behaviour and designed the custom dither,
but do not claim enough Linux DRM, KMS, DKMS, or kernel-framework expertise to
independently guarantee every implementation detail. The source is published
for review and improvement, not as a claim of upstream kernel quality. Expert
review is welcome.

**YOU USE THIS EXPERIMENTAL DRIVER AND EVERY NON-EDID OR ABOVE-SPECIFICATION
MODE ENTIRELY AT YOUR OWN RISK. YOU ARE RESPONSIBLE FOR RECOVERY ACCESS,
BACKUPS, MODE SELECTION AND HARDWARE COMPATIBILITY. THE AUTHORS AND
CONTRIBUTORS ARE NOT RESPONSIBLE FOR LOSS OF DISPLAY, DATA, TIME, HARDWARE,
INCOME OR ANY OTHER DIRECT OR INDIRECT LOSS.**

The software is provided without warranty. See [LICENSE](LICENSE).
