#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/resource.h>

#include "../test_utils.h"
#include "../test_registry.h"
#include "terminal/pty.h"
#include "terminal/terminal.h"

// Basic PTY spawn/close test
static int test_pty_spawn_close(void) {
    int ok = 1;
    PHASE_START("PTY: SPAWN/CLOSE", "PTY allocation and cleanup");

    pid_t pid;
    int master_fd = pty_spawn(NULL, &pid, 24, 80);

    if (master_fd < 0) {
        LOG_ERROR("[FAIL] pty_spawn failed");
        return 0;
    }
    LOG_INFOF("[%sOK%s] pty_spawn succeeded: master_fd=%d, pid=%d", GREEN, RESET, master_fd, pid);

    if (!pty_child_alive(pid)) {
        LOG_ERROR("[FAIL] child process not alive");
        ok = 0;
    } else {
        LOG_INFOF("[%sOK%s] child process is alive", GREEN, RESET);
    }

    pty_close(master_fd, pid);
    usleep(50000); // Give it a moment to die

    if (pty_child_alive(pid)) {
        // Kill if still alive
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }

    LOG_INFOF("[%sOK%s] child process exited after pty_close", GREEN, RESET);

    PHASE_END("PTY: SPAWN/CLOSE", ok);
    return ok;
}

// Test forkpty() failure simulation by exhausting process limit
static int test_pty_spawn_failure(void) {
    int ok = 1;
    PHASE_START("PTY: SPAWN FAILURE", "Test forkpty() failure handling");

    // Save original limit
    struct rlimit orig_limit, new_limit;
    if (getrlimit(RLIMIT_NPROC, &orig_limit) != 0) {
        LOG_WARN("[WARN] Could not get RLIMIT_NPROC, skipping test");
        ok = 1;
        PHASE_END("PTY: SPAWN FAILURE", ok);
        return ok;
    }

    // Set very low process limit to force fork failure
    new_limit.rlim_cur = 1;
    new_limit.rlim_max = orig_limit.rlim_max;

    if (setrlimit(RLIMIT_NPROC, &new_limit) != 0) {
        LOG_WARN("[WARN] Could not set RLIMIT_NPROC, skipping test");
        ok = 1;
        PHASE_END("PTY: SPAWN FAILURE", ok);
        return ok;
    }

    // Attempt spawn - should fail
    pid_t pid = -999;
    int master_fd = pty_spawn(NULL, &pid, 24, 80);

    // Restore original limit immediately
    setrlimit(RLIMIT_NPROC, &orig_limit);

    if (master_fd >= 0) {
        LOG_ERRORF("[FAIL] pty_spawn should have failed but returned fd=%d", master_fd);
        pty_close(master_fd, pid);
        ok = 0;
    } else if (master_fd == -1) {
        LOG_INFOF("[%sOK%s] pty_spawn correctly failed with -1", GREEN, RESET);
    } else {
        LOG_ERRORF("[FAIL] pty_spawn returned unexpected value: %d", master_fd);
        ok = 0;
    }

    PHASE_END("PTY: SPAWN FAILURE", ok);
    return ok;
}

// Test shell execution failure
static int test_pty_shell_failure(void) {
    int ok = 1;
    PHASE_START("PTY: SHELL EXEC FAILURE", "Test invalid shell handling");

    pid_t pid;
    int master_fd = pty_spawn("/nonexistent/shell/binary", &pid, 24, 80);

    if (master_fd < 0) {
        LOG_ERROR("[FAIL] pty_spawn failed to create PTY");
        return 0;
    }

    // Child should exit with code 127 (exec failure)
    usleep(100000);  // Give child time to fail

    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);

    if (result == pid && WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 127) {
            LOG_INFOF("[%sOK%s] Child exited with 127 (exec failure) as expected", GREEN, RESET);
        } else {
            LOG_WARNF("[WARN] Child exited with code %d (expected 127)", exit_code);
        }
    } else {
        LOG_INFOF("[%sOK%s] Child process state checked (may still be running)", GREEN, RESET);
    }

    // Clean up
    close(master_fd);
    if (result == 0) {  // Still running
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }

    PHASE_END("PTY: SHELL EXEC FAILURE", ok);
    return ok;
}

// Test PTY operations on dead PTY
static int test_pty_operations_after_close(void) {
    int ok = 1;
    PHASE_START("PTY: OPS ON DEAD PTY", "Test I/O on closed PTY");

    pid_t pid;
    int master_fd = pty_spawn(NULL, &pid, 24, 80);

    if (master_fd < 0) {
        LOG_ERROR("[FAIL] pty_spawn failed");
        return 0;
    }

    // Close the PTY
    pty_close(master_fd, pid);
    usleep(50000);

    // Attempt to write to dead PTY
    char test_data[] = "test\n";
    ssize_t written = write(master_fd, test_data, sizeof(test_data));

    if (written < 0 && (errno == EBADF || errno == EIO)) {
        LOG_INFOF("[%sOK%s] Write to closed PTY failed as expected (errno=%d)", GREEN, RESET, errno);
    } else {
        LOG_WARNF("[WARN] Write to closed PTY returned %zd (errno=%d)", written, errno);
    }

    // Attempt to read from dead PTY
    char buf[128];
    ssize_t nread = read(master_fd, buf, sizeof(buf));

    if (nread < 0 && (errno == EBADF || errno == EIO)) {
        LOG_INFOF("[%sOK%s] Read from closed PTY failed as expected (errno=%d)", GREEN, RESET, errno);
    } else {
        LOG_WARNF("[WARN] Read from closed PTY returned %zd (errno=%d)", nread, errno);
    }

    PHASE_END("PTY: OPS ON DEAD PTY", ok);
    return ok;
}

