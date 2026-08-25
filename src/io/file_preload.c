/*
 * file_preload.c - Async file preloading for μEmacs
 *
 * Reads file content in background thread while editor initialization runs.
 * Pattern matches git_status.c for simplicity.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "util/file_preload.h"
#include "constants.h"

/* Preload state */
static pthread_mutex_t preload_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t preload_thread;
static int thread_active = 0;

static struct {
    char fname[NFILEN];     /* Filename being preloaded */
    char *data;             /* File content. Raw malloc by contract:
                             * allocated on the preload thread, ownership
                             * transfers through file_preload_get() and
                             * the consumer raw-frees (buffer.c). Not a
                             * SAFE_* allocation. */
    size_t size;            /* Content size */
    int status;             /* 0=pending, 1=done, -1=error */
} preload_cache;

/*
 * Background thread function - reads entire file into memory
 */
static void *preload_worker(void *arg)
{
    (void)arg;
    char fname[NFILEN];

    /* Copy filename under lock */
    pthread_mutex_lock(&preload_lock);
    strncpy(fname, preload_cache.fname, NFILEN - 1);
    fname[NFILEN - 1] = '\0';
    pthread_mutex_unlock(&preload_lock);

    /* Get file size */
    struct stat st;
    if (stat(fname, &st) != 0 || !S_ISREG(st.st_mode)) {
        pthread_mutex_lock(&preload_lock);
        preload_cache.status = -1;
        preload_cache.data = nullptr;
        preload_cache.size = 0;
        pthread_mutex_unlock(&preload_lock);
        return nullptr;
    }

    size_t file_size = (size_t)st.st_size;

    /* Allocate buffer (+1 for potential null terminator) */
    char *buf = malloc(file_size + 1);
    if (!buf) {
        pthread_mutex_lock(&preload_lock);
        preload_cache.status = -1;
        preload_cache.data = nullptr;
        preload_cache.size = 0;
        pthread_mutex_unlock(&preload_lock);
        return nullptr;
    }

    /* Open and read file */
    int fd = open(fname, O_RDONLY);
    if (fd < 0) {
        free(buf);
        pthread_mutex_lock(&preload_lock);
        preload_cache.status = -1;
        preload_cache.data = nullptr;
        preload_cache.size = 0;
        pthread_mutex_unlock(&preload_lock);
        return nullptr;
    }

    /* Read entire file */
    size_t total = 0;
    while (total < file_size) {
        ssize_t n = read(fd, buf + total, file_size - total);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;  /* EOF or error */
        }
        total += (size_t)n;
    }
    close(fd);

    buf[total] = '\0';  /* Null terminate for safety */

    /* Store result */
    pthread_mutex_lock(&preload_lock);
    preload_cache.data = buf;
    preload_cache.size = total;
    preload_cache.status = 1;  /* Done */
    pthread_mutex_unlock(&preload_lock);

    return nullptr;
}

void file_preload_start(const char *fname)
{
    if (!fname || fname[0] == '\0') return;

    /* Skip stdin marker */
    if (fname[0] == '-' && fname[1] == '\0') return;

    pthread_mutex_lock(&preload_lock);

    /* Cancel any existing preload */
    if (thread_active) {
        pthread_mutex_unlock(&preload_lock);
        file_preload_cleanup();
        pthread_mutex_lock(&preload_lock);
    }

    /* Initialize cache */
    strncpy(preload_cache.fname, fname, NFILEN - 1);
    preload_cache.fname[NFILEN - 1] = '\0';
    preload_cache.data = nullptr;
    preload_cache.size = 0;
    preload_cache.status = 0;  /* Pending */

    /* Spawn worker thread */
    if (pthread_create(&preload_thread, nullptr, preload_worker, nullptr) == 0) {
        thread_active = 1;
    } else {
        preload_cache.status = -1;  /* Failed to spawn */
    }

    pthread_mutex_unlock(&preload_lock);
}

int file_preload_ready(const char *fname)
{
    if (!fname) return -1;

    pthread_mutex_lock(&preload_lock);

    /* Check if this is the file we're preloading */
    if (strcmp(preload_cache.fname, fname) != 0) {
        pthread_mutex_unlock(&preload_lock);
        return -1;  /* Different file or not started */
    }

    int status = preload_cache.status;
    pthread_mutex_unlock(&preload_lock);

    if (status == 0) return 0;   /* Still loading */
    if (status == 1) return 1;   /* Ready */
    return -1;                    /* Error */
}

int file_preload_get(const char *fname, char **data, size_t *size)
{
    if (!fname || !data || !size) return -1;

    *data = nullptr;
    *size = 0;

    pthread_mutex_lock(&preload_lock);

    /* Check if this is the file we're preloading */
    if (strcmp(preload_cache.fname, fname) != 0) {
        pthread_mutex_unlock(&preload_lock);
        return -1;  /* Different file or not started */
    }

    /* If still pending, wait for thread */
    if (preload_cache.status == 0 && thread_active) {
        pthread_mutex_unlock(&preload_lock);
        pthread_join(preload_thread, nullptr);
        pthread_mutex_lock(&preload_lock);
        thread_active = 0;
    }

    /* Check result */
    if (preload_cache.status != 1 || !preload_cache.data) {
        pthread_mutex_unlock(&preload_lock);
        return -1;
    }

    /* Transfer ownership to caller */
    *data = preload_cache.data;
    *size = preload_cache.size;

    /* Clear cache (caller now owns the data) */
    preload_cache.data = nullptr;
    preload_cache.size = 0;
    preload_cache.fname[0] = '\0';
    preload_cache.status = 0;

    pthread_mutex_unlock(&preload_lock);
    return 0;
}

void file_preload_cleanup(void)
{
    pthread_mutex_lock(&preload_lock);

    /* Wait for thread if active */
    if (thread_active) {
        pthread_mutex_unlock(&preload_lock);
        pthread_join(preload_thread, nullptr);
        pthread_mutex_lock(&preload_lock);
        thread_active = 0;
    }

    /* Free any cached data */
    if (preload_cache.data) {
        free(preload_cache.data);
        preload_cache.data = nullptr;
    }

    preload_cache.size = 0;
    preload_cache.fname[0] = '\0';
    preload_cache.status = 0;

    pthread_mutex_unlock(&preload_lock);
}
