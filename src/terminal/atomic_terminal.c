/*
 * atomic_terminal.c - Signal-safe atomic terminal operations
 * Implements atomic terminal state management for μEmacs
 */

#include "μemacs/atomic_terminal.h"
#include "estruct.h"
#include "edef.h"
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Global atomic terminal state instance */
struct atomic_terminal_state terminal_state = {0};

/* Initialize atomic terminal state */
int atomic_terminal_init(void)
{
    /* Initialize all atomic fields */
    atomic_init(&terminal_state.cursor_row, 0);
    atomic_init(&terminal_state.cursor_col, 0);
    atomic_init(&terminal_state.screen_rows, 24);  /* Default size */
    atomic_init(&terminal_state.screen_cols, 80);
    atomic_init(&terminal_state.cursor_visible, true);
    atomic_init(&terminal_state.in_update, false);
    atomic_init(&terminal_state.update_generation, 1);
    atomic_init(&terminal_state.screen_dirty, true);
    
    /* Get actual terminal size */
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        atomic_set_screen_size(ws.ws_row, ws.ws_col);
    }
    
    return 0;
}

/* Atomic terminal resize - signal safe */
int atomic_terminal_resize(int new_rows, int new_cols)
{
    if (new_rows <= 0 || new_cols <= 0) {
        return -1;
    }
    
    /* Begin atomic update */
    if (!atomic_begin_update()) {
        return -1; /* Update already in progress */
    }
    
    /* Update screen size atomically */
    atomic_set_screen_size(new_rows, new_cols);
    
    /* Mark screen as dirty for full refresh */
    atomic_mark_screen_dirty();
    
    /* End atomic update */
    atomic_end_update();
    
    return 0;
}

/* Atomic cursor movement - signal safe */
int atomic_terminal_move_cursor(int row, int col)
{
    int max_rows, max_cols;
    atomic_get_screen_size(&max_rows, &max_cols);
    
    /* Bounds checking */
    if (row < 0 || row >= max_rows || col < 0 || col >= max_cols) {
        return -1;
    }
    
    /* Check if we can update (not during another update) */
    if (atomic_is_updating()) {
        return -1;
    }
    
    /* Update cursor position atomically */
    atomic_set_cursor(row, col);
    
    return 0;
}

/* Atomic screen clear - signal safe */
int atomic_terminal_clear_screen(void)
{
    /* Begin atomic update */
    if (!atomic_begin_update()) {
        return -1;
    }
    
    /* Reset cursor to top-left */
    atomic_set_cursor(0, 0);
    
    /* Mark entire screen as dirty */
    atomic_mark_screen_dirty();
    
    /* End atomic update */
    atomic_end_update();
    
    return 0;
}