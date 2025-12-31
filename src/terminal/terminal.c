/*
 * terminal.c - Split-Window Terminal with Line-Based Output
 *
 * Implements terminal emulation using normal buffer lines instead of a custom grid.
 * Features:
 * - Split window at configurable percentage of screen height
 * - Line-based scrollback (configurable via terminal_scrollback)
 * - Graphics tunneling (Kitty/Sixel passthrough)
 * - Basic ANSI/VT100 parsing for colors and cursor movement
 *
 * C23 compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "memory.h"
#include "line.h"
#include "terminal/terminal.h"
#include "terminal/pty.h"
#include "util/logger.h"
#include "gapbuffer.h"

/* --- ANSI Parser States --- */
#define PARSE_NORMAL  0
#define PARSE_ESC     1
#define PARSE_CSI     2
#define PARSE_OSC     3
#define PARSE_APC     4   /* Kitty graphics */
#define PARSE_DCS     5   /* Sixel */

/* --- Terminal State (Line-Based) --- */
typedef struct {
    int fd;                      /* PTY master fd */
    pid_t pid;                   /* Child shell PID */
    int rows;                    /* Terminal height in rows */
    int cols;                    /* Terminal width in columns */

    /* Line-based state */
    struct line *current_line;   /* Current output line */
    int cur_col;                 /* Column within current line */
    int line_count;              /* Lines accumulated */

    /* ANSI parser state machine */
    int parse_state;
    char csi_buf[64];            /* CSI parameter accumulator */
    int csi_len;

    /* Graphics tunneling state */
    bool in_graphics;            /* Currently tunneling graphics */
    char graphics_terminator;    /* Expected terminator */
} terminal_state_t;

/* --- Graphics Tunneling Logic --- */

/*
 * Identify if a sequence is a graphics command (Kitty/Sixel)
 * returns length of header if match, 0 otherwise.
 */
static size_t is_graphics_header(const char *data, size_t len) {
    if (len < 3) return 0;

    /* Kitty: ESC _ G */
    if (data[0] == '\033' && data[1] == '_' && data[2] == 'G') return 3;

    /* Sixel: ESC P q */
    if (data[0] == '\033' && data[1] == 'P' && data[2] == 'q') return 3;

    return 0;
}

/* --- Line Management --- */

/*
 * Create a new line and link it into the buffer
 */
static struct line *term_create_line(struct buffer *bp) {
    struct line *newlp = lalloc(0);
    if (!newlp) {
        LOG_ERROR("Terminal: Failed to allocate new line");
        return nullptr;
    }

    /* Link at the end of buffer (before header line) */
    struct line *header = bp->b_linep;
    struct line *last = lback(header);

    newlp->l_fp = header;
    newlp->l_bp = last;
    last->l_fp = newlp;
    header->l_bp = newlp;

    LOG_DEBUG("Terminal: Created new line in buffer");
    return newlp;
}

/*
 * Append a character to the current line
 */
static void term_putchar(terminal_state_t *ts, struct buffer *bp, char c) {
    if (!ts->current_line) {
        ts->current_line = term_create_line(bp);
        if (!ts->current_line) return;
        ts->line_count++;
        ts->cur_col = 0;
    }

    struct line *lp = ts->current_line;
    int len = llength(lp);

    /* Ensure we have space up to cur_col with spaces if needed */
    while (len < ts->cur_col) {
        char space = ' ';
        gap_buffer_insert(lp->gb, len, &space, 1);
        len++;
    }

    /* Insert or overwrite character */
    if (ts->cur_col < len) {
        /* Overwrite existing character */
        gap_buffer_delete(lp->gb, ts->cur_col, 1);
    }
    gap_buffer_insert(lp->gb, ts->cur_col, &c, 1);
    ts->cur_col++;
}

/*
 * Handle newline - create new line, prune if over scrollback limit
 */
