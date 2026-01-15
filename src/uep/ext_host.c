/*
 * ext_host.c - Extension Host with Atomic Ring Buffer IPC
 *
 * Manages out-of-process extensions using lock-free communication.
 * NO blocking - polling in main loop handles all messages.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>

/* Editor internals - same includes as extension_api.c */
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "internal/line.h"

#include "uep/ext_host.h"
#include "uep/ext_ipc.h"
#include "util/logger.h"

/* =========================================================================
 * Global State
 * ========================================================================= */

static ext_host_entry_t g_extensions[EXT_HOST_MAX_EXTENSIONS];
static int g_num_extensions = 0;
static bool g_initialized = false;
static char *g_runner_path = NULL;

/* Runtime names */
static const char *runtime_names[] = {
    [EXT_RUNTIME_C]       = "C",
    [EXT_RUNTIME_RUST]    = "Rust",
    [EXT_RUNTIME_ZIG]     = "Zig",
    [EXT_RUNTIME_GO]      = "Go",
    [EXT_RUNTIME_ADA]     = "Ada",
    [EXT_RUNTIME_HASKELL] = "Haskell",
    [EXT_RUNTIME_CRYSTAL] = "Crystal",
    [EXT_RUNTIME_PASCAL]  = "Pascal",
    [EXT_RUNTIME_UNKNOWN] = "Unknown",
};

/* =========================================================================
 * Internal Helpers
 * ========================================================================= */

static ext_host_entry_t *find_entry(const char *name) {
    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        if (g_extensions[i].active && g_extensions[i].name &&
            strcmp(g_extensions[i].name, name) == 0) {
            return &g_extensions[i];
        }
    }
    return NULL;
}

static void free_entry(ext_host_entry_t *entry) {
    if (!entry) return;
    free(entry->name);
    free(entry->path);
    if (entry->ipc) ext_ipc_destroy(entry->ipc);
    memset(entry, 0, sizeof(*entry));
}

static ext_host_entry_t *alloc_entry(void) {
    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        if (!g_extensions[i].active) {
            g_extensions[i].active = true;
            g_extensions[i].pending_slot = -1;
            g_num_extensions++;
            return &g_extensions[i];
        }
    }
    return NULL;
}

static ext_host_command_t *find_command_slot(ext_host_entry_t *entry, const char *name) {
    for (int i = 0; i < EXT_HOST_MAX_COMMANDS_PER_EXT; i++) {
        if (entry->commands[i].active &&
            strcmp(entry->commands[i].name, name) == 0) {
            return &entry->commands[i];
        }
    }
    return NULL;
}

static ext_host_command_t *alloc_command_slot(ext_host_entry_t *entry) {
    for (int i = 0; i < EXT_HOST_MAX_COMMANDS_PER_EXT; i++) {
        if (!entry->commands[i].active) {
            return &entry->commands[i];
        }
    }
    return NULL;
}

