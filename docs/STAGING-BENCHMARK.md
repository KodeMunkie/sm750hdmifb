# DMA batch comparison

Build with `make check staging-benchmark` and load the candidate module after a
normal desktop logout. Only the current kernel needs to change. The default is
sixteen rows, with two coherent buffers each reserving room for 128 rows.

From a graphical desktop terminal in the repository root run:

```sh
pkexec /bin/bash tools/benchmark-staging.sh "$DISPLAY" "$HOME/.Xauthority"
```

The ordinary X11 window requests fullscreen through the window manager. It
alternates the same two checkerboard images 44 times per phase, paced at 250 ms.
Press any key in that window to abort. The fourteen phases compare 4, 8, 16, 32,
64, 96 and 128 rows, then repeat in reverse to reduce ordering effects. Allow about three minutes and
leave the benchmark window visible. The script restores sixteen rows on exit.

`upload_us` measures conversion, snapshot updates, CPU staging copies, DMA
submission and completion, starting before conversion and ending after the
final DMA drain. `flip_us` measures presentation separately, including acquiring
the mode lock and waiting for the pending-flip bit to clear. Neither metric is
the application's latency or the desktop compositor's rendering time.

Only exact full-frame staged updates emit timing records. The analyser requires
at least 30 successful full-frame DMA samples in every phase, discards the first
four warmup samples and reports median/p95 upload times. A compositor that
reports split partial damage can make the result UNVERIFIED; do not infer an
optimal batch size from an incomplete run. Check that both directions agree and
repeat before treating a small median difference as a performance win. Kernel
logging is enabled only for the measurement and happens after the sampled times.

The dated capture directory under `/tmp` (or `$TMPDIR`) retains each phase log
and the summary. No default configuration is changed based on the result.

This comparison targets the 2048-pixel RGB565 physical scanout (4096-byte rows),
including the 2464/2560 logical softscale modes. The analyser checks that each
frame's batch count matches the selected size, including its shorter final
batch. Other scanout widths can bypass row batching and will be rejected.

The default is 16 rows, matching the observed higher frame rate. In the expanded
capture, mean upload plus flip time was 96.465 ms at 16 rows versus 96.530 ms
at eight rows. This is a small difference; upload time alone favoured eight.
