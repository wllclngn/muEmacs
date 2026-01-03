/*
 * linux-modern.c - Modern Linux-specific features for μEmacs
 * 
 * File watching with inotify, signal handling, system integration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <spawn.h>

extern char **environ;

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "file_utils.h"
#include "memory.h"
#include "string_utils.h"
#include "util/logger.h"
#include <pthread.h>

/* Function prototypes */
void handle_external_modification(int wd);
void handle_file_deletion(int wd);

/* Modern Terminal Integration (2025) */
#include "terminal/terminal.h"
#include "terminal/pty.h"

static int inotify_fd = -1;
static int active_terminal_fd = -1;

void register_terminal_fd(int fd) {
    active_terminal_fd = fd;
}

void unregister_terminal_fd(void) {
    active_terminal_fd = -1;
}

/* get_terminal_fd() and check_terminal_output() are now in terminal.c */
static int watch_descriptors[MAXWATCH];
static char *watch_files[MAXWATCH];
static int watch_count = 0;
static pthread_mutex_t watch_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAXWATCH 32
#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

/* Initialize file watching */
int init_file_watch(void) {
    inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd < 0) {
        return false;
    }
    
    memset(watch_descriptors, -1, sizeof(watch_descriptors));
    memset(watch_files, 0, sizeof(watch_files));
    watch_count = 0;
    
    return true;
}

/* Add a file to watch list */
int watch_file(const char *filepath) {
    int wd;
    
    pthread_mutex_lock(&watch_mutex);
    
    if (watch_count >= MAXWATCH) {
        pthread_mutex_unlock(&watch_mutex);
        return false;
    }
    
    wd = inotify_add_watch(inotify_fd, filepath,
                           IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF);
    if (wd < 0) {
        pthread_mutex_unlock(&watch_mutex);
        return false;
    }
    
    watch_descriptors[watch_count] = wd;
    watch_files[watch_count] = safe_strdup(filepath, "watch_file");
    watch_count++;
    
    pthread_mutex_unlock(&watch_mutex);
    return true;
}

/* Remove a file from watch list */
void unwatch_file(const char *filepath) {
    int i;
    
    pthread_mutex_lock(&watch_mutex);
    
    for (i = 0; i < watch_count; i++) {
        if (watch_files[i] && strcmp(watch_files[i], filepath) == 0) {
            inotify_rm_watch(inotify_fd, watch_descriptors[i]);
            SAFE_FREE(watch_files[i]);
            
            /* Compact the arrays */
            for (; i < watch_count - 1; i++) {
                watch_descriptors[i] = watch_descriptors[i + 1];
                watch_files[i] = watch_files[i + 1];
            }
            watch_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&watch_mutex);
}

/* Check for file changes */
void check_file_changes(void) {
    char buffer[EVENT_BUF_LEN];
    int length, i;

    pthread_mutex_lock(&watch_mutex);
    
    if (inotify_fd < 0) {
        pthread_mutex_unlock(&watch_mutex);
        return;
    }
    
    /* Non-blocking check using poll */
    struct pollfd pfd = { .fd = inotify_fd, .events = POLLIN, .revents = 0 };

    if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN)) {
        pthread_mutex_unlock(&watch_mutex);
        return;
    }
    
    length = read(inotify_fd, buffer, EVENT_BUF_LEN);
    pthread_mutex_unlock(&watch_mutex);
    
    if (length < 0) return;
    
    i = 0;
    while (i < length) {
        struct inotify_event *event = (struct inotify_event *)&buffer[i];
        
        if (event->mask & IN_MODIFY) {
            /* File was modified externally */
            handle_external_modification(event->wd);
        } else if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
            /* File was deleted or moved */
            handle_file_deletion(event->wd);
        }
        
        i += EVENT_SIZE + event->len;
    }
}

/* Handle external file modification */
void handle_external_modification(int wd) {
    int i;
    struct buffer *bp;
    
    /* Find which file was modified */
    for (i = 0; i < watch_count; i++) {
        if (watch_descriptors[i] == wd) {
            /* Find the buffer for this file */
            for (bp = bheadp; bp != nullptr; bp = bp->b_bufp) {
                if (strcmp(bp->b_fname, watch_files[i]) == 0) {
                    /* Mark buffer as externally modified */
                    mlwrite("WARNING: %s MODIFIED EXTERNALLY!", bp->b_fname);
                    /* Could prompt for reload here */
                    break;
                }
            }
            break;
        }
    }
}

/* Handle file deletion */
void handle_file_deletion(int wd) {
    int i;
    struct buffer *bp;
    
    for (i = 0; i < watch_count; i++) {
        if (watch_descriptors[i] == wd) {
            for (bp = bheadp; bp != nullptr; bp = bp->b_bufp) {
                if (strcmp(bp->b_fname, watch_files[i]) == 0) {
                    mlwrite("WARNING: %s WAS DELETED!", bp->b_fname);
                    break;
                }
            }
            /* Remove from watch list */
            unwatch_file(watch_files[i]);
            break;
        }
    }
}

