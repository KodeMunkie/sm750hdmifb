#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

readonly project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
readonly package=sm750hdmifb
readonly module=sm750hdmidrm
readonly version=0.5.5
readonly arch=all
readonly build_root="${project_dir}/build/package/${package}_${version}_${arch}"
readonly dist_dir="${project_dir}/dist"
readonly deb="${dist_dir}/${package}_${version}_${arch}.deb"
readonly source_archive="${dist_dir}/${package}-${version}-source.tar.gz"

rm -rf "${build_root}"
install -d "${build_root}/DEBIAN" \
	"${build_root}/usr/src/${package}-${version}/src" \
	"${build_root}/usr/share/doc/${package}" \
	"${build_root}/usr/lib/${package}" \
	"${build_root}/etc/modprobe.d" \
	"${build_root}/etc/modules-load.d" \
	"${build_root}/usr/share/X11/xorg.conf.d" \
	"${dist_dir}"

install -m 0644 "${project_dir}/Makefile" "${project_dir}/README.md" \
	"${project_dir}/LICENSE" "${project_dir}/dkms.conf" \
	"${build_root}/usr/src/${package}-${version}/"
install -m 0644 "${project_dir}/src/Makefile" "${project_dir}/src/Kconfig" \
	"${project_dir}/src"/ddk750*.c "${project_dir}/src"/ddk750*.h \
	"${project_dir}/src/sii902x.c" "${project_dir}/src/sii902x.h" \
	"${project_dir}/src/sm750_dither.c" \
	"${project_dir}/src/sm750_dither.h" \
	"${project_dir}/src/sm750_drm.c" \
	"${project_dir}/src/sm750_drm_mode.c" \
	"${project_dir}/src/sm750_drm_mode.h" \
	"${build_root}/usr/src/${package}-${version}/src/"
install -m 0644 "${project_dir}/packaging/sm750hdmidrm.conf" \
	"${build_root}/etc/modprobe.d/sm750hdmidrm.conf"
install -m 0644 "${project_dir}/packaging/sm750hdmidrm.modules-load" \
	"${build_root}/etc/modules-load.d/sm750hdmidrm.conf"
install -m 0644 "${project_dir}/packaging/20-sm750hdmidrm.conf" \
	"${build_root}/usr/share/X11/xorg.conf.d/20-sm750hdmidrm.conf"
install -m 0755 "${project_dir}/packaging/write-xorg-config.sh" \
	"${build_root}/usr/lib/${package}/write-xorg-config"
install -m 0644 "${project_dir}/packaging/copyright" \
	"${build_root}/usr/share/doc/${package}/copyright"
install -m 0755 "${project_dir}/packaging/drm-postinst" \
	"${build_root}/DEBIAN/postinst"
install -m 0755 "${project_dir}/packaging/drm-prerm" \
	"${build_root}/DEBIAN/prerm"
install -m 0755 "${project_dir}/packaging/drm-postrm" \
	"${build_root}/DEBIAN/postrm"

cat >"${build_root}/DEBIAN/control" <<EOF
Package: ${package}
Version: ${version}
Section: kernel
Priority: optional
Architecture: ${arch}
Maintainer: kodemunkie <kodemunkie@users.noreply.github.com>
Depends: dkms, make, gcc
Provides: sm750hdmi-dkms
Conflicts: sm750hdmi-dkms, sm750hdmidrm-xrgb8888-dkms, sm750hdmidrm-rgb565-dither-dkms
Replaces: sm750hdmi-dkms, sm750hdmidrm-xrgb8888-dkms, sm750hdmidrm-rgb565-dither-dkms
Description: Experimental DRM/KMS driver for SM750G10 HDMI cards
 Out-of-tree DRM driver for SE-DP750A-HDMI boards using an SM750G10 and
 external SiI9024A HDMI transmitter. Includes EDID, atomic modesetting,
 RGB565 dither, wide softscaling, coalesced shadow updates, hardware cursor
 and interrupt-completed DMA shadow uploads.
EOF

printf '/etc/modprobe.d/%s.conf\n/etc/modules-load.d/%s.conf\n' \
	"${module}" "${module}" >"${build_root}/DEBIAN/conffiles"
dpkg-deb --root-owner-group --build "${build_root}" "${deb}"

tar -C "${project_dir}" -czf "${source_archive}" \
	--transform="s|^|${package}-${version}/|" \
	--exclude-vcs --exclude='./build' --exclude='./dist' \
	--exclude='src/*.o' --exclude='src/*.ko' --exclude='src/*.cmd' \
	--exclude='src/.*.cmd' --exclude='src/*.mod' \
	--exclude='src/*.mod.c' --exclude='src/Module.symvers' \
	--exclude='src/modules.order' \
	--exclude='tests/check-vblank' \
	--exclude='tests/test-bbdither-rgb565' \
	--exclude='tests/test-scale-optimizations' \
	.gitignore LICENSE Makefile README.md build-package.sh dkms.conf \
	src docs tests tools packaging .github

(cd "${dist_dir}" && sha256sum "$(basename "${deb}")" \
	"$(basename "${source_archive}")" >SHA256SUMS)
printf '%s\n%s\n%s\n' "${deb}" "${source_archive}" \
	"${dist_dir}/SHA256SUMS"
