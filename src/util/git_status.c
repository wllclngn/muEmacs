// git_status.c - Asynchronous Git status for μEmacs status line
//
// C23 modernization: Uses posix_spawn() instead of popen()

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>

#include "git_status.h"

extern char **environ;

/*
 * run_cmd_capture - Run a shell command and capture stdout
 *
 * Uses posix_spawn() with pipe redirection instead of popen().
 * Returns bytes read, or -1 on error. Output is null-terminated.
 */
static int run_cmd_capture(const char *cmd, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return -1;
    out[0] = '\0';

    int pipefd[2];
    if (pipe(pipefd) == -1) return -1;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);

    /* Redirect stderr to /dev/null */
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", 1, 0);

    pid_t pid;
    char *argv[] = {"/bin/sh", "-c", (char *)cmd, NULL};

    int err = posix_spawn(&pid, "/bin/sh", &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);

    if (err != 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    close(pipefd[1]);  /* Close write end in parent */

    /* Read output */
    ssize_t total = 0;
    while ((size_t)total < out_sz - 1) {
        ssize_t n = read(pipefd[0], out + total, out_sz - 1 - (size_t)total);
        if (n <= 0) break;
        total += n;
    }
    out[total] = '\0';
    close(pipefd[0]);

    /* Wait for child */
    int status;
    waitpid(pid, &status, 0);

    return (int)total;
}

// Simple throttled background updater with cached result
static int enabled = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char cached[128];
static time_t last_update = 0;
static int in_progress = 0;

static void* updater(void* arg) {
    (void)arg;
    char branch[96] = {0};
    int is_dirty = 0;

    // Use posix_spawn-based capture; blocking here is acceptable (background thread)
    if (run_cmd_capture("git rev-parse --abbrev-ref HEAD", branch, sizeof(branch)) > 0) {
        // strip newline
        size_t len = strlen(branch);
        if (len && (branch[len-1] == '\n' || branch[len-1] == '\r')) branch[len-1] = '\0';
    }

    if (branch[0] != '\0') {
        char line[8] = {0};
        if (run_cmd_capture("git status --porcelain -uno | head -n1", line, sizeof(line)) > 0) {
            is_dirty = 1;
        }
    }

    pthread_mutex_lock(&lock);
    if (branch[0] == '\0') {
        cached[0] = '\0';
    } else {
        snprintf(cached, sizeof(cached), "git:%s%s", branch, is_dirty ? "*" : "");
    }
    last_update = time(nullptr);
    in_progress = 0;
    pthread_mutex_unlock(&lock);
    return nullptr;
}

/* Called after settings load to sync with TOML config */
void git_status_set_enabled(int val) {
    const char* test_env = getenv("ENABLE_EXPECT"); // Disable during integration tests
    enabled = (val && !test_env) ? 1 : 0;
}

/* Legacy init - now a no-op, use git_status_set_enabled() after settings load */
void git_status_init(void) {
    /* Default disabled until settings_load() calls git_status_set_enabled() */
    enabled = 0;
}

void git_status_request_async(const char* cwd) {
    (void)cwd; // current impl ignores cwd and uses process CWD
    if (!enabled) return;
    time_t now = time(nullptr);

    pthread_mutex_lock(&lock);
    int should_start = (!in_progress && (now - last_update >= 2)); // throttle 2s
    if (should_start) in_progress = 1;
    pthread_mutex_unlock(&lock);

    if (should_start) {
        pthread_t th;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &attr, updater, nullptr) != 0) {
            pthread_mutex_lock(&lock);
            in_progress = 0; // failed to spawn
            pthread_mutex_unlock(&lock);
        }
        pthread_attr_destroy(&attr);
    }
}

int git_status_get_cached(char* out, size_t out_sz) {
    if (!enabled || !out || out_sz == 0) return 0;
    pthread_mutex_lock(&lock);
    size_t len = strlen(cached);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, cached, len);
    out[len] = '\0';
    pthread_mutex_unlock(&lock);
    return (int)len;
}