static void term_newline(terminal_state_t *ts, struct buffer *bp) {
    /* Create new line */
    struct line *newlp = term_create_line(bp);
    if (!newlp) {
        LOG_WARN("Terminal: Failed to create newline");
        return;
    }

    ts->current_line = newlp;
    ts->cur_col = 0;
    ts->line_count++;

    /* Scrollback pruning */
    if (terminal_scrollback > 0 && ts->line_count > terminal_scrollback) {
        /* Remove oldest line (first after header) */
        struct line *oldest = lforw(bp->b_linep);
        if (oldest != bp->b_linep && oldest != ts->current_line) {
            oldest->l_bp->l_fp = oldest->l_fp;
            oldest->l_fp->l_bp = oldest->l_bp;
            lfree(oldest);
            ts->line_count--;
            LOG_DEBUGF("Terminal: Pruned oldest line, line_count=%d", ts->line_count);
        }
    }
}

/*
 * Handle carriage return - move to column 0
 */
static void term_cr(terminal_state_t *ts) {
    ts->cur_col = 0;
}

/*
 * Handle backspace - move cursor left
 */
static void term_bs(terminal_state_t *ts) {
    if (ts->cur_col > 0) ts->cur_col--;
}

/*
 * Handle tab - move to next 8-column boundary
 */
static void term_tab(terminal_state_t *ts, struct buffer *bp) {
    int next_tab = ((ts->cur_col / 8) + 1) * 8;
    while (ts->cur_col < next_tab && ts->cur_col < ts->cols) {
        term_putchar(ts, bp, ' ');
    }
}

/*
 * Handle CSI (Control Sequence Introducer) commands
 */
static void handle_csi(terminal_state_t *ts, struct buffer *bp, char final) {
    ts->csi_buf[ts->csi_len] = '\0';
    LOG_DEBUGF("Terminal: CSI command '%c' params='%s'", final, ts->csi_buf);

    /* Parse parameters - simple comma-separated integers */
    int params[8] = {0};
    int param_count = 0;
    char *p = ts->csi_buf;

    while (*p && param_count < 8) {
        if (isdigit((unsigned char)*p)) {
            params[param_count] = (int)strtol(p, &p, 10);
            param_count++;
        } else if (*p == ';') {
            param_count++;
            p++;
        } else {
            p++;
        }
    }
    if (param_count == 0) param_count = 1;

    (void)bp;  /* Some commands may use bp in future */

    switch (final) {
    case 'm':  /* SGR - Select Graphic Rendition (colors) */
        /* We ignore colors for now - terminal uses buffer colors */
        break;

    case 'K':  /* Erase in Line */
        if (ts->current_line) {
            int mode = params[0];
            int len = llength(ts->current_line);
            if (mode == 0 && ts->cur_col < len) {
                /* Erase from cursor to end of line */
                gap_buffer_delete(ts->current_line->gb, ts->cur_col, len - ts->cur_col);
            } else if (mode == 1 && ts->cur_col > 0) {
                /* Erase from start to cursor */
                for (int i = 0; i < ts->cur_col; i++) {
                    lputc(ts->current_line, i, ' ');
                }
            } else if (mode == 2) {
                /* Erase entire line */
                gap_buffer_delete(ts->current_line->gb, 0, len);
            }
        }
        break;

    case 'J':  /* Erase in Display */
        /* For simplicity, we only handle clear-all (mode 2) */
        if (params[0] == 2) {
            /* Clear all lines except current */
            struct line *lp = lforw(bp->b_linep);
            while (lp != bp->b_linep) {
                struct line *next = lforw(lp);
                if (lp != ts->current_line) {
                    lp->l_bp->l_fp = lp->l_fp;
                    lp->l_fp->l_bp = lp->l_bp;
                    lfree(lp);
                    ts->line_count--;
                }
                lp = next;
            }
            if (ts->current_line) {
                gap_buffer_delete(ts->current_line->gb, 0, llength(ts->current_line));
            }
            ts->cur_col = 0;
        }
        break;

    case 'H':  /* Cursor Position */
    case 'f':
        /* In line-based mode, we can only handle column positioning */
        if (param_count >= 2) {
            ts->cur_col = params[1] > 0 ? params[1] - 1 : 0;
        } else {
            ts->cur_col = 0;
        }
        break;

    case 'C':  /* Cursor Forward */
        ts->cur_col += params[0] > 0 ? params[0] : 1;
        if (ts->cur_col >= ts->cols) ts->cur_col = ts->cols - 1;
        break;

    case 'D':  /* Cursor Back */
        ts->cur_col -= params[0] > 0 ? params[0] : 1;
        if (ts->cur_col < 0) ts->cur_col = 0;
        break;

    case 'G':  /* Cursor Horizontal Absolute */
        ts->cur_col = params[0] > 0 ? params[0] - 1 : 0;
        break;

    default:
        /* Unknown CSI command - ignore */
        break;
    }
}

