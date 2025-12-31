// winsize.c - Dynamic terminal window size detection
// C23: All updates use atomic operations for signal safety (SIGWINCH handler)
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>
#include <stdatomic.h>
#include "estruct.h"
#include "edef.h"

// Get actual terminal dimensions - uses atomic stores for signal safety
void update_terminal_size(void) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        // Update terminal structure with actual size (atomic for SIGWINCH safety)
        if (ws.ws_row > 0 && ws.ws_col > 0) {
            atomic_store_explicit(&term.t_mrow, (short)ws.ws_row, memory_order_release);
            atomic_store_explicit(&term.t_nrow, (short)(ws.ws_row - 1), memory_order_release);
            atomic_store_explicit(&term.t_mcol, (short)ws.ws_col, memory_order_release);
            atomic_store_explicit(&term.t_ncol, (short)ws.ws_col, memory_order_release);
        }
    } else {
        // Fallback to environment or defaults
        char *lines = getenv("LINES");
        char *cols = getenv("COLUMNS");
        char *endptr;

        if (lines) {
            errno = 0;
            long val = strtol(lines, &endptr, 10);
            if (errno == 0 && endptr != lines && val > 0) {
                atomic_store_explicit(&term.t_mrow, (short)(val - 1), memory_order_release);
                atomic_store_explicit(&term.t_nrow, (short)(val - 1), memory_order_release);
            }
        }
        if (cols) {
            errno = 0;
            long val = strtol(cols, &endptr, 10);
            if (errno == 0 && endptr != cols && val > 0) {
                atomic_store_explicit(&term.t_mcol, (short)val, memory_order_release);
                atomic_store_explicit(&term.t_ncol, (short)val, memory_order_release);
            }
        }

        // Last resort defaults (atomic reads + stores)
        if (atomic_load_explicit(&term.t_mrow, memory_order_acquire) <= 0) {
            atomic_store_explicit(&term.t_mrow, 24, memory_order_release);
            atomic_store_explicit(&term.t_nrow, 23, memory_order_release);
        }
        if (atomic_load_explicit(&term.t_mcol, memory_order_acquire) <= 0) {
            atomic_store_explicit(&term.t_mcol, 80, memory_order_release);
            atomic_store_explicit(&term.t_ncol, 80, memory_order_release);
        }
    }
}

// Handle terminal resize signal - CRITICAL: runs in signal context
// All accesses MUST be async-signal-safe (atomic operations are safe)
void handle_winch(int sig) {
    (void)sig;
    update_terminal_size();
    atomic_store_explicit(&sgarbf, true, memory_order_release);  // Force screen redraw
}