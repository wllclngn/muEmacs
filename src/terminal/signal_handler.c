/*      SIGNAL_HANDLER.C
 *
 *      Signal-safe handlers for terminal signals
 *
 *      2024 - Proper signal handling for terminal applications
 *
 *      Key design principles:
 *      - Handlers only set flags (async-signal-safe)
 *      - All real work done in main loop via signal_handle_pending()
 *      - Terminal restoration uses only write() and tcsetattr()
 *      - No malloc, printf, or other unsafe functions in handlers
 */

#include "terminal/signal_handler.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <execinfo.h>
#include "util/logger.h"

/* Global signal flags */
volatile sig_atomic_t sig_winch_pending = 0;
volatile sig_atomic_t sig_int_pending = 0;
volatile sig_atomic_t sig_pipe_pending = 0;
volatile sig_atomic_t sig_hup_pending = 0;
volatile sig_atomic_t sig_term_pending = 0;
volatile sig_atomic_t sig_cont_pending = 0;

/* Window size after SIGWINCH (updated by handler) */
volatile sig_atomic_t sig_new_rows = 0;
volatile sig_atomic_t sig_new_cols = 0;

/* Saved termios for async-signal-safe restoration */
static struct termios saved_termios;
static bool termios_saved = false;

/* Original signal handlers for cleanup */
static struct sigaction orig_winch;
static struct sigaction orig_int;
static struct sigaction orig_pipe;
static struct sigaction orig_hup;
static struct sigaction orig_term;
static struct sigaction orig_cont;
static bool handlers_installed = false;

/* Constant escape sequences for signal-safe writes */
static const char seq_exit_alt_screen[] = "\033[?1049l";
static const char seq_show_cursor[] = "\033[?25h";
static const char seq_reset_attrs[] = "\033[0m";
static const char seq_bracketed_off[] = "\033[?2004l";
static const char seq_focus_off[] = "\033[?1004l";

/* ========== Signal Handlers ========== */

static void handle_winch(int sig) {
    (void)sig;

    /* Dimensions before the pending flag: a consumer that sees the
     * flag must never read a prior resize's rows/cols. (ioctl is
     * async-signal-safe.) */
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        sig_new_rows = ws.ws_row;
        sig_new_cols = ws.ws_col;
    }
    sig_winch_pending = 1;
}

static void handle_int(int sig) {
    (void)sig;
    sig_int_pending = 1;
}

static void handle_pipe(int sig) {
    (void)sig;
    sig_pipe_pending = 1;
}

static void handle_hup(int sig) {
    (void)sig;
    sig_hup_pending = 1;
    /* HUP typically means terminal is gone - restore and prepare to exit */
    signal_restore_terminal_safe();
}

static void handle_term(int sig) {
    (void)sig;
    sig_term_pending = 1;
    /* Restore terminal before exit */
    signal_restore_terminal_safe();
}

static void handle_cont(int sig) {
    (void)sig;
    sig_cont_pending = 1;
    /* After SIGCONT, terminal may need re-initialization */
}

/* ========== Public API ========== */

void signal_handlers_init(void) {
    if (handlers_installed) return;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  /* Restart interrupted system calls */

    /* SIGWINCH - window resize */
    sa.sa_handler = handle_winch;
    sigaction(SIGWINCH, &sa, &orig_winch);

    /* SIGINT - interrupt (Ctrl+C) */
    sa.sa_handler = handle_int;
    sigaction(SIGINT, &sa, &orig_int);

    /* SIGPIPE - broken pipe */
    sa.sa_handler = handle_pipe;
    sigaction(SIGPIPE, &sa, &orig_pipe);

    /* SIGHUP - hangup */
    sa.sa_handler = handle_hup;
    sigaction(SIGHUP, &sa, &orig_hup);

    /* SIGTERM - termination */
    sa.sa_handler = handle_term;
    sigaction(SIGTERM, &sa, &orig_term);

    /* SIGCONT - continue after stop */
    sa.sa_handler = handle_cont;
    sigaction(SIGCONT, &sa, &orig_cont);

    handlers_installed = true;
}