static bool dir_has_extension(const char *dir, const char *ext) {
    DIR *d = opendir(dir);
    if (!d) return false;
    size_t ext_len = strlen(ext);
    struct dirent *e;
    bool found = false;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if (len > ext_len && strcmp(e->d_name + len - ext_len, ext) == 0) {
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void ext_host_init(void) {
    if (g_initialized) return;
    LOG_INFO("ext_host: Initializing");
    memset(g_extensions, 0, sizeof(g_extensions));
    g_num_extensions = 0;
    g_initialized = true;
}

void ext_host_cleanup(void) {
    if (!g_initialized) return;
    LOG_INFOF("ext_host: Cleanup (%d extensions)", g_num_extensions);
    ext_host_shutdown_all();
    free(g_runner_path);
    g_runner_path = NULL;
    g_initialized = false;
}

/* =========================================================================
 * Runtime Detection
 * ========================================================================= */

ext_runtime_t ext_host_detect_runtime(const char *dir) {
    if (!dir) return EXT_RUNTIME_UNKNOWN;
    if (dir_has_extension(dir, ".go"))  return EXT_RUNTIME_GO;
    if (dir_has_extension(dir, ".adb")) return EXT_RUNTIME_ADA;
    if (dir_has_extension(dir, ".ads")) return EXT_RUNTIME_ADA;
    if (dir_has_extension(dir, ".hs"))  return EXT_RUNTIME_HASKELL;
    if (dir_has_extension(dir, ".cr"))  return EXT_RUNTIME_CRYSTAL;
    if (dir_has_extension(dir, ".pas")) return EXT_RUNTIME_PASCAL;
    if (dir_has_extension(dir, ".pp"))  return EXT_RUNTIME_PASCAL;
    if (dir_has_extension(dir, ".rs"))  return EXT_RUNTIME_RUST;
    if (dir_has_extension(dir, ".zig")) return EXT_RUNTIME_ZIG;
    return EXT_RUNTIME_C;
}

bool ext_host_needs_isolation(ext_runtime_t rt) {
    return rt == EXT_RUNTIME_GO || rt == EXT_RUNTIME_ADA ||
           rt == EXT_RUNTIME_HASKELL || rt == EXT_RUNTIME_CRYSTAL ||
           rt == EXT_RUNTIME_PASCAL;
}

const char *ext_host_runtime_name(ext_runtime_t rt) {
    if (rt >= 0 && rt <= EXT_RUNTIME_UNKNOWN) return runtime_names[rt];
    return "Invalid";
}

/* =========================================================================
 * Extension Spawning
 * ========================================================================= */

int ext_host_spawn(const char *name, const char *exe_path, ext_runtime_t runtime) {
    if (!g_initialized) ext_host_init();
    if (!name || !exe_path) return -1;
    if (find_entry(name)) return -2;

    ext_host_entry_t *entry = alloc_entry();
    if (!entry) return -3;

    /* Create IPC channel */
    ext_ipc_channel_t *ipc = ext_ipc_create();
    if (!ipc) {
        free_entry(entry);
        g_num_extensions--;
        return -4;
    }

    /* Get memfd for child */
    int memfd = ext_ipc_get_memfd(ipc);

    /* Find runner */
    const char *runner = g_runner_path;
    if (!runner) {
        static const char *build = "/home/mod/personal/PROGRAMMING/SYSTEM PROGRAMS/LINUX/μEmacs/build/bin/uemacs-ext-runner";
        runner = (access(build, X_OK) == 0) ? build : "uemacs-ext-runner";
    }

    LOG_DEBUGF("ext_host: Spawning '%s' (runtime=%s)", name, ext_host_runtime_name(runtime));

    pid_t pid = fork();
    if (pid < 0) {
        ext_ipc_destroy(ipc);
        free_entry(entry);
        g_num_extensions--;
        return -5;
    }

    if (pid == 0) {
        /* Child - clear CLOEXEC on memfd */
        fcntl(memfd, F_SETFD, 0);

        char memfd_str[16];
        snprintf(memfd_str, sizeof(memfd_str), "%d", memfd);

        execl(runner, runner, "--memfd", memfd_str, "--ext", exe_path, NULL);
        _exit(127);
    }

    /* Parent */
    ext_ipc_set_pid(ipc, pid);

    entry->name = strdup(name);
    entry->path = strdup(exe_path);
    entry->runtime = runtime;
    entry->ipc = ipc;
    entry->state = EXT_STATE_PENDING;
    entry->pid = pid;

    /* Wait for READY with timeout, processing init messages */
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int msg_count = 0, cmd_count = 0;

    while (1) {
        /* Check if ready */
        if (atomic_load_explicit(&ipc->shm->ext_ready, memory_order_acquire)) {
            entry->state = EXT_STATE_READY;
            LOG_INFOF("ext_host: '%s' ready (%d msgs, %d cmds)", name, msg_count, cmd_count);
            return 0;
        }

        /* Process any init messages from extension */
        int slot_idx = ext_ipc_find_pending_slot(&ipc->shm->to_editor);
        if (slot_idx >= 0) {
            ext_ipc_slot_t *slot = &ipc->shm->to_editor.slots[slot_idx];

            switch (slot->msg_type) {
            case EXT_MSG_REGISTER_CMD: {
                ext_ipc_cmd_register_t *req = (ext_ipc_cmd_register_t *)slot->payload;
                int result = ext_host_register_command(name, req->name);
                LOG_DEBUGF("ext_host: '%s' registered '%s' (r=%d)", name, req->name, result);
                ext_ipc_slot_complete(slot, result, NULL, 0);
                cmd_count++;
                break;
            }
            case EXT_MSG_EVENT_SUBSCRIBE:
            case EXT_MSG_MODELINE_REGISTER:
            case EXT_MSG_LOG:
                ext_ipc_slot_complete(slot, 0, NULL, 0);
                break;
            default:
                ext_ipc_slot_complete(slot, 0, NULL, 0);
                break;
            }
            msg_count++;
            clock_gettime(CLOCK_MONOTONIC, &start);  /* Reset timeout on activity */
            continue;
        }

        /* Check timeout */
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = (now.tv_sec - start.tv_sec) * 1000 +
                       (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed > 5000) {
            LOG_WARNF("ext_host: '%s' timeout waiting for READY", name);
            entry->state = EXT_STATE_ERROR;
            return -6;
        }

        sched_yield();
    }
}

void ext_host_shutdown_all(void) {
    LOG_DEBUGF("ext_host: Shutting down %d extensions", g_num_extensions);

    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        if (!g_extensions[i].active || !g_extensions[i].ipc) continue;

        /* Signal shutdown */
        atomic_store(&g_extensions[i].ipc->shm->shutdown, 1);
    }

    /* Wait briefly for graceful shutdown */
    usleep(100000);

    /* Kill any remaining */
    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        if (!g_extensions[i].active) continue;
        if (g_extensions[i].pid > 0) {
            kill(g_extensions[i].pid, SIGTERM);
            waitpid(g_extensions[i].pid, NULL, WNOHANG);
        }
        free_entry(&g_extensions[i]);
    }

    g_num_extensions = 0;
    LOG_INFO("ext_host: Shutdown complete");
}

