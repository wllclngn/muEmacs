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
#include "string_utils.h"
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

    /* Multi-terminal tracking */
    char name[64];               /* Terminal name (e.g., "shell", "build") */
} terminal_state_t;

/* --- Terminal Registry (Multi-Terminal Support) --- */

/* Registry of active terminal buffers */
static struct buffer *terminal_registry[MAX_TERMINALS] = {0};

/* Find slot for terminal buffer */
static int terminal_registry_add(struct buffer *bp) {
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminal_registry[i] == nullptr) {
            terminal_registry[i] = bp;
            LOG_DEBUGF("Terminal: Registered buffer '%s' in slot %d", bp->b_bname, i);
            return i;
        }
    }
    return -1;  /* Registry full */
}

/* Remove terminal from registry */
static void terminal_registry_remove(struct buffer *bp) {
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminal_registry[i] == bp) {
            terminal_registry[i] = nullptr;
            LOG_DEBUGF("Terminal: Unregistered buffer '%s' from slot %d", bp->b_bname, i);
            return;
        }
    }
}

/* Count active terminals */
int terminal_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminal_registry[i] != nullptr) count++;
    }
    return count;
}

/* Find terminal buffer by name suffix (e.g., "shell" matches "*Terminal:shell*") */
struct buffer *terminal_find(const char *name) {
    char full_name[80];

    /* Build expected buffer name */
    if (name == nullptr || name[0] == '\0') {
        snprintf(full_name, sizeof(full_name), "*Terminal*");
    } else {
        snprintf(full_name, sizeof(full_name), "*Terminal:%s*", name);
    }

    /* Search registry */
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminal_registry[i] != nullptr) {
            if (strcmp(terminal_registry[i]->b_bname, full_name) == 0) {
                return terminal_registry[i];
            }
        }
    }

    /* Also search by exact match on full name */
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminal_registry[i] != nullptr) {
            if (strcmp(terminal_registry[i]->b_bname, name) == 0) {
                return terminal_registry[i];
            }
        }
    }

    return nullptr;
}

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

    /* Register in terminal registry */
    terminal_registry_add(bp);

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

        /* Remove from registry */
        terminal_registry_remove(bp);
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

/*
 * terminal_send_line - Send current line to terminal
 * Bound to C-x C-t
 *
 * Sends the current line content to the terminal PTY, followed by Enter.
 * Useful for REPL interaction.
 */
int terminal_send_line(int f, int n) {
    (void)f; (void)n;

    /* Check if terminal is open */
    if (!terminal_window || !terminal_window->w_bufp) {
        mlwrite("[No terminal open]");
        return false;
    }

    struct buffer *term_bp = terminal_window->w_bufp;
    if (!term_bp->b_term_data) {
        mlwrite("[Terminal not active]");
        return false;
    }

    terminal_state_t *ts = (terminal_state_t *)term_bp->b_term_data;
    if (ts->fd < 0) {
        mlwrite("[Terminal PTY closed]");
        return false;
    }

    /* Don't send from terminal buffer itself */
    if (curbp == term_bp) {
        mlwrite("[Cannot send from terminal buffer]");
        return false;
    }

    /* Get current line content */
    struct line *lp = curwp->w_dotp;
    int len = llength(lp);

    /* Send line to terminal using gap buffer */
    if (len > 0) {
        char *text = malloc(len + 1);
        if (text) {
            gap_buffer_get_text(lp->gb, 0, len, text, len);
            pty_write(ts->fd, text, len);
            free(text);
        }
    }
    /* Send newline (Enter) */
    pty_write(ts->fd, "\r", 1);

    LOG_INFOF("Terminal: Sent %d chars + Enter to PTY", len);
    mlwrite("[Sent line to terminal]");

    /* Move to next line */
    cursor_down(false, 1);

    return true;
}

/*
 * terminal_send_region - Send region to terminal
 * Bound to C-x T
 *
 * Sends the marked region to the terminal PTY.
 * Each line is sent followed by Enter.
 */