void signal_handlers_cleanup(void) {
    if (!handlers_installed) return;

    sigaction(SIGWINCH, &orig_winch, nullptr);
    sigaction(SIGINT, &orig_int, nullptr);
    sigaction(SIGPIPE, &orig_pipe, nullptr);
    sigaction(SIGHUP, &orig_hup, nullptr);
    sigaction(SIGTERM, &orig_term, nullptr);
    sigaction(SIGCONT, &orig_cont, nullptr);

    handlers_installed = false;
}

bool signal_handle_pending(bool *size_changed, terminal_size_t *new_size) {
    bool handled = false;

    if (size_changed) *size_changed = false;

    /* SIGWINCH */
    if (sig_winch_pending) {
        sig_winch_pending = 0;
        handled = true;

        if (size_changed && new_size) {
            *size_changed = true;
            new_size->rows = sig_new_rows;
            new_size->cols = sig_new_cols;

            /* Double-check with fresh ioctl */
            struct winsize ws;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
                new_size->rows = ws.ws_row;
                new_size->cols = ws.ws_col;
            }
        }
    }

    /* SIGCONT - may need to refresh terminal state */
    if (sig_cont_pending) {
        sig_cont_pending = 0;
        handled = true;
        /* Caller should re-enter raw mode and refresh display */
    }

    /* Note: SIGINT, SIGPIPE, SIGHUP, SIGTERM are checked separately */

    return handled;
}

bool signal_should_exit(void) {
    bool should_exit = sig_hup_pending || sig_term_pending || sig_pipe_pending;
    if (should_exit) {
        LOG_WARNF("Signal exit triggered: SIGHUP=%d SIGTERM=%d SIGPIPE=%d",
                  (int)sig_hup_pending, (int)sig_term_pending, (int)sig_pipe_pending);
    }
    return should_exit;
}

void signal_clear_exit(void) {
    sig_hup_pending = 0;
    sig_term_pending = 0;
    sig_pipe_pending = 0;
}

void signal_restore_terminal_safe(void) {
    /* Only use async-signal-safe functions here:
     * write(), tcsetattr(), _exit()
     *
     * Do NOT use: printf, malloc, free, syslog, etc.
     */

    /* Reset terminal modes - best effort, ignore failures */
    if (write(STDOUT_FILENO, seq_bracketed_off, sizeof(seq_bracketed_off) - 1) < 0) {}
    if (write(STDOUT_FILENO, seq_focus_off, sizeof(seq_focus_off) - 1) < 0) {}
    if (write(STDOUT_FILENO, seq_reset_attrs, sizeof(seq_reset_attrs) - 1) < 0) {}
    if (write(STDOUT_FILENO, seq_show_cursor, sizeof(seq_show_cursor) - 1) < 0) {}
    if (write(STDOUT_FILENO, seq_exit_alt_screen, sizeof(seq_exit_alt_screen) - 1) < 0) {}

    /* Restore original termios */
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
    }
}

void signal_save_termios(const struct termios *t) {
    if (t) {
        saved_termios = *t;
        termios_saved = true;
    }
}

sigset_t signal_block_all(void) {
    sigset_t all, old;
    sigfillset(&all);
    sigprocmask(SIG_BLOCK, &all, &old);
    return old;
}

void signal_unblock(sigset_t old_mask) {
    sigprocmask(SIG_SETMASK, &old_mask, nullptr);
}

bool signal_get_terminal_size(terminal_size_t *size) {
    if (!size) return false;

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
        return false;
    }

    size->rows = ws.ws_row;
    size->cols = ws.ws_col;
    return true;
}

bool signal_check_interrupt(void) {
    if (sig_int_pending) {
        sig_int_pending = 0;
        return true;
    }
    return false;
}

void signal_clear_interrupt(void) {
    sig_int_pending = 0;
}

