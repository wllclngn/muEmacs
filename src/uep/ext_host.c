/*
 * ext_host.c - Extension Host Implementation
 *
 * Manages out-of-process extensions for polyglot runtime isolation.
 * Spawns extensions, routes API calls, handles lifecycle.
 *
 * C23 compliant
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "uep/ext_host.h"
#include "uep/ext_ipc.h"
#include "util/logger.h"

/*
 * Remote extension registry (protected by g_mutex)
 */
static ext_host_entry_t g_extensions[EXT_HOST_MAX_EXTENSIONS];
static int g_num_extensions = 0;
static bool g_initialized = false;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Path to extension runner executable
 */
static char *g_runner_path = NULL;

/*
 * Runtime name strings
 */
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
 * Internal helpers
 * ========================================================================= */

/*
 * Find extension entry by name
 */
static ext_host_entry_t *find_entry(const char *name)
{
    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        if (g_extensions[i].active && g_extensions[i].name &&
            strcmp(g_extensions[i].name, name) == 0) {
            return &g_extensions[i];
        }
    }
    return NULL;
}

/*
 * Free extension entry resources
 */
static void free_entry(ext_host_entry_t *entry)
{
    if (!entry) return;

    free(entry->name);
    free(entry->path);
    if (entry->ipc) {
        ext_ipc_destroy(entry->ipc);
    }

    memset(entry, 0, sizeof(*entry));
}

/*
 * Find and reserve a free slot in registry (thread-safe)
 * Marks slot as active to prevent races.
 * Caller must call release_slot() on failure.
 */
static ext_host_entry_t *reserve_slot(void)
{
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        if (!g_extensions[i].active) {
            g_extensions[i].active = true;  /* Reserve immediately */
            g_num_extensions++;
            LOG_DEBUGF("ext_host: Reserved slot %d (total=%d)", i, g_num_extensions);
            pthread_mutex_unlock(&g_mutex);
            return &g_extensions[i];
        }
    }
    pthread_mutex_unlock(&g_mutex);
    LOG_WARN("ext_host: No free slots available");
    return NULL;
}

/*
 * Release a reserved slot on failure
 */
static void release_slot(ext_host_entry_t *entry)
{
    pthread_mutex_lock(&g_mutex);
    int idx = (int)(entry - g_extensions);
    LOG_DEBUGF("ext_host: Releasing slot %d", idx);
    free_entry(entry);
    g_num_extensions--;
    pthread_mutex_unlock(&g_mutex);
}

/*
 * Check if directory contains files matching extension
 */
static bool dir_has_extension(const char *dir_path, const char *ext)
{
    DIR *dir = opendir(dir_path);
    if (!dir) return false;

    size_t ext_len = strlen(ext);
    struct dirent *entry;
    bool found = false;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN)
            continue;

        size_t name_len = strlen(entry->d_name);
        if (name_len > ext_len &&
            strcmp(entry->d_name + name_len - ext_len, ext) == 0) {
            found = true;
            break;
        }
    }

    closedir(dir);
    return found;
}

/*
 * Send shutdown message and wait for process to exit
 */
static int shutdown_extension(ext_host_entry_t *entry, int timeout_ms)
{
    if (!entry || !entry->active) return -1;

    /* Send shutdown message */
    if (entry->ipc && entry->state == EXT_STATE_READY) {
        ext_ipc_notify(entry->ipc, EXT_MSG_SHUTDOWN, NULL, 0);
    }

    /* Wait for process to exit */
    if (entry->pid > 0) {
        int status;
        int elapsed = 0;
        const int poll_interval = 50;

        while (elapsed < timeout_ms) {
            pid_t result = waitpid(entry->pid, &status, WNOHANG);
            if (result == entry->pid) {
                /* Process exited */
                entry->state = EXT_STATE_DEAD;
                entry->pid = 0;
                break;
            } else if (result < 0 && errno != EINTR) {
                /* Error */
                break;
            }

            usleep(poll_interval * 1000);
            elapsed += poll_interval;
        }

        /* Force kill if still running */
        if (entry->pid > 0) {
            kill(entry->pid, SIGKILL);
            waitpid(entry->pid, &status, 0);
            entry->pid = 0;
        }
    }

    free_entry(entry);
    return 0;
}

/*
 * Find command slot in extension
 */
