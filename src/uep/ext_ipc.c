/*
 * ext_ipc.c - Extension IPC Implementation
 *
 * Process isolation for polyglot extensions using Linux kernel primitives:
 * - memfd_create(): Anonymous shared memory
 * - eventfd(): Lightweight signaling
 * - pidfd_open(): Process lifecycle management
 *
 * Zero external dependencies - pure POSIX + Linux syscalls.
 *
 * C23 compliant
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <sys/syscall.h>

#include "uep/ext_ipc.h"
#include "util/logger.h"

/* pidfd_open() wrapper - not in older glibc */
#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif

static int pidfd_open_wrapper(pid_t pid, unsigned int flags) {
    return (int)syscall(SYS_pidfd_open, pid, flags);
}

/* =========================================================================
 * Channel lifecycle
 * ========================================================================= */

ext_ipc_channel_t *ext_ipc_create(size_t shm_size) {
    ext_ipc_channel_t *ch = calloc(1, sizeof(*ch));
    if (!ch) {
        LOG_ERROR("ext_ipc: Failed to allocate channel");
        return nullptr;
    }

    /* Use default size if not specified */
    if (shm_size == 0) {
        shm_size = EXT_IPC_DEFAULT_SHM_SIZE;
    }

    ch->memfd = -1;
    ch->eventfd_request = -1;
    ch->eventfd_response = -1;
    ch->pidfd = -1;
    ch->shm = MAP_FAILED;
    ch->shm_size = shm_size;
    atomic_store(&ch->seq, 0);

    /* Create anonymous shared memory */
    ch->memfd = memfd_create("uep-ext-shm", MFD_CLOEXEC);
    if (ch->memfd < 0) {
        LOG_ERRORF("ext_ipc: memfd_create failed: %s", strerror(errno));
        goto fail;
    }

    /* Size the shared memory region */
    if (ftruncate(ch->memfd, (off_t)shm_size) < 0) {
        LOG_ERRORF("ext_ipc: ftruncate failed: %s", strerror(errno));
        goto fail;
    }

    /* Map shared memory */
    ch->shm = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED, ch->memfd, 0);
    if (ch->shm == MAP_FAILED) {
        LOG_ERRORF("ext_ipc: mmap failed: %s", strerror(errno));
        goto fail;
    }

    /* Initialize shared memory to zero */
    memset(ch->shm, 0, shm_size);

    /* Create eventfd for request signaling (editor -> extension) */
    ch->eventfd_request = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (ch->eventfd_request < 0) {
        LOG_ERRORF("ext_ipc: eventfd (request) failed: %s", strerror(errno));
        goto fail;
    }

    /* Create eventfd for response signaling (extension -> editor) */
    ch->eventfd_response = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (ch->eventfd_response < 0) {
        LOG_ERRORF("ext_ipc: eventfd (response) failed: %s", strerror(errno));
        goto fail;
    }

    LOG_DEBUGF("ext_ipc: Created channel memfd=%d evreq=%d evresp=%d shm=%zu",
               ch->memfd, ch->eventfd_request, ch->eventfd_response, shm_size);

    return ch;

fail:
    ext_ipc_destroy(ch);
    return nullptr;
}

ext_ipc_channel_t *ext_ipc_attach(int memfd, int evreq, int evresp, size_t shm_size) {
    if (memfd < 0 || evreq < 0 || evresp < 0) {
        LOG_ERROR("ext_ipc: Invalid file descriptors for attach");
        return nullptr;
    }

    if (shm_size == 0) {
        shm_size = EXT_IPC_DEFAULT_SHM_SIZE;
    }

    ext_ipc_channel_t *ch = calloc(1, sizeof(*ch));
    if (!ch) {
        LOG_ERROR("ext_ipc: Failed to allocate channel for attach");
        return nullptr;
    }

    ch->memfd = memfd;
    ch->eventfd_request = evreq;
    ch->eventfd_response = evresp;
    ch->pidfd = -1;  /* Child doesn't have pidfd to parent */
    ch->shm_size = shm_size;
    atomic_store(&ch->seq, 0);

    /* Map the shared memory */
    ch->shm = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED, memfd, 0);
    if (ch->shm == MAP_FAILED) {
        LOG_ERRORF("ext_ipc: mmap attach failed: %s", strerror(errno));
        free(ch);
        return nullptr;
    }

    LOG_DEBUGF("ext_ipc: Attached to channel memfd=%d evreq=%d evresp=%d",
               memfd, evreq, evresp);

    return ch;
}

