#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
builder=$project_dir/build-package.sh
dkms_config=$project_dir/dkms.conf
workflow=$project_dir/.github/workflows/build.yml

version=$(sed -n 's/^readonly version=//p' "$builder")
test -n "$version"
grep -F "PACKAGE_VERSION=\"$version\"" "$dkms_config" >/dev/null

artifact_count=0
while IFS= read -r artifact; do
	case "$artifact" in
		sm750hdmifb-"$version"|sm750hdmifb_"$version"_all.deb) ;;
		*)
			echo "Workflow references stale release artifact: $artifact" >&2
			exit 1
			;;
	esac
	artifact_count=$((artifact_count + 1))
done < <(rg -o 'sm750hdmifb(-[0-9]+\.[0-9]+\.[0-9]+|_[0-9]+\.[0-9]+\.[0-9]+_all\.deb)' "$workflow")

test "$artifact_count" -gt 0
echo "Release version consistency checks passed: $version"
