#!/usr/bin/env python3
"""Ensure incomplete, partial-DMA and failed-flip runs cannot win a comparison."""
import pathlib
import subprocess
import tempfile
import unittest

analyser = pathlib.Path(__file__).resolve().parents[1] / 'tools/staging-results.py'


class ResultsTest(unittest.TestCase):
    def run_fixture(self, frames=40, failed=False, partial=False):
        with tempfile.TemporaryDirectory() as folder:
            for phase, rows in enumerate((4, 8, 16, 32, 64, 96, 128, 128, 96, 64, 32, 16, 8, 4), 1):
                lines = []
                for frame in range(frames):
                    ok = 0 if failed and phase == 2 and frame == 10 else 1
                    size = 100 if partial and phase == 3 else 4423680
                    batches = (4423680 + rows * 4096 - 1) // (rows * 4096)
                    lines.append(f'staging-bench rows={rows} frame_bytes=4423680 '
                                 f'dma_bytes={size} batches={batches} upload_us=20000 '
                                 f'flip_us=1000 ok={ok}\n')
                pathlib.Path(folder, f'phase-{phase}-{rows}.log').write_text(''.join(lines))
            return subprocess.run(['python3', str(analyser), folder],
                                  capture_output=True, text=True)

    def test_complete_comparison(self):
        result = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('20.000', result.stdout)

    def test_insufficient_frames(self):
        self.assertNotEqual(self.run_fixture(frames=29).returncode, 0)

    def test_failed_flip(self):
        self.assertNotEqual(self.run_fixture(failed=True).returncode, 0)

    def test_incomplete_dma_frame(self):
        self.assertNotEqual(self.run_fixture(partial=True).returncode, 0)


if __name__ == '__main__':
    unittest.main()
