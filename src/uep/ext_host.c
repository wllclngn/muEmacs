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
#include <sys/prctl.h>
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

/* Config getters from extension_api.c */
extern int extension_config_get_int(const char *ext_name, const char *key, int default_val);
extern bool extension_config_get_bool(const char *ext_name, const char *key, bool default_val);
extern const char *extension_config_get_string(const char *ext_name, const char *key, const char *default_val);

/* =========================================================================
 * Global State
 * ========================================================================= */

static ext_host_entry_t g_extensions[EXT_HOST_MAX_EXTENSIONS];
static int g_num_extensions = 0;
static bool g_initialized = false;
static char *g_runner_path = NULL;

/* SIGCHLD handling - flag set by signal handler, real work done in reap function */
static volatile sig_atomic_t g_sigchld_pending = 0;

/* Extension init timeout (ms) - configurable via [extensions] init_timeout in settings.toml */
static int g_ext_init_timeout_ms = 10000;  /* Default 10 seconds for Go/Haskell runtimes */

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
 * Configuration
 * ========================================================================= */

void ext_host_set_init_timeout(int timeout_ms) {
    if (timeout_ms >= 1000) {  /* Minimum 1 second */
        g_ext_init_timeout_ms = timeout_ms;
        LOG_DEBUGF("ext_host: Init timeout set to %d ms", timeout_ms);
    }
}

int ext_host_get_init_timeout(void) {
    return g_ext_init_timeout_ms;
}

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

static ext_host_entry_t *find_extension_by_pid(pid_t pid) {
    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        if (g_extensions[i].active && g_extensions[i].pid == pid) {
            return &g_extensions[i];
        }
    }
    return NULL;
}

/* SIGCHLD handler - just set flag, do real work in ext_host_reap_children() */
static void sigchld_handler(int sig) {
    (void)sig;
    g_sigchld_pending = 1;
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void ext_host_init(void) {
    if (g_initialized) return;
    LOG_INFO("ext_host: Initializing");
    memset(g_extensions, 0, sizeof(g_extensions));
    g_num_extensions = 0;

    /* Register SIGCHLD handler for child crash detection */
    struct sigaction sa = {
        .sa_handler = sigchld_handler,
        .sa_flags = SA_NOCLDSTOP | SA_RESTART,  /* Only on exit, auto-restart syscalls */
    };
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        LOG_WARN("ext_host: Failed to register SIGCHLD handler");
    }

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
           rt == EXT_RUNTIME_PASCAL || rt == EXT_RUNTIME_RUST ||
           rt == EXT_RUNTIME_ZIG;
}

const char *ext_host_runtime_name(ext_runtime_t rt) {
    if (rt >= 0 && rt <= EXT_RUNTIME_UNKNOWN) return runtime_names[rt];
    return "Invalid";
}

/* =========================================================================
 * Extension Spawning - Parallel Implementation
 * ========================================================================= */

/*
 * Internal: Fork extension process without waiting for READY.
 * Returns entry pointer on success, NULL on failure.
 */
static ext_host_entry_t *spawn_fork_only(const char *name, const char *exe_path,
                                          ext_runtime_t runtime) {
    ext_host_entry_t *entry = alloc_entry();
    if (!entry) return NULL;

    /* Create IPC channel */
    ext_ipc_channel_t *ipc = ext_ipc_create();
    if (!ipc) {
        free_entry(entry);
        g_num_extensions--;
        return NULL;
    }

    /* Create death pipe - when parent dies, child gets POLLHUP on read end */
    if (pipe(ipc->death_pipe) < 0) {
        ext_ipc_destroy(ipc);
        free_entry(entry);
        g_num_extensions--;
        return NULL;
    }

    /* Get memfd for child */
    int memfd = ext_ipc_get_memfd(ipc);

    /* Find runner */
    const char *runner = g_runner_path;
    if (!runner) {
        static const char *build = "/home/mod/personal/PROGRAMMING/SYSTEM PROGRAMS/LINUX/μEmacs/build/bin/uemacs-ext-runner";
        runner = (access(build, X_OK) == 0) ? build : "uemacs-ext-runner";
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(ipc->death_pipe[0]);
        close(ipc->death_pipe[1]);
        ext_ipc_destroy(ipc);
        free_entry(entry);
        g_num_extensions--;
        return NULL;
    }

    if (pid == 0) {
        /* Child - close write end, keep read end for death detection */
        close(ipc->death_pipe[1]);

        /* Clear CLOEXEC on fds we need to pass */
        fcntl(memfd, F_SETFD, 0);
        fcntl(ipc->death_pipe[0], F_SETFD, 0);

        char memfd_str[16];
        snprintf(memfd_str, sizeof(memfd_str), "%d", memfd);

        char deathfd_str[16];
        snprintf(deathfd_str, sizeof(deathfd_str), "%d", ipc->death_pipe[0]);

        execl(runner, runner, "--memfd", memfd_str, "--ext", exe_path,
              "--deathfd", deathfd_str, NULL);
        _exit(127);
    }

    /* Parent - close read end, keep write end (just hold it open) */
    close(ipc->death_pipe[0]);
    ipc->death_pipe[0] = -1;

    ext_ipc_set_pid(ipc, pid);

    entry->name = strdup(name);
    entry->path = strdup(exe_path);
    entry->runtime = runtime;
    entry->ipc = ipc;
    entry->state = EXT_STATE_PENDING;
    entry->pid = pid;

    return entry;
}

