#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Exercise actual driver timer bodies with controlled userspace schedules.

The timer model preserves running/queued state, nonblocking cancellation and
explicit rearming during a callback. This does not load a kernel module.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile


SOURCE = Path(__file__).resolve().parents[1] / "src" / "sm750_drm.c"

PREFIX = r"""
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef int64_t ktime_t;
typedef pthread_mutex_t spinlock_t;
enum hrtimer_restart { HRTIMER_NORESTART, HRTIMER_RESTART };
struct hrtimer {
    pthread_mutex_t lock;
    atomic_bool queued, running;
    atomic_llong expires;
};
struct drm_display_mode { int clock, htotal, vdisplay, vtotal; };
struct drm_device { int unused; };
struct drm_crtc { struct drm_device *dev; struct drm_display_mode mode; };
struct drm_simple_display_pipe { struct drm_crtc crtc; };
struct sm750_drm_device {
    struct drm_device drm;
    struct drm_simple_display_pipe pipe;
    struct hrtimer vblank_timer;
    spinlock_t vblank_lock;
    u64 vblank_line_ns, vblank_frame_ns;
    unsigned short vblank_start, vblank_vtotal;
    bool vblank_timer_enabled;
};

static struct sm750_drm_device device;
static pthread_mutex_t event_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool pause_callback, callback_paused, release_callback;
static atomic_bool pause_cancel, cancel_paused, release_cancel;
static atomic_bool drm_entered, callback_done, enable_waiting, enable_done;
static atomic_bool sync_cancel_entered, sync_stop_done;
static atomic_int events;
static _Thread_local bool in_callback, in_enable;
static u32 scanline = 1000;

#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define pipe_to_sm750(p) container_of(p, struct sm750_drm_device, pipe)
#define to_sm750_drm(d) container_of(d, struct sm750_drm_device, drm)
#define NSEC_PER_SEC 1000000000ULL
#define SM750_VBLANK_RETRY_NS 1000000ULL
#define SM750_VBLANK_MIN_DELAY_NS 50000ULL
#define SM750_DRM_DISPLAY_CTRL 0
#define DISPLAY_CTRL_TIMING 1
#define SM750_DRM_CURRENT_LINE 1
#define SM750_DRM_CURRENT_LINE_MASK 0x7ff
#define HRTIMER_MODE_REL 0
#define ns_to_ktime(ns) ((ktime_t)(ns))
#define DIV_ROUND_CLOSEST_ULL(n, d) (((n) + (d) / 2) / (d))
#define max_t(type, a, b) ((type)(a) > (type)(b) ? (type)(a) : (type)(b))

static void tick(void) {
    const struct timespec delay = { .tv_nsec = 1000000 };
    nanosleep(&delay, NULL);
}
static void await(atomic_bool *flag) {
    for (int i = 0; i < 2000 && !atomic_load(flag); ++i) tick();
    assert(atomic_load(flag));
}
static void mock_lock(spinlock_t *lock) {
    if (in_callback && atomic_exchange(&pause_callback, false)) {
        atomic_store(&callback_paused, true);
        await(&release_callback);
    }
    if (in_enable) atomic_store(&enable_waiting, true);
    assert(!pthread_mutex_lock(lock));
}
#define spin_lock_irqsave(lock, flags) do { (flags) = 0; mock_lock(lock); } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(flags); assert(!pthread_mutex_unlock(lock)); } while (0)
static u32 peek32(u32 reg) { return reg == SM750_DRM_DISPLAY_CTRL ? DISPLAY_CTRL_TIMING : scanline; }
static bool hrtimer_is_queued(struct hrtimer *t) { return atomic_load(&t->queued); }
static void hrtimer_start(struct hrtimer *t, ktime_t delay, int mode) {
    (void)mode;
    pthread_mutex_lock(&t->lock);
    atomic_store(&t->expires, delay);
    atomic_store(&t->queued, true);
    pthread_mutex_unlock(&t->lock);
}
static void hrtimer_forward_now(struct hrtimer *t, ktime_t interval) {
    /* Forwarding an already rearmed timer is invalid in the kernel too. */
    assert(!atomic_load(&t->queued));
    atomic_fetch_add(&t->expires, interval);
}
static int hrtimer_try_to_cancel(struct hrtimer *t) {
    if (atomic_exchange(&pause_cancel, false)) {
        atomic_store(&cancel_paused, true);
        await(&release_cancel);
    }
    pthread_mutex_lock(&t->lock);
    int result = atomic_load(&t->running) ? -1 : atomic_exchange(&t->queued, false);
    pthread_mutex_unlock(&t->lock);
    return result;
}
static void hrtimer_cancel(struct hrtimer *t) {
    atomic_store(&sync_cancel_entered, true);
    while (hrtimer_try_to_cancel(t) < 0) tick();
}
static void drm_crtc_handle_vblank(struct drm_crtc *crtc) {
    (void)crtc;
    /* DRM may acquire its locks only after the private lock is released. */
    assert(!pthread_mutex_trylock(&device.vblank_lock));
    pthread_mutex_unlock(&device.vblank_lock);
    atomic_store(&drm_entered, true);
    pthread_mutex_lock(&event_lock);
    pthread_mutex_lock(&time_lock);
    atomic_fetch_add(&events, 1);
    pthread_mutex_unlock(&time_lock);
    pthread_mutex_unlock(&event_lock);
}
"""