int terminal_send_region(int f, int n) {
    (void)f; (void)n;

    /* Check if terminal is open */
    if (!terminal_window || !terminal_window->w_bufp) {
        mlwrite("[No terminal open]");
        return false;
    }

    struct buffer *term_bp = terminal_window->w_bufp;
    if (!term_bp->b_term_data) {
        mlwrite("[Terminal not active]");
        return false;
    }

    terminal_state_t *ts = (terminal_state_t *)term_bp->b_term_data;
    if (ts->fd < 0) {
        mlwrite("[Terminal PTY closed]");
        return false;
    }

    /* Don't send from terminal buffer itself */
    if (curbp == term_bp) {
        mlwrite("[Cannot send from terminal buffer]");
        return false;
    }

    /* Get region bounds */
    struct region reg;
    if (getregion(&reg) != true) {
        mlwrite("[No region defined]");
        return false;
    }

    /* Send region content line by line */
    struct line *lp = reg.r_linep;
    int offset = reg.r_offset;
    long remaining = reg.r_size;
    int lines_sent = 0;

    while (remaining > 0 && lp != curbp->b_linep) {
        int line_len = llength(lp);

        /* Calculate how much of this line to send */
        int start = (lp == reg.r_linep) ? offset : 0;
        int avail = line_len - start;
        int to_send = (avail < remaining) ? avail : (int)remaining;

        /* Send the line portion using gap buffer */
        if (to_send > 0) {
            char *text = malloc(to_send + 1);
            if (text) {
                gap_buffer_get_text(lp->gb, start, to_send, text, to_send);
                pty_write(ts->fd, text, to_send);
                free(text);
            }
        }

        remaining -= to_send;

        /* If not at end of line, we're done */
        if (start + to_send < line_len) {
            break;
        }

        /* Send newline and move to next line */
        if (remaining > 0) {
            pty_write(ts->fd, "\r", 1);
            remaining--;  /* Account for newline */
            lp = lforw(lp);
            lines_sent++;
        }
    }

    LOG_INFOF("Terminal: Sent region (%ld bytes, %d lines) to PTY",
              reg.r_size, lines_sent + 1);
    mlwrite("[Sent region to terminal: %ld bytes]", reg.r_size);

    return true;
}

/* ============================================================
 * Multi-Terminal Support (Phase 2)
 * ============================================================ */

/*
 * terminal_create_named - Create a terminal with a specific name
 *
 * Internal function that creates a named terminal buffer.
 * Buffer name format: "*Terminal:name*" or "*Terminal*" if name is empty.
 */
