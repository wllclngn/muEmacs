/*
 * term_grid.h - VT100 Cell Grid for Terminal Emulator
 *
 * Provides a true cell grid (rows x cols) for fullscreen terminal applications.
 * Used by the dual-mode terminal: line mode for normal scrollback, grid mode
 * when programs request alternate screen buffer (ESC[?1049h).
 *
 * C23 compliant
 */

#ifndef TERM_GRID_H
#define TERM_GRID_H

#include <stdint.h>
#include <stdbool.h>

/* Cell attribute flags */
#define TATTR_BOLD      0x0001
#define TATTR_DIM       0x0002
#define TATTR_ITALIC    0x0004
#define TATTR_UNDERLINE 0x0008
#define TATTR_BLINK     0x0010
#define TATTR_REVERSE   0x0020
#define TATTR_HIDDEN    0x0040
#define TATTR_STRIKE    0x0080

/* Single terminal cell */
typedef struct {
    uint32_t ch;               /* Unicode codepoint (0 = space) */
    uint8_t fg_r, fg_g, fg_b; /* Foreground RGB */
    uint8_t bg_r, bg_g, bg_b; /* Background RGB */
    uint16_t attrs;            /* TATTR_* flags */
    bool fg_default;           /* Use terminal default fg */
    bool bg_default;           /* Use terminal default bg */
} term_cell_t;

/* Terminal cell grid */
typedef struct {
    int rows, cols;
    term_cell_t *cells;        /* Contiguous [rows * cols] block */
    int cursor_row, cursor_col;
    term_cell_t pen;           /* Current SGR attributes for new chars */
    int scroll_top;            /* Scroll region top (0-based, inclusive) */
    int scroll_bottom;         /* Scroll region bottom (0-based, inclusive) */
    int saved_cursor_row, saved_cursor_col;
    term_cell_t saved_pen;
    bool *dirty_rows;          /* [rows] dirty tracking */
    bool origin_mode;          /* DECOM */
    bool autowrap;             /* DECAWM - default true */
    bool cursor_visible;       /* DECTCEM - default true */
    bool wrap_pending;         /* Deferred wrap at right margin */
} term_grid_t;

/* Grid lifecycle */
term_grid_t *term_grid_alloc(int rows, int cols);
void term_grid_free(term_grid_t *grid);
void term_grid_resize(term_grid_t *grid, int new_rows, int new_cols);
void term_grid_clear(term_grid_t *grid);

/* Inline cell accessor for hot path */
static inline term_cell_t *term_grid_cell(term_grid_t *g, int row, int col) {
    return &g->cells[row * g->cols + col];
}

/* Default cell (space with default colors) */
static inline term_cell_t term_cell_default(void) {
    return (term_cell_t){ .ch = ' ', .fg_default = true, .bg_default = true };
}

/* Grid operations */
void term_grid_put_char(term_grid_t *grid, uint32_t ch);
void term_grid_newline(term_grid_t *grid);
void term_grid_cr(term_grid_t *grid);
void term_grid_move_cursor(term_grid_t *grid, int row, int col);
void term_grid_scroll_up(term_grid_t *grid, int n);
void term_grid_scroll_down(term_grid_t *grid, int n);
void term_grid_erase_display(term_grid_t *grid, int mode);
void term_grid_erase_line(term_grid_t *grid, int mode);
void term_grid_insert_lines(term_grid_t *grid, int n);
void term_grid_delete_lines(term_grid_t *grid, int n);
void term_grid_insert_chars(term_grid_t *grid, int n);
void term_grid_delete_chars(term_grid_t *grid, int n);
void term_grid_erase_chars(term_grid_t *grid, int n);
void term_grid_set_scroll_region(term_grid_t *grid, int top, int bottom);
void term_grid_mark_dirty(term_grid_t *grid, int row);
void term_grid_mark_all_dirty(term_grid_t *grid);

#endif /* TERM_GRID_H */