SUFFIX = r"""
static void *fire(void *unused) {
    (void)unused;
    struct hrtimer *t = &device.vblank_timer;
    pthread_mutex_lock(&t->lock);
    atomic_store(&t->queued, false);
    atomic_store(&t->running, true);
    pthread_mutex_unlock(&t->lock);
    in_callback = true;
    enum hrtimer_restart restart = sm750_vblank_timer_fn(t);
    in_callback = false;
    pthread_mutex_lock(&t->lock);
    /* __run_hrtimer preserves an explicit arm made during the callback. */
    if (restart != HRTIMER_NORESTART && !atomic_load(&t->queued))
        atomic_store(&t->queued, true);
    atomic_store(&t->running, false);
    pthread_mutex_unlock(&t->lock);
    atomic_store(&callback_done, true);
    return NULL;
}
static void *enable(void *unused) {
    (void)unused;
    in_enable = true;
    assert(!sm750_crtc_enable_vblank(&device.pipe.crtc));
    in_enable = false;
    atomic_store(&enable_done, true);
    return NULL;
}
static void *disable(void *unused) {
    (void)unused;
    sm750_crtc_disable_vblank(&device.pipe.crtc);
    return NULL;
}
static void *stop_sync(void *unused) {
    (void)unused;
    sm750_vblank_timer_stop(&device, true);
    atomic_store(&sync_stop_done, true);
    return NULL;
}
static void init(void) {
    pthread_mutex_init(&device.vblank_lock, NULL);
    pthread_mutex_init(&device.vblank_timer.lock, NULL);
    device.pipe.crtc.dev = &device.drm;
    device.pipe.crtc.mode = (struct drm_display_mode) {
        .clock = 148500, .htotal = 2200, .vdisplay = 1080, .vtotal = 1125,
    };
}
static void check_normal(void) {
    device.pipe.crtc.mode.clock = 0;
    assert(sm750_crtc_enable_vblank(&device.pipe.crtc) == -EINVAL);
    assert(!atomic_load(&device.vblank_timer.queued));
    device.pipe.crtc.mode.clock = 148500;
    assert(!sm750_crtc_enable_vblank(&device.pipe.crtc));
    assert(device.vblank_frame_ns > 16600000 && device.vblank_frame_ns < 16700000);
    assert(atomic_load(&device.vblank_timer.expires) == (long long)(80 * device.vblank_line_ns));
    fire(NULL);
    assert(atomic_load(&events) == 1);
    assert(atomic_load(&device.vblank_timer.queued));
    sm750_crtc_disable_vblank(&device.pipe.crtc);
    fire(NULL); /* A last callback after disable must not report a tick. */
    assert(atomic_load(&events) == 1);
    assert(!atomic_load(&device.vblank_timer.queued));
}
static void check_blocked(pthread_mutex_t *lock) {
    assert(!sm750_crtc_enable_vblank(&device.pipe.crtc));
    pthread_mutex_lock(lock);
    pthread_t callback;
    pthread_create(&callback, NULL, fire, NULL);
    await(&drm_entered);
    assert(!atomic_load(&callback_done));
    /* Both hooks run under a DRM lock while the old callback needs it. */
    sm750_crtc_disable_vblank(&device.pipe.crtc);
    assert(!sm750_crtc_enable_vblank(&device.pipe.crtc));
    assert(atomic_load(&device.vblank_timer.queued));
    assert(!atomic_load(&callback_done));
    pthread_mutex_unlock(lock);
    pthread_join(callback, NULL);
    assert(atomic_load(&device.vblank_timer.queued));
    fire(NULL);
    assert(atomic_load(&events) == 2);
    sm750_vblank_timer_stop(&device, true);
}
static void check_superseded(void) {
    assert(!sm750_crtc_enable_vblank(&device.pipe.crtc));
    atomic_store(&pause_callback, true);
    pthread_t callback;
    pthread_create(&callback, NULL, fire, NULL);
    await(&callback_paused);
    sm750_crtc_disable_vblank(&device.pipe.crtc);
    assert(!sm750_crtc_enable_vblank(&device.pipe.crtc));
    long long new_expiry = atomic_load(&device.vblank_timer.expires);
    atomic_store(&release_callback, true);
    pthread_join(callback, NULL);
    assert(!atomic_load(&events));
    assert(atomic_load(&device.vblank_timer.queued));
    assert(atomic_load(&device.vblank_timer.expires) == new_expiry);
    fire(NULL);
    assert(atomic_load(&events) == 1);
    sm750_vblank_timer_stop(&device, true);
}
static void check_cancel_enable(void) {
    assert(!sm750_crtc_enable_vblank(&device.pipe.crtc));
    atomic_store(&pause_cancel, true);
    pthread_t cancel_thread, enable_thread;
    pthread_create(&cancel_thread, NULL, disable, NULL);
    await(&cancel_paused);
    /* Cancellation must retain the private lock until its attempt ends. */
    assert(pthread_mutex_trylock(&device.vblank_lock) == EBUSY);
    pthread_create(&enable_thread, NULL, enable, NULL);
    await(&enable_waiting);
    assert(!atomic_load(&enable_done));
    atomic_store(&release_cancel, true);
    pthread_join(cancel_thread, NULL);
    pthread_join(enable_thread, NULL);
    assert(device.vblank_timer_enabled);
    assert(atomic_load(&device.vblank_timer.queued));
    fire(NULL);
    assert(atomic_load(&events) == 1);
    sm750_vblank_timer_stop(&device, true);
}
static void check_sync_stop(void) {
    assert(!sm750_crtc_enable_vblank(&device.pipe.crtc));
    pthread_mutex_lock(&time_lock);
    pthread_t callback, stop_thread;
    pthread_create(&callback, NULL, fire, NULL);
    await(&drm_entered);
    sm750_crtc_disable_vblank(&device.pipe.crtc);
    /* Teardown waits in another thread that holds no DRM lock. */
    pthread_create(&stop_thread, NULL, stop_sync, NULL);
    await(&sync_cancel_entered);
    assert(!atomic_load(&sync_stop_done));
    pthread_mutex_unlock(&time_lock);
    pthread_join(callback, NULL);
    pthread_join(stop_thread, NULL);
    assert(atomic_load(&callback_done) && atomic_load(&sync_stop_done));
    assert(!device.vblank_timer_enabled);
    assert(!atomic_load(&device.vblank_timer.queued));
    assert(!atomic_load(&device.vblank_timer.running));
}
int main(int argc, char **argv) {
    assert(argc == 2);
    init();
    if (!strcmp(argv[1], "normal")) check_normal();
    else if (!strcmp(argv[1], "time-lock")) check_blocked(&time_lock);
    else if (!strcmp(argv[1], "event-lock")) check_blocked(&event_lock);
    else if (!strcmp(argv[1], "superseded")) check_superseded();
    else if (!strcmp(argv[1], "cancel-enable")) check_cancel_enable();
    else if (!strcmp(argv[1], "sync-stop")) check_sync_stop();
    else return 1;
    printf("Vblank timer schedule passed: %s\n", argv[1]);
    return 0;
}
"""