/* --- ANSI Parser --- */

static void parse_ansi(terminal_state_t *ts, struct buffer *bp, const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];

        /* Graphics tunneling - pass through until terminator */
        if (ts->in_graphics) {
            write(STDOUT_FILENO, &data[i], 1);
            /* Check for terminator (ST = ESC \ or BEL for APC) */
            if (c == '\007' || (c == '\\' && i > 0 && data[i-1] == '\033')) {
                ts->in_graphics = false;
            }
            continue;
        }

        switch (ts->parse_state) {
        case PARSE_NORMAL:
            if (c == '\033') {
                ts->parse_state = PARSE_ESC;
            } else if (c == '\r') {
                term_cr(ts);
            } else if (c == '\n') {
                term_newline(ts, bp);
            } else if (c == '\b') {
                term_bs(ts);
            } else if (c == '\t') {
                term_tab(ts, bp);
            } else if (c == '\007') {
                /* Bell - ignore */
            } else if (c >= 0x20 && c < 0x7F) {
                /* Printable ASCII */
                term_putchar(ts, bp, (char)c);
            } else if (c >= 0x80) {
                /* UTF-8 lead byte or continuation - pass through */
                term_putchar(ts, bp, (char)c);
            }
            break;

        case PARSE_ESC:
            if (c == '[') {
                ts->parse_state = PARSE_CSI;
                ts->csi_len = 0;
            } else if (c == '_') {
                /* APC - Application Program Command (Kitty graphics) */
                ts->parse_state = PARSE_APC;
                ts->in_graphics = true;
                /* Pass through ESC _ */
                write(STDOUT_FILENO, "\033_", 2);
            } else if (c == 'P') {
                /* DCS - Device Control String (Sixel) */
                ts->parse_state = PARSE_DCS;
                ts->in_graphics = true;
                write(STDOUT_FILENO, "\033P", 2);
            } else if (c == ']') {
                /* OSC - Operating System Command */
                ts->parse_state = PARSE_OSC;
            } else {
                /* Unknown escape - return to normal */
                ts->parse_state = PARSE_NORMAL;
            }
            break;

        case PARSE_CSI:
            if (c >= 0x40 && c <= 0x7E) {
                /* Final byte - execute CSI command */
                handle_csi(ts, bp, (char)c);
                ts->parse_state = PARSE_NORMAL;
            } else if (ts->csi_len < 63) {
                /* Parameter or intermediate byte */
                ts->csi_buf[ts->csi_len++] = (char)c;
            }
            break;

        case PARSE_OSC:
            /* Skip OSC sequences until BEL or ST */
            if (c == '\007') {
                ts->parse_state = PARSE_NORMAL;
            } else if (c == '\\' && i > 0 && data[i-1] == '\033') {
                ts->parse_state = PARSE_NORMAL;
            }
            break;

        case PARSE_APC:
        case PARSE_DCS:
            /* Handled above in graphics tunneling */
            break;
        }
    }
}

/* --- Public API --- */

void terminal_init(void) {
    LOG_INFO("Terminal: Initializing split-window terminal subsystem");
}