void ext_ipc_destroy(ext_ipc_channel_t *ch) {
    if (!ch) return;

    LOG_DEBUG("ext_ipc: Destroying channel");

    if (ch->shm != MAP_FAILED && ch->shm != nullptr) {
        munmap(ch->shm, ch->shm_size);
    }

    if (ch->memfd >= 0) close(ch->memfd);
    if (ch->eventfd_request >= 0) close(ch->eventfd_request);
    if (ch->eventfd_response >= 0) close(ch->eventfd_response);
    if (ch->pidfd >= 0) close(ch->pidfd);

    free(ch);
}

/* =========================================================================
 * Synchronous communication
 * ========================================================================= */

int ext_ipc_call(ext_ipc_channel_t *ch, uint32_t type,
                 const void *payload, uint32_t payload_len,
                 void *response, uint32_t *response_len) {
    if (!ch || ch->shm == MAP_FAILED) {
        return EXT_IPC_ERR_INVALID;
    }

    if (payload_len > EXT_IPC_MAX_PAYLOAD) {
        LOG_ERRORF("ext_ipc: Payload too large: %u > %u",
                   payload_len, (uint32_t)EXT_IPC_MAX_PAYLOAD);
        return EXT_IPC_ERR_TOOBIG;
    }

    ext_ipc_msg_t *msg = (ext_ipc_msg_t *)ch->shm;

    /* Write request to shared memory */
    uint32_t seq = atomic_fetch_add(&ch->seq, 1) + 1;
    atomic_store(&msg->sequence, seq);
    msg->msg_type = type;
    msg->payload_len = payload_len;
    msg->response_len = 0;
    msg->result = 0;
    msg->flags = 0;

    if (payload && payload_len > 0) {
        memcpy(msg->payload, payload, payload_len);
    }

    /* Memory barrier - ensure writes are visible before signaling */
    atomic_thread_fence(memory_order_release);

    /* Signal the extension */
    uint64_t signal = 1;
    ssize_t written = write(ch->eventfd_request, &signal, sizeof(signal));
    if (written != sizeof(signal)) {
        LOG_ERRORF("ext_ipc: Failed to signal request: %s", strerror(errno));
        return EXT_IPC_ERR_SYSCALL;
    }

    /* Wait for response */
    struct pollfd pfd = {
        .fd = ch->eventfd_response,
        .events = POLLIN,
        .revents = 0
    };

    int poll_result = poll(&pfd, 1, EXT_IPC_TIMEOUT_MS);
    if (poll_result < 0) {
        LOG_ERRORF("ext_ipc: poll failed: %s", strerror(errno));
        return EXT_IPC_ERR_SYSCALL;
    }
    if (poll_result == 0) {
        LOG_WARN("ext_ipc: Timeout waiting for response");
        return EXT_IPC_ERR_TIMEOUT;
    }

    /* Consume the eventfd signal */
    uint64_t consumed;
    if (read(ch->eventfd_response, &consumed, sizeof(consumed)) != sizeof(consumed)) {
        /* Non-blocking read may fail with EAGAIN if already consumed */
        if (errno != EAGAIN) {
            LOG_WARNF("ext_ipc: Failed to consume response signal: %s", strerror(errno));
        }
    }

    /* Memory barrier - ensure we read updated values */
    atomic_thread_fence(memory_order_acquire);

    /* Read response from shared memory */
    if (response && response_len && msg->response_len > 0) {
        uint32_t copy_len = msg->response_len;
        if (copy_len > *response_len) {
            copy_len = *response_len;
        }
        memcpy(response, msg->payload, copy_len);
        *response_len = msg->response_len;
    } else if (response_len) {
        *response_len = 0;
    }

    return msg->result;
}

int ext_ipc_notify(ext_ipc_channel_t *ch, uint32_t type,
                   const void *payload, uint32_t payload_len) {
    if (!ch || ch->shm == MAP_FAILED) {
        return EXT_IPC_ERR_INVALID;
    }

    if (payload_len > EXT_IPC_MAX_PAYLOAD) {
        return EXT_IPC_ERR_TOOBIG;
    }

    ext_ipc_msg_t *msg = (ext_ipc_msg_t *)ch->shm;

    /* Write notification to shared memory */
    uint32_t seq = atomic_fetch_add(&ch->seq, 1) + 1;
    atomic_store(&msg->sequence, seq);
    msg->msg_type = type;
    msg->payload_len = payload_len;
    msg->response_len = 0;
    msg->result = 0;
    msg->flags = 0;

    if (payload && payload_len > 0) {
        memcpy(msg->payload, payload, payload_len);
    }

    atomic_thread_fence(memory_order_release);

    /* Signal (don't wait for response) */
    uint64_t signal = 1;
    ssize_t written = write(ch->eventfd_request, &signal, sizeof(signal));
    if (written != sizeof(signal)) {
        return EXT_IPC_ERR_SYSCALL;
    }

    return EXT_IPC_OK;
}

