/*
 * event_loop.c - Event loop abstraction + poll() backend for μEmacs
 *
 * Provides backend-agnostic event dispatching with the poll() backend
 * as the always-available fallback. Higher-tier backends (epoll, io_uring)
 * are selected at init time if available.
 *
 * C23 compliant
 */

#include "platform/event_loop.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include "memory.h"
#include "util/logger.h"

#ifdef HAVE_SYS_TIMERFD_H
#include <sys/timerfd.h>
#endif

#ifdef HAVE_SYS_SIGNALFD_H
#include <sys/signalfd.h>
#endif

// Source types
typedef enum {
    SRC_NONE = 0,
    SRC_FD,
    SRC_TIMER,
    SRC_SIGNAL,
} src_type_t;

// Event source slot
typedef struct {
    int fd;                 // Monitored fd (-1 = inactive)
    evt_flags_t interest;
    evt_callback_t callback;
    void *userdata;
    src_type_t type;
    bool active;
    bool owns_fd;           // True if we created the fd (timer/signal)
} evt_source_t;

// Global state
static evt_source_t g_sources[EVT_MAX_SOURCES];
static evt_loop_backend_t g_backend_type = EVT_LOOP_POLL;
static const evt_loop_ops_t *g_backend_ops = nullptr;
static bool g_initialized = false;

// Dispatch table for ready fds (shared with backend implementations)
int g_ready_slots[EVT_MAX_SOURCES];
evt_flags_t g_ready_flags[EVT_MAX_SOURCES];
int g_ready_count = 0;

// Poll backend implementation
static int poll_init(void) { return 0; }
static void poll_destroy(void) {}

static int poll_add_fd(int slot_idx, int fd, evt_flags_t interest) {
    (void)slot_idx; (void)fd; (void)interest;
    return 0; // poll rebuilds pfds each wait
}

static void poll_remove_fd(int slot_idx, int fd) {
    (void)slot_idx; (void)fd;
}

static void poll_modify_fd(int slot_idx, int fd, evt_flags_t interest) {
    (void)slot_idx; (void)fd; (void)interest;
}

static int poll_wait(int timeout_ms) {
    struct pollfd pfds[EVT_MAX_SOURCES];
    int slot_map[EVT_MAX_SOURCES]; // pfds index -> source index
    int nfds = 0;

    // Build pollfd array from active sources
    for (int i = 0; i < EVT_MAX_SOURCES; i++) {
        if (!g_sources[i].active || g_sources[i].fd < 0)
            continue;

        short events = 0;
        if (g_sources[i].interest & EVT_READABLE) events |= POLLIN;
        if (g_sources[i].interest & EVT_WRITABLE) events |= POLLOUT;

        pfds[nfds].fd = g_sources[i].fd;
        pfds[nfds].events = events;
        pfds[nfds].revents = 0;
        slot_map[nfds] = i;
        nfds++;
    }

    if (nfds == 0) return 0;

    int ret = poll(pfds, (nfds_t)nfds, timeout_ms);
    if (ret <= 0) return ret;

    // Report ready fds
    g_ready_count = 0;
    for (int i = 0; i < nfds && g_ready_count < EVT_MAX_SOURCES; i++) {
        if (pfds[i].revents == 0) continue;

        evt_flags_t flags = 0;
        if (pfds[i].revents & POLLIN)  flags |= EVT_READABLE;
        if (pfds[i].revents & POLLOUT) flags |= EVT_WRITABLE;
        if (pfds[i].revents & POLLHUP) flags |= EVT_HANGUP;
        if (pfds[i].revents & (POLLERR | POLLNVAL)) flags |= EVT_ERROR;

        g_ready_slots[g_ready_count] = slot_map[i];
        g_ready_flags[g_ready_count] = flags;
        g_ready_count++;
    }

    return g_ready_count;
}

static const evt_loop_ops_t poll_ops = {
    .init      = poll_init,
    .destroy   = poll_destroy,
    .add_fd    = poll_add_fd,
    .remove_fd = poll_remove_fd,
    .modify_fd = poll_modify_fd,
    .wait      = poll_wait,
};