void terminal_process_output(struct buffer *bp, const char *data, size_t len) {
    if (!bp || len == 0 || !bp->b_term_data) {
        LOG_DEBUG("Terminal: process_output called with invalid parameters");
        return;
    }

    terminal_state_t *ts = (terminal_state_t *)bp->b_term_data;

    /* Check for graphics header at start of data */
    size_t head_len = is_graphics_header(data, len);
    if (head_len > 0) {
        LOG_DEBUG("Terminal: Graphics sequence detected, tunneling to host");
        ts->in_graphics = true;
        write(STDOUT_FILENO, data, len);
        return;
    }

    /* Parse ANSI sequences and output to buffer lines */
    parse_ansi(ts, bp, data, len);

    /* Mark buffer as changed for display refresh */
    bp->b_flag |= BFCHG;
    LOG_DEBUGF("Terminal: Processed %zu bytes, line_count=%d", len, ts->line_count);
}

/* Forward declaration for window function */
extern struct window *terminal_split_bottom(int height_rows);

int terminal_open_split(int f, int n) {
    (void)f; (void)n;

    LOG_INFO("Terminal: terminal_open_split() called");

    /* Check if terminal feature is enabled */
    if (!terminal_enabled) {
        LOG_WARN("Terminal: Feature disabled in settings");
        mlwrite("Terminal feature is disabled in settings");
        return false;
    }

    /* Check if terminal already open */
    if (terminal_window != nullptr) {
        LOG_DEBUG("Terminal: Already open, switching to existing window");
        curwp = terminal_window;
        curbp = curwp->w_bufp;
        mlwrite("[Terminal already open]");
        return true;
    }

    /* Create/find terminal buffer */
    char bufname[] = "*Terminal*";
    LOG_DEBUGF("Terminal: Creating buffer '%s'", bufname);
    struct buffer *bp = bfind(bufname, true, 0);
    if (!bp) {
        LOG_ERROR("Terminal: Failed to create buffer");
        mlwrite("Cannot create terminal buffer");
        return false;
    }
    bp->b_mode |= MDTBUFFER;
    LOG_DEBUGF("Terminal: Buffer created, mode=0x%x", bp->b_mode);

    /* Calculate window height */
    int height = (term.t_nrow * terminal_height_percent) / 100;
    if (height < 3) height = 3;
    if (height > term.t_nrow - 5) height = term.t_nrow - 5;
    LOG_INFOF("Terminal: Calculated height=%d rows (screen=%d, pct=%d%%)",
              height, term.t_nrow, terminal_height_percent);

    /* Create split window at bottom */
    struct window *term_wp = terminal_split_bottom(height);
    if (!term_wp) {
        LOG_ERROR("Terminal: Failed to create split window");
        mlwrite("Cannot create terminal window");
        bp->b_mode &= ~MDTBUFFER;
        return false;
    }
    LOG_DEBUGF("Terminal: Split window created at row %d", term_wp->w_toprow);

    /* Link buffer to window */
    term_wp->w_bufp = bp;
    bp->b_nwnd++;
    term_wp->w_dotp = bp->b_linep;
    term_wp->w_doto = 0;
    term_wp->w_linep = lforw(bp->b_linep);
    term_wp->w_flag |= WFTERM | WFMODE | WFHARD;
    LOG_DEBUGF("Terminal: Buffer linked to window, flag=0x%x", term_wp->w_flag);

    /* Initialize terminal state */
    terminal_state_t *ts = safe_alloc(sizeof(terminal_state_t), "term state", __FILE__, __LINE__);
    if (!ts) {
        LOG_ERROR("Terminal: Failed to allocate terminal state");
        mlwrite("Cannot allocate terminal state");
        return false;
    }

    memset(ts, 0, sizeof(terminal_state_t));
    ts->rows = height;
    ts->cols = term.t_ncol;
    ts->parse_state = PARSE_NORMAL;
    LOG_DEBUGF("Terminal: State initialized, rows=%d, cols=%d", ts->rows, ts->cols);

    /* Create initial line */
    ts->current_line = term_create_line(bp);
    if (ts->current_line) {
        ts->line_count = 1;
        bp->b_dotp = ts->current_line;
        bp->b_doto = 0;
        term_wp->w_dotp = ts->current_line;
        LOG_DEBUG("Terminal: Initial line created");
    }

    /* Spawn PTY */
    const char *shell = terminal_shell[0] ? terminal_shell : nullptr;
    LOG_INFOF("Terminal: Spawning PTY with shell='%s'", shell ? shell : "$SHELL");
    ts->fd = pty_spawn(shell, &ts->pid, ts->rows, ts->cols);
    if (ts->fd < 0) {
        LOG_ERROR("Terminal: Failed to spawn PTY");
        SAFE_FREE(ts);
        bp->b_mode &= ~MDTBUFFER;
        mlwrite("Cannot spawn PTY");
        return false;
    }
    LOG_INFOF("Terminal: PTY spawned, fd=%d, pid=%d", ts->fd, (int)ts->pid);

    /* Register PTY fd for polling */
    extern void register_terminal_fd(int fd);
    register_terminal_fd(ts->fd);
    LOG_DEBUG("Terminal: PTY fd registered for polling");

    bp->b_term_data = ts;
    terminal_window = term_wp;

    /* Switch focus to terminal */
    curwp = term_wp;
    curbp = bp;

    LOG_INFO("Terminal: Successfully opened");
    mlwrite("[Terminal: %s]", shell ? shell : "$SHELL");
    return true;
}

