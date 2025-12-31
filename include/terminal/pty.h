/*
 * pty.h - PTY (Pseudo-Terminal) management for terminal integration
 */

#ifndef PTY_H
#define PTY_H

#include <stdbool.h>
#include <sys/types.h>

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
int pty_spawn(const char *shell, pid_t *child_pid, int rows, int cols);

/*
 * pty_resize - Resize PTY terminal dimensions
 *
 * master_fd: PTY master file descriptor
 * rows: New terminal rows
 * cols: New terminal columns
 *
 * Returns: 0 on success, -1 on failure
 */
int pty_resize(int master_fd, int rows, int cols);

/*
 * pty_read - Read available data from PTY (non-blocking)
 *
 * master_fd: PTY master file descriptor
 * buf: Buffer to read into
 * len: Maximum bytes to read
 *
 * Returns: bytes read (>0), 0 if nothing available, -1 on error/closed
 */
ssize_t pty_read(int master_fd, char *buf, size_t len);

/*
 * pty_write - Write data to PTY
 *
 * master_fd: PTY master file descriptor
 * buf: Data to write
 * len: Number of bytes to write
 *
 * Returns: bytes written on success, -1 on error
 */
ssize_t pty_write(int master_fd, const char *buf, size_t len);

/*
 * pty_close - Close PTY and wait for child process
 *
 * master_fd: PTY master file descriptor
 * child_pid: Child process PID
 */
void pty_close(int master_fd, pid_t child_pid);

/*
 * pty_child_alive - Check if child process is still running
 *
 * child_pid: Child process PID
 *
 * Returns: true if alive, false if exited
 */
bool pty_child_alive(pid_t child_pid);

/*
 * pty_data_available - Check if data is available to read
 *
 * master_fd: PTY master file descriptor
 * timeout_ms: Timeout in milliseconds (0 = non-blocking check)
 *
 * Returns: true if data available, false otherwise
 */
bool pty_data_available(int master_fd, int timeout_ms);

#endif /* PTY_H */
