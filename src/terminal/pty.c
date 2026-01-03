/*
 * pty.c - PTY (Pseudo-Terminal) management for terminal integration
 *
 * Provides functions to spawn shell processes with PTY allocation,
 * read/write to the PTY, resize the terminal, and clean up.
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <poll.h>
#include <time.h>

#include "estruct.h"
#include "edef.h"
#include "util/logger.h"

/*
 * pty_spawn - Allocate PTY and fork shell process
 *
 * shell: Path to shell (NULL or empty uses $SHELL or /bin/sh)
 * child_pid: Output parameter for child process PID
 * rows: Initial terminal rows
 * cols: Initial terminal columns
 *
 * Returns: master fd on success, -1 on failure
 */
int pty_spawn(const char *shell, pid_t *child_pid, int rows, int cols) {
    int master_fd;
    pid_t pid;
    struct winsize ws = {
        .ws_row = (unsigned short)rows,
        .ws_col = (unsigned short)cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };

    LOG_INFO("PTY: pty_spawn() called");
    LOG_INFOF("PTY: Setting PTY size to %dx%d (rows x cols)", rows, cols);
    LOG_DEBUGF("PTY: Requested shell='%s'", shell ? shell : "(null)");

    /* Use forkpty() for simple PTY allocation and fork */
    pid = forkpty(&master_fd, NULL, NULL, &ws);
    if (pid < 0) {
        LOG_ERROR("PTY: forkpty() failed");
        return -1;  /* forkpty failed */
    }

    if (pid == 0) {
        /* Child process - exec shell */
        const char *sh = shell;

        /* Use provided shell, or $SHELL, or fallback to /bin/sh */
        if (!sh || *sh == '\0') {
            sh = getenv("SHELL");
        }
        if (!sh || *sh == '\0') {
            sh = "/bin/sh";
        }

        /* Set up environment for the shell */
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);

        /* Set $UE_* environment variables for editor integration
         * Note: This is in the forked child, so we can safely setenv() */
        setenv("UE_TERMINAL", "1", 1);  /* Indicate we're in μEmacs terminal */

        /* Clear signals to defaults using sigaction */
        struct sigaction sa;
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGQUIT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
        sigaction(SIGCHLD, &sa, NULL);

        /* Execute shell with interactive flag */
        execlp(sh, sh, "-i", (char *)NULL);

        /* If exec fails, exit with error */
        _exit(127);
    }

    /* Parent process - set non-blocking I/O and close-on-exec on master */
    LOG_DEBUGF("PTY: Parent process, child pid=%d, master_fd=%d", (int)pid, master_fd);
    int flags = fcntl(master_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
        LOG_DEBUG("PTY: Set non-blocking I/O on master fd");
    } else {
        LOG_WARN("PTY: Failed to get/set non-blocking flags");
    }

    /* Set close-on-exec to prevent fd leak to child processes */
    int fd_flags = fcntl(master_fd, F_GETFD, 0);
    if (fd_flags >= 0) {
        fcntl(master_fd, F_SETFD, fd_flags | FD_CLOEXEC);
        LOG_DEBUG("PTY: Set close-on-exec on master fd");
    }

    *child_pid = pid;
    LOG_INFOF("PTY: Successfully spawned shell, pid=%d, fd=%d", (int)pid, master_fd);
    return master_fd;
}

/*
 * pty_resize - Resize PTY terminal dimensions
 *
 * master_fd: PTY master file descriptor
 * rows: New terminal rows
 * cols: New terminal columns
 *
 * Returns: 0 on success, -1 on failure
 */
int pty_resize(int master_fd, int rows, int cols) {
    LOG_INFOF("PTY: pty_resize() fd=%d, rows=%d, cols=%d", master_fd, rows, cols);

    struct winsize ws = {
        .ws_row = (unsigned short)rows,
        .ws_col = (unsigned short)cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };

    if (ioctl(master_fd, TIOCSWINSZ, &ws) < 0) {
        LOG_ERROR("PTY: ioctl TIOCSWINSZ failed");
        return -1;
    }
    LOG_DEBUG("PTY: Resize successful");
    return 0;
}