/* Legacy function name for compatibility */
int terminal_buffer_open(int f, int n) {
    return terminal_open_split(f, n);
}

int terminal_close(int f, int n) {
    (void)f; (void)n;

    LOG_INFO("Terminal: terminal_close() called");

    if (!terminal_window) {
        LOG_DEBUG("Terminal: No terminal window to close");
        return true;
    }

    struct buffer *bp = terminal_window->w_bufp;

    /* Close PTY */
    if (bp && bp->b_term_data) {
        terminal_state_t *ts = (terminal_state_t *)bp->b_term_data;
        LOG_INFOF("Terminal: Closing PTY fd=%d, pid=%d", ts->fd, (int)ts->pid);

        /* Unregister PTY fd */
        extern void unregister_terminal_fd(void);
        unregister_terminal_fd();
        LOG_DEBUG("Terminal: PTY fd unregistered");

        /* Close PTY and kill child */
        if (ts->fd >= 0) {
            close(ts->fd);
            ts->fd = -1;
            LOG_DEBUG("Terminal: PTY fd closed");
        }
        if (ts->pid > 0) {
            LOG_DEBUGF("Terminal: Sending SIGTERM to pid %d", (int)ts->pid);
            kill(ts->pid, SIGTERM);
            waitpid(ts->pid, nullptr, WNOHANG);
            ts->pid = 0;
        }

        SAFE_FREE(ts);
        bp->b_term_data = nullptr;
        LOG_DEBUG("Terminal: Terminal state freed");
    }

    /* Remove terminal window - give space back to window above */
    struct window *wp = wheadp;
    struct window *prev = nullptr;

    while (wp && wp != terminal_window) {
        prev = wp;
        wp = wp->w_wndp;
    }

    if (wp == terminal_window) {
        LOG_DEBUG("Terminal: Removing terminal window from list");
        /* Unlink from window list */
        if (prev) {
            prev->w_wndp = wp->w_wndp;
            /* Give height back to previous window */
            int reclaimed_rows = wp->w_ntrows + 1;
            prev->w_ntrows += reclaimed_rows;
            prev->w_flag |= WFMODE | WFHARD;
            LOG_DEBUGF("Terminal: Gave %d rows back to window above", reclaimed_rows);
        } else {
            wheadp = wp->w_wndp;
        }

        /* Switch to previous window */
        if (prev) {
            curwp = prev;
            curbp = prev->w_bufp;
            LOG_DEBUG("Terminal: Switched to previous window");
        } else if (wheadp) {
            curwp = wheadp;
            curbp = wheadp->w_bufp;
            LOG_DEBUG("Terminal: Switched to head window");
        }

        /* Free window structure */
        SAFE_FREE(wp);
        LOG_DEBUG("Terminal: Window structure freed");
    }

    /* Clear terminal buffer mode */
    if (bp) {
        bp->b_mode &= ~MDTBUFFER;
        bp->b_nwnd--;
        LOG_DEBUG("Terminal: Buffer mode cleared");
    }

    terminal_window = nullptr;
    sgarbf = true;

    LOG_INFO("Terminal: Successfully closed");
    mlwrite("[Terminal closed]");
    return true;
}