static ext_host_command_t *find_command_slot(ext_host_entry_t *entry, const char *name)
{
    for (int i = 0; i < EXT_HOST_MAX_COMMANDS_PER_EXT; i++) {
        if (entry->commands[i].active &&
            strcmp(entry->commands[i].name, name) == 0) {
            return &entry->commands[i];
        }
    }
    return NULL;
}

/*
 * Find free command slot
 */
static ext_host_command_t *find_free_command_slot(ext_host_entry_t *entry)
{
    for (int i = 0; i < EXT_HOST_MAX_COMMANDS_PER_EXT; i++) {
        if (!entry->commands[i].active) {
            return &entry->commands[i];
        }
    }
    return NULL;
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void ext_host_init(void)
{
    if (g_initialized) return;

    LOG_INFO("ext_host: Initializing extension host");
    memset(g_extensions, 0, sizeof(g_extensions));
    g_num_extensions = 0;
    g_initialized = true;
}

void ext_host_cleanup(void)
{
    if (!g_initialized) return;

    LOG_INFOF("ext_host: Cleanup starting (%d extensions)", g_num_extensions);
    ext_host_shutdown_all();

    free(g_runner_path);
    g_runner_path = NULL;

    g_initialized = false;
}

/* =========================================================================
 * Runtime detection
 * ========================================================================= */

ext_runtime_t ext_host_detect_runtime(const char *ext_dir)
{
    if (!ext_dir) return EXT_RUNTIME_UNKNOWN;

    /* Check for language-specific source files */
    if (dir_has_extension(ext_dir, ".go"))  return EXT_RUNTIME_GO;
    if (dir_has_extension(ext_dir, ".adb")) return EXT_RUNTIME_ADA;
    if (dir_has_extension(ext_dir, ".ads")) return EXT_RUNTIME_ADA;
    if (dir_has_extension(ext_dir, ".hs"))  return EXT_RUNTIME_HASKELL;
    if (dir_has_extension(ext_dir, ".cr"))  return EXT_RUNTIME_CRYSTAL;
    if (dir_has_extension(ext_dir, ".pas")) return EXT_RUNTIME_PASCAL;
    if (dir_has_extension(ext_dir, ".pp"))  return EXT_RUNTIME_PASCAL;
    if (dir_has_extension(ext_dir, ".rs"))  return EXT_RUNTIME_RUST;
    if (dir_has_extension(ext_dir, ".zig")) return EXT_RUNTIME_ZIG;

    /* Default to C for .c/.h only directories */
    return EXT_RUNTIME_C;
}

bool ext_host_needs_isolation(ext_runtime_t runtime)
{
    switch (runtime) {
    case EXT_RUNTIME_C:
    case EXT_RUNTIME_RUST:
    case EXT_RUNTIME_ZIG:
        /* These runtimes are ASan-compatible, can run in-process */
        return false;

    case EXT_RUNTIME_GO:
    case EXT_RUNTIME_ADA:
    case EXT_RUNTIME_HASKELL:
    case EXT_RUNTIME_CRYSTAL:
    case EXT_RUNTIME_PASCAL:
    case EXT_RUNTIME_UNKNOWN:
    default:
        /* These have their own thread/memory runtimes, need isolation */
        return true;
    }
}

const char *ext_host_runtime_name(ext_runtime_t runtime)
{
    if (runtime >= 0 && runtime <= EXT_RUNTIME_UNKNOWN) {
        return runtime_names[runtime];
    }
    return "Invalid";
}

/* =========================================================================
 * Extension lifecycle
 * ========================================================================= */

int ext_host_spawn(const char *name, const char *exe_path, ext_runtime_t runtime)
{
    LOG_DEBUGF("ext_host: Spawning '%s' (runtime=%s, path=%s)",
               name, ext_host_runtime_name(runtime), exe_path);

    if (!g_initialized) {
        ext_host_init();
    }

    if (!name || !exe_path) {
        LOG_ERROR("ext_host: spawn called with NULL name or path");
        return -1;
    }

    /* Check if already loaded (needs lock for thread safety) */
    pthread_mutex_lock(&g_mutex);
    if (find_entry(name)) {
        pthread_mutex_unlock(&g_mutex);
        LOG_WARNF("ext_host: Extension '%s' already loaded", name);
        return -2;  /* Already exists */
    }
    pthread_mutex_unlock(&g_mutex);

    /* Reserve a slot atomically to prevent races */
    ext_host_entry_t *entry = reserve_slot();
    if (!entry) {
        LOG_ERROR("ext_host: No free slots available");
        return -3;  /* No slots available */
    }
    LOG_DEBUGF("ext_host: Reserved slot for '%s'", name);

    /* Create IPC channel */
    ext_ipc_channel_t *ipc = ext_ipc_create(EXT_IPC_DEFAULT_SHM_SIZE);
    if (!ipc) {
        LOG_ERRORF("ext_host: Failed to create IPC channel for '%s'", name);
        release_slot(entry);
        return -4;  /* IPC creation failed */
    }
    LOG_DEBUG("ext_host: IPC channel created");

    /* Get file descriptors for child */
    int memfd, evreq, evresp;
    if (ext_ipc_get_fds_for_child(ipc, &memfd, &evreq, &evresp) < 0) {
        LOG_ERRORF("ext_host: Failed to get FDs for '%s'", name);
        ext_ipc_destroy(ipc);
        release_slot(entry);
        return -5;
    }

    /* Determine runner path - use build dir if available */
    const char *runner = g_runner_path;
    if (!runner) {
        /* Try build directory first for development */
        static const char *build_runner = "/home/mod/personal/PROGRAMMING/SYSTEM PROGRAMS/LINUX/μEmacs/build/bin/uemacs-ext-runner";
        if (access(build_runner, X_OK) == 0) {
            runner = build_runner;
        } else {
            runner = "uemacs-ext-runner";  /* Fall back to PATH */
        }
    }
    LOG_DEBUGF("ext_host: Using runner '%s'", runner);

    /* Fork child process */
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERRORF("ext_host: Fork failed for '%s': %s", name, strerror(errno));
        ext_ipc_destroy(ipc);
        release_slot(entry);
        return -6;  /* Fork failed */
    }

    if (pid == 0) {
        /* Child process */

        /* Clear close-on-exec for IPC fds */
        fcntl(memfd, F_SETFD, 0);
        fcntl(evreq, F_SETFD, 0);
        fcntl(evresp, F_SETFD, 0);

        /* Convert fds to strings */
        char memfd_str[16], evreq_str[16], evresp_str[16], size_str[32];
        snprintf(memfd_str, sizeof(memfd_str), "%d", memfd);
        snprintf(evreq_str, sizeof(evreq_str), "%d", evreq);
        snprintf(evresp_str, sizeof(evresp_str), "%d", evresp);
        snprintf(size_str, sizeof(size_str), "%zu", (size_t)EXT_IPC_DEFAULT_SHM_SIZE);

        /* Log exec attempt (child process) */
        LOG_DEBUGF("ext_host[child]: exec %s --memfd %s --evreq %s --evresp %s --size %s --ext %s",
                   runner, memfd_str, evreq_str, evresp_str, size_str, exe_path);

        /* Execute runner with extension path */
        execlp(runner, runner,
               "--memfd", memfd_str,
               "--evreq", evreq_str,
               "--evresp", evresp_str,
               "--size", size_str,
               "--ext", exe_path,
               (char *)NULL);

        /* exec failed */
        LOG_ERRORF("ext_host[child]: execlp failed: %s", strerror(errno));
        _exit(127);
    }

    /* Parent process */
    LOG_DEBUGF("ext_host: Forked child pid=%d for '%s'", pid, name);

    /* Set pid in IPC channel for pidfd */
    ext_ipc_set_pid(ipc, pid);

    /* Initialize entry (active already set by reserve_slot) */
    entry->name = strdup(name);
    entry->path = strdup(exe_path);
    entry->runtime = runtime;
    entry->state = EXT_STATE_INIT;
    entry->pid = pid;
    entry->ipc = ipc;
    entry->num_commands = 0;

    /* Wait for READY message from extension (editor waits on response eventfd) */
    LOG_DEBUGF("ext_host: Waiting for READY from '%s' (timeout=%dms)", name, EXT_IPC_TIMEOUT_MS);
    ext_ipc_msg_t *msg;
    int result = ext_ipc_recv_from_ext(ipc, &msg, EXT_IPC_TIMEOUT_MS);
    if (result == EXT_IPC_OK && msg->msg_type == EXT_MSG_READY) {
        /* Send acknowledgment so runner can proceed to init() */
        ext_ipc_respond_to_ext(ipc, 0, NULL, 0);

        entry->state = EXT_STATE_READY;
        LOG_INFOF("ext_host: Extension '%s' ready (pid=%d)", name, pid);

        /*
         * Process any registration messages from extension's init()
         * Extension's init() may call register_command, subscribe_event, etc.
         * which send messages we need to handle before returning.
         */
        LOG_DEBUG("ext_host: Processing init messages...");
        int msg_count = 0;
        while (msg_count < 100) {  /* Safety limit */
            ext_ipc_msg_t *init_msg;
            int recv_result = ext_ipc_recv_from_ext(ipc, &init_msg, 100);  /* 100ms timeout */
            if (recv_result == EXT_IPC_ERR_TIMEOUT) {
                break;  /* No more messages pending */
            }
            if (recv_result != EXT_IPC_OK) {
                LOG_WARNF("ext_host: Error receiving init message: %d", recv_result);
                break;
            }

            LOG_DEBUGF("ext_host: Processing init message type=0x%x", init_msg->msg_type);

            /* Handle the message inline (same logic as ext_host_handle_message) */
            switch (init_msg->msg_type) {
            case EXT_MSG_REGISTER_CMD: {
                ext_ipc_cmd_register_t *req = (ext_ipc_cmd_register_t *)init_msg->payload;
                LOG_DEBUGF("ext_host: Registering command '%s' from '%s' during init", req->name, name);
                int reg_result = ext_host_register_command(name, req->name);
                ext_ipc_respond_to_ext(ipc, reg_result, NULL, 0);
                break;
            }
            case EXT_MSG_EVENT_SUBSCRIBE: {
                ext_ipc_event_sub_t *req = (ext_ipc_event_sub_t *)init_msg->payload;
                int sub_result = ext_host_subscribe_event(name, req->event_name, req->priority);
                ext_ipc_respond_to_ext(ipc, sub_result, NULL, 0);
                break;
            }
            default:
                /* Other messages - just acknowledge */
                ext_ipc_respond_to_ext(ipc, 0, NULL, 0);
                break;
            }
            msg_count++;
        }
        LOG_DEBUGF("ext_host: Processed %d init messages from '%s'", msg_count, name);

        return 0;
    }

    /* Extension failed to initialize - clean up */
    LOG_WARNF("ext_host: Extension '%s' failed to send READY (result=%d, msg_type=0x%x)",
              name, result, msg ? msg->msg_type : 0);

    /* Kill the child process before releasing slot */
    LOG_DEBUGF("ext_host: Killing child pid=%d", pid);
    if (pid > 0) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }

    /* Release the reserved slot (frees entry resources) */
    LOG_DEBUGF("ext_host: Releasing slot for '%s'", name);
    release_slot(entry);

    return -7;
}

