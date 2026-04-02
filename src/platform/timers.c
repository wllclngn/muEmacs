/*
 * timers.c - timerfd-based precision timers for μEmacs
 *
 * Provides named timers for auto-save, cursor blink, resize debounce,
 * and extension timeouts. Each timer is backed by a timerfd that can
 * be registered with the event loop for callback dispatch.
 *
 * C23 compliant
 */

#include "platform/timers.h"
#include <unistd.h>
#include <string.h>
#include "util/logger.h"

#ifdef HAVE_SYS_TIMERFD_H
#include <sys/timerfd.h>
#define TIMERS_SUPPORTED 1
#else
#define TIMERS_SUPPORTED 0
#endif

static int timer_fds[TIMER_MAX];
static int timers_initialized = 0;

int timers_init(void) {
    if (timers_initialized) return 0;

    for (int i = 0; i < TIMER_MAX; i++)
        timer_fds[i] = -1;

#if TIMERS_SUPPORTED
    for (int i = 0; i < TIMER_MAX; i++) {
        timer_fds[i] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (timer_fds[i] < 0) {
            LOG_DEBUGF("Timers: timerfd_create failed for timer %d", i);
            // Non-fatal: individual timers may fail
        }
    }
#endif

    timers_initialized = 1;
    return 0;
}

void timers_cleanup(void) {
    if (!timers_initialized) return;

    for (int i = 0; i < TIMER_MAX; i++) {
        if (timer_fds[i] >= 0) {
            close(timer_fds[i]);
            timer_fds[i] = -1;
        }
    }
    timers_initialized = 0;
}

int timer_arm(timer_id_t id, uint64_t initial_ms, uint64_t interval_ms) {
    if (id >= TIMER_MAX) return -1;

#if TIMERS_SUPPORTED
    int fd = timer_fds[id];
    if (fd < 0) return -1;

    struct itimerspec spec;
    memset(&spec, 0, sizeof(spec));

    if (initial_ms > 0) {
        spec.it_value.tv_sec = (time_t)(initial_ms / 1000);
        spec.it_value.tv_nsec = (long)((initial_ms % 1000) * 1000000);
    }

    if (interval_ms > 0) {
        spec.it_interval.tv_sec = (time_t)(interval_ms / 1000);
        spec.it_interval.tv_nsec = (long)((interval_ms % 1000) * 1000000);
    }

    return timerfd_settime(fd, 0, &spec, nullptr);
#else
    (void)id; (void)initial_ms; (void)interval_ms;
    return -1;
#endif
}

int timer_disarm(timer_id_t id) {
    return timer_arm(id, 0, 0);
}

int timer_get_fd(timer_id_t id) {
    if (id >= TIMER_MAX) return -1;
    return timer_fds[id];
}

int timers_available(void) {
#if TIMERS_SUPPORTED
    return 1;
#else
    return 0;
#endif
}