/*
 * Internal: Process init messages for a single extension.
 * Returns true if extension became ready.
 */
static bool process_init_messages(ext_host_entry_t *entry) {
    if (!entry || !entry->ipc || entry->state != EXT_STATE_PENDING) return false;

    ext_ipc_shm_t *shm = entry->ipc->shm;

    /* Check if ready */
    if (atomic_load_explicit(&shm->ext_ready, memory_order_acquire)) {
        entry->state = EXT_STATE_READY;
        return true;
    }

    /* Process pending messages */
    ext_ipc_ring_t *ring = &shm->to_editor;
    for (int s = 0; s < EXT_IPC_RING_SLOTS; s++) {
        ext_ipc_slot_t *slot = &ring->slots[s];
        if (atomic_load_explicit(&slot->state, memory_order_acquire) != EXT_SLOT_PENDING)
            continue;

        switch (slot->msg_type) {
        case EXT_MSG_REGISTER_CMD: {
            ext_ipc_cmd_register_t *req = (ext_ipc_cmd_register_t *)slot->payload;
            int result = ext_host_register_command(entry->name, req->name);
            ext_ipc_slot_complete(slot, result, NULL, 0);
            entry->num_commands++;
            break;
        }
        case EXT_MSG_EVENT_SUBSCRIBE:
        case EXT_MSG_MODELINE_REGISTER:
            ext_ipc_slot_complete(slot, 0, NULL, 0);
            break;
        case EXT_MSG_LOG: {
            const char *msg = (const char *)slot->payload;
            if (msg && slot->payload_len > 0) {
                LOG_INFOF("ext[%s]: %s", entry->name, msg);
            }
            ext_ipc_slot_complete(slot, 0, NULL, 0);
            break;
        }
        default:
            ext_ipc_slot_complete(slot, 0, NULL, 0);
            break;
        }
    }

    /* Check again after processing */
    if (atomic_load_explicit(&shm->ext_ready, memory_order_acquire)) {
        entry->state = EXT_STATE_READY;
        return true;
    }

    return false;
}

int ext_host_spawn_async(const char *name, const char *exe_path, ext_runtime_t runtime) {
    if (!g_initialized) ext_host_init();
    if (!name || !exe_path) return -1;
    if (find_entry(name)) return -2;

    LOG_DEBUGF("ext_host: Spawning async '%s' (runtime=%s)", name, ext_host_runtime_name(runtime));

    ext_host_entry_t *entry = spawn_fork_only(name, exe_path, runtime);
    if (!entry) return -3;

    LOG_DEBUGF("ext_host: '%s' forked (pid=%d), state=PENDING", name, entry->pid);
    return 0;
}

int ext_host_await_pending(int timeout_ms) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int ready_count = 0;
    int pending_count;

    LOG_DEBUGF("ext_host: Awaiting pending extensions (timeout=%dms)", timeout_ms);

    do {
        pending_count = 0;

        /* Process ALL pending extensions in parallel */
        for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
            ext_host_entry_t *e = &g_extensions[i];
            if (!e->active || e->state != EXT_STATE_PENDING) continue;

            pending_count++;

            if (process_init_messages(e)) {
                LOG_INFOF("ext_host: '%s' ready (%d commands)", e->name, e->num_commands);
                ready_count++;
                pending_count--;
            }
        }

        if (pending_count == 0) break;

        /* Check timeout */
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = (now.tv_sec - start.tv_sec) * 1000 +
                       (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed > timeout_ms) {
            /* Mark remaining pending extensions as error */
            for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
                ext_host_entry_t *e = &g_extensions[i];
                if (e->active && e->state == EXT_STATE_PENDING) {
                    LOG_WARNF("ext_host: '%s' timeout (%dms) waiting for READY", e->name, timeout_ms);
                    e->state = EXT_STATE_ERROR;
                }
            }
            break;
        }

        /* Brief yield to let extensions process */
        sched_yield();

    } while (pending_count > 0);

    LOG_INFOF("ext_host: %d extension(s) ready", ready_count);
    return ready_count;
}

int ext_host_spawn(const char *name, const char *exe_path, ext_runtime_t runtime) {
    /* Convenience wrapper: spawn + wait for single extension */
    int rc = ext_host_spawn_async(name, exe_path, runtime);
    if (rc < 0) return rc;

    int ready = ext_host_await_pending(g_ext_init_timeout_ms);
    ext_host_entry_t *entry = find_entry(name);
    if (!entry || entry->state != EXT_STATE_READY) return -6;

    return 0;
}