int ext_host_shutdown(const char *name, int timeout_ms)
{
    ext_host_entry_t *entry = find_entry(name);
    if (!entry) {
        return -1;
    }

    int result = shutdown_extension(entry, timeout_ms);
    if (result == 0) {
        g_num_extensions--;
    }
    return result;
}

void ext_host_shutdown_all(void)
{
    LOG_DEBUGF("ext_host: Shutting down all extensions (%d active)", g_num_extensions);

    int shutdown_count = 0;

    /* Send SIGTERM to all extension processes - don't wait */
    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        if (g_extensions[i].active) {
            /* Notify via IPC if possible */
            if (g_extensions[i].ipc && g_extensions[i].state == EXT_STATE_READY) {
                ext_ipc_notify(g_extensions[i].ipc, EXT_MSG_SHUTDOWN, NULL, 0);
            }
            /* SIGTERM the process */
            if (g_extensions[i].pid > 0) {
                kill(g_extensions[i].pid, SIGTERM);
            }
            /* Clean up our side immediately */
            free_entry(&g_extensions[i]);
            shutdown_count++;
        }
    }

    /* Kernel will reap children when we exit - no need to wait */
    g_num_extensions = 0;
    LOG_INFOF("ext_host: Shutdown complete (%d extensions)", shutdown_count);
}