int terminal_create_named(const char *name) {
    LOG_INFOF("Terminal: terminal_create_named('%s') called", name ? name : "");

    /* Check if terminal feature is enabled */
    if (!terminal_enabled) {
        LOG_WARN("Terminal: Feature disabled in settings");
        mlwrite("Terminal feature is disabled in settings");
        return false;
    }

    /* Check registry capacity */
    if (terminal_count() >= MAX_TERMINALS) {
        mlwrite("[Maximum %d terminals already open]", MAX_TERMINALS);
        return false;
    }

    /* Build buffer name */
    char bufname[80];
    if (name == nullptr || name[0] == '\0') {
        snprintf(bufname, sizeof(bufname), "*Terminal*");
    } else {
        snprintf(bufname, sizeof(bufname), "*Terminal:%s*", name);
    }

    /* Check if terminal with this name already exists */
    struct buffer *existing = bfind(bufname, false, 0);
    if (existing && existing->b_term_data) {
        /* Switch to existing terminal instead */
        LOG_DEBUGF("Terminal: '%s' already exists, switching to it", bufname);
        if (swbuffer(existing) == true) {
            mlwrite("[Switched to %s]", bufname);
            return true;
        }
        return false;
    }

    /* Create terminal buffer */
    LOG_DEBUGF("Terminal: Creating buffer '%s'", bufname);
    struct buffer *bp = bfind(bufname, true, 0);
    if (!bp) {
        LOG_ERROR("Terminal: Failed to create buffer");
        mlwrite("Cannot create terminal buffer");
        return false;
    }
    bp->b_mode |= MDTBUFFER;

    /* Calculate window height */
    int height = (term.t_nrow * terminal_height_percent) / 100;
    if (height < 3) height = 3;
    if (height > term.t_nrow - 5) height = term.t_nrow - 5;

    /* Create split window at bottom */
    struct window *term_wp = terminal_split_bottom(height);
    if (!term_wp) {
        LOG_ERROR("Terminal: Failed to create split window");
        mlwrite("Cannot create terminal window");
        bp->b_mode &= ~MDTBUFFER;
        return false;
    }

    /* Link buffer to window */
    term_wp->w_bufp = bp;
    bp->b_nwnd++;
    term_wp->w_dotp = bp->b_linep;
    term_wp->w_doto = 0;
    term_wp->w_linep = lforw(bp->b_linep);
    term_wp->w_flag |= WFTERM | WFMODE | WFHARD;

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

    /* Store terminal name */
    if (name && name[0]) {
        snprintf(ts->name, sizeof(ts->name), "%s", name);
    } else {
        ts->name[0] = '\0';
    }

    /* Create initial line */
    ts->current_line = term_create_line(bp);
    if (ts->current_line) {
        ts->line_count = 1;
        bp->b_dotp = ts->current_line;
        bp->b_doto = 0;
        term_wp->w_dotp = ts->current_line;
    }

    /* Spawn PTY */
    const char *shell = terminal_shell[0] ? terminal_shell : nullptr;
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

    bp->b_term_data = ts;
    terminal_window = term_wp;

    /* Register in terminal registry */
    terminal_registry_add(bp);

    /* Switch focus to terminal */
    curwp = term_wp;
    curbp = bp;

    LOG_INFOF("Terminal: Successfully created '%s'", bufname);
    mlwrite("[Terminal: %s]", bufname);
    return true;
}

/*
 * terminal_create - Create a named terminal (M-x: terminal-create)
 *
 * Prompts for a name and creates a new terminal.
 * If name is empty, creates default *Terminal*.
 */
int terminal_create(int f, int n) {
    (void)f; (void)n;

    char name[64] = "";
    int status = minibuf_read("Terminal name: ", name, sizeof(name));

    /* User cancelled */
    if (status == ABORT) {
        return ABORT;
    }

    return terminal_create_named(name);
}

/*
 * terminal_switch - Switch to a terminal (M-x: terminal-switch)
 *
 * Lists available terminals and switches to selected one.
 */
int terminal_switch(int f, int n) {
    (void)f; (void)n;

    int count = terminal_count();
    if (count == 0) {
        mlwrite("[No terminals open]");
        return false;
    }

    /* Build list of terminal names */
    char prompt[256] = "Switch to terminal (";
    int first = 1;
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminal_registry[i] != nullptr) {
            if (!first) {
                strncat(prompt, ", ", sizeof(prompt) - strlen(prompt) - 1);
            }
            terminal_state_t *ts = (terminal_state_t *)terminal_registry[i]->b_term_data;
            const char *tname = (ts && ts->name[0]) ? ts->name : "default";
            strncat(prompt, tname, sizeof(prompt) - strlen(prompt) - 1);
            first = 0;
        }
    }
    strncat(prompt, "): ", sizeof(prompt) - strlen(prompt) - 1);

    char name[64] = "";
    int status = minibuf_read(prompt, name, sizeof(name));
    if (status == ABORT || status == false) {
        return status == ABORT ? ABORT : false;
    }

    /* Find matching terminal */
    struct buffer *bp = terminal_find(name);
    if (!bp) {
        /* Try "default" as alias for empty name */
        if (strcmp(name, "default") == 0) {
            bp = terminal_find("");
        }
    }

    if (!bp) {
        mlwrite("[No terminal '%s' found]", name);
        return false;
    }

    /* Switch to terminal buffer */
    if (swbuffer(bp) == true) {
        mlwrite("[Switched to %s]", bp->b_bname);
        return true;
    }

    return false;
}

/*
 * terminal_list - List all active terminals (M-x: terminal-list)
 *
 * Creates a buffer showing all active terminals with their status.
 */