/* ========== Crash Signal Handlers ========== */

/* Async-signal-safe signal name lookup */
static const char *crash_signal_name(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (Segmentation fault)";
    case SIGABRT: return "SIGABRT (Abort)";
    case SIGFPE:  return "SIGFPE (Floating point exception)";
    case SIGILL:  return "SIGILL (Illegal instruction)";
    case SIGBUS:  return "SIGBUS (Bus error)";
    default:      return "Unknown signal";
    }
}

/*
 * Crash handler — prints stack trace and restores terminal.
 *
 * Uses only async-signal-safe functions:
 *   dprintf, write, backtrace, backtrace_symbols_fd, _exit, tcsetattr
 *
 * SA_RESETHAND ensures one-shot (prevents recursive crashes).
 */
static void handle_crash(int sig) {
    /* Restore terminal first so the trace is readable */
    signal_restore_terminal_safe();

    /* Print crash banner */
    dprintf(STDERR_FILENO, "\n\033[1;31m=== μEmacs CRASH ===\033[0m\n");
    dprintf(STDERR_FILENO, "Signal: %d (%s)\n", sig, crash_signal_name(sig));

    /* Stack trace via glibc backtrace (compile with -rdynamic for symbols) */
    void *frames[64];
    int n = backtrace(frames, 64);
    if (n > 0) {
        dprintf(STDERR_FILENO, "Stack trace (%d frames):\n", n);
        backtrace_symbols_fd(frames, n, STDERR_FILENO);
    }

    dprintf(STDERR_FILENO, "\033[1;31m=== END CRASH ===\033[0m\n\n");

    /* Die without running atexit handlers (unsafe in signal context) */
    _exit(128 + sig);
}

void signal_install_crash_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = handle_crash;
    sa.sa_flags = (int)SA_RESETHAND;  /* One-shot: prevents recursive crashes */

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
}

/* ========== signalfd Integration ========== */

#ifdef HAVE_SYS_SIGNALFD_H
#include <sys/signalfd.h>

static int g_signalfd = -1;

int signal_create_signalfd(void) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGWINCH);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGPIPE);
    sigaddset(&mask, SIGCONT);

    /* Block these signals so they go to signalfd */
    sigprocmask(SIG_BLOCK, &mask, nullptr);

    g_signalfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    return g_signalfd;
}

int signal_get_signalfd(void) {
    return g_signalfd;
}

void signal_process_signalfd(void) {
    if (g_signalfd < 0) return;

    struct signalfd_siginfo si;
    while (read(g_signalfd, &si, sizeof(si)) == (ssize_t)sizeof(si)) {
        switch (si.ssi_signo) {
        case SIGWINCH:
            sig_winch_pending = 1;
            sig_new_rows = (int)si.ssi_utime;  /* Not available via signalfd */
            /* Fetch size via ioctl instead */
            {
                struct winsize ws;
                if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
                    sig_new_rows = ws.ws_row;
                    sig_new_cols = ws.ws_col;
                }
            }
            break;
        case SIGINT:
            sig_int_pending = 1;
            break;
        case SIGTERM:
            sig_term_pending = 1;
            signal_restore_terminal_safe();
            break;
        case SIGHUP:
            sig_hup_pending = 1;
            signal_restore_terminal_safe();
            break;
        case SIGPIPE:
            sig_pipe_pending = 1;
            break;
        case SIGCONT:
            sig_cont_pending = 1;
            break;
        }
    }
}

void signal_close_signalfd(void) {
    if (g_signalfd >= 0) {
        close(g_signalfd);
        g_signalfd = -1;
    }
}

#else /* !HAVE_SYS_SIGNALFD_H */

int signal_create_signalfd(void) { return -1; }
int signal_get_signalfd(void) { return -1; }
void signal_process_signalfd(void) {}
void signal_close_signalfd(void) {}

#endif /* HAVE_SYS_SIGNALFD_H */
