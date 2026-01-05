/* curses.c - Modern raw ANSI terminal driver for μEmacs
 *
 * Pure POSIX implementation - no ncurses dependency.
 * Uses raw termios for terminal mode and direct ANSI escape sequences.
 *
 * This approach:
 * - Has zero external dependencies
 * - Inherits user's terminal theme automatically
 * - Supports full truecolor when available
 * - Works with any modern terminal (kitty, alacritty, xterm, etc.)
 *
 * C23 Compliant.
 */

#define termdef 1

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "terminal_ops.h"
#include "terminal/palette.h"
#include "terminal/signal_handler.h"
#include "util/logger.h"
#include "input_ring.h"

#define MARGIN 8
#define SCRSIZ 64
#define NPAUSE 10

/*
 * Frame buffer for montauk-style single atomic writes.
 * Build entire screen update, write once - no incremental flushes.
 */
#define FRAME_BUF_SIZE (256 * 1024)  /* 256KB - enough for large terminals */
static char frame_buf[FRAME_BUF_SIZE];
static int frame_pos = 0;

/* Legacy obuf API - routes to frame buffer */
#define OBUF_SIZE FRAME_BUF_SIZE
#define obuf frame_buf
#define obuf_pos frame_pos

/* Global input ring buffer - replaces old fragmented ibuf/pending buffers.
 * 8KB lock-free SPSC ring for high-throughput batch reads.
 */
input_ring_t g_input_ring;
static bool input_ring_initialized = false;

/* Original terminal settings (restored on exit) */
static struct termios orig_termios;
static bool termios_saved = false;

/* Global state for async-signal-safe restoration */
static volatile sig_atomic_t alt_screen_active = 0;
static volatile sig_atomic_t cursor_hidden = 0;

/* Forward declarations */
static void raw_open(void);
static void raw_close(void);
static void obuf_puts(const char *s);

/* Forward declarations for all terminal functions */
void configure_highlight_colors(uint8_t r, uint8_t g, uint8_t b);
static void raw_kopen(void);
static void raw_kclose(void);
static int raw_getchar(void);
static int raw_putchar(int c);
static void raw_flush(void);
static void raw_move(int row, int col);
static void raw_eeol(void);
static void raw_eeop(void);
static void raw_beep(void);
static void raw_rev(int state);
static int raw_rez(const char *res);
static void raw_scroll(int from, int to, int count);

/* The terminal structure definition */
struct terminal term = {
    0, 0, 0, 0,         /* size vars - set in open() */
    MARGIN,
    SCRSIZ,
    NPAUSE,
    raw_open,
    raw_close,
    raw_kopen,
    raw_kclose,
    raw_getchar,
    raw_putchar,
    raw_flush,
    raw_move,
    raw_eeol,
    raw_eeop,
    raw_beep,
    raw_rev,
    raw_rez,
    raw_scroll
};

/* Test hook: if non-null, returns this function's result instead */
int (*typahead_override)(void) = nullptr;

/*
 * Best-effort write - async-signal-safe, ignores return value
 * Used in signal handlers where we can't do anything about failures
 */
static inline void best_effort_write(int fd, const char *buf, size_t len) {
    ssize_t ret = write(fd, buf, len);
    (void)ret;  /* Intentionally ignore - nothing we can do in signal handler */
}

/*
 * Async-signal-safe terminal restoration
 * Only uses write() - no malloc, no stdio, no locks
 * Called from signal handlers or atexit
 */
static void restore_terminal_minimal(void) {
    /* These are compile-time constant strings - safe in signal handlers */
    static const char alt_off[]  = "\033[?1049l";  /* Exit alternate screen */
    static const char show_cur[] = "\033[?25h";    /* Show cursor */
    static const char reset[]    = "\033[0m";      /* Reset attributes */

    /* Only restore if we changed state */
    if (alt_screen_active) {
        best_effort_write(STDOUT_FILENO, alt_off, sizeof(alt_off) - 1);
    }
    if (cursor_hidden) {
        best_effort_write(STDOUT_FILENO, show_cur, sizeof(show_cur) - 1);
    }
    best_effort_write(STDOUT_FILENO, reset, sizeof(reset) - 1);

    /* Restore termios if saved - tcsetattr is async-signal-safe per POSIX */
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    }
}

