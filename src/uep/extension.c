/*
 * extension.c - μEmacs Extension Loader (Layer 3)
 *
 * High-performance C23 shared library loader using modern Linux APIs:
 * - posix_spawn() for subprocess creation (no fork+shell overhead)
 * - statx() with AT_STATX_DONT_SYNC for fast metadata
 * - io_uring for batch syscalls (optional, detected at runtime)
 * - inotify for lazy rebuild detection
 * - Thread pool for parallel dlopen() calls
 *
 * C23 compliant, Linux 2026 standards
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/inotify.h>
#include <errno.h>
#include <signal.h>
#include <linux/limits.h>

/* io_uring support (optional - detected at runtime via weak symbol) */
#ifdef HAVE_LIBURING
#include <liburing.h>
#endif

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "uep/extension.h"
#include "uep/extension_api.h"
#include "uep/ext_host.h"
#include "internal/memory.h"
#include "util/logger.h"
#include <time.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * TIMING INSTRUMENTATION
 * ═══════════════════════════════════════════════════════════════════════════ */

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Thread pool configuration for parallel loading */
constexpr int LOADER_THREAD_COUNT = 4;  /* Parallel dlopen is safe - init is serialized */
constexpr int MAX_BUILD_BATCH = 32;
constexpr int MAX_LOAD_BATCH = 64;

/* inotify buffer size */
constexpr int INOTIFY_BUF_SIZE = 4096;

/* io_uring configuration */
#ifdef HAVE_LIBURING
constexpr int URING_QUEUE_DEPTH = 64;
constexpr int URING_BATCH_SIZE = 32;
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * DATA STRUCTURES
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Loaded extension tracking */
typedef struct {
    char *path;                         /* Full path to .so */
    void *handle;                       /* dlopen handle */
    struct uemacs_extension *ext;       /* Extension descriptor */
    bool active;                        /* Is this slot in use? */
} loaded_extension_t;

/* Thread pool task for parallel dlopen (Phase 1 only) */
typedef struct {
    const char *path;
    void *handle;               /* Result of dlopen */
    extension_entry_fn entry;   /* Result of dlsym */
    ext_runtime_t runtime;      /* Detected runtime type */
    bool needs_isolation;       /* Out-of-process extension? */
    int dlopen_result;          /* 0 = success, -1 = error */
    atomic_bool done;
} dlopen_task_t;

/* Build task for batch building */
typedef struct {
    char *path;
    bool needs_build;
} build_task_t;

/* ═══════════════════════════════════════════════════════════════════════════
 * GLOBAL STATE
 * ═══════════════════════════════════════════════════════════════════════════ */

static loaded_extension_t extensions[UEMACS_MAX_EXTENSIONS];
static int extension_count_internal = 0;
static bool extension_initialized = false;

/* Mutex for thread-safe slot allocation during parallel loading */
static pthread_mutex_t extension_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Auto-load directory from settings */
static char autoload_dir[1024] = "";

/* Auto-build setting (default: disabled) */
static bool extension_auto_build = false;

/* Cached path to build script */
static char cached_build_script[1024] = "";

/* inotify state for lazy rebuild detection */
static int inotify_fd = -1;
static int inotify_wd = -1;

/* Cached API header mtime (avoid repeated statx calls) */
static time_t cached_api_mtime = 0;
static bool api_mtime_cached = false;

/* ═══════════════════════════════════════════════════════════════════════════
 * ASYNC LOADING - Pending init queue
 *
 * Background threads do dlopen() + dlsym(), then add to this queue.
 * Main loop drains queue and calls init() (single-threaded, no races).
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char *path;
    void *handle;
    extension_entry_fn entry;
    ext_runtime_t runtime;
    bool needs_isolation;
} pending_ext_t;

#define MAX_PENDING_EXTENSIONS 64
static pending_ext_t pending_queue[MAX_PENDING_EXTENSIONS];
static int pending_count = 0;
static pthread_mutex_t pending_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool autoload_in_progress = false;

/* Add extension to pending queue (called from background thread) */
static void pending_queue_add(const char *path, void *handle,
                              extension_entry_fn entry, ext_runtime_t runtime,
                              bool needs_isolation) {
    pthread_mutex_lock(&pending_mutex);
    if (pending_count < MAX_PENDING_EXTENSIONS) {
        pending_ext_t *p = &pending_queue[pending_count++];
        p->path = strdup(path);
        p->handle = handle;
        p->entry = entry;
        p->runtime = runtime;
        p->needs_isolation = needs_isolation;
    }
    pthread_mutex_unlock(&pending_mutex);
}

/* Forward declarations */
extern struct uemacs_api *uemacs_get_api(void);
static bool needs_rebuild(const char *ext_path);
static char *get_extension_dir(const char *so_path);
static char *get_extension_name(const char *path);
static loaded_extension_t *find_extension(const char *name);
static loaded_extension_t *find_extension_by_path(const char *path);
static void extension_load_dir_async(const char *dir);

/* ═══════════════════════════════════════════════════════════════════════════
 * MODERN LINUX SYSCALLS
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Get file mtime using statx() with AT_STATX_DONT_SYNC.
 * This avoids unnecessary disk sync for cached metadata - significantly
 * faster than stat() for repeated calls on the same filesystem.
 *
 * Falls back to stat() if statx() fails (shouldn't happen on Linux 4.11+).
 */
[[nodiscard]]
static time_t mtime_fast(const char *path) {
    struct statx stx;

    if (statx(AT_FDCWD, path, AT_STATX_DONT_SYNC | AT_SYMLINK_NOFOLLOW,
              STATX_MTIME, &stx) == 0) {
        return stx.stx_mtime.tv_sec;
    }

    /* Fallback to legacy stat() */
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_mtime;
    }

    return 0;
}

/*
 * Check if path is a directory using statx().
 */
[[nodiscard]]
static bool is_directory_fast(const char *path) {
    struct statx stx;

    /* Follow symlinks so symlinked extension directories work */
    if (statx(AT_FDCWD, path, AT_STATX_DONT_SYNC,
              STATX_TYPE, &stx) == 0) {
        return S_ISDIR(stx.stx_mode);
    }

    return false;
}

/*
 * Check if path is a regular file using statx().
 */
[[nodiscard]]
static bool is_file_fast(const char *path) {
    struct statx stx;

    /* Follow symlinks so symlinked extension files work */
    if (statx(AT_FDCWD, path, AT_STATX_DONT_SYNC,
              STATX_TYPE, &stx) == 0) {
        return S_ISREG(stx.stx_mode);
    }

    return false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * INOTIFY - LAZY REBUILD DETECTION
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Initialize inotify watching on the extensions directory.
 * This allows us to skip the full directory scan on startup if nothing changed.
 */
static void inotify_watch_init(const char *dir) {
    if (inotify_fd >= 0) return;  /* Already initialized */

    inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0) {
        LOG_DEBUGF("Extension: inotify_init1 failed: %s", strerror(errno));
        return;
    }

    /* Watch for file modifications, creates, deletes, moves */
    inotify_wd = inotify_add_watch(inotify_fd, dir,
        IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_TO | IN_MOVED_FROM);

    if (inotify_wd < 0) {
        LOG_DEBUGF("Extension: inotify_add_watch failed: %s", strerror(errno));
        close(inotify_fd);
        inotify_fd = -1;
        return;
    }

    LOG_DEBUGF("Extension: inotify watching %s", dir);
}

/*
 * Check if any inotify events are pending (files changed since last check).
 * Non-blocking - returns immediately.
 */
