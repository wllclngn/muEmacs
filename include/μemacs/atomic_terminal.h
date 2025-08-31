/*
 * atomic_terminal.h - Signal-safe atomic terminal state management
 * Provides atomic operations for terminal display state to prevent corruption
 */

#ifndef ATOMIC_TERMINAL_H
#define ATOMIC_TERMINAL_H

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>

/* Atomic terminal state structure */
struct atomic_terminal_state {
    _Atomic int cursor_row;         /* Current cursor row */
    _Atomic int cursor_col;         /* Current cursor column */
    _Atomic int screen_rows;        /* Terminal height */
    _Atomic int screen_cols;        /* Terminal width */
    _Atomic bool cursor_visible;    /* Cursor visibility state */
    _Atomic bool in_update;         /* Update in progress flag */
    _Atomic uint32_t update_generation; /* Update sequence number */
    _Atomic bool screen_dirty;      /* Screen needs refresh */
};

/* Global atomic terminal state */
extern struct atomic_terminal_state terminal_state;

/* Initialize atomic terminal state */
int atomic_terminal_init(void);

/* Atomic cursor position operations */
static inline void atomic_set_cursor(int row, int col)
{
    atomic_store_explicit(&terminal_state.cursor_row, row, memory_order_release);
    atomic_store_explicit(&terminal_state.cursor_col, col, memory_order_release);
}

static inline void atomic_get_cursor(int *row, int *col)
{
    *row = atomic_load_explicit(&terminal_state.cursor_row, memory_order_acquire);
    *col = atomic_load_explicit(&terminal_state.cursor_col, memory_order_acquire);
}

/* Atomic screen size operations */
static inline void atomic_set_screen_size(int rows, int cols)
{
    atomic_store_explicit(&terminal_state.screen_rows, rows, memory_order_release);
    atomic_store_explicit(&terminal_state.screen_cols, cols, memory_order_release);
}

static inline void atomic_get_screen_size(int *rows, int *cols)
{
    *rows = atomic_load_explicit(&terminal_state.screen_rows, memory_order_acquire);
    *cols = atomic_load_explicit(&terminal_state.screen_cols, memory_order_acquire);
}

/* Atomic cursor visibility */
static inline void atomic_set_cursor_visible(bool visible)
{
    atomic_store_explicit(&terminal_state.cursor_visible, visible, memory_order_release);
}

static inline bool atomic_get_cursor_visible(void)
{
    return atomic_load_explicit(&terminal_state.cursor_visible, memory_order_acquire);
}

/* Atomic update state management */
static inline bool atomic_begin_update(void)
{
    bool expected = false;
    if (atomic_compare_exchange_strong_explicit(&terminal_state.in_update, &expected, true,
                                               memory_order_acquire, memory_order_relaxed)) {
        atomic_fetch_add_explicit(&terminal_state.update_generation, 1, memory_order_release);
        return true;
    }
    return false; /* Update already in progress */
}

static inline void atomic_end_update(void)
{
    atomic_store_explicit(&terminal_state.in_update, false, memory_order_release);
}

static inline bool atomic_is_updating(void)
{
    return atomic_load_explicit(&terminal_state.in_update, memory_order_acquire);
}

/* Atomic screen dirty flag */
static inline void atomic_mark_screen_dirty(void)
{
    atomic_store_explicit(&terminal_state.screen_dirty, true, memory_order_release);
}

static inline void atomic_mark_screen_clean(void)
{
    atomic_store_explicit(&terminal_state.screen_dirty, false, memory_order_release);
}

static inline bool atomic_is_screen_dirty(void)
{
    return atomic_load_explicit(&terminal_state.screen_dirty, memory_order_acquire);
}

/* Get current update generation for cache invalidation */
static inline uint32_t atomic_get_update_generation(void)
{
    return atomic_load_explicit(&terminal_state.update_generation, memory_order_acquire);
}

/* Signal-safe terminal operations */
int atomic_terminal_resize(int new_rows, int new_cols);
int atomic_terminal_move_cursor(int row, int col);
int atomic_terminal_clear_screen(void);

#endif /* ATOMIC_TERMINAL_H */