def main():
    source = SOURCE.read_text()
    names = (
        "sm750_vblank_first_delay_locked", "sm750_vblank_timer_fn",
        "sm750_vblank_timer_stop", "sm750_pipe_enable_vblank",
        "sm750_crtc_enable_vblank", "sm750_crtc_disable_vblank",
    )
    bodies = []
    for name in names:
        match = re.search(
            r"^static (?:u64|enum hrtimer_restart|void|int) " + name
            + r"\([^;]*?\)\n\{.*?^}", source, re.M | re.S,
        )
        if match is None:
            raise AssertionError("Missing driver function: " + name)
        bodies.append(match.group())

    with tempfile.TemporaryDirectory(prefix="sm750-vblank-test-") as tmp:
        root = Path(tmp)
        harness = root / "timer.c"
        binary = root / "timer"
        harness.write_text(PREFIX + "\n\n".join(bodies) + SUFFIX)
        compiler = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(compiler + ["-O2", "-Wall", "-Wextra", "-Werror",
                       "-pthread", str(harness), "-o", str(binary)], check=True)
        for case in ("normal", "time-lock", "event-lock", "superseded",
                     "cancel-enable", "sync-stop"):
            subprocess.run([str(binary), case], check=True, timeout=10)


if __name__ == "__main__":
    main()