/* Public API for signal-safe restoration */
void terminal_restore_minimal(void) {
    restore_terminal_minimal();
}

/* Track alternate screen state for signal-safe restoration */
/* NOTE: Authoritative versions are in cleanup.c with atomic tracking */
static void curses_set_alt_screen(bool active) {
    alt_screen_active = active ? 1 : 0;
}

/* Set cursor visibility and emit escape sequence */
/* NOTE: Authoritative versions are in cleanup.c with atomic tracking */
static void curses_set_cursor_visible(bool visible) {
    if (visible) {
        obuf_puts("\033[?25h");  /* Show cursor */
        cursor_hidden = 0;
    } else {
        obuf_puts("\033[?25l");  /* Hide cursor */
        cursor_hidden = 1;
    }
}

/* Get terminal size via ioctl */
static void get_terminal_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        /* Fallback defaults - ioctl failed OR returned invalid size (0x0 in pty) */
        *rows = 24;
        *cols = 80;
    }
}

/* Write raw bytes to output buffer, flush if needed */
static void obuf_write(const char *data, int len) {
    while (len > 0) {
        int space = OBUF_SIZE - obuf_pos;
        if (space == 0) {
            raw_flush();
            space = OBUF_SIZE;
        }
        int chunk = (len < space) ? len : space;
        memcpy(&obuf[obuf_pos], data, chunk);
        obuf_pos += chunk;
        data += chunk;
        len -= chunk;
    }
}

/* Write null-terminated string to output buffer */
static void obuf_puts(const char *s) {
    obuf_write(s, strlen(s));
}

/* Write formatted string to output buffer */
static void obuf_printf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0 && len < (int)sizeof(buf)) {
        obuf_write(buf, len);
    }
}

static void raw_open(void) {
    int rows, cols;

    LOG_DEBUG("TUI: Opening raw terminal mode");

    /* Save original terminal settings */
    if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
        termios_saved = true;
    } else {
        LOG_WARN("TUI: Failed to save terminal settings");
    }

    /* Set raw mode - modern approach like montauk */
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;  /* No timeout - use poll() for blocking */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    /* Set non-blocking I/O - use poll() for waiting */
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    /* Set close-on-exec on stdin to prevent fd leak to spawned processes */
    int fd_flags = fcntl(STDIN_FILENO, F_GETFD, 0);
    if (fd_flags >= 0) {
        fcntl(STDIN_FILENO, F_SETFD, fd_flags | FD_CLOEXEC);
    }

    /* Save termios for signal-safe restoration and install unified signal handlers */
    signal_save_termios(&orig_termios);
    signal_handlers_init();

    /* Get terminal size */
    get_terminal_size(&rows, &cols);
    LOG_DEBUGF("TUI: Terminal size %dx%d", cols, rows);

    /* Terminal capabilities: All modern terminals support clear-to-EOL
     * and we use truecolor-only (no reverse video). Legacy capability
     * flags (eolexist, revexist) have been removed. */

    /* Set terminal size in struct */
    term.t_mrow = rows;
    term.t_nrow = rows - 1;
    term.t_mcol = cols;
    term.t_ncol = cols;

    /* Enter alternate screen buffer - track state for signal-safe restore */
    obuf_puts("\033[?1049h");  /* Alternate screen */
    alt_screen_active = 1;
    obuf_puts("\033[2J");      /* Clear entire screen (montauk pattern) */
    obuf_puts("\033[H");       /* Cursor home */
    obuf_puts("\033[0m");      /* Reset all attributes - start clean */
    obuf_puts("\033[?25h");    /* Show cursor */
    cursor_hidden = 0;

    /* Enable bracketed paste mode (DEC 2004)
     * Terminal wraps pasted text with ESC[200~ ... ESC[201~
     * This allows us to detect and handle pastes specially
     * Supported by: xterm, kitty, iTerm2, Alacritty, most modern terminals */
    obuf_puts("\033[?2004h");

    raw_flush();

    /* Initialize palette system - colors are configured via settings.toml or env vars */
    palette_init();

    /* NOTE: OSC drain removed - we no longer send OSC queries, so no garbage to drain */
}

/* Safe write loop - handles EINTR and partial writes */
static void safe_write(int fd, const char *buf, size_t count) {
    size_t written = 0;
    while (written < count) {
        ssize_t n = write(fd, buf + written, count - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            /* On other errors (EPIPE, EIO), we can't do much in a TUI driver 
               except abort or ignore. Since this is stdout, we ignore. */
            break;
        }
        written += n;
    }
}

