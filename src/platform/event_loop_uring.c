/*
 * event_loop_uring.c - io_uring backend for the μEmacs event loop
 *
 * Uses raw io_uring syscalls (no liburing dependency). Ported from
 * ABRAXAS's proven io_uring implementation.
 *
 * Multi-shot IORING_OP_POLL_ADD for persistent fd monitoring.
 * One-shot IORING_OP_TIMEOUT for adaptive frame interval.
 *
 * Requires Linux 5.10+ for IORING_POLL_ADD_MULTI.
 *
 * C23 compliant
 */

#include "platform/event_loop.h"

#ifdef HAVE_IO_URING

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/io_uring.h>
#include <stdatomic.h>
#include "util/logger.h"

#define URING_ENTRIES 32

// io_uring context
typedef struct {
    int ring_fd;

    // Submission queue
    unsigned *sq_head;
    unsigned *sq_tail;
    unsigned *sq_ring_mask;
    unsigned *sq_ring_entries;
    unsigned *sq_flags;
    unsigned *sq_array;
    struct io_uring_sqe *sqes;
    size_t sq_ring_sz;
    void *sq_ring_ptr;

    // Completion queue
    unsigned *cq_head;
    unsigned *cq_tail;
    unsigned *cq_ring_mask;
    unsigned *cq_ring_entries;
    struct io_uring_cqe *cqes;
    size_t cq_ring_sz;
    void *cq_ring_ptr;
} uring_ctx_t;

static uring_ctx_t g_uring;

// Raw syscalls
static int io_uring_setup(unsigned entries, struct io_uring_params *p) {
    return (int)syscall(__NR_io_uring_setup, entries, p);
}

static int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                          unsigned flags) {
    return (int)syscall(__NR_io_uring_enter, fd, to_submit, min_complete,
                        flags, nullptr, 0);
}

static int uring_map_rings(struct io_uring_params *p) {
    size_t sq_sz = p->sq_off.array + p->sq_entries * sizeof(unsigned);
    size_t cq_sz = p->cq_off.cqes + p->cq_entries * sizeof(struct io_uring_cqe);

    // Single mmap if IORING_FEAT_SINGLE_MMAP
    if (p->features & IORING_FEAT_SINGLE_MMAP) {
        if (cq_sz > sq_sz) sq_sz = cq_sz;
        cq_sz = sq_sz;
    }

    void *sq_ptr = mmap(nullptr, sq_sz, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, g_uring.ring_fd,
                        IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED) return -1;

    g_uring.sq_ring_ptr = sq_ptr;
    g_uring.sq_ring_sz = sq_sz;

    void *cq_ptr;
    if (p->features & IORING_FEAT_SINGLE_MMAP) {
        cq_ptr = sq_ptr;
    } else {
        cq_ptr = mmap(nullptr, cq_sz, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE, g_uring.ring_fd,
                      IORING_OFF_CQ_RING);
        if (cq_ptr == MAP_FAILED) {
            munmap(sq_ptr, sq_sz);
            return -1;
        }
    }

    g_uring.cq_ring_ptr = cq_ptr;
    g_uring.cq_ring_sz = cq_sz;

    g_uring.sq_head = (unsigned *)((char *)sq_ptr + p->sq_off.head);
    g_uring.sq_tail = (unsigned *)((char *)sq_ptr + p->sq_off.tail);
    g_uring.sq_ring_mask = (unsigned *)((char *)sq_ptr + p->sq_off.ring_mask);
    g_uring.sq_ring_entries = (unsigned *)((char *)sq_ptr + p->sq_off.ring_entries);
    g_uring.sq_flags = (unsigned *)((char *)sq_ptr + p->sq_off.flags);
    g_uring.sq_array = (unsigned *)((char *)sq_ptr + p->sq_off.array);

    g_uring.cq_head = (unsigned *)((char *)cq_ptr + p->cq_off.head);
    g_uring.cq_tail = (unsigned *)((char *)cq_ptr + p->cq_off.tail);
    g_uring.cq_ring_mask = (unsigned *)((char *)cq_ptr + p->cq_off.ring_mask);
    g_uring.cq_ring_entries = (unsigned *)((char *)cq_ptr + p->cq_off.ring_entries);
    g_uring.cqes = (struct io_uring_cqe *)((char *)cq_ptr + p->cq_off.cqes);

    // Map SQE array
    size_t sqe_sz = p->sq_entries * sizeof(struct io_uring_sqe);
    g_uring.sqes = mmap(nullptr, sqe_sz, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, g_uring.ring_fd,
                        IORING_OFF_SQES);
    if (g_uring.sqes == MAP_FAILED) {
        munmap(sq_ptr, sq_sz);
        if (!(p->features & IORING_FEAT_SINGLE_MMAP))
            munmap(cq_ptr, cq_sz);
        return -1;
    }

    return 0;
}

static struct io_uring_sqe *uring_get_sqe(void) {
    unsigned tail = *g_uring.sq_tail;
    unsigned head = atomic_load_explicit((_Atomic unsigned *)g_uring.sq_head,
                                         memory_order_acquire);
    if (tail - head >= *g_uring.sq_ring_entries)
        return nullptr; // Queue full

    unsigned idx = tail & *g_uring.sq_ring_mask;
    struct io_uring_sqe *sqe = &g_uring.sqes[idx];
    memset(sqe, 0, sizeof(*sqe));
    g_uring.sq_array[idx] = idx;
    return sqe;
}

static void uring_submit_sqe(void) {
    atomic_store_explicit((_Atomic unsigned *)g_uring.sq_tail,
                          *g_uring.sq_tail + 1, memory_order_release);
}

// Track which slots have multi-shot polls registered
static bool slot_registered[EVT_MAX_SOURCES];