/* =========================================================================
 * Extension-side functions
 * ========================================================================= */

int ext_ipc_recv(ext_ipc_channel_t *ch, ext_ipc_msg_t **msg, int timeout_ms) {
    if (!ch || ch->shm == MAP_FAILED || !msg) {
        return EXT_IPC_ERR_INVALID;
    }

    /* Wait for signal from editor */
    struct pollfd pfd = {
        .fd = ch->eventfd_request,
        .events = POLLIN,
        .revents = 0
    };

    int poll_result = poll(&pfd, 1, timeout_ms);
    if (poll_result < 0) {
        if (errno == EINTR) {
            return EXT_IPC_ERR_TIMEOUT;  /* Treat interrupt as timeout */
        }
        LOG_ERRORF("ext_ipc: recv poll failed: %s", strerror(errno));
        return EXT_IPC_ERR_SYSCALL;
    }
    if (poll_result == 0) {
        return EXT_IPC_ERR_TIMEOUT;
    }

    /* Check for hangup (editor closed the channel) */
    if (pfd.revents & (POLLHUP | POLLERR)) {
        LOG_INFO("ext_ipc: Channel closed by editor");
        return EXT_IPC_ERR_CLOSED;
    }

    /* Consume the eventfd signal */
    uint64_t consumed;
    if (read(ch->eventfd_request, &consumed, sizeof(consumed)) != sizeof(consumed)) {
        if (errno != EAGAIN) {
            LOG_WARNF("ext_ipc: Failed to consume request signal: %s", strerror(errno));
        }
    }

    /* Memory barrier */
    atomic_thread_fence(memory_order_acquire);

    /* Return pointer to message in shared memory */
    *msg = (ext_ipc_msg_t *)ch->shm;

    return EXT_IPC_OK;
}

int ext_ipc_respond(ext_ipc_channel_t *ch, int32_t result,
                    const void *data, uint32_t len) {
    if (!ch || ch->shm == MAP_FAILED) {
        return EXT_IPC_ERR_INVALID;
    }

    if (len > EXT_IPC_MAX_PAYLOAD) {
        return EXT_IPC_ERR_TOOBIG;
    }

    ext_ipc_msg_t *msg = (ext_ipc_msg_t *)ch->shm;

    /* Write response */
    msg->result = result;
    msg->response_len = len;

    if (data && len > 0) {
        memcpy(msg->payload, data, len);
    }

    atomic_thread_fence(memory_order_release);

    /* Signal the editor */
    uint64_t signal = 1;
    ssize_t written = write(ch->eventfd_response, &signal, sizeof(signal));
    if (written != sizeof(signal)) {
        LOG_ERRORF("ext_ipc: Failed to signal response: %s", strerror(errno));
        return EXT_IPC_ERR_SYSCALL;
    }

    return EXT_IPC_OK;
}

/*
 * Extension calls editor (reverse direction of ext_ipc_call)
 * Signals eventfd_response, waits on eventfd_request for response
 */
int ext_ipc_call_editor(ext_ipc_channel_t *ch, uint32_t type,
                        const void *payload, uint32_t payload_len,
                        void *response, uint32_t *response_len) {
    if (!ch || ch->shm == MAP_FAILED) {
        return EXT_IPC_ERR_INVALID;
    }

    if (payload_len > EXT_IPC_MAX_PAYLOAD) {
        return EXT_IPC_ERR_TOOBIG;
    }

    ext_ipc_msg_t *msg = (ext_ipc_msg_t *)ch->shm;

    /* Write request to shared memory */
    uint32_t seq = atomic_fetch_add(&ch->seq, 1) + 1;
    atomic_store(&msg->sequence, seq);
    msg->msg_type = type;
    msg->payload_len = payload_len;
    msg->response_len = 0;
    msg->result = 0;
    msg->flags = 0;

    if (payload && payload_len > 0) {
        memcpy(msg->payload, payload, payload_len);
    }

    atomic_thread_fence(memory_order_release);

    /* Signal editor via response eventfd */
    uint64_t signal = 1;
    if (write(ch->eventfd_response, &signal, sizeof(signal)) != sizeof(signal)) {
        return EXT_IPC_ERR_SYSCALL;
    }

    /* Wait for editor response on request eventfd */
    struct pollfd pfd = {
        .fd = ch->eventfd_request,
        .events = POLLIN,
        .revents = 0
    };

    int poll_result = poll(&pfd, 1, EXT_IPC_TIMEOUT_MS);
    if (poll_result < 0) {
        return EXT_IPC_ERR_SYSCALL;
    }
    if (poll_result == 0) {
        return EXT_IPC_ERR_TIMEOUT;
    }

    /* Consume the signal */
    uint64_t consumed;
    if (read(ch->eventfd_request, &consumed, sizeof(consumed)) < 0) { /* ignore */ }

    atomic_thread_fence(memory_order_acquire);

    /* Read response */
    if (response && response_len && msg->response_len > 0) {
        uint32_t copy_len = msg->response_len < *response_len ? msg->response_len : *response_len;
        memcpy(response, msg->payload, copy_len);
        *response_len = msg->response_len;
    } else if (response_len) {
        *response_len = 0;
    }

    return msg->result;
}