static void raw_close(void) {
    LOG_DEBUG("TUI: Closing raw terminal mode");

    /* Cleanup unified signal handlers first */
    signal_handlers_cleanup();

    /* Disable bracketed paste mode */
    obuf_puts("\033[?2004l");
    /* Show cursor */
    obuf_puts("\033[?25h");
    cursor_hidden = 0;
    /* Reset attributes */
    obuf_puts("\033[0m");
    /* Exit alternate screen buffer */
    obuf_puts("\033[?1049l");
    alt_screen_active = 0;

    /* Final flush - single atomic write (montauk pattern) */
    if (frame_pos > 0) {
        safe_write(STDOUT_FILENO, frame_buf, frame_pos);
        frame_pos = 0;
    }

    /* Restore original terminal settings */
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    }

    /* Restore blocking I/O */
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
}

static void raw_kopen(void) {
    /* Keyboard already set up in raw_open */
}

static void raw_kclose(void) {
    /* Keyboard restored in raw_close */
}

/*
 * Refill ring buffer from terminal - batch read for efficiency.
 * This is the ONLY place we call read() on STDIN - all other code
 * consumes from the ring buffer.
 */
static void refill_ring_if_needed(void) {
    if (!input_ring_initialized) {
        input_ring_init(&g_input_ring);
        input_ring_initialized = true;
    }

    /* Check how much space is available in the ring */
    size_t free_space = input_ring_free_space(&g_input_ring);
    if (free_space == 0) {
        return;  /* Ring is full, consumer needs to drain it */
    }

    /* Check how many bytes are available to read without blocking */
    int available = 0;
    if (ioctl(STDIN_FILENO, FIONREAD, &available) != 0 || available <= 0) {
        return;  /* No input pending */
    }

    /* Read as much as we can fit in the ring (up to available) */
    size_t to_read = (size_t)available;
    if (to_read > free_space) {
        to_read = free_space;
    }

    /* Batch read into temporary buffer, then push to ring */
    uint8_t tmp[INPUT_RING_SIZE];
    if (to_read > sizeof(tmp)) {
        to_read = sizeof(tmp);
    }

    ssize_t n;
    do {
        n = read(STDIN_FILENO, tmp, to_read);
    } while (n < 0 && errno == EINTR);

    if (n > 0) {
        input_ring_push_bulk(&g_input_ring, tmp, (size_t)n);
    }
}

/* Read a single byte from input - now uses ring buffer */
static int read_byte(void) {
    if (!input_ring_initialized) {
        input_ring_init(&g_input_ring);
        input_ring_initialized = true;
    }

    /* Try to get from ring first */
    int c = input_ring_pop(&g_input_ring);
    if (c >= 0) {
        return c;
    }

    /* Ring empty - do a blocking single-byte read as fallback.
     * This only happens when there's truly no input pending. */
    unsigned char byte;
    ssize_t n;
    do {
        n = read(STDIN_FILENO, &byte, 1);
    } while (n < 0 && errno == EINTR);

    if (n == 1) {
        return byte;
    }
    return -1;  /* No input or error */
}

/* Read from ring buffer, refilling from terminal if needed */
static int get_input_byte(void) {
    if (!input_ring_initialized) {
        input_ring_init(&g_input_ring);
        input_ring_initialized = true;
    }

    /* Try ring first */
    int c = input_ring_pop(&g_input_ring);
    if (c >= 0) {
        return c;
    }

    /* Ring empty - try to refill */
    refill_ring_if_needed();

    /* Try ring again */
    c = input_ring_pop(&g_input_ring);
    if (c >= 0) {
        return c;
    }

    /* Still empty - fall back to blocking read */
    return read_byte();
}

/* Push byte back into ring buffer (for lookahead/unget) */
static void unget_byte(int c) {
    if (c >= 0) {
        /* Push to front of ring - we need to decrement head */
        /* Note: This is slightly tricky with SPSC - we're modifying head
         * from the consumer side, which is allowed since consumer owns head */
        size_t head = atomic_load_explicit(&g_input_ring.head, memory_order_relaxed);
        size_t new_head = (head - 1) & INPUT_RING_MASK;
        g_input_ring.buf[new_head] = (uint8_t)c;
        atomic_store_explicit(&g_input_ring.head, new_head, memory_order_release);
    }
}