// Backend ops

static int uring_be_init(void) {
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    g_uring.ring_fd = io_uring_setup(URING_ENTRIES, &params);
    if (g_uring.ring_fd < 0) return -1;

    if (uring_map_rings(&params) < 0) {
        close(g_uring.ring_fd);
        g_uring.ring_fd = -1;
        return -1;
    }

    memset(slot_registered, 0, sizeof(slot_registered));
    return 0;
}

static void uring_be_destroy(void) {
    if (g_uring.ring_fd < 0) return;

    if (g_uring.sq_ring_ptr)
        munmap(g_uring.sq_ring_ptr, g_uring.sq_ring_sz);
    if (g_uring.cq_ring_ptr && g_uring.cq_ring_ptr != g_uring.sq_ring_ptr)
        munmap(g_uring.cq_ring_ptr, g_uring.cq_ring_sz);
    if (g_uring.sqes)
        munmap(g_uring.sqes, *g_uring.sq_ring_entries * sizeof(struct io_uring_sqe));

    close(g_uring.ring_fd);
    memset(&g_uring, 0, sizeof(g_uring));
    g_uring.ring_fd = -1;
}

static int uring_be_add_fd(int slot_idx, int fd, evt_flags_t interest) {
    struct io_uring_sqe *sqe = uring_get_sqe();
    if (!sqe) return -1;

    short poll_mask = 0;
    if (interest & EVT_READABLE) poll_mask |= POLLIN;
    if (interest & EVT_WRITABLE) poll_mask |= POLLOUT;

    sqe->opcode = IORING_OP_POLL_ADD;
    sqe->fd = fd;
    sqe->poll32_events = (__u32)poll_mask;
    sqe->user_data = (__u64)slot_idx;
    sqe->len = IORING_POLL_ADD_MULTI;  // Multi-shot: stays registered

    uring_submit_sqe();
    slot_registered[slot_idx] = true;

    // Submit immediately
    io_uring_enter(g_uring.ring_fd, 1, 0, 0);
    return 0;
}

static void uring_be_remove_fd(int slot_idx, int fd) {
    if (!slot_registered[slot_idx]) return;

    struct io_uring_sqe *sqe = uring_get_sqe();
    if (!sqe) return;

    sqe->opcode = IORING_OP_POLL_REMOVE;
    sqe->user_data = (__u64)slot_idx;

    uring_submit_sqe();
    io_uring_enter(g_uring.ring_fd, 1, 0, 0);
    slot_registered[slot_idx] = false;
    (void)fd;
}

static void uring_be_modify_fd(int slot_idx, int fd, evt_flags_t interest) {
    uring_be_remove_fd(slot_idx, fd);
    uring_be_add_fd(slot_idx, fd, interest);
}

// Shared with event_loop.c
extern int g_ready_slots[];
extern evt_flags_t g_ready_flags[];
extern int g_ready_count;

static int uring_be_wait(int timeout_ms) {
    // Submit a timeout SQE if requested
    struct __kernel_timespec ts;
    bool has_timeout = false;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (long long)(timeout_ms % 1000) * 1000000LL;

        struct io_uring_sqe *sqe = uring_get_sqe();
        if (sqe) {
            sqe->opcode = IORING_OP_TIMEOUT;
            sqe->addr = (__u64)&ts;
            sqe->len = 1;
            sqe->off = 0;
            sqe->user_data = (__u64)(EVT_MAX_SOURCES + 1); // sentinel
            uring_submit_sqe();
            has_timeout = true;
        }
    }

    // Wait for at least 1 completion
    unsigned to_submit = has_timeout ? 1 : 0;
    int ret = io_uring_enter(g_uring.ring_fd, to_submit, 1, IORING_ENTER_GETEVENTS);
    if (ret < 0 && errno != ETIME)
        return -1;

    // Drain CQEs
    g_ready_count = 0;
    unsigned head = *g_uring.cq_head;
    unsigned tail = atomic_load_explicit((_Atomic unsigned *)g_uring.cq_tail,
                                         memory_order_acquire);

    while (head != tail && g_ready_count < EVT_MAX_SOURCES) {
        unsigned idx = head & *g_uring.cq_ring_mask;
        struct io_uring_cqe *cqe = &g_uring.cqes[idx];

        uint64_t ud = cqe->user_data;
        if (ud < EVT_MAX_SOURCES && cqe->res >= 0) {
            evt_flags_t flags = 0;
            if (cqe->res & POLLIN)  flags |= EVT_READABLE;
            if (cqe->res & POLLOUT) flags |= EVT_WRITABLE;
            if (cqe->res & POLLHUP) flags |= EVT_HANGUP;
            if (cqe->res & POLLERR) flags |= EVT_ERROR;

            g_ready_slots[g_ready_count] = (int)ud;
            g_ready_flags[g_ready_count] = flags;
            g_ready_count++;
        }
        // Timeout and unknown CQEs are silently consumed

        head++;
    }

    atomic_store_explicit((_Atomic unsigned *)g_uring.cq_head,
                          head, memory_order_release);

    return g_ready_count;
}

static const evt_loop_ops_t uring_ops = {
    .init      = uring_be_init,
    .destroy   = uring_be_destroy,
    .add_fd    = uring_be_add_fd,
    .remove_fd = uring_be_remove_fd,
    .modify_fd = uring_be_modify_fd,
    .wait      = uring_be_wait,
};

int evt_loop_uring_probe(void) {
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    int fd = io_uring_setup(1, &params);
    if (fd < 0) return -1;
    close(fd);

    evt_loop_set_backend(EVT_LOOP_URING, &uring_ops);
    return 0;
}

#endif /* HAVE_IO_URING */