void terminal_buffer_close(struct buffer *bp) {
    if (bp && terminal_window && terminal_window->w_bufp == bp) {
        LOG_INFO("Terminal: Buffer being closed, closing terminal");
        terminal_close(0, 0);
    }
}

/*
 * Terminal Input Escape Design (2025)
 * ====================================
 * When in terminal buffer, most keys go to the PTY (shell).
 * These keys ESCAPE to the editor:
 *
 *   C-x     - Ctrl-X prefix (C-x o, C-x b, C-x C-c, etc.)
 *   ESC     - Meta prefix (ESC x = M-x, ESC < = beginning-of-file, etc.)
 *   Alt-x   - Direct M-x on modern terminals (same as ESC x)
 *
 * Once a prefix key is recognized, prefix_dispatch() reads subsequent
 * keys directly via input_read_event(), bypassing this handler entirely.
 * No bypass flag needed.
 */

/* Event-based terminal key handler - modern interface */
bool terminal_handle_key_event(input_key_event_t *evt) {
    if (!evt) return false;

    /* First check BEFORE terminal buffer checks - these keys ALWAYS escape */
    uint32_t code = evt->code;
    uint16_t mods = evt->modifiers;

    /* C-x (Ctrl-X) - editor prefix, never goes to terminal */
    if ((mods & MOD_CTRL) && (code == 'X' || code == 'x')) {
        LOG_DEBUG("Terminal: C-x prefix -> editor");
        return false;
    }

    /* ESC (0x1B) - Meta prefix for M-x, M-<, M->, etc. */
    if (evt->type == KEY_CHAR && code == 0x1B && mods == 0) {
        LOG_DEBUG("Terminal: ESC prefix -> editor");
        return false;
    }

    /* Alt-x or Meta-x - direct M-x on modern terminals */
    if ((mods & (MOD_ALT | MOD_META)) && (code == 'x' || code == 'X')) {
        LOG_DEBUG("Terminal: Alt/Meta-x -> editor");
        return false;
    }

    /* Check if we're in terminal window */
    if (!(curwp->w_flag & WFTERM)) return false;
    if (!curbp || !(curbp->b_mode & MDTBUFFER) || !curbp->b_term_data) return false;

    terminal_state_t *ts = (terminal_state_t *)curbp->b_term_data;
    if (ts->fd < 0) {
        LOG_WARN("Terminal: PTY fd is closed, cannot handle key");
        return false;
    }

    LOG_DEBUGF("Terminal: Handling key code=0x%x mods=0x%x", evt->code, evt->modifiers);

    char buf[16];
    int len = 0;
    int c = (int)evt->code;
    bool has_ctrl = evt_has_ctrl(evt);

    if (c == '\r' || c == '\n' || (has_ctrl && c == 'M')) {
        buf[0] = '\r';
        len = 1;
    } else if (c == 127 || (has_ctrl && c == 'H')) {
        buf[0] = 127;
        len = 1;
    } else if (has_ctrl && c >= 'A' && c <= 'Z') {
        /* Control characters */
        buf[0] = (char)(c - 'A' + 1);
        len = 1;
    } else if (evt->type == KEY_SPECIAL) {
        /* Special keys - arrow keys, etc. */
        switch (evt->code) {
            case SPECIAL_UP:    memcpy(buf, "\033[A", 4); len = 3; break;
            case SPECIAL_DOWN:  memcpy(buf, "\033[B", 4); len = 3; break;
            case SPECIAL_RIGHT: memcpy(buf, "\033[C", 4); len = 3; break;
            case SPECIAL_LEFT:  memcpy(buf, "\033[D", 4); len = 3; break;
            case SPECIAL_HOME:  memcpy(buf, "\033[H", 4); len = 3; break;
            case SPECIAL_END:   memcpy(buf, "\033[F", 4); len = 3; break;
            default: break;
        }
    } else if (c >= 0 && c < 256) {
        buf[0] = (char)c;
        len = 1;
    }

    if (len > 0) {
        LOG_DEBUGF("Terminal: Writing %d bytes to PTY", len);
        pty_write(ts->fd, buf, len);
    }

    return true;
}

