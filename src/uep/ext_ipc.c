/*
 * ext_ipc.c - Atomic Ring Buffer IPC Implementation
 *
 * Lock-free IPC using only atomics and shared memory.
 * No eventfd, no blocking syscalls in hot path.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <uep/ext_ipc.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sched.h>
#include <errno.h>
#include <stdio.h>

#include "util/logger.h"

/* memfd_create may not be in older glibc */
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

static int memfd_create_wrapper(const char *name, unsigned int flags) {
#ifdef SYS_memfd_create
    return (int)syscall(SYS_memfd_create, name, flags);
#else
    (void)name; (void)flags;
    errno = ENOSYS;
    return -1;
#endif
}

/* =========================================================================
 * Channel Lifecycle
 * ========================================================================= */

ext_ipc_channel_t *ext_ipc_create(void) {
    ext_ipc_channel_t *ch = calloc(1, sizeof(*ch));
    if (!ch) return NULL;

    /* Create memfd for shared memory */
    ch->memfd = memfd_create_wrapper("uemacs_ext_ipc", MFD_CLOEXEC);
    if (ch->memfd < 0) {
        LOG_ERRORF("ext_ipc: memfd_create failed: %s", strerror(errno));
        free(ch);
        return NULL;
    }

    /* Size the shared memory */
    ch->shm_size = sizeof(ext_ipc_shm_t);
    if (ftruncate(ch->memfd, ch->shm_size) < 0) {
        LOG_ERRORF("ext_ipc: ftruncate failed: %s", strerror(errno));
        close(ch->memfd);
        free(ch);
        return NULL;
    }

    /* Map shared memory */
    ch->shm = mmap(NULL, ch->shm_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED, ch->memfd, 0);
    if (ch->shm == MAP_FAILED) {
        LOG_ERRORF("ext_ipc: mmap failed: %s", strerror(errno));
        close(ch->memfd);
        free(ch);
        return NULL;
    }

    /* Initialize header */
    ch->shm->magic = EXT_IPC_MAGIC;
    ch->shm->version = EXT_IPC_VERSION;
    atomic_store(&ch->shm->ext_ready, 0);
    atomic_store(&ch->shm->shutdown, 0);

    /* Initialize all slots to EMPTY */
    for (int i = 0; i < EXT_IPC_RING_SLOTS; i++) {
        atomic_store(&ch->shm->to_ext.slots[i].state, EXT_SLOT_EMPTY);
        atomic_store(&ch->shm->to_editor.slots[i].state, EXT_SLOT_EMPTY);
    }

    LOG_DEBUG("ext_ipc: Channel created");
    return ch;
}

ext_ipc_channel_t *ext_ipc_attach(int memfd) {
    ext_ipc_channel_t *ch = calloc(1, sizeof(*ch));
    if (!ch) return NULL;

    ch->memfd = memfd;
    ch->shm_size = sizeof(ext_ipc_shm_t);

    /* Map shared memory */
    ch->shm = mmap(NULL, ch->shm_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED, ch->memfd, 0);
    if (ch->shm == MAP_FAILED) {
        free(ch);
        return NULL;
    }

    /* Validate */
    if (ch->shm->magic != EXT_IPC_MAGIC) {
        munmap(ch->shm, ch->shm_size);
        free(ch);
        return NULL;
    }

    return ch;
}

void ext_ipc_destroy(ext_ipc_channel_t *ch) {
    if (!ch) return;

    LOG_DEBUG("ext_ipc: Destroying channel");

    if (ch->shm && ch->shm != MAP_FAILED) {
        munmap(ch->shm, ch->shm_size);
    }
    if (ch->memfd >= 0) {
        close(ch->memfd);
    }
    free(ch);
}

void ext_ipc_set_pid(ext_ipc_channel_t *ch, pid_t pid) {
    if (ch) ch->child_pid = pid;
}

int ext_ipc_get_memfd(ext_ipc_channel_t *ch) {
    return ch ? ch->memfd : -1;
}

/* =========================================================================
 * Ring Buffer Operations
 * ========================================================================= */

int ext_ipc_find_empty_slot(ext_ipc_ring_t *ring) {
    for (int i = 0; i < EXT_IPC_RING_SLOTS; i++) {
        if (atomic_load_explicit(&ring->slots[i].state,
                                 memory_order_acquire) == EXT_SLOT_EMPTY) {
            return i;
        }
    }
    return -1;
}

int ext_ipc_find_pending_slot(ext_ipc_ring_t *ring) {
    for (int i = 0; i < EXT_IPC_RING_SLOTS; i++) {
        if (atomic_load_explicit(&ring->slots[i].state,
                                 memory_order_acquire) == EXT_SLOT_PENDING) {
            return i;
        }
    }
    return -1;
}

void ext_ipc_slot_write(ext_ipc_slot_t *slot, uint32_t msg_type,
                        const void *payload, uint32_t len) {
    slot->msg_type = msg_type;
    slot->payload_len = len;
    slot->result = 0;

    if (payload && len > 0) {
        uint32_t copy_len = len;
        if (copy_len > EXT_IPC_MAX_PAYLOAD) {
            copy_len = EXT_IPC_MAX_PAYLOAD;
        }
        memcpy(slot->payload, payload, copy_len);
        slot->payload_len = copy_len;
    }

    /* Release barrier - ensure payload visible before state change */
    atomic_store_explicit(&slot->state, EXT_SLOT_PENDING, memory_order_release);
}

void ext_ipc_slot_complete(ext_ipc_slot_t *slot, int32_t result,
                           const void *data, uint32_t len) {
    slot->result = result;

    if (data && len > 0) {
        uint32_t copy_len = len;
        if (copy_len > EXT_IPC_MAX_PAYLOAD) {
            copy_len = EXT_IPC_MAX_PAYLOAD;
        }
        memcpy(slot->payload, data, copy_len);
        slot->payload_len = copy_len;
    } else {
        slot->payload_len = 0;
    }

    /* Release barrier */
    atomic_store_explicit(&slot->state, EXT_SLOT_COMPLETE, memory_order_release);
}

void ext_ipc_slot_release(ext_ipc_slot_t *slot) {
    atomic_store_explicit(&slot->state, EXT_SLOT_EMPTY, memory_order_release);
}

bool ext_ipc_slot_wait_complete(ext_ipc_slot_t *slot, int timeout_ms) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (1) {
        uint32_t state = atomic_load_explicit(&slot->state, memory_order_acquire);
        if (state == EXT_SLOT_COMPLETE) {
            return true;
        }

        /* Check timeout */
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                          (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= timeout_ms) {
            return false;
        }

        /* Spin hint */
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#else
        sched_yield();
#endif
    }
}