// Allocate a source slot
static int alloc_slot(void) {
    for (int i = 0; i < EVT_MAX_SOURCES; i++) {
        if (!g_sources[i].active)
            return i;
    }
    return -1;
}

// Backend selection and setup
void evt_loop_set_backend(evt_loop_backend_t type, const evt_loop_ops_t *ops) {
    g_backend_type = type;
    g_backend_ops = ops;
}

// Try to probe for better backends; declared in their respective files
#ifdef HAVE_IO_URING
extern int evt_loop_uring_probe(void);
#endif
extern int evt_loop_epoll_probe(void);

int evt_loop_init(void) {
    if (g_initialized) return 0;

    memset(g_sources, 0, sizeof(g_sources));
    for (int i = 0; i < EVT_MAX_SOURCES; i++) {
        g_sources[i].fd = -1;
        g_sources[i].active = false;
    }

    // Default to poll
    g_backend_type = EVT_LOOP_POLL;
    g_backend_ops = &poll_ops;

    // Try higher-tier backends
#ifdef HAVE_IO_URING
    if (evt_loop_uring_probe() == 0) {
        LOG_DEBUG("EventLoop: using io_uring backend");
    } else
#endif
    if (evt_loop_epoll_probe() == 0) {
        LOG_DEBUG("EventLoop: using epoll backend");
    } else {
        LOG_DEBUG("EventLoop: using poll backend");
    }

    if (g_backend_ops->init() != 0)
        return -1;

    g_initialized = true;
    return 0;
}

void evt_loop_destroy(void) {
    if (!g_initialized) return;

    // Close owned fds
    for (int i = 0; i < EVT_MAX_SOURCES; i++) {
        if (g_sources[i].active && g_sources[i].owns_fd && g_sources[i].fd >= 0) {
            close(g_sources[i].fd);
        }
        g_sources[i].active = false;
        g_sources[i].fd = -1;
    }

    if (g_backend_ops)
        g_backend_ops->destroy();

    g_initialized = false;
}

evt_loop_backend_t evt_loop_backend(void) {
    return g_backend_type;
}

bool evt_loop_available(void) {
    return g_initialized;
}

evt_token_t evt_loop_add_fd(int fd, evt_flags_t interest,
                             evt_callback_t cb, void *ud) {
    if (!g_initialized || fd < 0)
        return EVT_TOKEN_INVALID;

    int slot = alloc_slot();
    if (slot < 0) return EVT_TOKEN_INVALID;

    g_sources[slot].fd = fd;
    g_sources[slot].interest = interest;
    g_sources[slot].callback = cb;
    g_sources[slot].userdata = ud;
    g_sources[slot].type = SRC_FD;
    g_sources[slot].active = true;
    g_sources[slot].owns_fd = false;

    g_backend_ops->add_fd(slot, fd, interest);
    return (evt_token_t)slot;
}

void evt_loop_remove(evt_token_t token) {
    if (token >= EVT_MAX_SOURCES) return;
    int slot = (int)token;

    if (!g_sources[slot].active) return;

    g_backend_ops->remove_fd(slot, g_sources[slot].fd);

    if (g_sources[slot].owns_fd && g_sources[slot].fd >= 0)
        close(g_sources[slot].fd);

    g_sources[slot].active = false;
    g_sources[slot].fd = -1;
}

void evt_loop_modify(evt_token_t token, evt_flags_t new_interest) {
    if (token >= EVT_MAX_SOURCES) return;
    int slot = (int)token;

    if (!g_sources[slot].active) return;
    g_sources[slot].interest = new_interest;
    g_backend_ops->modify_fd(slot, g_sources[slot].fd, new_interest);
}

