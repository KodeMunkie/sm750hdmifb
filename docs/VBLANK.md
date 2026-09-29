<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Vblank timer locking

The driver uses its private hrtimer on all supported kernels. Linux 7.x uses
CRTC enable/disable hooks; Linux 6.x uses the simple display pipe hooks. Both
select the same timer implementation and retain DRM vblank event pacing.
Timestamps use DRM's callback-time fallback instead of the generic timer's
synthetic timestamp helper, as the pre-7.0 private timer already did.

The generic DRM timer helper in Linux 7.0.14 can spin waiting for an earlier
callback to finish when restarting a timer. Its enable hook runs while DRM
holds `vblank_time_lock`. The old callback needs that lock, or `event_lock`,
to finish, so a rapid disable/re-enable can deadlock. The driver avoids that
helper rather than waiting for the callback under a DRM lock.

## Locking rules

- `vblank_lock` protects timer enable state, timing, start and nonblocking
  cancellation. Starting and cancelling under the same lock prevents an older
  disable from cancelling a newly enabled timer.
- A callback releases `vblank_lock` before calling `drm_crtc_handle_vblank()`.
  The callback therefore never holds the private lock while acquiring DRM locks.
- Enable can rearm a running timer without waiting. If a previous callback has
  not yet processed its tick and sees the timer already queued, it returns
  without forwarding the new expiry or reporting an obsolete tick. The hrtimer
  core preserves a timer explicitly queued while its callback was running.
- Disable uses [`hrtimer_try_to_cancel()`](https://docs.kernel.org/driver-api/basics.html#c.hrtimer_try_to_cancel), which does not wait for a callback.
  A callback already past its enable check can finish and cause one final
  invocation; that invocation observes the disabled state and stops.
- Remove, system shutdown and suspend call `hrtimer_cancel()` only after DRM
  atomic shutdown or suspend has returned and released its locks. This ensures
  all callbacks have finished before device teardown or power loss.

## Validation

`make check` includes `tests/test-vblank-timer.py`. It compiles the actual
driver timer function bodies with a userspace model of the relevant hrtimer
and lock semantics. Deterministic schedules exercise callbacks blocked on
DRM locks, rapid disable/re-enable, a callback superseded before taking the
private lock, cancellation racing with enable, and synchronous teardown.
The normal cases also check timing and disabled callbacks.

Module checks require the private timer callback on every kernel and reject
dependencies on the generic DRM timer helpers. `make check-all-kernels`
builds each available supported kernel/scanout-format combination.

The schedule tests do not load the module or reproduce a hardware shutdown.
They verify the identified lock dependency and timer rearm behavior; a real
power-off test is still needed to validate behavior on a particular board.