/*
 * Frame interval for idle polling - configurable via [performance] TOML section.
 * Default 100ms = 10fps when idle. Input is still immediate (poll returns on any input).
 * Uses perf_frame_interval global from settings.
 */
#define GET_FRAME_INTERVAL() (perf_frame_interval > 0 ? perf_frame_interval : 100)

static int raw_getchar(void) {
    int c;
    int no_data_count = 0;  /* Track consecutive loops without actual data */

    /*
     * Block until we get actual input.
     *
     * CRITICAL: We must NEVER return 0 for "no input" because 0 (NUL) is a
     * valid character. Returning 0 causes higher-level code to interpret it
     * as CTRL+'@', leading to "[KEY NOT BOUND]" spam.
     */
    for (;;) {
        /* Handle pending signals (SIGWINCH, SIGINT, etc.) via unified handler */
        bool size_changed = false;
        terminal_size_t new_size = {0, 0};
        signal_handle_pending(&size_changed, &new_size);

        if (size_changed && new_size.rows > 0 && new_size.cols > 0) {
            term.t_mrow = new_size.rows;
            term.t_nrow = new_size.rows - 1;
            term.t_mcol = new_size.cols;
            term.t_ncol = new_size.cols;
            sgarbf = true;  /* Force redraw */
            /* Don't return - keep waiting for real input */
        }

        /* Check if we should exit (SIGHUP, SIGTERM, SIGPIPE) */
        if (signal_should_exit()) {
            signal_clear_exit();
            return -1;  /* Signal to caller to exit gracefully */
        }

        c = get_input_byte();
        if (c >= 0) {
            /* Got actual input - return it */
            return c;
        }

        /* No input available - wait with poll */
        extern int get_terminal_fd(void);
        extern void check_terminal_output(void);
        int pty_fd = get_terminal_fd();
        struct pollfd pfds[2];
        int nfds = 1;

        pfds[0].fd = STDIN_FILENO;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;

        if (pty_fd >= 0) {
            pfds[1].fd = pty_fd;
            pfds[1].events = POLLIN;
            pfds[1].revents = 0;
            nfds = 2;
        }

        poll(pfds, nfds, GET_FRAME_INTERVAL());

        /* Check for EOF/HUP on stdin (e.g., closed pipe, disconnected terminal) */
        if (pfds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            return -1;  /* stdin closed or error - exit gracefully */
        }

        /* Proactively batch-fill ring buffer when input becomes available.
         * This is the key optimization: one syscall fills up to 8KB instead
         * of reading one byte at a time. */
        if (pfds[0].revents & POLLIN) {
            size_t before = input_ring_available(&g_input_ring);
            refill_ring_if_needed();
            size_t after = input_ring_available(&g_input_ring);

            if (after > before) {
                no_data_count = 0;  /* Reset - we actually got data */
            } else {
                /* poll said POLLIN but no data arrived - likely EOF condition
                 * (e.g., stdin redirected to /dev/null) */
                if (++no_data_count > 5) {
                    return -1;  /* Persistent EOF - give up */
                }
            }
        }

        /* Process PTY output if available */
        if (pty_fd >= 0 && (pfds[1].revents & POLLIN)) {
            check_terminal_output();
            update(false);
        }
    }
}

