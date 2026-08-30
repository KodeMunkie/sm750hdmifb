<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Testing and recovery

## Offline checks

`make check` builds the module for the running kernel and runs metadata, mode,
dither, softscale, sharpening, cursor, DMA, changed-region, Xorg and packaging
checks. DRM documentation calls a reported changed screen region "damage";
this means pixels needing refresh, not corruption or physical damage.
It does not install or load the resulting module.

`make check-all-kernels` repeats module builds for all supported installed
kernel-header trees and all three scanout defaults.

`./build-package.sh` creates the DKMS package and source archive. Run
`tests/audit-release.sh` afterward to produce the final audit report.

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