int terminal_list(int f, int n) {
    (void)f; (void)n;

    int count = terminal_count();
    if (count == 0) {
        mlwrite("[No terminals open]");
        return true;
    }

    /* Create or find list buffer */
    struct buffer *bp = bfind("*Terminal List*", true, 0);
    if (!bp) {
        mlwrite("Cannot create list buffer");
        return false;
    }

    /* Clear existing content */
    if (bclear(bp) != true) {
        return false;
    }

    /* Use blistp pattern for addline() - save and restore */
    extern struct buffer *blistp;
    struct buffer *saved_blistp = blistp;
    blistp = bp;

    /* Add header */
    addline("Active Terminals:");
    addline("=================");
    addline("");

    /* List each terminal */
    char line[256];
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminal_registry[i] != nullptr) {
            struct buffer *tbp = terminal_registry[i];
            terminal_state_t *ts = (terminal_state_t *)tbp->b_term_data;

            const char *name = (ts && ts->name[0]) ? ts->name : "(default)";
            const char *status = (ts && ts->fd >= 0) ? "running" : "stopped";
            int pid = ts ? (int)ts->pid : 0;

            snprintf(line, sizeof(line), "  %-20s  %-10s  pid=%d  %s",
                     tbp->b_bname, name, pid, status);
            addline(line);
        }
    }

    addline("");
    snprintf(line, sizeof(line), "Total: %d terminal(s)", count);
    addline(line);

    /* Restore blistp */
    blistp = saved_blistp;

    /* Show the buffer */
    bp->b_dotp = lforw(bp->b_linep);
    bp->b_doto = 0;

    if (swbuffer(bp) == true) {
        return true;
    }

    return false;
}

/*
 * terminal_send_buffer - Send entire buffer to terminal
 * Bound to C-x C-t (suggested)
 *
 * Sends the entire current buffer to the terminal PTY.
 * Useful for sending scripts to REPLs.
 */
int terminal_send_buffer(int f, int n) {
    (void)f; (void)n;

    /* Check if terminal is open */
    if (!terminal_window || !terminal_window->w_bufp) {
        mlwrite("[No terminal open]");
        return false;
    }

    struct buffer *term_bp = terminal_window->w_bufp;
    if (!term_bp->b_term_data) {
        mlwrite("[Terminal not active]");
        return false;
    }

    terminal_state_t *ts = (terminal_state_t *)term_bp->b_term_data;
    if (ts->fd < 0) {
        mlwrite("[Terminal PTY closed]");
        return false;
    }

    /* Don't send from terminal buffer itself */
    if (curbp == term_bp) {
        mlwrite("[Cannot send from terminal buffer]");
        return false;
    }

    /* Send buffer content line by line */
    struct line *lp;
    int lines_sent = 0;
    long bytes_sent = 0;

    for (lp = lforw(curbp->b_linep); lp != curbp->b_linep; lp = lforw(lp)) {
        int len = llength(lp);

        if (len > 0) {
            char *text = malloc(len + 1);
            if (text) {
                gap_buffer_get_text(lp->gb, 0, len, text, len);
                pty_write(ts->fd, text, len);
                bytes_sent += len;
                free(text);
            }
        }

        /* Send newline after each line */
        pty_write(ts->fd, "\r", 1);
        bytes_sent++;
        lines_sent++;
    }

    LOG_INFOF("Terminal: Sent buffer (%ld bytes, %d lines) to PTY",
              bytes_sent, lines_sent);
    mlwrite("[Sent buffer to terminal: %d lines]", lines_sent);

    return true;
}

/*
 * terminal_capture - Capture terminal output to current buffer
 *
 * With numeric argument: capture last N lines
 * Without argument: capture all visible terminal output
 */