void ext_host_shutdown_all(void) {
    LOG_DEBUGF("ext_host: Shutting down %d extensions", g_num_extensions);

    /* Phase 1: Signal shutdown to ALL extensions (atomic flag + SIGTERM) */
    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        if (!g_extensions[i].active) continue;
        if (g_extensions[i].ipc) {
            atomic_store(&g_extensions[i].ipc->shm->shutdown, 1);
        }
        if (g_extensions[i].pid > 0) {
            kill(g_extensions[i].pid, SIGTERM);
        }
    }

    /* Phase 2: Wait for ALL to exit concurrently (max 500ms total) */
    for (int retry = 0; retry < 5; retry++) {
        int alive = 0;
        for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
            if (!g_extensions[i].active || g_extensions[i].pid <= 0) continue;
            if (waitpid(g_extensions[i].pid, NULL, WNOHANG) > 0) {
                g_extensions[i].pid = 0;  /* Reaped */
            } else if (kill(g_extensions[i].pid, 0) == 0) {
                alive++;
            }
        }
        if (alive == 0) break;
        usleep(100000);  /* 100ms */
    }

    /* Phase 3: Force kill any remaining */
    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        if (!g_extensions[i].active) continue;
        if (g_extensions[i].pid > 0 && kill(g_extensions[i].pid, 0) == 0) {
            LOG_WARNF("ext_host: Force killing pid %d", g_extensions[i].pid);
            kill(g_extensions[i].pid, SIGKILL);
            waitpid(g_extensions[i].pid, NULL, 0);
        }
        free_entry(&g_extensions[i]);
    }

    g_num_extensions = 0;
    LOG_INFO("ext_host: Shutdown complete");
}

/* =========================================================================
 * SIGCHLD Reaping (child crash detection)
 * ========================================================================= */

void ext_host_reap_children(void) {
    if (!g_sigchld_pending) return;
    g_sigchld_pending = 0;

    int status;
    pid_t pid;

    /* Reap all terminated children (may be multiple) */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        ext_host_entry_t *entry = find_extension_by_pid(pid);
        if (!entry) continue;  /* Not one of our extensions */

        if (WIFEXITED(status)) {
            int code = WEXITSTATUS(status);
            if (code == 0) {
                LOG_INFOF("ext_host: Extension '%s' (pid %d) exited cleanly",
                          entry->name, pid);
            } else {
                LOG_WARNF("ext_host: Extension '%s' (pid %d) exited with code %d",
                          entry->name, pid, code);
            }
        } else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            LOG_ERRORF("ext_host: Extension '%s' (pid %d) killed by signal %d (%s)",
                       entry->name, pid, sig, strsignal(sig));
        }

        /* Mark as dead and clean up IPC resources */
        entry->state = EXT_STATE_DEAD;
        entry->pid = 0;

        /* Clean up IPC channel but preserve registry entry for error reporting */
        if (entry->ipc) {
            ext_ipc_destroy(entry->ipc);
            entry->ipc = NULL;
        }

        g_num_extensions--;
    }
}

/* =========================================================================
 * Non-blocking Message Polling (called from main loop)
 * ========================================================================= */

void ext_host_poll_nonblocking(void) {
    /* Check for crashed/exited children first */
    ext_host_reap_children();

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
            case EXT_MSG_SET_POINT: {
                ext_ipc_point_t *pt = (ext_ipc_point_t *)slot->payload;
                /* Move cursor to specified line/col */
                gotoline(true, pt->line);
                setccol(pt->col);
                ext_ipc_slot_complete(slot, 0, NULL, 0);
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
            case EXT_MSG_BUFFER_SET_SCRATCH: {
                uintptr_t handle = *(uintptr_t *)slot->payload;
                struct buffer *bp = (struct buffer *)handle;
                if (bp) bp->b_flag |= BFINVS;  /* Mark as scratch - no save prompt */
                ext_ipc_slot_complete(slot, 0, NULL, 0);
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
            case EXT_MSG_LOG: {
                const char *msg = (const char *)slot->payload;
                if (msg && slot->payload_len > 0) {
                    LOG_INFOF("ext[%s]: %s", e->name, msg);
                }
                ext_ipc_slot_complete(slot, 0, NULL, 0);
                break;
            }
            case EXT_MSG_CONFIG_INT: {
                struct { char ext_name[64]; char key[64]; int32_t default_val; } *req =
                    (void *)slot->payload;
                int32_t result = extension_config_get_int(req->ext_name, req->key, req->default_val);
                ext_ipc_slot_complete(slot, 0, &result, sizeof(result));
                break;
            }
            case EXT_MSG_CONFIG_BOOL: {
                struct { char ext_name[64]; char key[64]; int32_t default_val; } *req =
                    (void *)slot->payload;
                int32_t result = extension_config_get_bool(req->ext_name, req->key, req->default_val != 0) ? 1 : 0;
                ext_ipc_slot_complete(slot, 0, &result, sizeof(result));
                break;
            }
            case EXT_MSG_CONFIG_STRING: {
                struct { char ext_name[64]; char key[64]; char default_val[128]; } *req =
                    (void *)slot->payload;
                const char *result = extension_config_get_string(req->ext_name, req->key, req->default_val);
                ext_ipc_slot_complete(slot, 0, result, result ? strlen(result) + 1 : 0);
                break;
            }
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
