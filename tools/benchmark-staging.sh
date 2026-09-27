#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
test "$EUID" -eq 0
project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
parameters=/sys/module/sm750hdmidrm/parameters
test -w "$parameters/dma_batch_rows"
test "$(cat "$parameters/backbuffer_staging")" = Y
desktop_display=${1:-:0}
desktop_auth=${2:-$HOME/.Xauthority}
test -r "$desktop_auth"
output=$(mktemp -d "${TMPDIR:-/tmp}/sm750-batch-benchmark-XXXXXX")
chmod 0755 "$output"
old_timing=$(cat "$parameters/staging_timing")
cleanup() {
 printf '16\n' >"$parameters/dma_batch_rows"
 printf '%s\n' "$old_timing" >"$parameters/staging_timing"
 chmod -R a+rX "$output"
}
trap cleanup EXIT
printf 'Y\n' >"$parameters/staging_timing"
phase=0
for rows in 4 8 16 32 64 96 128 128 96 64 32 16 8 4; do
 phase=$((phase + 1))
 printf '%s\n' "$rows" >"$parameters/dma_batch_rows"
 sleep 1
 # journalctl accepts an epoch timestamp with a dot, independent of locale.
 since=@$(date +%s.%6N)
 echo "Phase $phase/14: $rows rows. Press any key in the pattern window to abort."
desktop_user=${3:-$(stat -c %U "$desktop_auth")}
 runuser -u "$desktop_user" -- env DISPLAY="$desktop_display" XAUTHORITY="$desktop_auth" \
  "$project/tools/staging-pattern"
 sleep 1
 journalctl -k --since "$since" --no-pager >"$output/phase-${phase}-${rows}.log"
 if grep -Eq 'flip timed out|DMA1 shadow upload timed out' "$output/phase-${phase}-${rows}.log"; then
  echo 'Driver timeout encountered; aborting comparison.' >&2
  exit 1
 fi
done
python3 "$project/tools/staging-results.py" "$output" | tee "$output/results.txt"
echo "Capture directory: $output"