int terminal_capture(int f, int n) {
    /* Check if terminal is open */
    if (!terminal_window || !terminal_window->w_bufp) {
        mlwrite("[No terminal open]");
        return false;
    }

    struct buffer *term_bp = terminal_window->w_bufp;

    /* Don't capture into terminal buffer itself */
    if (curbp == term_bp) {
        mlwrite("[Switch to a different buffer first]");
        return false;
    }

    /* Determine how many lines to capture */
    int lines_to_capture = f ? n : 0;  /* 0 = all */

    /* Count lines in terminal buffer */
    int total_lines = 0;
    struct line *lp;
    for (lp = lforw(term_bp->b_linep); lp != term_bp->b_linep; lp = lforw(lp)) {
        total_lines++;
    }

    if (total_lines == 0) {
        mlwrite("[Terminal buffer is empty]");
        return true;
    }

    /* If capturing specific count, find starting line */
    int skip_lines = 0;
    if (lines_to_capture > 0 && lines_to_capture < total_lines) {
        skip_lines = total_lines - lines_to_capture;
    } else {
        lines_to_capture = total_lines;
    }

    /* Find starting line */
    lp = lforw(term_bp->b_linep);
    for (int i = 0; i < skip_lines && lp != term_bp->b_linep; i++) {
        lp = lforw(lp);
    }

    /* Insert captured lines at current position */
    int captured = 0;
    while (lp != term_bp->b_linep && captured < lines_to_capture) {
        int len = llength(lp);

        if (len > 0) {
            char *text = malloc(len + 1);
            if (text) {
                gap_buffer_get_text(lp->gb, 0, len, text, len);
                /* Insert text character by character */
                for (int i = 0; i < len; i++) {
                    linsert(1, text[i]);
                }
                free(text);
            }
        }

        /* Insert newline */
        lnewline();
        captured++;
        lp = lforw(lp);
    }

    mlwrite("[Captured %d lines from terminal]", captured);
    return true;
}

/* ============================================================
 * REPL Integration (Phase 3)
 * ============================================================ */

/* REPL configuration - maps filetypes to REPL commands */
typedef struct {
    const char *filetype;
    const char *command;
} repl_config_t;

static const repl_config_t repl_commands[] = {
    {"python", "python3 -i"},
    {"py",     "python3 -i"},
    {"lua",    "lua -i"},
    {"ruby",   "irb"},
    {"rb",     "irb"},
    {"node",   "node"},
    {"js",     "node"},
    {"javascript", "node"},
    {"ts",     "npx ts-node"},
    {"typescript", "npx ts-node"},
    {"haskell", "ghci"},
    {"hs",     "ghci"},
    {"lisp",   "sbcl"},
    {"cl",     "sbcl"},
    {"scheme", "guile"},
    {"scm",    "guile"},
    {"clojure", "clj"},
    {"clj",    "clj"},
    {"elixir", "iex"},
    {"ex",     "iex"},
    {"erlang", "erl"},
    {"erl",    "erl"},
    {"r",      "R --interactive"},
    {"julia",  "julia"},
    {"jl",     "julia"},
    {"php",    "php -a"},
    {"perl",   "perl -de0"},
    {"pl",     "perl -de0"},
    {"sh",     "bash"},
    {"bash",   "bash"},
    {"zsh",    "zsh"},
    {"fish",   "fish"},
    {NULL,     NULL}
};

/*
 * Detect filetype from current buffer's filename extension
 */
static const char *detect_filetype(void) {
    if (!curbp || !curbp->b_fname[0]) {
        return NULL;
    }

    const char *ext = strrchr(curbp->b_fname, '.');
    if (!ext || !ext[1]) {
        return NULL;
    }

    return ext + 1;  /* Skip the dot */
}

/*
 * Find REPL command for given filetype
 */
static const char *find_repl_command(const char *filetype) {
    if (!filetype) return NULL;

    for (int i = 0; repl_commands[i].filetype != NULL; i++) {
        if (strcasecmp(repl_commands[i].filetype, filetype) == 0) {
            return repl_commands[i].command;
        }
    }

    return NULL;
}

/*
 * repl_start - Start a REPL for the current file's language
 *
 * Detects filetype from filename extension and starts appropriate REPL.
 * If no filetype detected, prompts for REPL command.
 */
