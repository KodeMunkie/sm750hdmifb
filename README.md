<!-- SPDX-License-Identifier: GPL-2.0-only -->
# SM750 HDMI DRM driver

`sm750hdmifb` is an experimental out-of-tree Linux DRM/KMS driver for the
single-HDMI `SE-DP750A-HDMI` adapter built around the Silicon Motion SM750G10
and a Silicon Image SiI9024A transmitter. It turns this specific inexpensive
PCIe display card into a usable Cinnamon/Xorg desktop adapter while leaving
other GPUs available for compute.

The Debian package and DKMS project are named `sm750hdmifb`. The kernel module
is deliberately named `sm750hdmidrm.ko`, avoiding a collision with Linux's old
staging `sm750fb.ko` module.

> **Experimental hardware driver:** this code programs display clocks and an
> external HDMI transmitter. Some optional modes exceed published limits.
> Unsupported combinations can produce a blank, unstable or distorted image.
> Keep another way to access the machine and read [Modes and clocks](docs/MODES.md)
> before enabling the driver catalogue or ultrawide scaling.

## Screenshots

![2464x1080 logical Cinnamon desktop](docs/images/desktop-2464x1080.png)

*2464x1080 logical desktop, software-scaled to a 2048x1080 HDMI signal.*

![2560x1080 logical Cinnamon desktop](docs/images/desktop-2560x1080.png)

*2560x1080 logical desktop using the optimized 5:4 horizontal scaler.*

## Highlights

- Atomic DRM/KMS and fbdev emulation
- HDMI connector, EDID and hotplug polling
- Driver and EDID mode policies
- XRGB8888, RGB565 and dithered RGB565 scanout
- 2464x1080 and 2560x1080 logical desktops
- Optional 8% post-scale sharpening
- Hardware cursor with software fallback
- Verified DMA1 shadow uploads with CPU fallback
- Difference-trimmed double shadow buffering
- Paced DRM vblank events

### Benjamin Brown's custom dither

The recommended scanout backend, `rgb565-bbdither`, is a custom 8x8 ordered
dither conceived and tuned by Benjamin Brown, with implementation and testing
assisted by AI. Its rank map and optional green-gain adjustment were developed
for desktop use on this card rather than copied from a generic colour library.
The pattern is anchored to absolute screen coordinates, so partial damage does
not change its phase and create crawling or mismatched patches.

Userspace still renders in XRGB8888. The driver converts damaged regions into
physical RGB565 immediately before upload. This halves device-bound pixel data
from four bytes to two bytes per pixel, which materially improves responsiveness
on the SM750's narrow PCIe 1.1 x1 link. Plain RGB565 and full XRGB8888 remain
available through module or GRUB parameters.

## Supported hardware

The tested board is sold or marked as `SE-DP750A-HDMI` and contains:

- Silicon Motion `SM750G10-AC`, revision A1
- PCI vendor/device ID `126f:0750`
- Silicon Image/Lattice `SiI9024ACNU` HDMI transmitter
- 16 MiB integrated display memory
- SiI9024A software-I2C wiring on SM750 GPIO12/13
- PCIe 1.1 x1; the tested link negotiates 2.5 GT/s x1

The PCI ID alone does not guarantee compatibility. Other SM750 boards may use
VGA, different DVO transmitters or different GPIO wiring. See
[Hardware and capabilities](docs/HARDWARE.md).

## Build from source

Ubuntu 24.04 or Linux Mint 22 users need DKMS, a compiler and headers for the
kernel they intend to boot:

```sh
sudo apt update
sudo apt install build-essential dkms linux-headers-$(uname -r) libdrm-dev
git clone https://github.com/KodeMunkie/sm750hdmifb.git
cd sm750hdmifb
make check
./build-package.sh
```

The build creates these files under `dist/`:

```text
sm750hdmifb_0.5.3_all.deb
sm750hdmifb-0.5.3-source.tar.gz
SHA256SUMS
```

Install the locally built package:

```sh
cd dist
sha256sum -c SHA256SUMS
sudo apt install ./sm750hdmifb_0.5.3_all.deb
sudo reboot
```

A reboot is strongly recommended. It allows the initramfs blacklist to prevent
the bundled `sm750fb` module binding first and avoids replacing a live display
driver under Xorg.

After reboot:

```sh
lspci -nnk -d 126f:0750
modinfo sm750hdmidrm
cat /sys/module/sm750hdmidrm/parameters/scanout_format
```

Prebuilt packages, once published, will be available from
[GitHub Releases](https://github.com/KodeMunkie/sm750hdmifb/releases). The
[manual build workflow](https://github.com/KodeMunkie/sm750hdmifb/actions/workflows/build.yml)
is intentionally not triggered on every commit. Building locally remains the
recommended path because DKMS compiles directly against the target machine's
kernel headers.

## Recommended ultrawide profile

Add module arguments to the existing `GRUB_CMDLINE_LINUX_DEFAULT` value in
`/etc/default/grub`:

```text
sm750hdmidrm.edid_only=0 sm750hdmidrm.softscale_wide=1 sm750hdmidrm.sharpen=1 sm750hdmidrm.scanout_format=rgb565-bbdither sm750hdmidrm.double_shadow=1
```

Then run `sudo update-grub` and reboot. This enables the driver catalogue,
2464/2560 logical modes, 8% sharpening, dithered RGB565 and difference-trimmed
double shadowing. Hardware cursor is enabled by default; DMA is opt-in with
`sm750hdmidrm.enable_dma=1` because CPU uploads are more responsive on the
development system.

To return to full 32-bit scanout, use:

```text
sm750hdmidrm.scanout_format=xrgb8888
```

All parameters, defaults, interactions and diagnostic switches are documented
in [Module parameters](docs/PARAMETERS.md).

## Soft scaling and the 2048 limit

The SM750 primary graphics plane stores its right edge in an 11-bit coordinate
field. The largest representable width is therefore 2048 pixels. Reducing the
height does not free bits for extra width.

With `softscale_wide=1`, userspace receives a real 2464x1080 or 2560x1080
logical framebuffer. Before scanout, the driver horizontally filters that image
to 2048x1080. The monitor receives a 2048x1080 timing; this is not a native
2560-pixel signal. The 2560 mode uses an optimized 5:4 path, while 2464 uses the
general mapped scaler. Optional sharpening adds a subtle fixed 8% horizontal
contrast correction after compression.

## Licence and provenance

This project is licensed under **GNU GPL version 2 only**. The included
Silicon Motion DDK-derived files come from Linux's GPL-2.0 staging `sm750fb`
driver. No proprietary Silicon Motion driver, binary object, firmware blob or
closed library is included or linked. The custom DRM, scaling and dither code
is source code distributed under the same compatible licence.

The development archive used several vendor drivers and an option ROM as
hardware references. Those materials are not shipped. Functional register
addresses and a diagnostic register/value sequence are documented separately
from source-code provenance. See [Provenance and licensing](docs/PROVENANCE.md)
for the file-level audit.

## AI-assisted development disclosure

This project was created through extensive AI-assisted or "vibe coding" under
Benjamin Brown's direction. Benjamin designed the intended behaviour, performed
the physical testing and developed the custom dither concept, but does not
claim sufficient personal expertise in Linux DRM, KMS, DKMS or kernel-driver
frameworks to independently guarantee every implementation detail. The source
is published for review, learning and improvement, not as a claim of upstream
kernel quality. Expert review is warmly encouraged.

The software is provided without warranty. See `LICENSE` for the legal terms.

## Further reading

- [Module parameters](docs/PARAMETERS.md)
- [Modes and clock risks](docs/MODES.md)
- [Hardware and capability comparison](docs/HARDWARE.md)
- [Provenance and licensing audit](docs/PROVENANCE.md)
- [Testing and recovery](docs/TESTING.md)