void terminal_resize(int new_rows, int new_cols) {
    LOG_INFOF("Terminal: terminal_resize() called, rows=%d, cols=%d", new_rows, new_cols);

    if (!terminal_window || !terminal_window->w_bufp) {
        LOG_DEBUG("Terminal: No terminal window to resize");
        return;
    }

    struct buffer *bp = terminal_window->w_bufp;
    if (!bp->b_term_data) {
        LOG_DEBUG("Terminal: No terminal state to resize");
        return;
    }

    terminal_state_t *ts = (terminal_state_t *)bp->b_term_data;
    int old_rows = ts->rows;
    int old_cols = ts->cols;
    ts->rows = new_rows;
    ts->cols = new_cols;

    if (ts->fd >= 0) {
        LOG_INFOF("Terminal: Resizing PTY from %dx%d to %dx%d",
                  old_cols, old_rows, new_cols, new_rows);
        pty_resize(ts->fd, new_rows, new_cols);
    }
}

/* No longer needed - use normal buffer display */
void terminal_render_window(struct window *wp) {
    (void)wp;
    /* This function is kept for API compatibility but does nothing.
     * Terminal buffers now render via normal display path since they
     * use standard buffer lines. */
}

/*
 * Get terminal PTY fd for polling
 */
int get_terminal_fd(void) {
    if (!terminal_window || !terminal_window->w_bufp) return -1;

    struct buffer *bp = terminal_window->w_bufp;
    if (!bp->b_term_data) return -1;

    terminal_state_t *ts = (terminal_state_t *)bp->b_term_data;
    return ts->fd;
}

/*
 * Check for terminal output and process it
 */
void check_terminal_output(void) {
    if (!terminal_window || !terminal_window->w_bufp) return;

    struct buffer *bp = terminal_window->w_bufp;
    if (!bp->b_term_data) return;

    terminal_state_t *ts = (terminal_state_t *)bp->b_term_data;
    if (ts->fd < 0) return;

    char buf[4096];
    ssize_t n = pty_read(ts->fd, buf, sizeof(buf));

    if (n > 0) {
        LOG_DEBUGF("Terminal: Read %zd bytes from PTY", n);
        int old_line_count = ts->line_count;
        terminal_process_output(bp, buf, (size_t)n);

        /* Update window view to show latest output */
        if (terminal_window && ts->current_line) {
            /* Scroll to bottom */
            terminal_window->w_dotp = ts->current_line;
            terminal_window->w_doto = ts->cur_col;

            /* Only do full refresh if new lines were added */
            if (ts->line_count != old_line_count) {
                /* Set top line to show recent output */
                int visible_rows = terminal_window->w_ntrows;
                struct line *lp = ts->current_line;
                for (int i = 0; i < visible_rows - 1 && lback(lp) != bp->b_linep; i++) {
                    lp = lback(lp);
                }
                terminal_window->w_linep = lp;
                terminal_window->w_flag |= WFHARD;
            } else {
                /* Just cursor moved or current line changed */
                terminal_window->w_flag |= WFEDIT;
            }
        }
    } else if (n < 0) {
        /* PTY closed - child exited */
        LOG_INFO("Terminal: PTY closed, child process exited");
        extern int terminal_close_on_exit;
        if (terminal_close_on_exit) {
            LOG_DEBUG("Terminal: close_on_exit enabled, closing terminal");
            mlwrite("[Shell exited]");
            terminal_close(0, 0);
        }
    }
}