/*
 * Send notification from extension to editor
 * Writes to eventfd_response - for Extension -> Editor messages
 */
int ext_ipc_notify_editor(ext_ipc_channel_t *ch, uint32_t type,
                          const void *payload, uint32_t payload_len) {
    if (!ch || ch->shm == MAP_FAILED) {
        return EXT_IPC_ERR_INVALID;
    }

    if (payload_len > EXT_IPC_MAX_PAYLOAD) {
        return EXT_IPC_ERR_TOOBIG;
    }

    ext_ipc_msg_t *msg = (ext_ipc_msg_t *)ch->shm;

    /* Write notification to shared memory */
    uint32_t seq = atomic_fetch_add(&ch->seq, 1) + 1;
    atomic_store(&msg->sequence, seq);
    msg->msg_type = type;
    msg->payload_len = payload_len;
    msg->response_len = 0;
    msg->result = 0;
    msg->flags = 0;

    if (payload && payload_len > 0) {
        memcpy(msg->payload, payload, payload_len);
    }

    atomic_thread_fence(memory_order_release);

    /* Signal editor via response eventfd */
    uint64_t signal = 1;
    ssize_t written = write(ch->eventfd_response, &signal, sizeof(signal));
    if (written != sizeof(signal)) {
        return EXT_IPC_ERR_SYSCALL;
    }

    return EXT_IPC_OK;
}

/*
 * Wait for message from extension (editor-side)
 * Waits on eventfd_response - for Extension -> Editor messages
 */
int ext_ipc_recv_from_ext(ext_ipc_channel_t *ch, ext_ipc_msg_t **msg, int timeout_ms) {
    if (!ch || ch->shm == MAP_FAILED || !msg) {
        return EXT_IPC_ERR_INVALID;
    }

    /* Wait for signal from extension on response fd */
    struct pollfd pfd = {
        .fd = ch->eventfd_response,
        .events = POLLIN,
        .revents = 0
    };

    int poll_result = poll(&pfd, 1, timeout_ms);
    if (poll_result < 0) {
        if (errno == EINTR) {
            return EXT_IPC_ERR_TIMEOUT;
        }
        LOG_ERRORF("ext_ipc: recv_from_ext poll failed: %s", strerror(errno));
        return EXT_IPC_ERR_SYSCALL;
    }
    if (poll_result == 0) {
        return EXT_IPC_ERR_TIMEOUT;
    }

    /* Check for hangup */
    if (pfd.revents & (POLLHUP | POLLERR)) {
        LOG_INFO("ext_ipc: Extension closed the channel");
        return EXT_IPC_ERR_CLOSED;
    }

    /* Consume the eventfd signal */
    uint64_t consumed;
    if (read(ch->eventfd_response, &consumed, sizeof(consumed)) != sizeof(consumed)) {
        if (errno != EAGAIN) {
            LOG_WARNF("ext_ipc: Failed to consume response signal: %s", strerror(errno));
        }
    }

    /* Memory barrier */
    atomic_thread_fence(memory_order_acquire);

    /* Return pointer to message in shared memory */
    *msg = (ext_ipc_msg_t *)ch->shm;

    return EXT_IPC_OK;
}

/*
 * Send response to an extension's RPC call (editor-side)
 * Signals eventfd_request - the reverse direction of ext_ipc_respond()
 */
