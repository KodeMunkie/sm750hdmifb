<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Hardware and capabilities

## Tested adapter

| Item | Observed value |
|---|---|
| Board family | `SE-DP750A-HDMI` |
| Graphics device | `SM750G10-AC`, revision A1 |
| PCI identity | `126f:0750` |
| HDMI transmitter | `SiI9024ACNU`, ID `b0:02:03` |
| Control bus | Software I2C, GPIO12 SCL / GPIO13 SDA |
| Display memory | 16 MiB integrated DDR |
| PCIe link | 2.5 GT/s x1 observed |
| Framebuffer aperture | 64 MiB BAR0; only strapped VRAM is usable |
| MMIO aperture | 2 MiB BAR1 |

The SM750 provides panel/DVO and analog output, not a native HDMI PHY. This
board's separate SiI9024A converts its 24-bit parallel RGB/DVO stream to HDMI.

## Official specification versus this driver

| Area | SM750 published capability | Driver use or extension |
|---|---|---|
| Host link | PCI Express x1; full datasheets identify PCIe 1.1 | Works over the observed 2.5 GT/s x1 link; RGB565 reduces upload pressure. |
| Memory | 16 MiB integrated option, external configurations up to 64 MiB | Detects and bounds allocations to the tested 16 MiB strap. |
| Resolution | Up to 1920x1200 in the English product brief | Physically tested through 2048 pixels wide; 2464/2560 are logical software-scaled desktops, not native output. |
| Display paths | Dual DAC/DVO and independent display layers | Uses the primary panel/DVO controller and one HDMI connector. |
| 2D engine | 128-bit BitBLT/ROP3, colour expansion and lines | Not exposed as Xorg acceleration; rendering uses DRM shadow buffers. |
| DMA | On-chip DMA controller | DMA1 uploads verified shadow spans, with automatic CPU fallback. |
| Cursors | Two hardware cursor layers | Exposes the primary 64x64 cursor plane with a software fallback. |
| Scaling | Hardware video/layer scaling is advertised | Ultrawide desktop compression is implemented in software before scanout. |

The driver does not make the silicon natively capable of a 2560-wide raster.
Its new practical capabilities come from DRM/KMS integration, HDMI transmitter
control, damage-aware conversion, custom dithering and software scaling.

References:

- [Silicon Motion SM750 product brief](https://www.siliconmotion.com/download/3PS/a/SM750_PB_EN_201910.pdf)
- [Silicon Motion SM750 datasheet v1.4, hosted by VersaLogic](https://d3v.versalogic.com/wp-content/themes/vsl-new/assets/resources/support/mpee-v5/SM750_Datasheet_v.1.4.pdf)