bool ext_host_is_alive(const char *name)
{
    ext_host_entry_t *entry = find_entry(name);
    if (!entry || !entry->ipc) {
        return false;
    }
    return ext_ipc_is_alive(entry->ipc);
}

ext_state_t ext_host_get_state(const char *name)
{
    ext_host_entry_t *entry = find_entry(name);
    if (!entry) {
        return EXT_STATE_DEAD;
    }
    return entry->state;
}

/* =========================================================================
 * Command routing
 * ========================================================================= */

int ext_host_register_command(const char *ext_name, const char *cmd_name)
{
    ext_host_entry_t *entry = find_entry(ext_name);
    if (!entry) {
        return -1;
    }

    /* Check if already registered */
    if (find_command_slot(entry, cmd_name)) {
        return 0;  /* Already exists, success */
    }

    /* Find free slot */
    ext_host_command_t *cmd = find_free_command_slot(entry);
    if (!cmd) {
        return -2;  /* No command slots available */
    }

    /* Register command */
    strncpy(cmd->name, cmd_name, sizeof(cmd->name) - 1);
    cmd->name[sizeof(cmd->name) - 1] = '\0';
    cmd->active = true;
    entry->num_commands++;

    return 0;
}

int ext_host_unregister_command(const char *ext_name, const char *cmd_name)
{
    ext_host_entry_t *entry = find_entry(ext_name);
    if (!entry) {
        return -1;
    }

    ext_host_command_t *cmd = find_command_slot(entry, cmd_name);
    if (!cmd) {
        return -2;  /* Not found */
    }

    cmd->active = false;
    memset(cmd->name, 0, sizeof(cmd->name));
    entry->num_commands--;

    return 0;
}