/* =========================================================================
 * Non-blocking Message Polling (called from main loop)
 * ========================================================================= */

void ext_host_poll_nonblocking(void) {
    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        ext_host_entry_t *e = &g_extensions[i];
        if (!e->active || !e->ipc || e->state == EXT_STATE_ERROR) continue;

        ext_ipc_ring_t *ring = &e->ipc->shm->to_editor;

        /* Check all slots for pending messages */
        for (int s = 0; s < EXT_IPC_RING_SLOTS; s++) {
            ext_ipc_slot_t *slot = &ring->slots[s];
            if (atomic_load_explicit(&slot->state, memory_order_acquire) != EXT_SLOT_PENDING)
                continue;

            /* Handle message */
            switch (slot->msg_type) {
            case EXT_MSG_CMD_COMPLETE: {
                ext_ipc_cmd_response_t *resp = (ext_ipc_cmd_response_t *)slot->payload;
                if (resp->message[0]) {
                    mlwrite("%s", resp->message);
                }
                e->state = EXT_STATE_READY;
                ext_ipc_slot_release(slot);
                /* Release the command slot we used */
                if (e->pending_slot >= 0) {
                    ext_ipc_slot_release(&e->ipc->shm->to_ext.slots[e->pending_slot]);
                    e->pending_slot = -1;
                }
                break;
            }
            case EXT_MSG_MESSAGE: {
                ext_ipc_message_t *msg = (ext_ipc_message_t *)slot->payload;
                mlwrite("%s", msg->text);
                ext_ipc_slot_complete(slot, 0, NULL, 0);
                break;
            }
            case EXT_MSG_GET_POINT: {
                ext_ipc_point_t pt = { .line = getcline(), .col = getccol(0) };
                ext_ipc_slot_complete(slot, 0, &pt, sizeof(pt));
                break;
            }
            case EXT_MSG_REGISTER_CMD: {
                ext_ipc_cmd_register_t *req = (ext_ipc_cmd_register_t *)slot->payload;
                int result = ext_host_register_command(e->name, req->name);
                ext_ipc_slot_complete(slot, result, NULL, 0);
                break;
            }
            case EXT_MSG_CURRENT_BUFFER: {
                uintptr_t handle = (uintptr_t)curbp;
                ext_ipc_slot_complete(slot, 0, &handle, sizeof(handle));
                break;
            }
            case EXT_MSG_BUFFER_CREATE: {
                const char *name = (const char *)slot->payload;
                struct buffer *bp = bfind((char *)name, 1, 0);  /* 1=create if not found */
                uintptr_t handle = (uintptr_t)bp;
                ext_ipc_slot_complete(slot, bp ? 0 : -1, &handle, sizeof(handle));
                break;
            }
            case EXT_MSG_BUFFER_SWITCH: {
                uintptr_t handle = *(uintptr_t *)slot->payload;
                struct buffer *bp = (struct buffer *)handle;
                int result = swbuffer(bp);
                update(1);  /* Force display update */
                ext_ipc_slot_complete(slot, result, NULL, 0);
                break;
            }
            case EXT_MSG_BUFFER_CLEAR: {
                uintptr_t handle = *(uintptr_t *)slot->payload;
                struct buffer *bp = (struct buffer *)handle;
                int result = bclear(bp);
                ext_ipc_slot_complete(slot, result, NULL, 0);
                break;
            }
            case EXT_MSG_BUFFER_INSERT: {
                const char *text = (const char *)slot->payload;
                int result = linstr(text);
                ext_ipc_slot_complete(slot, result, NULL, 0);
                break;
            }
            case EXT_MSG_PROMPT: {
                ext_ipc_prompt_req_t *req = (ext_ipc_prompt_req_t *)slot->payload;
                ext_ipc_prompt_resp_t resp = {0};
                char buf[1024] = {0};
                int result = minibuf_read(req->prompt, buf, sizeof(buf));
                if (result != true) {  /* User cancelled */
                    resp.cancelled = 1;
                } else {
                    resp.cancelled = 0;
                    strncpy(resp.response, buf, sizeof(resp.response) - 1);
                }
                ext_ipc_slot_complete(slot, 0, &resp, sizeof(resp));
                break;
            }
            case EXT_MSG_PROMPT_YN: {
                ext_ipc_prompt_req_t *req = (ext_ipc_prompt_req_t *)slot->payload;
                int32_t result = mlyesno(req->prompt) == true ? 1 : 0;
                ext_ipc_slot_complete(slot, 0, &result, sizeof(result));
                break;
            }
            case EXT_MSG_LOG:
                ext_ipc_slot_complete(slot, 0, NULL, 0);
                break;
            default:
                LOG_DEBUGF("ext_host: Unhandled message type 0x%x", slot->msg_type);
                ext_ipc_slot_complete(slot, 0, NULL, 0);
                break;
            }
        }
    }
}

