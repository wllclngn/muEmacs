/*
 * μEmacs - Modern C23 Implementation
 * 
 * Terminal cleanup and graceful shutdown handling
 */

#include <stdbool.h>
#include <stdatomic.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <termios.h>

#include "estruct.h"
#include "edef.h"

// Global state for signal-safe cleanup
static atomic_bool g_stop_requested = false;
static atomic_bool g_alt_screen_active = false;
static atomic_bool g_cursor_hidden = false;

// Async-signal-safe write helper
static void safe_write(int fd, const char* str, size_t len) {
    ssize_t result = write(fd, str, len);
    (void)result;
}

// Minimal terminal restoration (async-signal-safe)
static void restore_terminal_minimal(void) {
    // Exit alt screen if active
    if (atomic_load(&g_alt_screen_active)) {
        safe_write(STDOUT_FILENO, "\x1B[?1049l", 8);
        atomic_store(&g_alt_screen_active, false);
    }
    
    // Show cursor if hidden
    if (atomic_load(&g_cursor_hidden)) {
        safe_write(STDOUT_FILENO, "\x1B[?25h", 6);
        atomic_store(&g_cursor_hidden, false);
    }
    
    // Reset SGR
    safe_write(STDOUT_FILENO, "\x1B[0m", 4);
    
    // Disable bracketed paste
    safe_write(STDOUT_FILENO, "\x1B[?2004l", 8);
    
    // Disable focus events
    safe_write(STDOUT_FILENO, "\x1B[?1004l", 8);
}

// Full cleanup for atexit
static void on_atexit_restore(void) {
    fflush(stdout);
    fflush(stderr);
    
    restore_terminal_minimal();
    
    // Let ttclose handle termios restoration
    extern void ttclose(void);
    ttclose();
    
    if (isatty(STDOUT_FILENO)) {
        tcdrain(STDOUT_FILENO);
    }
}

// Signal handler
void terminal_signal_cleanup(int sig) {
    (void)sig;
    restore_terminal_minimal();
    atomic_store(&g_stop_requested, true);
}

// Public API
void terminal_cleanup_init(void) {
    atexit(on_atexit_restore);
}

void terminal_set_alt_screen(bool active) {
    if (active) {
        safe_write(STDOUT_FILENO, "\x1B[?1049h", 8);
        atomic_store(&g_alt_screen_active, true);
    } else {
        safe_write(STDOUT_FILENO, "\x1B[?1049l", 8);
        atomic_store(&g_alt_screen_active, false);
    }
}

void terminal_set_cursor_visible(bool visible) {
    if (!visible) {
        safe_write(STDOUT_FILENO, "\x1B[?25l", 6);
        atomic_store(&g_cursor_hidden, true);
    } else {
        safe_write(STDOUT_FILENO, "\x1B[?25h", 6);
        atomic_store(&g_cursor_hidden, false);
    }
}

bool terminal_should_exit(void) {
    return atomic_load(&g_stop_requested);
}

void terminal_force_exit(void) {
    atomic_store(&g_stop_requested, true);
}
