/*
 * event_loop_epoll.c - epoll backend for the μEmacs event loop
 *
 * Middle tier between poll() and io_uring. Uses edge-triggered epoll
 * for O(active) event notification.
 *
 * C23 compliant
 */

#include "platform/event_loop.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "util/logger.h"

static int g_epoll_fd = -1;

// Translate evt_flags to epoll events
static uint32_t to_epoll_events(evt_flags_t flags) {
    uint32_t ev = 0;  // Level-triggered (default) — correct for terminal I/O
    if (flags & EVT_READABLE) ev |= EPOLLIN;
    if (flags & EVT_WRITABLE) ev |= EPOLLOUT;
    return ev;
}

// Translate epoll events to evt_flags
static evt_flags_t from_epoll_events(uint32_t events) {
    evt_flags_t flags = 0;
    if (events & EPOLLIN)  flags |= EVT_READABLE;
    if (events & EPOLLOUT) flags |= EVT_WRITABLE;
    if (events & EPOLLHUP) flags |= EVT_HANGUP;
    if (events & EPOLLERR) flags |= EVT_ERROR;
    return flags;
}

static int epoll_be_init(void) {
    g_epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    return g_epoll_fd >= 0 ? 0 : -1;
}

static void epoll_be_destroy(void) {
    if (g_epoll_fd >= 0) {
        close(g_epoll_fd);
        g_epoll_fd = -1;
    }
}

static int epoll_be_add_fd(int slot_idx, int fd, evt_flags_t interest) {
    if (g_epoll_fd < 0) return -1;

    struct epoll_event ev = {
        .events = to_epoll_events(interest),
        .data.u64 = (uint64_t)slot_idx,
    };
    return epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

static void epoll_be_remove_fd(int slot_idx, int fd) {
    (void)slot_idx;
    if (g_epoll_fd >= 0)
        epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
}

static void epoll_be_modify_fd(int slot_idx, int fd, evt_flags_t interest) {
    if (g_epoll_fd < 0) return;

    struct epoll_event ev = {
        .events = to_epoll_events(interest),
        .data.u64 = (uint64_t)slot_idx,
    };
    epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}

// These are defined in event_loop.c and shared with backends
extern int g_ready_slots[];
extern evt_flags_t g_ready_flags[];
extern int g_ready_count;

static int epoll_be_wait(int timeout_ms) {
    if (g_epoll_fd < 0) return -1;

    struct epoll_event events[EVT_MAX_SOURCES];
    int n = epoll_wait(g_epoll_fd, events, EVT_MAX_SOURCES, timeout_ms);
    if (n <= 0) return n;

    g_ready_count = 0;
    for (int i = 0; i < n; i++) {
        g_ready_slots[g_ready_count] = (int)events[i].data.u64;
        g_ready_flags[g_ready_count] = from_epoll_events(events[i].events);
        g_ready_count++;
    }

    return g_ready_count;
}

static const evt_loop_ops_t epoll_ops = {
    .init      = epoll_be_init,
    .destroy   = epoll_be_destroy,
    .add_fd    = epoll_be_add_fd,
    .remove_fd = epoll_be_remove_fd,
    .modify_fd = epoll_be_modify_fd,
    .wait      = epoll_be_wait,
};

int evt_loop_epoll_probe(void) {
    int fd = epoll_create1(EPOLL_CLOEXEC);
    if (fd < 0) return -1;
    close(fd);

    evt_loop_set_backend(EVT_LOOP_EPOLL, &epoll_ops);
    return 0;
}