[[nodiscard]]
static bool inotify_has_changes(void) {
    if (inotify_fd < 0) return true;  /* No watch = assume changes */

    char buf[INOTIFY_BUF_SIZE];
    ssize_t len = read(inotify_fd, buf, sizeof(buf));

    if (len > 0) {
        LOG_DEBUG("Extension: inotify detected changes");
        return true;
    }

    if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_DEBUGF("Extension: inotify read error: %s", strerror(errno));
        return true;  /* Error = assume changes */
    }

    return false;  /* No events = no changes */
}

/*
 * Cleanup inotify resources.
 */
static void inotify_watch_cleanup(void) {
    if (inotify_wd >= 0 && inotify_fd >= 0) {
        inotify_rm_watch(inotify_fd, inotify_wd);
        inotify_wd = -1;
    }
    if (inotify_fd >= 0) {
        close(inotify_fd);
        inotify_fd = -1;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IO_URING - BATCH SYSCALLS (P3 OPTIMIZATION)
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifdef HAVE_LIBURING

/*
 * Result structure for batch mtime queries.
 */
typedef struct {
    time_t mtime;
    bool is_dir;
    bool valid;
} uring_stat_result_t;

/*
 * Batch query mtimes for multiple paths using io_uring.
 *
 * This is significantly faster than sequential statx() calls because:
 * 1. Single syscall submits all operations to kernel
 * 2. Kernel executes them in parallel
 * 3. Single syscall reaps all completions
 *
 * paths:   Array of paths to stat
 * count:   Number of paths
 * results: Output array (caller allocates, same size as paths)
 *
 * Returns: Number of successful stats
 */
[[nodiscard]]
static int io_uring_batch_stat(const char **paths, int count,
                               uring_stat_result_t *results) {
    if (count <= 0 || !paths || !results) return 0;

    struct io_uring ring;
    if (io_uring_queue_init(URING_QUEUE_DEPTH, &ring, 0) < 0) {
        LOG_WARN("Extension: io_uring_queue_init failed, falling back to sync");
        return -1;
    }

    /* Allocate statx buffers */
    struct statx *stx_bufs = SAFE_ARRAY(struct statx, count, "uring statx buffers");
    if (!stx_bufs) {
        io_uring_queue_exit(&ring);
        return -1;
    }

    /* Initialize results */
    for (int i = 0; i < count; i++) {
        results[i].mtime = 0;
        results[i].is_dir = false;
        results[i].valid = false;
    }

    int submitted = 0;
    int completed = 0;

    /* Submit in batches to avoid overflowing the queue */
    for (int batch_start = 0; batch_start < count; batch_start += URING_BATCH_SIZE) {
        int batch_end = batch_start + URING_BATCH_SIZE;
        if (batch_end > count) batch_end = count;
        int batch_size = batch_end - batch_start;

        /* Submit batch of statx operations */
        for (int i = batch_start; i < batch_end; i++) {
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
            if (!sqe) {
                LOG_WARN("Extension: io_uring SQ full");
                break;
            }

            io_uring_prep_statx(sqe, AT_FDCWD, paths[i],
                               AT_STATX_DONT_SYNC | AT_SYMLINK_NOFOLLOW,
                               STATX_MTIME | STATX_TYPE, &stx_bufs[i]);
            sqe->user_data = (uint64_t)i;
            submitted++;
        }

        int ret = io_uring_submit(&ring);
        if (ret < 0) {
            LOG_WARNF("Extension: io_uring_submit failed: %s", strerror(-ret));
            break;
        }

        /* Reap completions for this batch */
        for (int i = 0; i < batch_size && completed < submitted; i++) {
            struct io_uring_cqe *cqe;
            ret = io_uring_wait_cqe(&ring, &cqe);
            if (ret < 0) {
                LOG_WARNF("Extension: io_uring_wait_cqe failed: %s", strerror(-ret));
                break;
            }

            int idx = (int)cqe->user_data;
            if (cqe->res >= 0 && idx >= 0 && idx < count) {
                results[idx].mtime = stx_bufs[idx].stx_mtime.tv_sec;
                results[idx].is_dir = S_ISDIR(stx_bufs[idx].stx_mode);
                results[idx].valid = true;
            }

            io_uring_cqe_seen(&ring, cqe);
            completed++;
        }
    }

    free(stx_bufs);
    io_uring_queue_exit(&ring);

    LOG_DEBUGF("Extension: io_uring batch stat %d/%d paths", completed, count);
    return completed;
}

/*
 * Batch check which extension directories need rebuild.
 *
 * Uses io_uring to stat all source files and .so files in parallel,
 * then compares mtimes to determine which need rebuilding.
 *
 * Returns count of extensions needing rebuild, fills needs_build array.
 */
[[nodiscard]]
static int io_uring_check_rebuilds(const char **ext_paths, int count,
                                   bool *needs_build) {
    if (count <= 0) return 0;

    int rebuild_count = 0;

    /* For each extension, we need to check:
     * 1. Newest .so mtime
     * 2. Newest source file mtime
     * If source > so, needs rebuild
     *
     * This is a simplified version - for full optimization we'd
     * batch all the file stats across all extensions.
     */
    for (int i = 0; i < count; i++) {
        needs_build[i] = needs_rebuild(ext_paths[i]);
        if (needs_build[i]) rebuild_count++;
    }

    return rebuild_count;
}

#endif /* HAVE_LIBURING */

/* ═══════════════════════════════════════════════════════════════════════════
 * BUILD SCRIPT LOCATION
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Find uep_build.py script path (cached after first call).
 */
[[nodiscard]]
static const char *find_build_script(void) {
    if (cached_build_script[0]) {
        return cached_build_script;
    }

    static const char *locations[] = {
        /* Source tree (highest priority for development) */
        UEMACS_SOURCE_EDITOR_DIR "/../../scripts/uep_build.py",
        /* Installed locations */
        UEMACS_DATA_DIR "/uep_build.py",
        "/usr/local/share/muemacs/uep_build.py",
        "/usr/share/muemacs/uep_build.py",
        /* Relative fallbacks */
        "scripts/uep_build.py",
        "../scripts/uep_build.py",
        nullptr
    };

    for (int i = 0; locations[i]; i++) {
        if (access(locations[i], R_OK) == 0) {
            strncpy(cached_build_script, locations[i], sizeof(cached_build_script) - 1);
            cached_build_script[sizeof(cached_build_script) - 1] = '\0';
            LOG_DEBUGF("Extension: Found build script at %s", cached_build_script);
            return cached_build_script;
        }
    }

    LOG_WARN("Extension: Cannot find uep_build.py");
    return nullptr;
}

/*
 * Get cached API header mtime (statx only once per session).
 */
[[nodiscard]]
static time_t get_api_header_mtime(void) {
    if (api_mtime_cached) {
        return cached_api_mtime;
    }

    static const char *api_paths[] = {
        "/usr/local/include/uep/extension_api.h",
        "/usr/include/uep/extension_api.h",
        "include/uep/extension_api.h",
        "../include/uep/extension_api.h",
        /* Source tree path for development builds */
        UEMACS_SOURCE_EDITOR_DIR "/../../include/uep/extension_api.h",
        nullptr
    };

    for (int i = 0; api_paths[i]; i++) {
        time_t mtime = mtime_fast(api_paths[i]);
        if (mtime > 0) {
            cached_api_mtime = mtime;
            api_mtime_cached = true;
            return cached_api_mtime;
        }
    }

    api_mtime_cached = true;
    cached_api_mtime = 0;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * POSIX_SPAWN - FAST PROCESS CREATION
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Build single extension using posix_spawn() instead of popen().
 *
 * posix_spawn() uses vfork() internally on Linux, which is significantly
 * faster than popen()'s fork() + exec("/bin/sh") + exec(python).
 * We eliminate the shell entirely.
 *
 * Returns 0 on success, build exit code on failure.
 */
[[nodiscard]]
static int build_extension_posix_spawn(const char *ext_path) {
    const char *script = find_build_script();
    if (!script) return 2;

    LOG_INFOF("Extension: Building %s (posix_spawn)", ext_path);

    pid_t pid;
    char *argv[] = {
        "python3",
        (char *)script,
        (char *)ext_path,
        nullptr
    };

    /* Redirect stdout/stderr to /dev/null for quiet builds
     * (we check exit code, not output) */
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    int status = posix_spawn(&pid, "/usr/bin/python3", &actions, nullptr, argv, environ);

    posix_spawn_file_actions_destroy(&actions);

    if (status != 0) {
        LOG_ERRORF("Extension: posix_spawn failed: %s", strerror(status));
        return 1;
    }

    /* Wait for build to complete */
    int wstatus;
    if (waitpid(pid, &wstatus, 0) < 0) {
        LOG_ERRORF("Extension: waitpid failed: %s", strerror(errno));
        return 1;
    }

    if (WIFEXITED(wstatus)) {
        int exit_code = WEXITSTATUS(wstatus);
        if (exit_code == 0) {
            LOG_INFO("Extension: Build successful");
        } else if (exit_code == 2) {
            LOG_WARNF("Extension: No build method for %s", ext_path);
        } else {
            LOG_ERRORF("Extension: Build failed with code %d", exit_code);
        }
        return exit_code;
    }

    return 1;
}

/*
 * Batch build multiple extensions with a single Python invocation.
 *
 * Much faster than N separate calls because:
 * 1. Single Python interpreter startup
 * 2. Single posix_spawn() instead of N
 * 3. Python can parallelize internally
 */
static void batch_build_extensions(char **paths, int count) {
    if (count == 0) return;

    const char *script = find_build_script();
    if (!script) return;

    LOG_INFOF("Extension: Batch building %d extension(s)", count);

    /* Build argv: python3 uep_build.py path1 path2 path3 ... */
    char **argv = SAFE_ARRAY(char *, count + 3, "build argv");
    if (!argv) return;

    argv[0] = "python3";
    argv[1] = (char *)script;
    for (int i = 0; i < count; i++) {
        argv[i + 2] = paths[i];
    }
    argv[count + 2] = nullptr;

    /* Redirect stdout/stderr to /dev/null */
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid;
    int status = posix_spawn(&pid, "/usr/bin/python3", &actions, nullptr, argv, environ);

    posix_spawn_file_actions_destroy(&actions);
    SAFE_FREE(argv);

    if (status != 0) {
        LOG_ERRORF("Extension: Batch posix_spawn failed: %s", strerror(status));
        return;
    }

    int wstatus;
    waitpid(pid, &wstatus, 0);

    if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) {
        LOG_INFO("Extension: Batch build successful");
    } else {
        LOG_WARNF("Extension: Batch build had failures (code %d)",
                  WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TWO-PHASE PARALLEL LOADING
 *
 * Phase 1: Parallel dlopen (I/O bound, no shared state modification)
 * Phase 2: Serial init (shared state modification, must be sequential)
 *
 * This architecture eliminates race conditions without requiring mutexes
 * in event_bus, config storage, hooks, or other shared subsystems.
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Phase 1 worker: dlopen + dlsym only.
 * Safe to run in parallel - only writes to its own task struct.
 */
static void *dlopen_thread_fn(void *arg) {
    dlopen_task_t *task = arg;

    /* Detect runtime type */
    char *ext_dir = get_extension_dir(task->path);
    task->runtime = ext_host_detect_runtime(ext_dir);
    task->needs_isolation = ext_host_needs_isolation(task->runtime);
    free(ext_dir);

    /* Out-of-process extensions are handled in Phase 2 */
    if (task->needs_isolation) {
        task->dlopen_result = 0;  /* Mark as success for Phase 2 processing */
        atomic_store(&task->done, true);
        return nullptr;
    }

    /* In-process: do dlopen */
    task->handle = dlopen(task->path, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!task->handle) {
        LOG_ERRORF("Extension: dlopen failed: %s", dlerror());
        task->dlopen_result = -1;
        atomic_store(&task->done, true);
        return nullptr;
    }

    /* Get entry point */
    dlerror();  /* Clear error */
    union {
        void *ptr;
        extension_entry_fn fn;
    } entry_u;
    entry_u.ptr = dlsym(task->handle, "uemacs_extension_entry");
    task->entry = entry_u.fn;

    const char *dl_error = dlerror();
    if (dl_error || !task->entry) {
        LOG_ERRORF("Extension: dlsym failed: %s", dl_error ? dl_error : "NULL entry");
        dlclose(task->handle);
        task->handle = nullptr;
        task->dlopen_result = -1;
        atomic_store(&task->done, true);
        return nullptr;
    }

    task->dlopen_result = 0;
    atomic_store(&task->done, true);
    return nullptr;
}

/*
 * Async dlopen worker - fire and forget.
 * Does dlopen/dlsym, then adds to pending queue for main thread init.
 */
typedef struct {
    char *path;
} async_dlopen_arg_t;

static void *async_dlopen_worker(void *arg) {
    async_dlopen_arg_t *a = arg;
    char *path = a->path;
    free(a);

    /* Detect runtime type */
    char *ext_dir = get_extension_dir(path);
    ext_runtime_t runtime = ext_host_detect_runtime(ext_dir);
    bool needs_isolation = ext_host_needs_isolation(runtime);
    free(ext_dir);

    if (needs_isolation) {
        /* Out-of-process extension - add to queue for spawning */
        pending_queue_add(path, NULL, NULL, runtime, true);
        free(path);
        return NULL;
    }

    /* In-process: do dlopen */
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!handle) {
        LOG_ERRORF("Extension: dlopen failed: %s", dlerror());
        free(path);
        return NULL;
    }

    /* Get entry point */
    dlerror();
    union {
        void *ptr;
        extension_entry_fn fn;
    } entry_u;
    entry_u.ptr = dlsym(handle, "uemacs_extension_entry");
    extension_entry_fn entry = entry_u.fn;

    const char *dl_error = dlerror();
    if (dl_error || !entry) {
        LOG_ERRORF("Extension: dlsym failed: %s", dl_error ? dl_error : "NULL");
        dlclose(handle);
        free(path);
        return NULL;
    }

    /* Add to pending queue - main thread will call init */
    pending_queue_add(path, handle, entry, runtime, false);
    free(path);
    return NULL;
}

/* Fire-and-forget async extension load */
static void async_load_extension(const char *path) {
    async_dlopen_arg_t *arg = malloc(sizeof(*arg));
    if (!arg) return;
    arg->path = strdup(path);
    if (!arg->path) { free(arg); return; }

    pthread_t t;
    pthread_create(&t, NULL, async_dlopen_worker, arg);
    pthread_detach(t);
}

/*
 * Phase 2: Init all extensions.
 * Runs on main thread - safe to modify shared state.
 *
 * Uses parallel spawning for out-of-process extensions:
 * 1. Fork all out-of-process extensions asynchronously
 * 2. Wait for all READY messages in parallel using epoll
 * 3. Init all in-process extensions serially
 */
static int serial_init_extensions(dlopen_task_t *tasks, int count) {
    int loaded = 0;
    int async_spawned = 0;

    LOG_DEBUGF("serial_init_extensions: ENTRY count=%d", count);

    /* ─── Pass 1: Fork ALL out-of-process extensions in parallel ─── */
    for (int i = 0; i < count; i++) {
        LOG_DEBUGF("serial_init_extensions: task[%d] path=%s needs_iso=%d dlopen_result=%d",
                   i, tasks[i].path, tasks[i].needs_isolation, tasks[i].dlopen_result);
        dlopen_task_t *task = &tasks[i];

        if (task->dlopen_result != 0) continue;
        if (!task->needs_isolation) continue;

        char *ext_name = get_extension_name(task->path);
        if (!ext_name) {
            LOG_ERROR("Extension: Failed to extract name from path");
            continue;
        }

        LOG_INFOF("Extension: Spawning %s async (%s runtime)",
                  ext_name, ext_host_runtime_name(task->runtime));

        /* Fork without waiting - extensions init in parallel */
        int result = ext_host_spawn_async(ext_name, task->path, task->runtime);
        free(ext_name);

        if (result >= 0) {
            async_spawned++;
        } else {
            LOG_ERRORF("Extension: Failed to spawn: %s (error=%d)", task->path, result);
        }
    }

    /* ─── Pass 1b: Wait for ALL out-of-process extensions in parallel ─── */
    if (async_spawned > 0) {
        LOG_INFOF("Extension: Waiting for %d out-of-process extension(s) in parallel", async_spawned);
        loaded = ext_host_await_pending(ext_host_get_init_timeout());
    }

    /* ─── Pass 2: Init all in-process extensions ─── */
    for (int i = 0; i < count; i++) {
        dlopen_task_t *task = &tasks[i];

        /* Skip failed dlopen or out-of-process (already handled) */
        if (task->dlopen_result != 0) continue;
        if (task->needs_isolation) continue;

        /* In-process extension: run entry() and init() */

        /* Validate entry function pointer */
        if (!task->entry) {
            LOG_ERRORF("Extension: No entry function for %s (dlopen may have failed)", task->path);
            if (task->handle) dlclose(task->handle);
            continue;
        }

        /* Get extension descriptor */
        struct uemacs_extension *ext = task->entry();
        if (!ext) {
            LOG_ERROR("Extension: Entry function returned NULL");
            dlclose(task->handle);
            continue;
        }

        /* Validate name */
        if (!ext->name || !*ext->name) {
            LOG_ERROR("Extension: No name provided");
            dlclose(task->handle);
            continue;
        }

        /* Check API version */
        if (ext->api_version != UEMACS_API_VERSION) {
            LOG_ERRORF("Extension: API mismatch for '%s' (HAVE: v%d; NEED: v%d)",
                       ext->name, ext->api_version, UEMACS_API_VERSION);
            dlclose(task->handle);
            continue;
        }

        /* Check for duplicate name (safe - single thread) */
        if (find_extension(ext->name)) {
            LOG_ERRORF("Extension: Name already registered: %s", ext->name);
            dlclose(task->handle);
            continue;
        }

        /* Check if already loaded by path (safe - single thread) */
        if (find_extension_by_path(task->path)) {
            LOG_DEBUGF("Extension: Already loaded: %s", task->path);
            dlclose(task->handle);
            continue;
        }

        /* Find free slot (safe - single thread, no mutex needed) */
        loaded_extension_t *slot = nullptr;
        for (int j = 0; j < UEMACS_MAX_EXTENSIONS; j++) {
            if (!extensions[j].active) {
                extensions[j].active = true;
                slot = &extensions[j];
                break;
            }
        }
        if (!slot) {
            LOG_ERROR("Extension: Maximum extensions reached");
            dlclose(task->handle);
            continue;
        }

        /* Initialize extension (safe - single thread, can modify shared state) */
        if (ext->init) {
            struct uemacs_api *api = uemacs_get_api();
            LOG_DEBUGF("Extension: Initializing %s with API (get_function=%s, struct_size=%zu)",
                       ext->name, api->get_function ? "SET" : "NULL", api->struct_size);
            double init_start = get_time_ms();
            int init_result = ext->init(api);
            double init_time = get_time_ms() - init_start;

            if (init_result != 0) {
                LOG_ERRORF("Extension: init() failed with code %d: %s",
                           init_result, ext->name);
                slot->active = false;
                dlclose(task->handle);
                continue;
            }

            LOG_DEBUGF("Extension: init() took %.1fms for %s", init_time, ext->name);
        }

        /* Store in slot */
        slot->path = SAFE_STRDUP(task->path, "extension slot path");
        slot->handle = task->handle;
        slot->ext = ext;
        extension_count_internal++;

        LOG_INFOF("Extension: Loaded '%s' v%s",
                  ext->name,
                  ext->version ? ext->version : "?.?.?");

        if (ext->description) {
            LOG_INFOF("Extension: %s", ext->description);
        }

        loaded++;
    }

    return loaded;
}

/*
 * Load multiple extensions using two-phase parallel loading.
 *
 * Phase 1: Parallel dlopen (I/O bound)
 * Phase 2: Serial init (shared state)
 *
 * Returns count of successfully loaded extensions.
 */
[[nodiscard]]
static int parallel_load_extensions(const char **paths, int count) {
    if (count == 0) return 0;
    if (count == 1) {
        /* Single extension - use simple path */
        return (extension_load(paths[0]) == 0) ? 1 : 0;
    }

    double phase1_start = get_time_ms();

    /* Allocate tasks */
    dlopen_task_t *tasks = SAFE_ARRAY(dlopen_task_t, count, "extension dlopen tasks");
    if (!tasks) {
        return 0;
    }

    /* Initialize tasks */
    for (int i = 0; i < count; i++) {
        tasks[i].path = paths[i];
        tasks[i].handle = nullptr;
        tasks[i].entry = nullptr;
        tasks[i].dlopen_result = -1;
        atomic_store(&tasks[i].done, false);
    }

    /* PHASE 1: Parallel dlopen */
    pthread_t threads[LOADER_THREAD_COUNT];

    for (int batch_start = 0; batch_start < count; batch_start += LOADER_THREAD_COUNT) {
        int batch_size = count - batch_start;
        if (batch_size > LOADER_THREAD_COUNT) {
            batch_size = LOADER_THREAD_COUNT;
        }

        /* Launch threads */
        for (int i = 0; i < batch_size; i++) {
            int idx = batch_start + i;
            pthread_create(&threads[i], nullptr, dlopen_thread_fn, &tasks[idx]);
        }

        /* Wait for batch completion */
        for (int i = 0; i < batch_size; i++) {
            pthread_join(threads[i], nullptr);
        }
    }

    double phase1_time = get_time_ms() - phase1_start;
    LOG_DEBUGF("Extension: Phase 1 (parallel dlopen) completed in %.1fms", phase1_time);

    /* PHASE 2: Serial init */
    double phase2_start = get_time_ms();
    int loaded = serial_init_extensions(tasks, count);
    double phase2_time = get_time_ms() - phase2_start;

    LOG_DEBUGF("Extension: Phase 2 (serial init) completed in %.1fms", phase2_time);
    LOG_INFOF("Extension: Loaded %d/%d extensions (Phase1: %.1fms, Phase2: %.1fms)",
              loaded, count, phase1_time, phase2_time);

    SAFE_FREE(tasks);
    return loaded;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SOURCE FILE SCANNING
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Source file extensions to check */
static const char *source_exts[] = {
    ".c", ".h", ".go", ".rs", ".zig", ".nim", ".d", ".cr",
    ".f90", ".f95", ".pas", ".adb", ".ads", ".v", ".odin",
    ".toml", ".mod",
    nullptr
};

/* Build manifest files (presence indicates project) */
static const char *build_manifests[] = {
    "Cargo.toml", "go.mod", "Makefile", "build.py",
    "dub.json", "build.zig", "Package.swift", "shard.yml",
    nullptr
};

/*
 * Find the newest source file mtime in a directory (recursive into src/).
 * Uses statx() for speed.
 */
[[nodiscard]]
static time_t find_newest_source_mtime(const char *dir_path) {
    DIR *dp = opendir(dir_path);
    if (!dp) return 0;

    time_t newest = 0;
    struct dirent *entry;
    char path[PATH_MAX];

    while ((entry = readdir(dp)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        int len = snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        if (len < 0 || (size_t)len >= sizeof(path)) continue;

        time_t mtime = mtime_fast(path);
        if (mtime == 0) continue;

        /* Check if it's a source file */
        size_t namelen = strlen(entry->d_name);
        bool is_source = false;

        for (const char **ext = source_exts; *ext; ext++) {
            size_t extlen = strlen(*ext);
            if (namelen >= extlen &&
                strcmp(entry->d_name + namelen - extlen, *ext) == 0) {
                is_source = true;
                break;
            }
        }

        /* Also check exact matches (e.g., "Makefile") */
        if (!is_source) {
            for (const char **manifest = build_manifests; *manifest; manifest++) {
                if (strcmp(entry->d_name, *manifest) == 0) {
                    is_source = true;
                    break;
                }
            }
        }

        if (is_source && mtime > newest) {
            newest = mtime;
        }

        /* Recurse into src/ subdirectory */
        if (is_directory_fast(path) && strcmp(entry->d_name, "src") == 0) {
            time_t sub = find_newest_source_mtime(path);
            if (sub > newest) newest = sub;
        }
    }

    closedir(dp);
    return newest;
}

/*
 * Find the newest .so file mtime in a directory.
 */
[[nodiscard]]
static time_t find_newest_so_mtime(const char *dir_path) {
    DIR *dp = opendir(dir_path);
    if (!dp) return 0;

    time_t newest = 0;
    struct dirent *entry;
    char path[PATH_MAX];

    while ((entry = readdir(dp)) != nullptr) {
        size_t len = strlen(entry->d_name);
        if (len > 3 && strcmp(entry->d_name + len - 3, ".so") == 0) {
            snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
            time_t mtime = mtime_fast(path);
            if (mtime > newest) {
                newest = mtime;
            }
        }
    }

    closedir(dp);
    return newest;
}

/*
 * Find the .so file path in a directory.
 * Returns allocated string (caller must free) or nullptr.
 *
 * With standardized naming (language_tool.so matches language_tool/),
 * we prefer exact directory name match, then any .so as fallback.
 */
[[nodiscard]]
static char *find_first_so(const char *dir_path) {
    DIR *dp = opendir(dir_path);
    if (!dp) return nullptr;

    char *best = nullptr;       /* Exact match (dir_name.so) */
    char *fallback = nullptr;   /* Any .so file */
    struct dirent *entry;

    /* Extract directory basename for matching */
    const char *basename = strrchr(dir_path, '/');
    basename = basename ? basename + 1 : dir_path;
    size_t basename_len = strlen(basename);

    while ((entry = readdir(dp)) != nullptr) {
        size_t len = strlen(entry->d_name);
        if (len > 3 && strcmp(entry->d_name + len - 3, ".so") == 0) {
            /* Skip lib*.so files - these are typically runtime deps */
            if (strncmp(entry->d_name, "lib", 3) == 0) {
                continue;
            }

            size_t path_len = strlen(dir_path) + 1 + len + 1;
            char *so_path = SAFE_ALLOC_SIZED(char, path_len, "ext so path");
            if (!so_path) continue;
            snprintf(so_path, path_len, "%s/%s", dir_path, entry->d_name);

            /* Check for exact match: dirname.so */
            bool exact_match = strncmp(entry->d_name, basename, basename_len) == 0 &&
                               strcmp(entry->d_name + basename_len, ".so") == 0;

            if (exact_match) {
                /* Best: exact match (e.g., ada_fuzzy/ada_fuzzy.so) */
                SAFE_FREE(best);
                best = so_path;
            } else if (!fallback) {
                /* Fallback: any .so */
                fallback = so_path;
            } else {
                SAFE_FREE(so_path);
            }
        }
    }

    closedir(dp);

    /* Return best available */
    if (best) {
        SAFE_FREE(fallback);
        return best;
    }
    return fallback;
}

/*
 * Check if extension directory needs rebuild.
 * Compares newest SOURCE file mtime AND API header mtime against .so mtime.
 * If either source or API header is newer, extension needs rebuild.
 */
[[nodiscard]]
static bool needs_rebuild(const char *ext_path) {
    /* Get API header mtime - if header changed, ALL extensions need rebuild */
    time_t api_mtime = get_api_header_mtime();

    if (!is_directory_fast(ext_path)) {
        /* Single source file case */
        size_t len = strlen(ext_path);
        if (len <= 2 || strcmp(ext_path + len - 2, ".c") != 0) {
            return true;  /* Unknown format */
        }

        char so_path[PATH_MAX];
        snprintf(so_path, sizeof(so_path), "%.*s.so", (int)(len - 2), ext_path);

        time_t so_mtime = mtime_fast(so_path);
        if (so_mtime == 0) return true;  /* No .so exists */

        /* Check API header first */
        if (api_mtime > 0 && api_mtime > so_mtime) {
            LOG_DEBUGF("Extension: %s needs rebuild (API header newer)", ext_path);
            return true;
        }

        time_t src_mtime = mtime_fast(ext_path);
        return src_mtime > so_mtime;
    }

    /* Directory case - find newest .so */
    time_t so_mtime = find_newest_so_mtime(ext_path);
    if (so_mtime == 0) {
        LOG_DEBUGF("Extension: %s needs build (no .so found)", ext_path);
        return true;
    }

    /* Check API header first */
    if (api_mtime > 0 && api_mtime > so_mtime) {
        LOG_DEBUGF("Extension: %s needs rebuild (API header newer)", ext_path);
        return true;
    }

    /* Compare against newest source file (NOT directory mtime!) */
    time_t src_mtime = find_newest_source_mtime(ext_path);
    if (src_mtime > so_mtime) {
        LOG_DEBUGF("Extension: %s needs rebuild (source newer)", ext_path);
        return true;
    }

    LOG_DEBUGF("Extension: %s up-to-date", ext_path);
    return false;
}

/*
 * Check if directory is an extension project (has source or build manifest).
 */
[[nodiscard]]
static bool is_extension_dir(const char *path) {
    if (!is_directory_fast(path)) return false;

    char check[PATH_MAX];

    /* Check for build manifests */
    for (const char **manifest = build_manifests; *manifest; manifest++) {
        snprintf(check, sizeof(check), "%s/%s", path, *manifest);
        if (access(check, F_OK) == 0) {
            return true;
        }
    }

    /* Check for source files */
    DIR *dp = opendir(path);
    if (!dp) return false;

    struct dirent *entry;
    bool has_source = false;

    while ((entry = readdir(dp)) != nullptr) {
        size_t len = strlen(entry->d_name);
        for (const char **ext = source_exts; *ext; ext++) {
            size_t extlen = strlen(*ext);
            if (len >= extlen &&
                strcmp(entry->d_name + len - extlen, *ext) == 0) {
                has_source = true;
                break;
            }
        }
        if (has_source) break;
    }

    closedir(dp);
    return has_source;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * EXTENSION SLOT MANAGEMENT
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Find extension by name */
[[nodiscard]]
static loaded_extension_t *find_extension(const char *name) {
    if (!name) return nullptr;
    for (int i = 0; i < UEMACS_MAX_EXTENSIONS; i++) {
        if (extensions[i].active && extensions[i].ext &&
            extensions[i].ext->name &&
            strcmp(extensions[i].ext->name, name) == 0) {
            return &extensions[i];
        }
    }
    return nullptr;
}

/* Find extension by path */
[[nodiscard]]
static loaded_extension_t *find_extension_by_path(const char *path) {
    for (int i = 0; i < UEMACS_MAX_EXTENSIONS; i++) {
        if (extensions[i].active && extensions[i].path &&
            strcmp(extensions[i].path, path) == 0) {
            return &extensions[i];
        }
    }
    return nullptr;
}

/* Find free slot */
[[nodiscard]]
static loaded_extension_t *find_free_slot(void) {
    pthread_mutex_lock(&extension_mutex);
    loaded_extension_t *slot = nullptr;
    for (int i = 0; i < UEMACS_MAX_EXTENSIONS; i++) {
        if (!extensions[i].active) {
            extensions[i].active = true;  /* Atomically claim slot */
            slot = &extensions[i];
            break;
        }
    }
    pthread_mutex_unlock(&extension_mutex);
    return slot;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PUBLIC API
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Set the auto-load directory (called from settings parser) */
void extension_set_autoload_dir(const char *dir) {
    if (!dir || !*dir) return;

    /* Expand ~ if present */
    if (dir[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(autoload_dir, sizeof(autoload_dir), "%s%s", home, dir + 1);
        } else {
            strncpy(autoload_dir, dir, sizeof(autoload_dir) - 1);
            autoload_dir[sizeof(autoload_dir) - 1] = '\0';
        }
    } else {
        strncpy(autoload_dir, dir, sizeof(autoload_dir) - 1);
        autoload_dir[sizeof(autoload_dir) - 1] = '\0';
    }

    LOG_INFOF("Extension: Auto-load directory set to %s", autoload_dir);
}

/* Get the auto-load directory */
[[nodiscard]]
const char *extension_get_autoload_dir(void) {
    return autoload_dir[0] ? autoload_dir : nullptr;
}

/* Set auto-build enabled/disabled */
void extension_set_auto_build(bool enabled) {
    extension_auto_build = enabled;
    LOG_INFOF("Extension: Auto-build %s", enabled ? "enabled" : "disabled");
}

/* Get auto-build setting */
[[nodiscard]]
bool extension_get_auto_build(void) {
    return extension_auto_build;
}

/* Initialize extension subsystem */
void extension_init(void) {
    if (extension_initialized) return;

    memset(extensions, 0, sizeof(extensions));
    extension_count_internal = 0;
    extension_initialized = true;

    /* Initialize extension host for out-of-process extensions */
    ext_host_init();

    /* Pre-cache API header mtime */
    (void)get_api_header_mtime();

    /* Pre-cache build script path */
    (void)find_build_script();

    LOG_INFO("Extension: Subsystem initialized (Linux 2026 optimizations, hybrid IPC)");
}

/* Background autoload worker thread */
static void *autoload_worker(void *arg) {
    (void)arg;

    /* Block SIGHUP in this thread */
    sigset_t block_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &block_set, NULL);

    LOG_INFO("Extension: Background loading started");
    double start = get_time_ms();

    extension_load_dir_async(autoload_dir);

    double elapsed = get_time_ms() - start;
    LOG_INFOF("Extension: Background loading finished in %.1fms", elapsed);

    atomic_store(&autoload_in_progress, false);
    return NULL;
}

/* Auto-load extensions from configured directory (NON-BLOCKING) */
void extension_autoload(void) {
    if (!autoload_dir[0]) {
        LOG_DEBUG("Extension: No auto-load directory configured");
        return;
    }

    /* Initialize inotify watch */
    inotify_watch_init(autoload_dir);

    LOG_INFOF("Extension: Starting background load from %s", autoload_dir);

    /* Fire and forget - returns immediately */
    atomic_store(&autoload_in_progress, true);
    pthread_t thread;
    pthread_create(&thread, NULL, autoload_worker, NULL);
    pthread_detach(thread);
}

/*
 * Poll for pending extensions and init them (called from main loop).
 * Returns count of extensions initialized this call.
 */
int extension_poll_pending(void) {
    /* Quick check without lock */
    if (pending_count == 0) {
        return 0;
    }

    /* Drain queue under lock, then init outside lock */
    pending_ext_t local[MAX_PENDING_EXTENSIONS];
    int count = 0;

    pthread_mutex_lock(&pending_mutex);
    count = pending_count;
    if (count > 0) {
        memcpy(local, pending_queue, count * sizeof(pending_ext_t));
        pending_count = 0;
    }
    pthread_mutex_unlock(&pending_mutex);

    if (count == 0) {
        return 0;
    }

    LOG_DEBUGF("Extension: Initializing %d pending extension(s)", count);

    int initialized = 0;
    for (int i = 0; i < count; i++) {
        pending_ext_t *p = &local[i];

        if (p->needs_isolation) {
            /* Out-of-process extension */
            char *ext_name = get_extension_name(p->path);
            if (ext_name) {
                int result = ext_host_spawn_async(ext_name, p->path, p->runtime);
                if (result >= 0) {
                    initialized++;
                }
                free(ext_name);
            }
        } else if (p->handle && p->entry) {
            /* In-process extension - run init */
            struct uemacs_extension *ext = p->entry();
            if (ext && ext->name) {
                /* Check API version */
                if (ext->api_version != UEMACS_API_VERSION) {
                    LOG_ERRORF("Extension: API mismatch for '%s'", ext->name);
                    dlclose(p->handle);
                } else if (find_extension(ext->name)) {
                    LOG_DEBUGF("Extension: Already loaded: %s", ext->name);
                    dlclose(p->handle);
                } else {
                    /* Find slot and init */
                    loaded_extension_t *slot = find_free_slot();
                    if (slot && ext->init) {
                        struct uemacs_api *api = uemacs_get_api();
                        if (ext->init(api) == 0) {
                            slot->path = p->path;
                            p->path = NULL;  /* Transferred ownership */
                            slot->handle = p->handle;
                            slot->ext = ext;
                            extension_count_internal++;
                            initialized++;
                            LOG_INFOF("Extension: Loaded '%s' v%s",
                                      ext->name, ext->version ? ext->version : "?.?.?");
                        } else {
                            slot->active = false;
                            dlclose(p->handle);
                        }
                    }
                }
            }
        }

        free(p->path);
    }

    return initialized;
}

/*
 * Get extension directory from .so path.
 * Returns the parent directory of the .so file.
 */
static char *get_extension_dir(const char *so_path) {
    char *dir = SAFE_STRDUP(so_path, "extension dir");
    if (!dir) return NULL;

    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
    }
    return dir;
}

/*
 * Get extension name from path.
 * Extracts basename without .so extension.
 */
static char *get_extension_name(const char *path) {
    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;

    size_t len = strlen(basename);
    if (len > 3 && strcmp(basename + len - 3, ".so") == 0) {
        len -= 3;
    }

    char *name = SAFE_ALLOC_SIZED(char, len + 1, "extension name");
    if (name) {
        memcpy(name, basename, len);
        name[len] = '\0';
    }
    return name;
}

/* Load extension from .so file path */
int extension_load(const char *path) {
    if (!path || !*path) {
        LOG_ERROR("Extension: No path provided");
        return -1;
    }

    if (!extension_initialized) {
        extension_init();
    }

    /* Check if already loaded */
    if (find_extension_by_path(path)) {
        LOG_DEBUGF("Extension: Already loaded: %s", path);
        return 0;  /* Not an error - just skip */
    }

    /* Detect runtime type from extension directory */
    char *ext_dir = get_extension_dir(path);
    ext_runtime_t runtime = ext_host_detect_runtime(ext_dir);
    free(ext_dir);

    /* Check if extension needs process isolation (polyglot runtime) */
    if (ext_host_needs_isolation(runtime)) {
        char *ext_name = get_extension_name(path);
        if (!ext_name) {
            LOG_ERROR("Extension: Failed to extract name from path");
            return -1;
        }

        LOG_INFOF("Extension: Loading %s out-of-process (%s runtime)",
                  ext_name, ext_host_runtime_name(runtime));

        int result = ext_host_spawn(ext_name, path, runtime);
        free(ext_name);

        if (result < 0) {
            LOG_ERRORF("Extension: Failed to spawn out-of-process extension: %d", result);
            return -1;
        }

        return 0;
    }

    /* In-process loading for C/Rust/Zig extensions */

    /* Find free slot (atomically claimed) */
    loaded_extension_t *slot = find_free_slot();
    if (!slot) {
        LOG_ERROR("Extension: Maximum extensions reached");
        return -1;
    }

    /* Open shared library
     * RTLD_NODELETE: Keep library in memory even after dlclose().
     * Required for extensions with language runtimes (Crystal, Go, Haskell)
     * whose GC heaps cannot be cleanly deallocated.
     */
    LOG_INFOF("Extension: Loading %s in-process (%s runtime)",
              path, ext_host_runtime_name(runtime));

    double dlopen_start = get_time_ms();
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    double dlopen_time = get_time_ms() - dlopen_start;

    if (!handle) {
        LOG_ERRORF("Extension: dlopen failed: %s", dlerror());
        slot->active = false;  /* Release claimed slot */
        return -1;
    }

    LOG_DEBUGF("Extension: dlopen took %.1fms for %s", dlopen_time, path);

    dlerror();  /* Clear error */

    /* Find entry point using union to avoid ISO C warning */
    union {
        void *ptr;
        extension_entry_fn fn;
    } entry_u;
    entry_u.ptr = dlsym(handle, "uemacs_extension_entry");
    extension_entry_fn entry = entry_u.fn;

    const char *dl_error = dlerror();
    if (dl_error) {
        LOG_ERRORF("Extension: dlsym failed: %s", dl_error);
        dlclose(handle);
        slot->active = false;
        return -1;
    }

    if (!entry) {
        LOG_ERROR("Extension: Entry point is NULL");
        dlclose(handle);
        slot->active = false;
        return -1;
    }

    /* Get extension descriptor */
    struct uemacs_extension *ext = entry();
    if (!ext) {
        LOG_ERROR("Extension: Entry function returned NULL");
        dlclose(handle);
        slot->active = false;
        return -1;
    }

    /* Validate */
    if (!ext->name || !*ext->name) {
        LOG_ERROR("Extension: No name provided");
        dlclose(handle);
        slot->active = false;
        return -1;
    }

    /* Check API version */
    if (ext->api_version != UEMACS_API_VERSION) {
        LOG_ERRORF("Extension: API mismatch for '%s' (HAVE: v%d; NEED: v%d)",
                   ext->name, ext->api_version, UEMACS_API_VERSION);
        dlclose(handle);
        slot->active = false;
        return -2;  /* Special code for API mismatch */
    }

    /* Check for duplicate name */
    if (find_extension(ext->name)) {
        LOG_ERRORF("Extension: Name already registered: %s", ext->name);
        dlclose(handle);
        slot->active = false;
        return -1;
    }

    /* Initialize extension */
    if (ext->init) {
        struct uemacs_api *api = uemacs_get_api();
        double init_start = get_time_ms();
        int init_result = ext->init(api);
        double init_time = get_time_ms() - init_start;

        if (init_result != 0) {
            LOG_ERRORF("Extension: init() failed with code %d: %s",
                       init_result, ext->name);
            dlclose(handle);
            slot->active = false;
            return -1;
        }

        LOG_DEBUGF("Extension: init() took %.1fms for %s", init_time, ext->name);

        /* Warn about slow extensions (>100ms) */
        if (dlopen_time + init_time > 100.0) {
            LOG_WARNF("Extension: SLOW LOAD: %s took %.1fms (dlopen: %.1fms, init: %.1fms)",
                      ext->name, dlopen_time + init_time, dlopen_time, init_time);
        }
    }

    /* Store in slot (slot->active already true from find_free_slot) */
    slot->path = SAFE_STRDUP(path, "extension load path");
    slot->handle = handle;
    slot->ext = ext;
    extension_count_internal++;

    LOG_INFOF("Extension: Loaded '%s' v%s",
              ext->name,
              ext->version ? ext->version : "?.?.?");

    if (ext->description) {
        LOG_INFOF("Extension: %s", ext->description);
    }

    return 0;
}

/* Unload extension by name */
int extension_unload(const char *name) {
    if (!name || !*name) {
        LOG_ERROR("Extension: No name provided for unload");
        return -1;
    }

    loaded_extension_t *slot = find_extension(name);
    if (!slot) {
        LOG_WARNF("Extension: Not found: %s", name);
        return -1;
    }

    /* Copy name before dlclose */
    char name_copy[256];
    strncpy(name_copy, name, sizeof(name_copy) - 1);
    name_copy[sizeof(name_copy) - 1] = '\0';

    LOG_INFOF("Extension: Unloading '%s'", name_copy);

    /* Call cleanup */
    if (slot->ext && slot->ext->cleanup) {
        slot->ext->cleanup();
    }

    /* Close library */
    if (slot->handle) {
        dlclose(slot->handle);
    }

    /* Free path */
    free(slot->path);

    /* Clear slot */
    memset(slot, 0, sizeof(*slot));
    extension_count_internal--;

    LOG_INFOF("Extension: Unloaded '%s'", name_copy);
    return 0;
}

/* List loaded extensions */
void extension_list(void) {
    if (extension_count_internal == 0) {
        mlwrite("No extensions loaded");
        return;
    }

    mlwrite("Loaded extensions:");
    for (int i = 0; i < UEMACS_MAX_EXTENSIONS; i++) {
        if (extensions[i].active && extensions[i].ext) {
            struct uemacs_extension *ext = extensions[i].ext;
            mlwrite("  %s v%s - %s",
                    ext->name,
                    ext->version ? ext->version : "?.?.?",
                    ext->description ? ext->description : "(no description)");
        }
    }
}

/*
 * Load all extensions from directory.
 *
 * Optimized for Linux 2026:
 * - First pass: Collect outdated extensions for BATCH build
 * - Second pass: PARALLEL load all .so files (P2)
 * - Uses statx() with AT_STATX_DONT_SYNC for fast mtime checks
 * - Uses posix_spawn() for builds (no shell fork)
 * - Uses io_uring for batch syscalls when available (P3)
 */
int extension_load_dir(const char *dir) {
    if (!dir || !*dir) {
        LOG_ERROR("Extension: No directory provided");
        return -1;
    }

    DIR *dp = opendir(dir);
    if (!dp) {
        LOG_WARNF("Extension: Cannot open directory: %s", dir);
        return -1;
    }

#ifdef HAVE_LIBURING
    LOG_DEBUG("Extension: Using io_uring for batch syscalls");
#endif

    /* Arrays to collect paths for batch operations */
    char *build_paths[MAX_BUILD_BATCH];
    int build_count = 0;

    const char *load_paths[MAX_LOAD_BATCH];
    char *load_path_storage[MAX_LOAD_BATCH];
    int load_count = 0;

    /* Collect all directory entries first for batch processing */
    char *all_paths[MAX_LOAD_BATCH];
    int all_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dp)) != nullptr && all_count < MAX_LOAD_BATCH) {
        if (entry->d_name[0] == '.') continue;
        if (entry->d_name[0] == '_') continue;  /* Skip _disabled, etc. */

        /* Skip .disabled suffix (e.g., "ext.disabled") */
        size_t name_len = strlen(entry->d_name);
        if (name_len > 9 && strcmp(entry->d_name + name_len - 9, ".disabled") == 0) continue;

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);
        all_paths[all_count] = SAFE_STRDUP(full_path, "ext scan path");
        if (all_paths[all_count]) {
            all_count++;
        }
    }

#ifdef HAVE_LIBURING
    /* P3: Use io_uring to batch-stat all entries at once */
    uring_stat_result_t *stat_results = SAFE_ARRAY(uring_stat_result_t, all_count, "uring stat results");
    if (stat_results && all_count > 0) {
        int stat_count = io_uring_batch_stat((const char **)all_paths, all_count, stat_results);
        if (stat_count < 0) {
            /* io_uring failed, fall back to sync path */
            SAFE_FREE(stat_results);
            stat_results = nullptr;
        }
    }
#endif

    /* ─── PASS 1: Collect extensions needing rebuild ─────────────────── */
    if (extension_auto_build) {
        for (int i = 0; i < all_count; i++) {
            const char *full_path = all_paths[i];
            bool need_build = false;

#ifdef HAVE_LIBURING
            if (stat_results && stat_results[i].valid) {
                /* Use cached io_uring result */
                if (stat_results[i].is_dir) {
                    if (is_extension_dir(full_path) && needs_rebuild(full_path)) {
                        need_build = true;
                    }
                } else {
                    /* Check if it's a .c file */
                    size_t len = strlen(full_path);
                    if (len > 2 && strcmp(full_path + len - 2, ".c") == 0) {
                        if (needs_rebuild(full_path)) {
                            need_build = true;
                        }
                    }
                }
            } else
#endif
            {
                /* Sync fallback */
                if (is_directory_fast(full_path)) {
                    if (is_extension_dir(full_path) && needs_rebuild(full_path)) {
                        need_build = true;
                    }
                } else if (is_file_fast(full_path)) {
                    size_t len = strlen(full_path);
                    if (len > 2 && strcmp(full_path + len - 2, ".c") == 0) {
                        if (needs_rebuild(full_path)) {
                            need_build = true;
                        }
                    }
                }
            }

            if (need_build && build_count < MAX_BUILD_BATCH) {
                build_paths[build_count] = SAFE_STRDUP(full_path, "ext build path");
                if (build_paths[build_count]) {
                    build_count++;
                }
            }
        }

        /* Batch build all outdated extensions */
        if (build_count > 0) {
            batch_build_extensions(build_paths, build_count);

            /* Free build paths */
            for (int i = 0; i < build_count; i++) {
                SAFE_FREE(build_paths[i]);
            }
        }
    }

    /* ─── PASS 2: Collect all .so files for parallel loading ─────────── */
    /* Use pre-collected paths with io_uring results when available */
    for (int i = 0; i < all_count && load_count < MAX_LOAD_BATCH; i++) {
        const char *full_path = all_paths[i];
        size_t path_len = strlen(full_path);

        /* Direct .so in directory */
        if (path_len > 3 && strcmp(full_path + path_len - 3, ".so") == 0) {
#ifdef HAVE_LIBURING
            bool is_file = stat_results && stat_results[i].valid && !stat_results[i].is_dir;
#else
            bool is_file = is_file_fast(full_path);
#endif
            if (is_file) {
                load_path_storage[load_count] = SAFE_STRDUP(full_path, "ext load path");
                if (load_path_storage[load_count]) {
                    load_paths[load_count] = load_path_storage[load_count];
                    load_count++;
                }
            }
            continue;
        }

        /* Check subdirectory for .so */
#ifdef HAVE_LIBURING
        bool is_dir = stat_results && stat_results[i].valid && stat_results[i].is_dir;
#else
        bool is_dir = is_directory_fast(full_path);
#endif
        if (is_dir) {
            char *so_path = find_first_so(full_path);
            if (so_path && load_count < MAX_LOAD_BATCH) {
                load_path_storage[load_count] = so_path;
                load_paths[load_count] = so_path;
                load_count++;
            } else {
                SAFE_FREE(so_path);  /* No room in batch */
            }
        }
    }

    closedir(dp);

#ifdef HAVE_LIBURING
    SAFE_FREE(stat_results);
#endif

    /* Free collected paths (we've copied what we need) */
    for (int i = 0; i < all_count; i++) {
        SAFE_FREE(all_paths[i]);
    }

    /* ─── LOAD EXTENSIONS (PARALLEL) ─────────────────────────────────── */
    int loaded = 0;

    if (load_count > 0) {
        /* Use thread pool for parallel dlopen() - P2 optimization */
        loaded = parallel_load_extensions(load_paths, load_count);

        /* Free load paths */
        for (int i = 0; i < load_count; i++) {
            SAFE_FREE(load_path_storage[i]);
        }
    }

    LOG_INFOF("Extension: Loaded %d extensions from %s", loaded, dir);
    return loaded;
}

/*
 * Async version of extension_load_dir - fire and forget.
 * Scans directory, spawns async loaders for each .so found.
 * Extensions are added to pending queue, main loop calls init().
 */
static void extension_load_dir_async(const char *dir) {
    if (!dir || !*dir) return;

    DIR *dp = opendir(dir);
    if (!dp) {
        LOG_WARNF("Extension: Cannot open directory: %s", dir);
        return;
    }

    int spawned = 0;
    struct dirent *entry;
    char full_path[PATH_MAX];

    while ((entry = readdir(dp)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (entry->d_name[0] == '_') continue;

        size_t name_len = strlen(entry->d_name);
        if (name_len > 9 && strcmp(entry->d_name + name_len - 9, ".disabled") == 0)
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);

        /* Direct .so file */
        if (name_len > 3 && strcmp(entry->d_name + name_len - 3, ".so") == 0) {
            if (is_file_fast(full_path)) {
                async_load_extension(full_path);
                spawned++;
            }
            continue;
        }

        /* Subdirectory - look for .so inside */
        if (is_directory_fast(full_path)) {
            char *so_path = find_first_so(full_path);
            if (so_path) {
                async_load_extension(so_path);
                free(so_path);
                spawned++;
            }
        }
    }

    closedir(dp);
    LOG_INFOF("Extension: Spawned %d async loaders from %s", spawned, dir);
}

/* Get count of loaded extensions */
int extension_count(void) {
    return extension_count_internal;
}

/* Check if async loading is in progress */
bool extension_loading_in_progress(void) {
    return atomic_load(&autoload_in_progress) || pending_count > 0;
}

/* Cleanup extension subsystem */
void extension_cleanup(void) {
    if (!extension_initialized) return;

    LOG_INFO("Extension: Shutting down...");

    /* Shutdown all out-of-process extensions first */
    ext_host_cleanup();

    /* Cleanup inotify */
    inotify_watch_cleanup();

    /* Unload all in-process extensions in reverse order */
    for (int i = UEMACS_MAX_EXTENSIONS - 1; i >= 0; i--) {
        if (extensions[i].active && extensions[i].ext) {
            extension_unload(extensions[i].ext->name);
        }
    }

    extension_initialized = false;
    LOG_INFO("Extension: Subsystem cleaned up");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * INTERACTIVE COMMANDS
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Interactive command: Load extension */
int extension_load_cmd([[maybe_unused]] int f, [[maybe_unused]] int n) {
    char path[1024];

    if (minibuf_read("Extension path: ", path, sizeof(path)) != true) {
        return false;
    }

    /* Expand ~ */
    char expanded[1024];
    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(expanded, sizeof(expanded), "%s%s", home, path + 1);
        } else {
            strncpy(expanded, path, sizeof(expanded) - 1);
            expanded[sizeof(expanded) - 1] = '\0';
        }
    } else {
        strncpy(expanded, path, sizeof(expanded) - 1);
        expanded[sizeof(expanded) - 1] = '\0';
    }

    if (extension_load(expanded) == 0) {
        mlwrite("Extension loaded");
        return true;
    } else {
        mlwrite("Failed to load extension");
        return false;
    }
}

/* Interactive command: Unload extension */
int extension_unload_cmd([[maybe_unused]] int f, [[maybe_unused]] int n) {
    char name[256];

    if (minibuf_read("Extension name: ", name, sizeof(name)) != true) {
        return false;
    }

    if (extension_unload(name) == 0) {
        mlwrite("Extension unloaded");
        return true;
    } else {
        mlwrite("Failed to unload extension");
        return false;
    }
}

/* Interactive command: List extensions */
int extension_list_cmd([[maybe_unused]] int f, [[maybe_unused]] int n) {
    extension_list();
    return true;
}