int repl_start(int f, int n) {
    (void)f; (void)n;

    const char *filetype = detect_filetype();
    const char *repl_cmd = find_repl_command(filetype);
    char cmd[256];

    if (repl_cmd) {
        snprintf(cmd, sizeof(cmd), "%s", repl_cmd);
        mlwrite("[Starting %s REPL: %s]", filetype, cmd);
    } else {
        /* Prompt for REPL command */
        char input[256] = "";
        int status = minibuf_read("REPL command: ", input, sizeof(input));
        if (status == ABORT || status == false || input[0] == '\0') {
            return status == ABORT ? ABORT : false;
        }
        snprintf(cmd, sizeof(cmd), "%s", input);
    }

    /* Create terminal named "repl" */
    return terminal_create_named("repl");
}

/*
 * repl_eval_line - Send current line to REPL
 *
 * Wrapper around terminal_send_line that ensures a REPL terminal exists.
 */
int repl_eval_line(int f, int n) {
    /* Check if any terminal is open */
    if (!terminal_window) {
        mlwrite("[No terminal/REPL open - use M-x repl-start or M-x term]");
        return false;
    }

    return terminal_send_line(f, n);
}

/*
 * repl_eval_region - Send region to REPL
 *
 * Wrapper around terminal_send_region that ensures a REPL terminal exists.
 */
int repl_eval_region(int f, int n) {
    /* Check if any terminal is open */
    if (!terminal_window) {
        mlwrite("[No terminal/REPL open - use M-x repl-start or M-x term]");
        return false;
    }

    return terminal_send_region(f, n);
}

/*
 * repl_eval_buffer - Send entire buffer to REPL
 *
 * Wrapper around terminal_send_buffer that ensures a REPL terminal exists.
 */
int repl_eval_buffer(int f, int n) {
    /* Check if any terminal is open */
    if (!terminal_window) {
        mlwrite("[No terminal/REPL open - use M-x repl-start or M-x term]");
        return false;
    }

    return terminal_send_buffer(f, n);
}

/* ============================================================
 * Terminal Window Controls (Phase 4)
 * ============================================================ */

/*
 * terminal_toggle - Show/hide terminal window
 *
 * If terminal is visible, close it (but keep PTY running).
 * If terminal is hidden, show it.
 */
int terminal_toggle(int f, int n) {
    (void)f; (void)n;

    if (terminal_window) {
        /* Terminal is visible - close the window but keep buffer */
        struct buffer *bp = terminal_window->w_bufp;

        /* Remove terminal window */
        struct window *wp = wheadp;
        struct window *prev = NULL;

        while (wp && wp != terminal_window) {
            prev = wp;
            wp = wp->w_wndp;
        }

        if (wp == terminal_window) {
            if (prev) {
                prev->w_wndp = wp->w_wndp;
                int reclaimed = wp->w_ntrows + 1;
                prev->w_ntrows += reclaimed;
                prev->w_flag |= WFMODE | WFHARD;
            } else {
                wheadp = wp->w_wndp;
            }

            if (curwp == terminal_window) {
                curwp = prev ? prev : wheadp;
                curbp = curwp->w_bufp;
            }

            /* Decrement window count but keep buffer */
            if (bp) bp->b_nwnd--;

            SAFE_FREE(wp);
        }

        terminal_window = NULL;
        sgarbf = true;
        mlwrite("[Terminal hidden]");
        return true;
    } else {
        /* Terminal is hidden - find and show terminal buffer */
        struct buffer *bp = NULL;

        /* Look for any terminal buffer in registry */
        for (int i = 0; i < MAX_TERMINALS; i++) {
            if (terminal_registry[i] != NULL) {
                bp = terminal_registry[i];
                break;
            }
        }

        if (!bp) {
            /* No terminal exists - create one */
            return terminal_open_split(f, n);
        }

        /* Create window for existing terminal buffer */
        int height = (term.t_nrow * terminal_height_percent) / 100;
        if (height < 3) height = 3;
        if (height > term.t_nrow - 5) height = term.t_nrow - 5;

        struct window *term_wp = terminal_split_bottom(height);
        if (!term_wp) {
            mlwrite("Cannot create terminal window");
            return false;
        }

        term_wp->w_bufp = bp;
        bp->b_nwnd++;
        term_wp->w_dotp = bp->b_dotp;
        term_wp->w_doto = bp->b_doto;
        term_wp->w_linep = lforw(bp->b_linep);
        term_wp->w_flag |= WFTERM | WFMODE | WFHARD;

        terminal_window = term_wp;
        curwp = term_wp;
        curbp = bp;

        sgarbf = true;
        mlwrite("[Terminal shown]");
        return true;
    }
}

