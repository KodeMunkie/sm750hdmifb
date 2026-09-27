# DMA batch comparison

Build with `make check staging-benchmark` and load the candidate module after a
normal desktop logout. Only the current kernel needs to change. The default is
still eight rows, with two coherent buffers each reserving room for 128 rows.

From the graphical desktop terminal run:

```sh
pkexec /bin/bash /home/brownb2/Work/sm750hdmifb/tools/benchmark-staging.sh "$DISPLAY" "$HOME/.Xauthority"
```

The ordinary X11 window requests fullscreen through the window manager. It
alternates the same two checkerboard images 44 times per phase, paced at 250 ms.
Press any key in that window to abort. The fourteen phases compare 4, 8, 16, 32,
64, 96 and 128 rows, then repeat in reverse to reduce ordering effects. Allow about three minutes and
leave the benchmark window visible. The script restores eight rows on exit.

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

The dated capture directory under `/home/brownb2` retains each phase log and the
summary. No default configuration is changed based on the result.

This comparison targets the 2048-pixel RGB565 physical scanout (4096-byte rows),
including the 2464/2560 logical softscale modes. The analyser checks that each
frame's batch count matches the selected size, including its shorter final
batch. Other scanout widths can bypass row batching and will be rejected.

Two local benchmark runs favoured the retained eight-row default: median upload
90.200/90.336 ms at eight rows versus 91.722/91.831 ms at 32 rows. Larger
batches reduced submission counts but did not improve upload time.
