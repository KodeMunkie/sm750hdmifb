<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Testing and recovery

## Offline checks

`make check` builds the module for the running kernel and runs metadata, mode,
dither, softscale, sharpening, cursor, DMA, changed-region, Xorg and packaging
checks. DRM documentation calls a reported changed screen region "damage";
this means pixels needing refresh, not corruption or physical damage.
It does not install or load the resulting module.

`make check-all-kernels` repeats module builds for all supported installed
kernel-header trees and all three scanout defaults. The dithered RGB565 build
runs the full regression suite on each kernel; the other two defaults receive
module metadata and compiler-flag checks.

To include downloaded headers without installing a kernel, unpack both the
common and generic header packages into the same directory, then pass the
generic build directories explicitly:

```sh
CC=/path/to/gcc-15 JOBS=12 ./tests/build-all-kernels.sh \
    /path/to/unpacked/usr/src/linux-headers-VERSION-generic \
    /lib/modules/$(uname -r)/build
```

Use a compiler supported by the target headers. Run builds and packaging in an
isolated copy outside any source-only synced checkout; the scripts write build
outputs into that copy. Run `make KDIR=/path/to/headers check` there for the full
regression suite. No command above installs or loads the resulting module.

`./build-package.sh` creates the DKMS package and source archive. Run
`tests/audit-release.sh` afterward to produce the final audit report.

### Offline build validation (2026-10-28)

**Kernels >= 7.1 are untested.** The results below cover offline compilation
and regression checks, not boot or runtime validation on those kernels.

The offline matrix includes these header trees and all three scanout defaults:

| Kernel headers | Purpose |
|---|---|
| Ubuntu 6.17.0-42 | Minimum supported series |
| Mainline 6.18.0-061800 | Current upstream LTS baseline |
| Ubuntu 7.0.0-38 and 7.0.0-42 | Existing distro kernel interfaces |
| Mainline 7.1.0-070100 | Last atomic-state transaction interface |
| Mainline 7.2.0-070200 | Renamed `drm_atomic_commit` transaction interface |
| Mainline 7.3.0-070300rc3 | New CRTC/connector state-creation callbacks |
| Daily 7.3.0-070300daily20261028 | 7.4 merge-window CRTC/plane reset removal |

The daily snapshot still reports version 7.3 because 7.4-rc1 has not been
tagged. It exercises the merged 7.4 DRM changes, but is not a final 7.4 release.
Repeat the matrix when 7.4-rc1 and final 7.4 headers become available. These
checks do not establish display, DMA, suspend or hotplug behavior on hardware;
that still needs the hardware verification below.

## Hardware verification

After a deliberate package installation and reboot, verify:

```sh
lspci -nnk -d 126f:0750
modinfo sm750hdmidrm
xrandr --current
journalctl -b -k | grep -i sm750
```

Check login, logout, mode changes, cursor movement, window movement, partial
clock-widget updates, suspend/resume and a cold boot. Test full-screen updates
in both RGB565 dither and XRGB8888 before calling a build validated.

## Recovery

Keep remote access or another display adapter. Remove experimental kernel
arguments and run `sudo update-grub` if a mode is unstable. To remove the
package from a remote shell:

```sh
sudo apt remove sm750hdmifb
sudo update-initramfs -u
sudo reboot
```

Removal restores only predecessor files previously renamed by this package.
No timer, rollback service or display-manager restart service is installed.