/* ============================================================
 * Build Integration (Phase 3)
 * ============================================================ */

/* Build configuration - detects build system from project files */
typedef struct {
    const char *marker_file;
    const char *command;
} build_config_t;

static const build_config_t build_commands[] = {
    {"Cargo.toml",     "cargo build"},
    {"package.json",   "npm run build"},
    {"Makefile",       "make"},
    {"makefile",       "make"},
    {"GNUmakefile",    "make"},
    {"CMakeLists.txt", "cmake --build build"},
    {"meson.build",    "meson compile -C build"},
    {"build.gradle",   "./gradlew build"},
    {"pom.xml",        "mvn compile"},
    {"go.mod",         "go build"},
    {"build.zig",      "zig build"},
    {"dune-project",   "dune build"},
    {"stack.yaml",     "stack build"},
    {"*.cabal",        "cabal build"},
    {"setup.py",       "python setup.py build"},
    {"pyproject.toml", "python -m build"},
    {NULL,             NULL}
};

/* Error location storage */
typedef struct {
    char filename[NFILEN];
    int line;
    int col;
} error_location_t;

#define MAX_ERRORS 256
static error_location_t error_list[MAX_ERRORS];
static int error_count = 0;
static int current_error = -1;

/*
 * Detect build command based on project files in current directory
 */
static const char *detect_build_command(void) {
    /* Get directory from current file or use cwd */
    char dir[NFILEN] = ".";
    if (curbp && curbp->b_fname[0]) {
        safe_strcpy(dir, curbp->b_fname, sizeof(dir));
        char *slash = strrchr(dir, '/');
        if (slash) *slash = '\0';
        else safe_strcpy(dir, ".", sizeof(dir));
    }

    /* Check for each build marker file */
    for (int i = 0; build_commands[i].marker_file != NULL; i++) {
        char path[NFILEN * 2];  /* Extra space for dir + marker */
        snprintf(path, sizeof(path), "%s/%s", dir, build_commands[i].marker_file);

        if (access(path, F_OK) == 0) {
            return build_commands[i].command;
        }
    }

    return "make";  /* Default fallback */
}

/*
 * Parse GCC/Clang style error: "file:line:col: error: message"
 */
static int parse_error_line(const char *line, error_location_t *err) {
    /* Pattern: filename:line:col: */
    const char *p = line;

    /* Skip whitespace */
    while (*p && (*p == ' ' || *p == '\t')) p++;

    /* Find first colon (after filename) */
    const char *colon1 = strchr(p, ':');
    if (!colon1 || colon1 == p) return 0;

    /* Copy filename */
    size_t fname_len = colon1 - p;
    if (fname_len >= NFILEN) fname_len = NFILEN - 1;
    memcpy(err->filename, p, fname_len);
    err->filename[fname_len] = '\0';

    /* Parse line number */
    const char *colon2 = strchr(colon1 + 1, ':');
    if (!colon2) return 0;

    err->line = atoi(colon1 + 1);
    if (err->line <= 0) return 0;

    /* Parse column (optional) */
    err->col = atoi(colon2 + 1);
    if (err->col <= 0) err->col = 0;

    return 1;
}

/*
 * Parse Rust-style error: " --> file:line:col"
 */
static int parse_rust_error(const char *line, error_location_t *err) {
    const char *arrow = strstr(line, " --> ");
    if (!arrow) return 0;

    return parse_error_line(arrow + 5, err);
}

/*
 * Scan terminal buffer for error locations
 */