static int raw_putchar(int c) {
    /* COMPREHENSIVE DEBUG: Log every char sent to terminal */
    static int output_count = 0;
    if (output_count++ < 500) {  // Limit to first 500 chars per session
        LOG_DEBUGF("TERMINAL_OUT: char='%c' (0x%04x) count=%d",
                   (c >= 0x20 && c < 0x7f) ? c : '.', c, output_count);
    }

    /* Handle character output:
     * - Bytes 0-255: Pass through directly (file already contains UTF-8)
     * - Codepoints > 255: Encode as UTF-8 (for programmatically generated chars)
     */
    if (c < 0x100) {
        /* Byte value: output directly - terminal handles UTF-8 decoding.
         * This is correct because buffer content is already UTF-8 encoded. */
        char ch = (char)c;
        obuf_write(&ch, 1);
    } else if (c < 0x800) {
        /* 2-byte UTF-8: 110xxxxx 10xxxxxx */
        char buf[2];
        buf[0] = (char)(0xC0 | (c >> 6));
        buf[1] = (char)(0x80 | (c & 0x3F));
        obuf_write(buf, 2);
    } else if (c < 0x10000) {
        /* 3-byte UTF-8: 1110xxxx 10xxxxxx 10xxxxxx */
        char buf[3];
        buf[0] = (char)(0xE0 | (c >> 12));
        buf[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (c & 0x3F));
        obuf_write(buf, 3);
    } else if (c < 0x110000) {
        /* 4-byte UTF-8: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
        char buf[4];
        buf[0] = (char)(0xF0 | (c >> 18));
        buf[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (c & 0x3F));
        obuf_write(buf, 4);
    }
    return c;
}

/*
 * Flush frame buffer - montauk pattern: single atomic write.
 * All screen content is built up in frame_buf, then written in one syscall.
 */
static void raw_flush(void) {
    if (frame_pos > 0) {
        /* Hot path - logging disabled for performance
        LOG_DEBUGF("TUI: raw_flush() writing %d bytes", frame_pos); */
        safe_write(STDOUT_FILENO, frame_buf, frame_pos);
        frame_pos = 0;
    }
}

static void raw_move(int row, int col) {
    obuf_printf("\033[%d;%dH", row + 1, col + 1);
}

static void raw_eeol(void) {
    obuf_puts("\033[K");
}

static void raw_eeop(void) {
    /* Reset attributes then erase to end of screen */
    obuf_puts("\033[0m\033[J");
}

static void raw_beep(void) {
    /* BEL character */
    obuf_puts("\007");
}

/*
 * Palette-based highlighting.
 * Uses 256-color palette indices by default, with optional truecolor hex overrides.
 * Respects user's terminal theme settings.
 */

/* Legacy stub - no longer needed with palette system */
void configure_highlight_colors(uint8_t r, uint8_t g, uint8_t b) {
    (void)r; (void)g; (void)b;
    /* Colors now configured via g_palette in palette.c */
}

/* Palette-based highlight: set/reset background */
static int rev_state = 0;  /* Track current reverse/highlight state */

/*
 * Force-sync the highlight state at frame boundaries.
 * Under rapid input, rev_state can desync from actual terminal state.
 * Call this at start of update cycle to ensure clean slate.
 */
void highlight_force_reset(void) {
    obuf_puts("\033[49m");  /* Always emit reset regardless of state */
    rev_state = 0;
}

/*
 * Synchronized Updates (DEC private mode 2026)
 * Prevents flicker by telling terminal to batch updates.
 * Terminal waits for sync_end before displaying any changes.
 *
 * Source: https://sw.kovidgoyal.net/kitty/protocol-extensions/
 *         https://gitlab.freedesktop.org/terminal-wg/specifications/-/merge_requests/2
 *
 * Supported by: kitty, foot, iTerm2, WezTerm, Ghostty, many others
 * Falls back gracefully on unsupported terminals (sequence ignored).
 */
static int sync_active = 0;

void sync_frame_start(void) {
    if (!sync_active) {
        obuf_puts("\033[?2026h");  /* Begin synchronized update */
        sync_active = 1;
    }
}

void sync_frame_end(void) {
    if (sync_active) {
        obuf_puts("\033[?2026l");  /* End synchronized update */
        sync_active = 0;
    }
}

static void raw_rev(int state) {
    LOG_DEBUGF("RAW_REV: state=%d rev_state=%d (will %s)",
               state, rev_state, state == rev_state ? "SKIP" : "EMIT");
    /* Skip redundant calls - major performance optimization */
    if (state == rev_state) return;
    rev_state = state;

    if (state) {
        /* Use palette-based or truecolor highlight */
        if (g_palette.highlight_bg == -2 && palette_truecolor_capable()) {
            LOG_DEBUGF("RAW_REV: emitting truecolor RGB(%d,%d,%d)",
                       g_palette.highlight_rgb[0], g_palette.highlight_rgb[1], g_palette.highlight_rgb[2]);
            obuf_printf("\033[48;2;%d;%d;%dm",
                        g_palette.highlight_rgb[0],
                        g_palette.highlight_rgb[1],
                        g_palette.highlight_rgb[2]);
        } else {
            const char *seq = sgr_palette_bg(g_palette.highlight_bg);
            LOG_DEBUGF("RAW_REV: emitting palette bg=%d seq=[%s]",
                       g_palette.highlight_bg, seq + 1);  /* Skip ESC for readable log */
            obuf_puts(seq);
        }
    } else {
        LOG_DEBUG("RAW_REV: emitting reset (49m)");
        obuf_puts("\033[49m");  /* Reset to terminal default background */
    }
}

static int raw_rez(const char *res) {
    (void)res;
    return true;
}

/*
 * Scroll a region of the screen using ANSI scroll region.
 * from: top line of region
 * to: bottom line of region
 * count: positive = scroll up, negative = scroll down
 */
static void raw_scroll(int from, int to, int count) {
    if (count == 0) return;

    /* Set scroll region: ESC [ top ; bottom r */
    obuf_printf("\033[%d;%dr", from + 1, to + 1);

    /* Perform scroll */
    if (count > 0) {
        /* Scroll up: ESC [ n S */
        obuf_printf("\033[%dS", count);
    } else {
        /* Scroll down: ESC [ n T */
        obuf_printf("\033[%dT", -count);
    }

    /* Reset scroll region to full screen */
    obuf_printf("\033[1;%dr", term.t_nrow + 1);
}

/*
 * Low-level terminal I/O functions (tt* API)

 * These wrap the raw operations for the rest of the editor.
 */

void ttopen(void) {
    raw_open();
}

void ttclose(void) {
    raw_close();
}

int ttputc(int c) {
    return raw_putchar(c);
}

/* Bulk string write - much faster than char-by-char */
void ttputs(const char *s) {
    if (s) obuf_puts(s);
}

/* Bulk write with explicit length - for binary data */
void ttwrite(const char *data, int len) {
    if (data && len > 0) obuf_write(data, len);
}

void ttflush(void) {
    raw_flush();
}

/*
 * Frame rate constant: 33ms = ~30fps, balances responsiveness with efficiency
 * - Input arrives instantly (poll returns immediately on POLLIN)
 * - Idle CPU usage near 0% (sleeps between checks)
 * - Pattern borrowed from montauk/OUROBOROS TUI systems
 */

int terminal_read_byte(void) {
    /* Block until we get a character */
    int c;
    while ((c = raw_getchar()) == 0) {
        /* Wait for input with poll - uses GET_FRAME_INTERVAL() from above */
        struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        poll(&pfd, 1, GET_FRAME_INTERVAL());

        /* Signal-based resize detection via unified handler */
        bool size_changed = false;
        terminal_size_t new_size = {0, 0};
        signal_handle_pending(&size_changed, &new_size);

        if (size_changed && new_size.rows > 0 && new_size.cols > 0) {
            term.t_mrow = new_size.rows;
            term.t_nrow = new_size.rows - 1;
            term.t_mcol = new_size.cols;
            term.t_ncol = new_size.cols;
            sgarbf = true;
        }
    }
    return c;
}

/*
 * Check if input is available with optional timeout
 * timeout_ms: milliseconds to wait (0 for non-blocking, -1 for indefinite)
 * Returns: true if input available, false otherwise
 *
 * Modern poll()-based approach like montauk - cleaner than VTIME
 */
bool has_input_available(int timeout_ms) {
    /* Check ring buffer first - instant return if data available */
    if (input_ring_initialized && !input_ring_empty(&g_input_ring)) {
        return true;
    }

    struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};

    /* Clamp timeout to reasonable bounds */
    int to = timeout_ms;
    if (to < 0) to = -1;  /* -1 means wait indefinitely */
    if (to > 10000) to = 10000;  /* Cap at 10 seconds */

    int rv = poll(&pfd, 1, to);
    return (rv > 0 && (pfd.revents & POLLIN));
}

/* Check for pending input - returns COUNT of bytes, not just boolean */
int input_pending(void) {
    /* Allow tests to override */
    if (typahead_override) {
        return typahead_override();
    }

    /* Count bytes in ring buffer first */
    size_t ring_count = 0;
    if (input_ring_initialized) {
        ring_count = input_ring_available(&g_input_ring);
    }

    /* Add bytes pending in kernel buffer (not yet read into ring) */
    int kernel_count = 0;
    ioctl(STDIN_FILENO, FIONREAD, &kernel_count);
    if (kernel_count < 0) kernel_count = 0;

    return (int)ring_count + kernel_count;
}
