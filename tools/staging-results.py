#!/usr/bin/env python3
"""Summarise completed full-frame samples, excluding warmup and failed flips."""
import pathlib
import re
import statistics
import sys

pattern = re.compile(r'staging-bench rows=(\d+) frame_bytes=(\d+) '
                     r'dma_bytes=(\d+) batches=(\d+) upload_us=(\d+) '
                     r'flip_us=(\d+) ok=(\d+)')
directory = pathlib.Path(sys.argv[1])
groups = {}
invalid = False
for log in sorted(directory.glob('phase-*.log')):
    samples = [tuple(map(int, match.groups()))
               for match in pattern.finditer(log.read_text())]
    rows = int(log.stem.split('-')[-1])
    valid = [s for s in samples if s[0] == rows and s[1] == s[2]
             and s[6] == 1 and s[3] == (s[1] + rows * 4096 - 1) // (rows * 4096)]
    if len(valid) < 30 or any(s[6] != 1 for s in samples):
        print(f'UNVERIFIED: {log.name}: {len(valid)} complete DMA frames; '
              'need 30 and no failed flips')
        invalid = True
    else:
        groups.setdefault(rows, []).extend(valid[4:])
if invalid or set(groups) != {4, 8, 16, 32, 64, 96, 128} or len(list(directory.glob('phase-*.log'))) != 14:
    sys.exit('No batch-size recommendation: incomplete or invalid capture.')
print('rows  frames  median upload ms  p95 upload ms  median flip ms  batches/frame')
for rows, samples in sorted(groups.items()):
    times = sorted(s[4] / 1000 for s in samples)
    p95 = times[min(len(times) - 1, int((len(times) - 1) * .95))]
    print(f'{rows:4} {len(samples):7} {statistics.median(times):17.3f} '
          f'{p95:14.3f} {statistics.median(s[5] for s in samples)/1000:15.3f} '
          f'{statistics.median(s[3] for s in samples):14.0f}')
print('Upload excludes vblank wait; defaults have been restored to eight rows.')