static void scan_build_errors(void) {
    error_count = 0;
    current_error = -1;

    /* Find first terminal buffer */
    struct buffer *bp = NULL;
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminal_registry[i] != NULL) {
            bp = terminal_registry[i];
            break;
        }
    }

    if (!bp) return;

    /* Scan each line */
    struct line *lp;
    for (lp = lforw(bp->b_linep); lp != bp->b_linep && error_count < MAX_ERRORS; lp = lforw(lp)) {
        int len = llength(lp);
        if (len == 0) continue;

        char *text = malloc(len + 1);
        if (!text) continue;

        gap_buffer_get_text(lp->gb, 0, len, text, len);
        text[len] = '\0';

        error_location_t err;

        /* Try different error formats */
        if (parse_error_line(text, &err) ||
            parse_rust_error(text, &err)) {
            /* Only add if file exists */
            if (access(err.filename, F_OK) == 0) {
                error_list[error_count++] = err;
            }
        }

        free(text);
    }
}

/*
 * build_run - Run build command and capture output
 *
 * Detects build system and runs appropriate command in terminal.
 * Parses output for error locations.
 */
int build_run(int f, int n) {
    (void)f; (void)n;

    const char *cmd = detect_build_command();

    /* Prompt to confirm or modify command */
    char build_cmd[256];
    safe_strcpy(build_cmd, cmd, sizeof(build_cmd));

    int status = minibuf_read("Build: ", build_cmd, sizeof(build_cmd));
    if (status == ABORT) return ABORT;
    if (status == false || build_cmd[0] == '\0') {
        safe_strcpy(build_cmd, cmd, sizeof(build_cmd));
    }

    /* Create or switch to build terminal */
    struct buffer *bp = terminal_find("build");
    if (!bp) {
        terminal_create_named("build");
        bp = terminal_find("build");
    }

    if (!bp || !bp->b_term_data) {
        mlwrite("[Cannot create build terminal]");
        return false;
    }

    /* Send build command to terminal */
    terminal_state_t *ts = (terminal_state_t *)bp->b_term_data;
    if (ts->fd >= 0) {
        pty_write(ts->fd, build_cmd, strlen(build_cmd));
        pty_write(ts->fd, "\r", 1);
    }

    mlwrite("[Building: %s]", build_cmd);

    /* Note: Error parsing happens when user calls build-next-error */
    return true;
}

/*
 * build_next_error - Jump to next error location
 */
int build_next_error(int f, int n) {
    (void)f; (void)n;

    /* Rescan errors if first time or at end */
    if (current_error < 0 || current_error >= error_count - 1) {
        scan_build_errors();
    }

    if (error_count == 0) {
        mlwrite("[No errors found]");
        return false;
    }

    current_error++;
    if (current_error >= error_count) {
        current_error = 0;  /* Wrap around */
    }

    error_location_t *err = &error_list[current_error];

    /* Open file and go to line */
    if (getfile(err->filename, true) != true) {
        mlwrite("[Cannot open %s]", err->filename);
        return false;
    }

    /* Go to line */
    gotoline(true, err->line);

    /* Go to column if specified */
    if (err->col > 0) {
        goto_line_start(false, 1);
        move_char_forward(true, err->col - 1);
    }

    mlwrite("[Error %d/%d: %s:%d]", current_error + 1, error_count,
            err->filename, err->line);
    return true;
}

/*
 * build_prev_error - Jump to previous error location
 */
int build_prev_error(int f, int n) {
    (void)f; (void)n;

    if (error_count == 0) {
        scan_build_errors();
    }

    if (error_count == 0) {
        mlwrite("[No errors found]");
        return false;
    }

    current_error--;
    if (current_error < 0) {
        current_error = error_count - 1;  /* Wrap around */
    }

    error_location_t *err = &error_list[current_error];

    /* Open file and go to line */
    if (getfile(err->filename, true) != true) {
        mlwrite("[Cannot open %s]", err->filename);
        return false;
    }

    /* Go to line */
    gotoline(true, err->line);

    /* Go to column if specified */
    if (err->col > 0) {
        goto_line_start(false, 1);
        move_char_forward(true, err->col - 1);
    }

    mlwrite("[Error %d/%d: %s:%d]", current_error + 1, error_count,
            err->filename, err->line);
    return true;
}
