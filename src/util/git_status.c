// git_status.c - Asynchronous Git status for μEmacs status line

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "git_status.h"

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

    // Use popen; blocking here is acceptable (background thread)
    FILE* fp = popen("git rev-parse --abbrev-ref HEAD 2>/dev/null", "r");
    if (fp) {
        if (fgets(branch, (int)sizeof(branch), fp) != NULL) {
            // strip newline
            size_t len = strlen(branch);
            if (len && (branch[len-1] == '\n' || branch[len-1] == '\r')) branch[len-1] = '\0';
        }
        pclose(fp);
    }

    if (branch[0] != '\0') {
        fp = popen("git status --porcelain -uno 2>/dev/null | head -n1", "r");
        if (fp) {
            char line[8];
            if (fgets(line, (int)sizeof(line), fp) != NULL) {
                is_dirty = 1;
            }
            pclose(fp);
        }
    }

    pthread_mutex_lock(&lock);
    if (branch[0] == '\0') {
        cached[0] = '\0';
    } else {
        snprintf(cached, sizeof(cached), "git:%s%s", branch, is_dirty ? "*" : "");
    }
    last_update = time(NULL);
    in_progress = 0;
    pthread_mutex_unlock(&lock);
    return NULL;
}

void git_status_init(void) {
    const char* env = getenv("UEMACS_GIT_STATUS");
    const char* test_env = getenv("ENABLE_EXPECT"); // Disable during integration tests
    enabled = (env && strcmp(env, "1") == 0 && !test_env) ? 1 : 0;
}

void git_status_request_async(const char* cwd) {
    (void)cwd; // current impl ignores cwd and uses process CWD
    if (!enabled) return;
    time_t now = time(NULL);

    pthread_mutex_lock(&lock);
    int should_start = (!in_progress && (now - last_update >= 2)); // throttle 2s
    if (should_start) in_progress = 1;
    pthread_mutex_unlock(&lock);

    if (should_start) {
        pthread_t th;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &attr, updater, NULL) != 0) {
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