bool ext_host_has_command(const char *cmd_name)
{
    return ext_host_find_command_owner(cmd_name) != NULL;
}

int ext_host_invoke_command(const char *cmd_name, int f, int n)
{
    /* Find extension that owns this command */
    const char *owner = ext_host_find_command_owner(cmd_name);
    if (!owner) {
        return -1;  /* Command not found */
    }

    ext_host_entry_t *entry = find_entry(owner);
    if (!entry || !entry->ipc || entry->state != EXT_STATE_READY) {
        return -2;  /* Extension not ready */
    }

    /* Build request payload */
    ext_ipc_cmd_invoke_t req = {
        .f = f,
        .n = n,
    };
    strncpy(req.name, cmd_name, sizeof(req.name) - 1);

    /* Mark extension busy */
    entry->state = EXT_STATE_BUSY;

    /* Send invoke request */
    int result;
    uint32_t resp_len = sizeof(result);
    int ipc_result = ext_ipc_call(entry->ipc, EXT_MSG_INVOKE_CMD,
                                  &req, sizeof(req),
                                  &result, &resp_len);

    /* Mark extension ready again */
    entry->state = EXT_STATE_READY;

    if (ipc_result < 0) {
        return ipc_result;  /* IPC error */
    }

    return result;
}

const char *ext_host_find_command_owner(const char *cmd_name)
{
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
 * Event routing
 * ========================================================================= */

int ext_host_subscribe_event(const char *ext_name, const char *event_name, int priority)
{
    /* TODO: Implement event subscription registry */
    (void)ext_name;
    (void)event_name;
    (void)priority;
    return 0;
}

int ext_host_unsubscribe_event(const char *ext_name, const char *event_name)
{
    /* TODO: Implement event unsubscription */
    (void)ext_name;
    (void)event_name;
    return 0;
}

int ext_host_emit_event(const char *event_name, const void *data, size_t data_len)
{
    if (!event_name) return 0;

    int notified = 0;

    /* Broadcast event to all ready extensions */
    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        if (!g_extensions[i].active || g_extensions[i].state != EXT_STATE_READY) {
            continue;
        }

        /* TODO: Check if extension subscribed to this event */

        /* Send event notification */
        int result = ext_ipc_notify(g_extensions[i].ipc, EXT_MSG_EVENT_EMIT,
                                    data, data_len);
        if (result == EXT_IPC_OK) {
            notified++;
        }
    }

    return notified;
}

/* =========================================================================
 * Message handling
 * ========================================================================= */