/* =========================================================================
 * Command Invocation (Non-blocking)
 * ========================================================================= */

int ext_host_invoke_command(const char *cmd_name, int f, int n) {
    const char *owner = ext_host_find_command_owner(cmd_name);
    if (!owner) return -1;

    ext_host_entry_t *entry = find_entry(owner);
    if (!entry || !entry->ipc || entry->state != EXT_STATE_READY) return -2;

    ext_ipc_ring_t *ring = &entry->ipc->shm->to_ext;

    /* Find empty slot */
    int slot_idx = ext_ipc_find_empty_slot(ring);
    if (slot_idx < 0) return -3;

    ext_ipc_slot_t *slot = &ring->slots[slot_idx];

    /* Build request */
    ext_ipc_cmd_invoke_t req = { .f = f, .n = n };
    strncpy(req.name, cmd_name, sizeof(req.name) - 1);

    /* Send command */
    ext_ipc_slot_write(slot, EXT_MSG_INVOKE_CMD, &req, sizeof(req));

    entry->state = EXT_STATE_BUSY;
    entry->pending_slot = slot_idx;

    /* Wait for completion while handling API calls */
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (entry->state == EXT_STATE_BUSY) {
        ext_host_poll_nonblocking();

        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = (now.tv_sec - start.tv_sec) * 1000 +
                       (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed > 30000) {
            LOG_WARNF("ext_host: Command '%s' timeout", cmd_name);
            entry->state = EXT_STATE_READY;
            return -4;
        }

#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#else
        sched_yield();
#endif
    }

    return 0;
}

/* =========================================================================
 * Command Registry
 * ========================================================================= */

int ext_host_register_command(const char *ext_name, const char *cmd_name) {
    ext_host_entry_t *entry = find_entry(ext_name);
    if (!entry) return -1;
    if (find_command_slot(entry, cmd_name)) return 0;

    ext_host_command_t *cmd = alloc_command_slot(entry);
    if (!cmd) return -2;

    strncpy(cmd->name, cmd_name, sizeof(cmd->name) - 1);
    cmd->active = true;
    entry->num_commands++;
    return 0;
}

int ext_host_unregister_command(const char *ext_name, const char *cmd_name) {
    ext_host_entry_t *entry = find_entry(ext_name);
    if (!entry) return -1;

    ext_host_command_t *cmd = find_command_slot(entry, cmd_name);
    if (!cmd) return -2;

    cmd->active = false;
    memset(cmd->name, 0, sizeof(cmd->name));
    entry->num_commands--;
    return 0;
}

bool ext_host_has_command(const char *cmd_name) {
    return ext_host_find_command_owner(cmd_name) != NULL;
}

const char *ext_host_find_command_owner(const char *cmd_name) {
    if (!cmd_name) return NULL;
    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        if (!g_extensions[i].active) continue;
        for (int j = 0; j < EXT_HOST_MAX_COMMANDS_PER_EXT; j++) {
            if (g_extensions[i].commands[j].active &&
                strcmp(g_extensions[i].commands[j].name, cmd_name) == 0) {
                return g_extensions[i].name;
            }
        }
    }
    return NULL;
}

/* =========================================================================
 * Query Functions
 * ========================================================================= */

int ext_host_count(void) {
    return g_num_extensions;
}

bool ext_host_is_loaded(const char *name) {
    return find_entry(name) != NULL;
}

ext_state_t ext_host_get_state(const char *name) {
    ext_host_entry_t *e = find_entry(name);
    return e ? e->state : EXT_STATE_ERROR;
}

void ext_host_set_runner_path(const char *path) {
    free(g_runner_path);
    g_runner_path = path ? strdup(path) : NULL;
}