int ext_ipc_respond_to_ext(ext_ipc_channel_t *ch, int32_t result,
                           const void *data, uint32_t len) {
    if (!ch || ch->shm == MAP_FAILED) {
        return EXT_IPC_ERR_INVALID;
    }

    if (len > EXT_IPC_MAX_PAYLOAD) {
        return EXT_IPC_ERR_TOOBIG;
    }

    ext_ipc_msg_t *msg = (ext_ipc_msg_t *)ch->shm;

    /* Write response */
    msg->result = result;
    msg->response_len = len;

    if (data && len > 0) {
        memcpy(msg->payload, data, len);
    }

    atomic_thread_fence(memory_order_release);

    /* Signal the extension via request eventfd (reverse direction) */
    uint64_t signal = 1;
    ssize_t written = write(ch->eventfd_request, &signal, sizeof(signal));
    if (written != sizeof(signal)) {
        LOG_ERRORF("ext_ipc: Failed to signal response to ext: %s", strerror(errno));
        return EXT_IPC_ERR_SYSCALL;
    }

    return EXT_IPC_OK;
}

/* =========================================================================
 * Event loop integration
 * ========================================================================= */

int ext_ipc_poll_fd(ext_ipc_channel_t *ch, bool is_editor) {
    if (!ch) return -1;

    /* Editor waits on response fd, extension waits on request fd */
    return is_editor ? ch->eventfd_response : ch->eventfd_request;
}

bool ext_ipc_is_alive(ext_ipc_channel_t *ch) {
    if (!ch || ch->pidfd < 0) {
        return false;
    }

    /* Use poll on pidfd to check if process is still alive */
    struct pollfd pfd = {
        .fd = ch->pidfd,
        .events = POLLIN,
        .revents = 0
    };

    int result = poll(&pfd, 1, 0);  /* Non-blocking check */
    if (result > 0 && (pfd.revents & POLLIN)) {
        /* pidfd is readable when process exits */
        return false;
    }

    return true;
}

/* =========================================================================
 * pidfd management
 * ========================================================================= */

/*
 * Set the pidfd for lifecycle management (called after fork in parent)
 */
int ext_ipc_set_pid(ext_ipc_channel_t *ch, pid_t pid) {
    if (!ch) return EXT_IPC_ERR_INVALID;

    int pidfd = pidfd_open_wrapper(pid, 0);
    if (pidfd < 0) {
        /* pidfd_open may fail on older kernels - not fatal */
        LOG_WARNF("ext_ipc: pidfd_open failed for pid %d: %s",
                  (int)pid, strerror(errno));
        ch->pidfd = -1;
        return EXT_IPC_OK;  /* Non-fatal */
    }

    ch->pidfd = pidfd;
    LOG_DEBUGF("ext_ipc: Set pidfd=%d for pid=%d", pidfd, (int)pid);
    return EXT_IPC_OK;
}

/*
 * Wait for extension process to exit
 */
int ext_ipc_wait(ext_ipc_channel_t *ch, int *exit_status, int timeout_ms) {
    if (!ch) return EXT_IPC_ERR_INVALID;

    if (ch->pidfd >= 0) {
        /* Use pidfd for waiting */
        struct pollfd pfd = {
            .fd = ch->pidfd,
            .events = POLLIN,
            .revents = 0
        };

        int result = poll(&pfd, 1, timeout_ms);
        if (result < 0) {
            return EXT_IPC_ERR_SYSCALL;
        }
        if (result == 0) {
            return EXT_IPC_ERR_TIMEOUT;
        }

        /* Process exited - read status via waitid */
        siginfo_t info;
        if (waitid(P_PIDFD, (id_t)ch->pidfd, &info, WEXITED | WNOHANG) == 0) {
            if (exit_status) {
                *exit_status = info.si_status;
            }
            return EXT_IPC_OK;
        }
    }

    return EXT_IPC_ERR_INVALID;
}

/*
 * Get file descriptors for passing to child process
 * NOTE: FD_CLOEXEC is NOT cleared here - do it in the child process
 * to avoid fd leakage to other parallel forks
 */
int ext_ipc_get_fds_for_child(ext_ipc_channel_t *ch,
                              int *memfd_out, int *evreq_out, int *evresp_out) {
    if (!ch || !memfd_out || !evreq_out || !evresp_out) {
        return EXT_IPC_ERR_INVALID;
    }

    /* Just return the fd numbers - caller (child process) must clear FD_CLOEXEC */
    *memfd_out = ch->memfd;
    *evreq_out = ch->eventfd_request;
    *evresp_out = ch->eventfd_response;

    return EXT_IPC_OK;
}
