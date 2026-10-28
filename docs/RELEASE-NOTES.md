<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Release notes

## 0.5.9 — Newer kernel support

- Add compatibility with newer Linux kernel interfaces through the current
  7.4 development code, including the 7.2 atomic transaction rename and the
  state-creation callbacks needed after removal of the old CRTC reset API.
- Preserve compatibility with the existing Linux 6.17 and 7.0 interfaces.
- **Kernels >= 7.1 are untested.** Offline build and regression checks passed
  against 7.1, 7.2, 7.3-rc3 and a 7.4 merge-window snapshot; these results do
  not establish boot or runtime behavior. Final 7.4 requires another build
  check when it is released.

The offline validation matrix and hardware verification steps are documented
in [Testing and recovery](TESTING.md).
