/*
 * terminal.h - Split-Window Terminal Integration Interface
 *
 * Implements terminal emulation with:
 * - Split window at configurable height (settings.toml)
 * - Multiple named terminal buffers (*Terminal:name*)
 * - Line-based output with scrollback
 * - Graphics tunneling (Kitty/Sixel passthrough)
 * - Basic ANSI/VT100 parsing
 *
 * C23 compliant
 */

#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/* Maximum number of concurrent terminals */
#define MAX_TERMINALS 8

/* Forward declarations */
struct buffer;
struct window;

/* --- Lifecycle API --- */

/* Initialize the terminal subsystem */
void terminal_init(void);

/* Open terminal in split window at bottom (M-x: term) */
int terminal_open_split(int f, int n);

/* Legacy name for compatibility */
int terminal_buffer_open(int f, int n);

/* Close terminal window and PTY */
int terminal_close(int f, int n);

/* Close terminal associated with a buffer (called when buffer killed) */
void terminal_buffer_close(struct buffer *bp);

/* --- Multiple Terminal API --- */

/* Create a named terminal (M-x: terminal-create)
 * Name is appended to buffer: "*Terminal:name*"
 * If name is NULL or empty, uses default "*Terminal*"
 */
int terminal_create(int f, int n);

/* Switch to a named terminal (M-x: terminal-switch)
 * Prompts for terminal name from active terminals
 */
int terminal_switch(int f, int n);

/* List all active terminals (M-x: terminal-list)
 * Shows terminal name, shell, and status
 */
int terminal_list(int f, int n);

/* Internal: Create terminal with specific name */
int terminal_create_named(const char *name);

/* Internal: Find terminal buffer by name */
struct buffer *terminal_find(const char *name);

/* Internal: Get count of active terminals */
int terminal_count(void);

/* --- I/O & Emulation --- */

/* Process PTY output (ANSI parsing + Graphics Tunneling) */
void terminal_process_output(struct buffer *bp, const char *data, size_t len);

/* Handle input for terminal (Modal) - returns true if key consumed */
bool terminal_handle_key(int c);

/* Check for and process terminal output (called from main loop) */
void check_terminal_output(void);

/* Get terminal PTY fd for polling (-1 if no terminal) */
int get_terminal_fd(void);

/* --- Resize --- */

/* Resize terminal PTY dimensions */
void terminal_resize(int new_rows, int new_cols);

/* --- Rendering --- */

/* No-op for compatibility - terminals now use normal buffer display */
void terminal_render_window(struct window *wp);

#endif /* TERMINAL_H */
