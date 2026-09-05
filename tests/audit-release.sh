#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dist_dir=$project_dir/dist
module=$project_dir/src/sm750hdmidrm.ko
deb=$dist_dir/sm750hdmifb_0.5.6_all.deb
source_archive=$dist_dir/sm750hdmifb-0.5.6-source.tar.gz
report=$dist_dir/RELEASE-AUDIT.txt

for required in "$module" "$deb" "$source_archive" "$project_dir/LICENSE"; do
	[[ -s $required ]] || {
		echo "Missing release input: $required" >&2
		exit 1
	}
done

if rg -n 'sm750hdmifb[-_]0\.5\.[0-5]' \
	"$project_dir/.github/workflows/build.yml"; then
	echo "Manual build workflow contains a stale release artifact version." >&2
	exit 1
fi

for forbidden in .audit-source .package-build xorg-sm750shadow xorg-sm750.strace; do
	if tar -tzf "$source_archive" | grep -F "$forbidden" >/dev/null ||
		dpkg-deb -c "$deb" | grep -F "$forbidden" >/dev/null; then
		echo "Forbidden development content packaged: $forbidden" >&2
		exit 1
	fi
done

release_text=(
	"$project_dir/.github"
	"$project_dir/src"
	"$project_dir/tests"
	"$project_dir/tools"
	"$project_dir/packaging"
	"$project_dir/README.md"
	"$project_dir/docs"
	"$project_dir/Makefile"
	"$project_dir/build-package.sh"
	"$project_dir/dkms.conf"
)

# Keep this generic: adding a real local username or machine ID to the audit
# would itself disclose the value in the published source tree.
private_pattern='/home/[[:alnum:]_.-]+|/Users/[[:alnum:]_.-]+|[A-Za-z]:\\Users\\|ghp_[[:alnum:]]{20,}|github_pat_[[:alnum:]_]{20,}'
if rg -n -i "$private_pattern" --glob '!audit-release.sh' "${release_text[@]}"; then
	echo "Local path, machine identifier, or credential pattern remains." >&2
	exit 1
fi

if rg -n -i 'without (the )?written consent|all rights reserved|confidential' \
	--glob '!audit-release.sh' \
	"${release_text[@]}"; then
	echo "Restrictive licence marker found in releasable code." >&2
	exit 1
fi

tmpdir=$(mktemp -d /tmp/sm750-release-audit.XXXXXX)
trap 'rm -rf -- "$tmpdir"' EXIT
dpkg-deb -x "$deb" "$tmpdir/deb"
tar -xzf "$source_archive" -C "$tmpdir"
if find "$tmpdir" -type f \( -name '*.o' -o -name '*.ko' -o -name '*.so' \
	-o -name '*.a' -o -name '*.bin' -o -name '*.rom' \) | grep .; then
	echo "Source or Debian package contains an unexpected binary object." >&2
	exit 1
fi

mkdir -p "$dist_dir"
{
	echo "SM750HDMIFB RELEASE AUDIT"
	echo "Generated: $(date --iso-8601=seconds)"
	echo
	echo "LICENCE"
	echo "Project licence: GNU GPL version 2 only (GPL-2.0-only)"
	echo "Linux staging DDK files: SPDX GPL-2.0"
	echo "Proprietary objects, firmware and vendor audit sources packaged: no"
	echo
	echo "MODULE"
	modinfo "$module" | sed -n '/^filename:/p;/^license:/p;/^description:/p;/^author:/p;/^alias:/p'
	echo
	echo "UNDEFINED KERNEL SYMBOLS"
	nm -u "$module" | sort
	echo
	echo "DEBIAN PACKAGE CONTENTS"
	dpkg-deb -c "$deb"
	echo
	echo "SOURCE ARCHIVE CONTENTS"
	tar -tzf "$source_archive"
	echo
	echo "CHECKSUMS"
	(cd "$dist_dir" && sha256sum sm750hdmifb_0.5.6_all.deb \
		sm750hdmifb-0.5.6-source.tar.gz)
} >"$report"

echo "Release audit passed: $report"