/*
 * pty_read - Read available data from PTY (non-blocking)
 *
 * master_fd: PTY master file descriptor
 * buf: Buffer to read into
 * len: Maximum bytes to read
 *
 * Returns: bytes read (>0), 0 if nothing available, -1 on error/closed
 */
ssize_t pty_read(int master_fd, char *buf, size_t len) {
    ssize_t n = read(master_fd, buf, len);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  /* Nothing available, not an error */
        }
        LOG_DEBUGF("PTY: read() error: %s", strerror(errno));
        return -1;  /* Real error */
    }

    if (n == 0) {
        LOG_DEBUG("PTY: EOF on read - PTY closed");
        return -1;  /* EOF - PTY closed */
    }

    return n;
}

/*
 * pty_write - Write data to PTY
 *
 * master_fd: PTY master file descriptor
 * buf: Data to write
 * len: Number of bytes to write
 *
 * Returns: bytes written on success, -1 on error
 */
ssize_t pty_write(int master_fd, const char *buf, size_t len) {
    ssize_t total = 0;

    while (len > 0) {
        ssize_t n = write(master_fd, buf, len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Would block, return what we've written so far */
                LOG_DEBUG("PTY: write() would block");
                break;
            }
            LOG_DEBUGF("PTY: write() error: %s", strerror(errno));
            return -1;  /* Real error */
        }
        total += n;
        buf += n;
        len -= (size_t)n;
    }

    return total;
}

/*
 * pty_close - Close PTY and wait for child process
 *
 * master_fd: PTY master file descriptor
 * child_pid: Child process PID
 */
void pty_close(int master_fd, pid_t child_pid) {
    LOG_INFOF("PTY: pty_close() fd=%d, pid=%d", master_fd, (int)child_pid);

    /* Close the master fd first */
    if (master_fd >= 0) {
        close(master_fd);
        LOG_DEBUG("PTY: Master fd closed");
    }

    /* Send SIGHUP to child (standard terminal hangup) */
    if (child_pid > 0) {
        LOG_DEBUGF("PTY: Sending SIGHUP to child %d", (int)child_pid);
        kill(child_pid, SIGHUP);

        /* Give it a moment to exit gracefully */
        int status;
        pid_t result = waitpid(child_pid, &status, WNOHANG);

        if (result == 0) {
            /* Still running, wait a bit then force kill */
            LOG_DEBUG("PTY: Child still running, waiting 100ms");
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };  /* 100ms */
            nanosleep(&ts, NULL);
            result = waitpid(child_pid, &status, WNOHANG);

            if (result == 0) {
                /* Still running, force kill */
                LOG_WARN("PTY: Child did not exit, sending SIGKILL");
                kill(child_pid, SIGKILL);
                waitpid(child_pid, &status, 0);
            }
        }
        LOG_DEBUG("PTY: Child process reaped");
    }
    LOG_INFO("PTY: pty_close() complete");
}

/*
 * pty_child_alive - Check if child process is still running
 *
 * child_pid: Child process PID
 *
 * Returns: true if alive, false if exited
 */
bool pty_child_alive(pid_t child_pid) {
    if (child_pid <= 0) {
        LOG_DEBUG("PTY: pty_child_alive() - invalid pid");
        return false;
    }

    int status;
    pid_t result = waitpid(child_pid, &status, WNOHANG);

    if (result == 0) {
        return true;  /* Still running */
    }

    LOG_DEBUGF("PTY: Child %d has exited", (int)child_pid);
    return false;  /* Exited or error */
}

/*
 * pty_data_available - Check if data is available to read
 *
 * master_fd: PTY master file descriptor
 * timeout_ms: Timeout in milliseconds (0 = non-blocking check)
 *
 * Returns: true if data available, false otherwise
 */
bool pty_data_available(int master_fd, int timeout_ms) {
    if (master_fd < 0) {
        return false;
    }

    struct pollfd pfd = {
        .fd = master_fd,
        .events = POLLIN,
        .revents = 0
    };

    int result = poll(&pfd, 1, timeout_ms);
    return (result > 0 && (pfd.revents & POLLIN));
}