int ext_host_handle_message(const char *ext_name)
{
    ext_host_entry_t *entry = find_entry(ext_name);
    if (!entry || !entry->ipc) {
        return -1;
    }

    ext_ipc_msg_t *msg;
    /* Use ext_ipc_recv_from_ext - extensions send via eventfd_response */
    int result = ext_ipc_recv_from_ext(entry->ipc, &msg, 0);  /* Non-blocking */
    if (result != EXT_IPC_OK) {
        return result;
    }

    LOG_DEBUGF("ext_host: Received message type=0x%x from '%s'", msg->msg_type, ext_name);

    /* Handle message based on type */
    switch (msg->msg_type) {
    case EXT_MSG_REGISTER_CMD: {
        ext_ipc_cmd_register_t *req = (ext_ipc_cmd_register_t *)msg->payload;
        LOG_DEBUGF("ext_host: Registering command '%s' from '%s'", req->name, ext_name);
        int reg_result = ext_host_register_command(ext_name, req->name);
        /* Use ext_ipc_respond_to_ext - respond via eventfd_request */
        ext_ipc_respond_to_ext(entry->ipc, reg_result, NULL, 0);
        break;
    }

    case EXT_MSG_UNREGISTER_CMD: {
        ext_ipc_cmd_register_t *req = (ext_ipc_cmd_register_t *)msg->payload;
        int unreg_result = ext_host_unregister_command(ext_name, req->name);
        ext_ipc_respond_to_ext(entry->ipc, unreg_result, NULL, 0);
        break;
    }

    case EXT_MSG_EVENT_SUBSCRIBE: {
        ext_ipc_event_sub_t *req = (ext_ipc_event_sub_t *)msg->payload;
        int sub_result = ext_host_subscribe_event(ext_name, req->event_name, req->priority);
        ext_ipc_respond_to_ext(entry->ipc, sub_result, NULL, 0);
        break;
    }

    case EXT_MSG_EVENT_UNSUBSCRIBE: {
        ext_ipc_event_sub_t *req = (ext_ipc_event_sub_t *)msg->payload;
        int unsub_result = ext_host_unsubscribe_event(ext_name, req->event_name);
        ext_ipc_respond_to_ext(entry->ipc, unsub_result, NULL, 0);
        break;
    }

    case EXT_MSG_MESSAGE: {
        ext_ipc_message_t *req = (ext_ipc_message_t *)msg->payload;
        /* TODO: Call mlwrite or similar to display message */
        (void)req;
        ext_ipc_respond_to_ext(entry->ipc, 0, NULL, 0);
        break;
    }

    case EXT_MSG_LOG_INFO:
    case EXT_MSG_LOG_WARN:
    case EXT_MSG_LOG_ERROR:
    case EXT_MSG_LOG_DEBUG: {
        ext_ipc_log_t *req = (ext_ipc_log_t *)msg->payload;
        /* TODO: Route to logging system */
        (void)req;
        ext_ipc_respond_to_ext(entry->ipc, 0, NULL, 0);
        break;
    }

    default:
        /* Unknown message type */
        LOG_WARNF("ext_host: Unknown message type 0x%x from '%s'", msg->msg_type, ext_name);
        ext_ipc_respond_to_ext(entry->ipc, -1, NULL, 0);
        break;
    }

    return 0;
}

void ext_host_poll_all(void)
{
    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS; i++) {
        if (!g_extensions[i].active || !g_extensions[i].ipc) {
            continue;
        }

        /* Check if extension process is still alive */
        if (!ext_ipc_is_alive(g_extensions[i].ipc)) {
            g_extensions[i].state = EXT_STATE_DEAD;
            continue;
        }

        /* Handle any pending messages */
        ext_host_handle_message(g_extensions[i].name);
    }
}

int ext_host_get_poll_fds(int *fds, int max_fds)
{
    int count = 0;

    for (int i = 0; i < EXT_HOST_MAX_EXTENSIONS && count < max_fds; i++) {
        if (!g_extensions[i].active || !g_extensions[i].ipc) {
            continue;
        }

        int fd = ext_ipc_poll_fd(g_extensions[i].ipc, true);
        if (fd >= 0) {
            fds[count++] = fd;
        }
    }

    return count;
}

/* =========================================================================
 * Extension iteration
 * ========================================================================= */

int ext_host_count(void)
{
    return g_num_extensions;
}

const ext_host_entry_t *ext_host_get_entry(int index)
{
    if (index < 0 || index >= EXT_HOST_MAX_EXTENSIONS) {
        return NULL;
    }

    if (!g_extensions[index].active) {
        return NULL;
    }

    return &g_extensions[index];
}

const ext_host_entry_t *ext_host_find(const char *name)
{
    return find_entry(name);
}

/* =========================================================================
 * Runner path management
 * ========================================================================= */

void ext_host_set_runner_path(const char *path)
{
    free(g_runner_path);
    g_runner_path = path ? strdup(path) : NULL;
}

const char *ext_host_get_runner_path(void)
{
    return g_runner_path ? g_runner_path : "uemacs-ext-runner";
}