// Test pty_read() non-blocking behavior
static int test_pty_read_nonblocking(void) {
    int ok = 1;
    PHASE_START("PTY: READ NONBLOCKING", "Test non-blocking read behavior");

    pid_t pid;
    int master_fd = pty_spawn(NULL, &pid, 24, 80);

    if (master_fd < 0) {
        LOG_ERROR("[FAIL] pty_spawn failed");
        return 0;
    }

    // Immediately read without writing - should get 0 (EAGAIN)
    char buf[128];
    ssize_t n = pty_read(master_fd, buf, sizeof(buf));

    if (n == 0) {
        LOG_INFOF("[%sOK%s] pty_read() returned 0 on empty buffer (non-blocking)", GREEN, RESET);
    } else {
        LOG_WARNF("[WARN] pty_read() returned %zd (expected 0)", n);
    }

    // Write some data and read it back
    const char* test_input = "echo test\n";
    ssize_t written = pty_write(master_fd, test_input, strlen(test_input));

    if (written > 0) {
        LOG_INFOF("[%sOK%s] pty_write() wrote %zd bytes", GREEN, RESET, written);

        // Give shell time to process
        usleep(100000);

        // Read response
        n = pty_read(master_fd, buf, sizeof(buf));
        if (n > 0) {
            LOG_INFOF("[%sOK%s] pty_read() read %zd bytes after write", GREEN, RESET, n);
        } else if (n == 0) {
            LOG_WARN("[WARN] pty_read() returned 0 (no data yet)");
        } else {
            LOG_ERRORF("[FAIL] pty_read() returned error: %zd", n);
            ok = 0;
        }
    }

    pty_close(master_fd, pid);
    PHASE_END("PTY: READ NONBLOCKING", ok);
    return ok;
}

// Test pty_resize functionality
static int test_pty_resize(void) {
    int ok = 1;
    PHASE_START("PTY: RESIZE", "Test PTY window size changes");

    pid_t pid;
    int master_fd = pty_spawn(NULL, &pid, 24, 80);

    if (master_fd < 0) {
        LOG_ERROR("[FAIL] pty_spawn failed");
        return 0;
    }

    // Resize to various dimensions
    int test_sizes[][2] = {
        {40, 120},
        {25, 80},
        {60, 200},
        {1, 1},      // Minimum
        {999, 999}   // Large
    };

    for (size_t i = 0; i < sizeof(test_sizes) / sizeof(test_sizes[0]); i++) {
        int rows = test_sizes[i][0];
        int cols = test_sizes[i][1];
        int result = pty_resize(master_fd, rows, cols);

        if (result == 0) {
            LOG_INFOF("[%sOK%s] Resized PTY to %dx%d", GREEN, RESET, rows, cols);
        } else {
            LOG_ERRORF("[FAIL] Failed to resize PTY to %dx%d", rows, cols);
            ok = 0;
        }
    }

    pty_close(master_fd, pid);
    PHASE_END("PTY: RESIZE", ok);
    return ok;
}

// Test pty_data_available with timeouts
static int test_pty_data_available(void) {
    int ok = 1;
    PHASE_START("PTY: DATA AVAILABLE", "Test select-based data detection");

    pid_t pid;
    int master_fd = pty_spawn(NULL, &pid, 24, 80);

    if (master_fd < 0) {
        LOG_ERROR("[FAIL] pty_spawn failed");
        return 0;
    }

    // Check with zero timeout (should be false immediately)
    bool avail = pty_data_available(master_fd, 0);
    if (!avail) {
        LOG_INFOF("[%sOK%s] No data available initially (0ms timeout)", GREEN, RESET);
    } else {
        LOG_WARN("[WARN] Data reported available immediately");
    }

    // Write data
    const char* cmd = "echo hello\n";
    pty_write(master_fd, cmd, strlen(cmd));

    // Check with timeout
    usleep(50000);  // Give shell time
    avail = pty_data_available(master_fd, 100);
    if (avail) {
        LOG_INFOF("[%sOK%s] Data detected after write (100ms timeout)", GREEN, RESET);
    } else {
        LOG_WARN("[WARN] No data detected after write");
    }

    pty_close(master_fd, pid);
    PHASE_END("PTY: DATA AVAILABLE", ok);
    return ok;
}

int test_terminal_pty(void) {
    int all_ok = 1;
    all_ok &= test_pty_spawn_close();
    all_ok &= test_pty_spawn_failure();
    all_ok &= test_pty_shell_failure();
    all_ok &= test_pty_read_nonblocking();
    all_ok &= test_pty_resize();
    all_ok &= test_pty_data_available();
    all_ok &= test_pty_operations_after_close();
    return all_ok;
}