evt_token_t evt_loop_add_timer_ms(uint64_t initial_ms, uint64_t interval_ms,
                                   evt_callback_t cb, void *ud) {
#ifdef HAVE_SYS_TIMERFD_H
    if (!g_initialized || !cb)
        return EVT_TOKEN_INVALID;

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd < 0) return EVT_TOKEN_INVALID;

    struct itimerspec spec = {
        .it_value = {
            .tv_sec = (time_t)(initial_ms / 1000),
            .tv_nsec = (long)((initial_ms % 1000) * 1000000),
        },
        .it_interval = {
            .tv_sec = (time_t)(interval_ms / 1000),
            .tv_nsec = (long)((interval_ms % 1000) * 1000000),
        },
    };

    if (timerfd_settime(tfd, 0, &spec, nullptr) < 0) {
        close(tfd);
        return EVT_TOKEN_INVALID;
    }

    int slot = alloc_slot();
    if (slot < 0) {
        close(tfd);
        return EVT_TOKEN_INVALID;
    }

    g_sources[slot].fd = tfd;
    g_sources[slot].interest = EVT_READABLE;
    g_sources[slot].callback = cb;
    g_sources[slot].userdata = ud;
    g_sources[slot].type = SRC_TIMER;
    g_sources[slot].active = true;
    g_sources[slot].owns_fd = true;

    g_backend_ops->add_fd(slot, tfd, EVT_READABLE);
    return (evt_token_t)slot;
#else
    (void)initial_ms; (void)interval_ms; (void)cb; (void)ud;
    return EVT_TOKEN_INVALID;
#endif
}

void evt_loop_cancel_timer(evt_token_t token) {
    evt_loop_remove(token);
}

evt_token_t evt_loop_add_signal(int signum, evt_callback_t cb, void *ud) {
#ifdef HAVE_SYS_SIGNALFD_H
    if (!g_initialized || !cb)
        return EVT_TOKEN_INVALID;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, signum);

    // Block signal so it goes to signalfd instead of default handler
    sigprocmask(SIG_BLOCK, &mask, nullptr);

    int sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sfd < 0) return EVT_TOKEN_INVALID;

    int slot = alloc_slot();
    if (slot < 0) {
        close(sfd);
        return EVT_TOKEN_INVALID;
    }

    g_sources[slot].fd = sfd;
    g_sources[slot].interest = EVT_READABLE;
    g_sources[slot].callback = cb;
    g_sources[slot].userdata = ud;
    g_sources[slot].type = SRC_SIGNAL;
    g_sources[slot].active = true;
    g_sources[slot].owns_fd = true;

    g_backend_ops->add_fd(slot, sfd, EVT_READABLE);
    return (evt_token_t)slot;
#else
    (void)signum; (void)cb; (void)ud;
    return EVT_TOKEN_INVALID;
#endif
}

int evt_loop_wait(int timeout_ms) {
    if (!g_initialized || !g_backend_ops)
        return -1;

    g_ready_count = 0;
    int n = g_backend_ops->wait(timeout_ms);
    if (n <= 0) return n;

    // Dispatch callbacks for ready sources
    for (int i = 0; i < g_ready_count; i++) {
        int slot = g_ready_slots[i];
        evt_flags_t flags = g_ready_flags[i];

        if (!g_sources[slot].active) continue;

        // For timer sources, drain the timerfd
        if (g_sources[slot].type == SRC_TIMER && (flags & EVT_READABLE)) {
            uint64_t expirations;
            (void)read(g_sources[slot].fd, &expirations, sizeof(expirations));
            flags = EVT_TIMER;
        }

        // For signal sources, drain the signalfd
#ifdef HAVE_SYS_SIGNALFD_H
        if (g_sources[slot].type == SRC_SIGNAL && (flags & EVT_READABLE)) {
            struct signalfd_siginfo si;
            (void)read(g_sources[slot].fd, &si, sizeof(si));
            flags = EVT_SIGNAL;
        }
#endif

        if (g_sources[slot].callback)
            g_sources[slot].callback((evt_token_t)slot, flags, g_sources[slot].userdata);
    }

    return g_ready_count;
}
