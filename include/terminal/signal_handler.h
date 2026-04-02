/*      SIGNAL_HANDLER.H
 *
 *      Signal-safe handlers for terminal signals
 *
 *      2024 - Proper signal handling for terminal applications
 *
 *      Signals handled:
 *      - SIGWINCH: Window resize
 *      - SIGINT:   Interrupt (Ctrl+C)
 *      - SIGPIPE:  Broken pipe (terminal closed)
 *      - SIGHUP:   Hangup (terminal disconnected)
 *      - SIGTERM:  Termination request
 *      - SIGCONT:  Continue (resume from stop)
 */

#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <signal.h>
#include <stdbool.h>
#include <termios.h>

/* Signal flags - volatile sig_atomic_t for async-signal safety */
extern volatile sig_atomic_t sig_winch_pending;
extern volatile sig_atomic_t sig_int_pending;
extern volatile sig_atomic_t sig_pipe_pending;
extern volatile sig_atomic_t sig_hup_pending;
extern volatile sig_atomic_t sig_term_pending;
extern volatile sig_atomic_t sig_cont_pending;

/* Terminal dimensions after SIGWINCH */
extern volatile sig_atomic_t sig_new_rows;
extern volatile sig_atomic_t sig_new_cols;

/* Window size structure */
typedef struct {
    int rows;
    int cols;
} terminal_size_t;

/*
 * Initialize signal handlers
 *
 * Must be called after terminal is opened and in raw mode.
 * Saves original termios for async-signal-safe restoration.
 */
void signal_handlers_init(void);

/*
 * Restore original signal handlers
 */
void signal_handlers_cleanup(void);

/*
 * Check and handle pending signals
 *
 * Call this from the main loop. Returns true if any signal was handled.
 * This function is NOT async-signal-safe and should only be called
 * from the main thread.
 *
 * Parameters:
 *   size_changed - Set to true if SIGWINCH was handled
 *   new_size     - Filled with new dimensions if size_changed
 *
 * Returns:
 *   true if any signal was handled
 */
bool signal_handle_pending(bool *size_changed, terminal_size_t *new_size);

/*
 * Check if terminal should exit
 *
 * Returns true if SIGHUP, SIGTERM, or fatal SIGPIPE was received.
 */
bool signal_should_exit(void);

/*
 * Clear exit signals after handling
 */
void signal_clear_exit(void);

/*
 * Async-signal-safe terminal restoration
 *
 * Call from signal handlers or during emergency cleanup.
 * Uses only async-signal-safe functions (write, tcsetattr).
 * Exits alternate screen, shows cursor, resets attributes.
 */
void signal_restore_terminal_safe(void);

/*
 * Save current termios for later restoration
 *
 * Call after setting up raw mode.
 */
void signal_save_termios(const struct termios *t);

/*
 * Block signals during critical sections
 *
 * Returns old signal mask for restoration.
 */
sigset_t signal_block_all(void);

/*
 * Restore signal mask
 */
void signal_unblock(sigset_t old_mask);

/*
 * Get current terminal size
 *
 * Uses ioctl(TIOCGWINSZ). Returns false if ioctl fails.
 */
bool signal_get_terminal_size(terminal_size_t *size);

/*
 * Handle SIGINT (Ctrl+C)
 *
 * Returns true if SIGINT is pending and clears the flag.
 * The application should use this to implement abort behavior.
 */
bool signal_check_interrupt(void);

/*
 * Clear SIGINT flag without handling
 */
void signal_clear_interrupt(void);

/*
 * Install crash signal handlers (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS)
 *
 * Prints stack trace to stderr and restores terminal before dying.
 * Uses SA_RESETHAND (one-shot) to prevent recursive crashes.
 * Call early in main() before entering raw mode.
 * Compile with -rdynamic for symbol names in backtraces.
 */
void signal_install_crash_handlers(void);

/*
 * signalfd integration (Linux 2.6.22+)
 *
 * Creates a signalfd for SIGWINCH, SIGINT, SIGTERM, SIGHUP, SIGPIPE, SIGCONT.
 * Blocks these signals via sigprocmask so they go to the fd instead of handlers.
 * Crash signals (SIGSEGV etc.) remain as traditional SA_RESETHAND handlers.
 */
int  signal_create_signalfd(void);
int  signal_get_signalfd(void);
void signal_process_signalfd(void);
void signal_close_signalfd(void);

#endif /* SIGNAL_HANDLER_H */