/* Cleanup file watching */
void cleanup_file_watch(void) {
    int i;
    
    if (inotify_fd >= 0) {
        for (i = 0; i < watch_count; i++) {
            inotify_rm_watch(inotify_fd, watch_descriptors[i]);
            SAFE_FREE(watch_files[i]);
        }
        close(inotify_fd);
        inotify_fd = -1;
    }
}

/* System integration functions */

/* Get current user's home directory */
char *get_home_directory(void) {
    char *home = getenv("HOME");
    if (home) return home;
    
    struct passwd *pw = getpwuid(getuid());
    if (pw) return pw->pw_dir;
    
    return "/tmp";
}

/* Clipboard functions moved to platform/clipboard.c
 * The new implementation supports:
 *   - OSC 52 (works over SSH/tmux)
 *   - Wayland (wl-copy/wl-paste)
 *   - X11 (xclip/xsel)
 *   - Custom commands
 *   - TOML configuration
 * See include/internal/clipboard.h for the API.
 */

/* Get Git branch for current file using posix_spawn */
int get_git_branch(char *branch, int maxlen) {
    int pipefd[2];
    pid_t pid;

    /* Get directory from filename, or use current dir */
    char dir[NFILEN];
    if (curbp->b_fname[0]) {
        safe_strcpy(dir, curbp->b_fname, sizeof(dir));
        char *slash = strrchr(dir, '/');
        if (slash) *slash = '\0';
        else safe_strcpy(dir, ".", sizeof(dir));
    } else {
        safe_strcpy(dir, ".", sizeof(dir));
    }

    if (pipe(pipefd) == -1) return false;

    /* Set up file actions to redirect stdout to pipe */
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    /* Redirect stderr to /dev/null */
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    /* Use git -C to specify directory */
    char *argv[] = {"git", "-C", dir, "symbolic-ref", "--short", "HEAD", NULL};

    if (posix_spawnp(&pid, "git", &actions, NULL, argv, environ) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }
    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]); /* Close write end in parent */

    int result = false;
    FILE *fp = fdopen(pipefd[0], "r");
    if (fp && safe_fread_line(branch, maxlen, fp) > 0) {
        /* Remove trailing newline */
        branch[strcspn(branch, "\n")] = '\0';
        result = true;
    }
    if (fp) fclose(fp);
    else close(pipefd[0]);

    waitpid(pid, NULL, 0);
    return result;
}

/* Check if file has uncommitted changes using posix_spawn */
int git_file_modified(void) {
    int pipefd[2];
    pid_t pid;
    char result[128];

    if (!curbp->b_fname[0]) return false;

    if (pipe(pipefd) == -1) return false;

    /* Set up file actions for stdout and stderr */
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    /* Redirect stderr to /dev/null */
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    char *argv[] = {"git", "status", "--porcelain", curbp->b_fname, NULL};

    if (posix_spawnp(&pid, "git", &actions, NULL, argv, environ) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }
    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]); /* Close write end in parent */

    int modified = false;
    FILE *fp = fdopen(pipefd[0], "r");
    if (fp && safe_fread_line(result, sizeof(result), fp) > 0) {
        modified = true; /* File is modified */
    }
    if (fp) fclose(fp);
    else close(pipefd[0]);

    waitpid(pid, NULL, 0);
    return modified;
}

/* Get system load average */
void get_system_load(double *load1, double *load5, double *load15) {
    FILE *fp = safe_fopen("/proc/loadavg", FILE_READ);
    if (fp) {
        if (fscanf(fp, "%lf %lf %lf", load1, load5, load15) != 3) {
            *load1 = *load5 = *load15 = 0.0;
        }
        safe_fclose(&fp);
    } else {
        *load1 = *load5 = *load15 = 0.0;
    }
}

/* Get memory usage */
void get_memory_usage(long *total, long *available) {
    FILE *fp = safe_fopen("/proc/meminfo", FILE_READ);
    char line[256];
    
    *total = *available = 0;
    
    if (fp) {
        while (safe_fread_line(line, sizeof(line), fp) > 0) {
            if (sscanf(line, "MemTotal: %ld kB", total) == 1) continue;
            if (sscanf(line, "MemAvailable: %ld kB", available) == 1) break;
        }
        safe_fclose(&fp);
    }
}

/* Modern signal handling is centralized in core/display.c (sizesignal).
 * Avoid installing duplicate handlers here. */

/* Initialize Linux-specific features */
void init_linux_features(void) {
    /* Initialize file watching */
    init_file_watch();
}

/* Cleanup Linux-specific features */
void cleanup_linux_features(void) {
    cleanup_file_watch();
}
