/*
 * linux-modern.c - Modern Linux-specific features for μEmacs
 * 
 * File watching with inotify, signal handling, system integration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "file_utils.h"
#include "memory.h"
#include "string_utils.h"

/* Function prototypes */
void handle_external_modification(int wd);
void handle_file_deletion(int wd);

/* File watching with inotify */
static int inotify_fd = -1;
static int watch_descriptors[MAXWATCH];
static char *watch_files[MAXWATCH];
static int watch_count = 0;

#define MAXWATCH 32
#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

/* Initialize file watching */
int init_file_watch(void) {
    inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd < 0) {
        return FALSE;
    }
    
    memset(watch_descriptors, -1, sizeof(watch_descriptors));
    memset(watch_files, 0, sizeof(watch_files));
    watch_count = 0;
    
    return TRUE;
}

/* Add a file to watch list */
int watch_file(const char *filepath) {
    int wd;
    
    if (watch_count >= MAXWATCH) {
        return FALSE;
    }
    
    wd = inotify_add_watch(inotify_fd, filepath,
                           IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF);
    if (wd < 0) {
        return FALSE;
    }
    
    watch_descriptors[watch_count] = wd;
    watch_files[watch_count] = safe_strdup(filepath, "watch_file");
    watch_count++;
    
    return TRUE;
}

/* Remove a file from watch list */
void unwatch_file(const char *filepath) {
    int i;
    
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
}

/* Check for file changes */
void check_file_changes(void) {
    char buffer[EVENT_BUF_LEN];
    int length, i;
    struct timeval tv;
    fd_set rfds;
    
    if (inotify_fd < 0) return;
    
    /* Non-blocking check */
    FD_ZERO(&rfds);
    FD_SET(inotify_fd, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    
    if (select(inotify_fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
        return;
    }
    
    length = read(inotify_fd, buffer, EVENT_BUF_LEN);
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
            for (bp = bheadp; bp != NULL; bp = bp->b_bufp) {
                if (strcmp(bp->b_fname, watch_files[i]) == 0) {
                    /* Mark buffer as externally modified */
                    mlwrite("WARNING: %s modified externally!", bp->b_fname);
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
            for (bp = bheadp; bp != NULL; bp = bp->b_bufp) {
                if (strcmp(bp->b_fname, watch_files[i]) == 0) {
                    mlwrite("WARNING: %s was deleted!", bp->b_fname);
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

/* Get system clipboard content using safe execution */
int get_clipboard(char *buf, int maxlen) {
    int pipefd[2];
    pid_t pid;
    int len = 0;
    
    if (pipe(pipefd) == -1) return FALSE;
    
    pid = fork();
    if (pid == 0) {
        // Child process
        close(pipefd[0]); // Close read end
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
        close(pipefd[1]);
        
        // Try xclip first
        execl("/usr/bin/xclip", "xclip", "-selection", "clipboard", "-o", (char *)NULL);
        // If xclip fails, try xsel
        execl("/usr/bin/xsel", "xsel", "--clipboard", "--output", (char *)NULL);
        _exit(127);
    } else if (pid > 0) {
        // Parent process
        close(pipefd[1]); // Close write end
        
        FILE *fp = fdopen(pipefd[0], "r");
        if (fp) {
            if (safe_fread_line(buf, maxlen, fp) > 0) {
                len = strlen(buf);
                // Remove trailing newline if present
                if (len > 0 && buf[len-1] == '\n') {
                    buf[len-1] = '\0';
                    len--;
                }
            }
            fclose(fp);
        }
        
        waitpid(pid, NULL, 0);
        close(pipefd[0]);
    } else {
        // Fork failed
        close(pipefd[0]);
        close(pipefd[1]);
        return FALSE;
    }
    
    return len > 0;
}

/* Set system clipboard content */
int set_clipboard(const char *text) {
    int pipefd[2];
    pid_t pid;
    
    if (pipe(pipefd) == -1) return FALSE;
    
    pid = fork();
    if (pid == 0) {
        // Child process
        close(pipefd[1]); // Close write end
        dup2(pipefd[0], STDIN_FILENO); // Redirect stdin from pipe
        close(pipefd[0]);
        
        // Try xclip first
        execl("/usr/bin/xclip", "xclip", "-selection", "clipboard", (char *)NULL);
        // If xclip fails, try xsel
        execl("/usr/bin/xsel", "xsel", "--clipboard", "--input", (char *)NULL);
        _exit(127);
    } else if (pid > 0) {
        // Parent process
        close(pipefd[0]); // Close read end
        
        ssize_t bytes_written = write(pipefd[1], text, strlen(text));
	(void)bytes_written;
        close(pipefd[1]);
        
        waitpid(pid, NULL, 0);
    } else {
        // Fork failed
        close(pipefd[0]);
        close(pipefd[1]);
        return FALSE;
    }
    return TRUE;
}

/* Get Git branch for current file using safe execution */
int get_git_branch(char *branch, int maxlen) {
    int pipefd[2];
    pid_t pid;
    const char *dir = curbp->b_fname[0] ? curbp->b_fname : ".";
    
    if (pipe(pipefd) == -1) return FALSE;
    
    pid = fork();
    if (pid == 0) {
        // Child process
        close(pipefd[0]); // Close read end
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
        close(pipefd[1]);
        
        // Change to directory safely
        if (chdir(dir) == 0) {
            execl("/usr/bin/git", "git", "symbolic-ref", "--short", "HEAD", (char *)NULL);
        }
        _exit(127);
    } else if (pid > 0) {
        // Parent process
        close(pipefd[1]); // Close write end
        
        FILE *fp = fdopen(pipefd[0], "r");
        if (fp && safe_fread_line(branch, maxlen, fp) > 0) {
            // Remove trailing newline
            branch[strcspn(branch, "\n")] = '\0';
            fclose(fp);
            waitpid(pid, NULL, 0);
            close(pipefd[0]);
            return TRUE;
        }
        
        if (fp) fclose(fp);
        waitpid(pid, NULL, 0);
        close(pipefd[0]);
    } else {
        // Fork failed
        close(pipefd[0]);
        close(pipefd[1]);
        return FALSE;
    }
    
    return FALSE;
}

/* Check if file has uncommitted changes */
int git_file_modified(void) {
    int pipefd[2];
    pid_t pid;
    char result[128];
    
    if (!curbp->b_fname[0]) return FALSE;
    
    if (pipe(pipefd) == -1) return FALSE;
    
    pid = fork();
    if (pid == 0) {
        // Child process
        close(pipefd[0]); // Close read end
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
        close(pipefd[1]);
        
        // Redirect stderr to /dev/null
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        
        execl("/usr/bin/git", "git", "status", "--porcelain", curbp->b_fname, (char *)NULL);
        _exit(127);
    } else if (pid > 0) {
        // Parent process
        close(pipefd[1]); // Close write end
        
        FILE *fp = fdopen(pipefd[0], "r");
        if (fp && safe_fread_line(result, sizeof(result), fp) > 0) {
            fclose(fp);
            waitpid(pid, NULL, 0);
            close(pipefd[0]);
            return TRUE; /* File is modified */
        }
        
        if (fp) fclose(fp);
        waitpid(pid, NULL, 0);
        close(pipefd[0]);
    } else {
        // Fork failed
        close(pipefd[0]);
        close(pipefd[1]);
        return FALSE;
    }
    
    return FALSE;
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
