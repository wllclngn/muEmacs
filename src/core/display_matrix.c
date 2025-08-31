// display_matrix.c - Display matrix with dirty region tracking

#include "μemacs/display_matrix.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "profiler.h"
#include "memory.h"
#include "utf8.h"

// Global display matrix and statistics
struct display_matrix *global_display_matrix = NULL;
struct display_matrix_stats display_matrix_global_stats = {0};

// Initialize display matrix
int display_matrix_init(int initial_rows, int initial_cols) {
    if (global_display_matrix) {
        return DISPLAY_MATRIX_SUCCESS; // Already initialized
    }
    
    if (initial_rows < DISPLAY_MATRIX_MIN_ROWS) initial_rows = DISPLAY_MATRIX_MIN_ROWS;
    if (initial_cols < DISPLAY_MATRIX_MIN_COLS) initial_cols = DISPLAY_MATRIX_MIN_COLS;
    
    global_display_matrix = safe_alloc(sizeof(struct display_matrix),
                                      "display matrix", __FILE__, __LINE__);
    if (!global_display_matrix) return DISPLAY_MATRIX_OUT_OF_MEM;
    
    // Allocate cell matrix
    size_t cell_count = initial_rows * initial_cols;
    global_display_matrix->cells = safe_alloc(cell_count * sizeof(struct display_cell),
                                            "display cells", __FILE__, __LINE__);
    if (!global_display_matrix->cells) {
        SAFE_FREE(global_display_matrix);
        return DISPLAY_MATRIX_OUT_OF_MEM;
    }
    
    // Allocate line dirty flags
    global_display_matrix->line_dirty = safe_alloc(initial_rows * sizeof(bool),
                                                  "line dirty flags", __FILE__, __LINE__);
    if (!global_display_matrix->line_dirty) {
        SAFE_FREE(global_display_matrix->cells);
        SAFE_FREE(global_display_matrix);
        return DISPLAY_MATRIX_OUT_OF_MEM;
    }
    
    // Initialize matrix properties
    global_display_matrix->rows = initial_rows;
    global_display_matrix->cols = initial_cols;
    global_display_matrix->capacity_rows = initial_rows;
    global_display_matrix->capacity_cols = initial_cols;
    
    // Initialize dirty tracking
    global_display_matrix->dirty_regions = NULL;
    global_display_matrix->full_redraw_pending = true;
    atomic_init(&global_display_matrix->generation, 0);
    
    memset(global_display_matrix->line_dirty, true, initial_rows * sizeof(bool));
    global_display_matrix->first_dirty_line = 0;
    global_display_matrix->last_dirty_line = initial_rows - 1;
    
    // Initialize cursor tracking
    global_display_matrix->cursor_row = 0;
    global_display_matrix->cursor_col = 0;
    global_display_matrix->old_cursor_row = -1;
    global_display_matrix->old_cursor_col = -1;
    global_display_matrix->cursor_visible = true;
    
    // Initialize selection tracking
    global_display_matrix->sel_start_row = -1;
    global_display_matrix->sel_start_col = -1;
    global_display_matrix->sel_end_row = -1;
    global_display_matrix->sel_end_col = -1;
    global_display_matrix->selection_active = false;
    
    // Initialize statistics
    atomic_init(&global_display_matrix->cells_updated, 0);
    atomic_init(&global_display_matrix->regions_merged, 0);
    atomic_init(&global_display_matrix->full_redraws, 0);
    atomic_init(&global_display_matrix->partial_redraws, 0);
    
    // Clear all cells
    display_matrix_clear_all();
    
    return DISPLAY_MATRIX_SUCCESS;
}

// Destroy display matrix
void display_matrix_destroy(void) {
    if (!global_display_matrix) return;
    
    SAFE_FREE(global_display_matrix->cells);
    SAFE_FREE(global_display_matrix->line_dirty);
    dirty_region_clear_all();
    SAFE_FREE(global_display_matrix);
}

// Resize display matrix
int display_matrix_resize(int new_rows, int new_cols) {
    if (!global_display_matrix) return DISPLAY_MATRIX_ERROR;
    
    if (new_rows < DISPLAY_MATRIX_MIN_ROWS) new_rows = DISPLAY_MATRIX_MIN_ROWS;
    if (new_cols < DISPLAY_MATRIX_MIN_COLS) new_cols = DISPLAY_MATRIX_MIN_COLS;
    
    // Check if we need to reallocate
    if (new_rows <= global_display_matrix->capacity_rows &&
        new_cols <= global_display_matrix->capacity_cols) {
        // Just update dimensions
        global_display_matrix->rows = new_rows;
        global_display_matrix->cols = new_cols;
        display_matrix_mark_all_dirty();
        return DISPLAY_MATRIX_SUCCESS;
    }
    
    // Need to reallocate
    size_t new_cell_count = new_rows * new_cols;
    struct display_cell *new_cells = safe_alloc(new_cell_count * sizeof(struct display_cell),
                                               "display cells resize", __FILE__, __LINE__);
    if (!new_cells) return DISPLAY_MATRIX_OUT_OF_MEM;
    
    bool *new_line_dirty = safe_alloc(new_rows * sizeof(bool),
                                     "line dirty resize", __FILE__, __LINE__);
    if (!new_line_dirty) {
        SAFE_FREE(new_cells);
        return DISPLAY_MATRIX_OUT_OF_MEM;
    }
    
    // Copy existing content
    int copy_rows = (new_rows < global_display_matrix->rows) ? new_rows : global_display_matrix->rows;
    int copy_cols = (new_cols < global_display_matrix->cols) ? new_cols : global_display_matrix->cols;
    
    for (int row = 0; row < copy_rows; row++) {
        for (int col = 0; col < copy_cols; col++) {
            new_cells[row * new_cols + col] = global_display_matrix->cells[row * global_display_matrix->cols + col];
        }
        new_line_dirty[row] = global_display_matrix->line_dirty[row];
    }
    
    // Initialize new cells
    for (int row = 0; row < new_rows; row++) {
        for (int col = copy_cols; col < new_cols; col++) {
            struct display_cell *cell = &new_cells[row * new_cols + col];
            cell->codepoint = ' ';
            cell->attr = ATTR_NORMAL;
            cell->fg_color = COLOR_DEFAULT;
            cell->bg_color = COLOR_DEFAULT;
            cell->flags = CELL_DIRTY;
        }
        if (row >= copy_rows) {
            new_line_dirty[row] = true;
        }
    }
    
    // Update matrix
    SAFE_FREE(global_display_matrix->cells);
    SAFE_FREE(global_display_matrix->line_dirty);
    global_display_matrix->cells = new_cells;
    global_display_matrix->line_dirty = new_line_dirty;
    global_display_matrix->rows = new_rows;
    global_display_matrix->cols = new_cols;
    global_display_matrix->capacity_rows = new_rows;
    global_display_matrix->capacity_cols = new_cols;
    
    display_matrix_mark_all_dirty();
    atomic_fetch_add(&global_display_matrix->generation, 1);
    
    return DISPLAY_MATRIX_SUCCESS;
}

// Set display cell
void display_matrix_set_cell(int row, int col, uint32_t codepoint,
                           uint8_t attr, uint8_t fg, uint8_t bg) {
    if (!global_display_matrix || row < 0 || row >= global_display_matrix->rows ||
        col < 0 || col >= global_display_matrix->cols) {
        return;
    }
    
    struct display_cell *cell = &global_display_matrix->cells[row * global_display_matrix->cols + col];
    
    // Check if cell actually changed
    if (cell->codepoint == codepoint && cell->attr == attr &&
        cell->fg_color == fg && cell->bg_color == bg) {
        return;
    }
    
    cell->codepoint = codepoint;
    cell->attr = attr;
    cell->fg_color = fg;
    cell->bg_color = bg;
    cell->flags |= CELL_DIRTY;
    
    display_matrix_mark_dirty(row, col);
    atomic_fetch_add(&global_display_matrix->cells_updated, 1);
}

// Get display cell
struct display_cell *display_matrix_get_cell(int row, int col) {
    if (!global_display_matrix || row < 0 || row >= global_display_matrix->rows ||
        col < 0 || col >= global_display_matrix->cols) {
        return NULL;
    }
    
    return &global_display_matrix->cells[row * global_display_matrix->cols + col];
}

// Clear display cell
void display_matrix_clear_cell(int row, int col) {
    display_matrix_set_cell(row, col, ' ', ATTR_NORMAL, COLOR_DEFAULT, COLOR_DEFAULT);
}

// Clear entire line
void display_matrix_clear_line(int row) {
    if (!global_display_matrix || row < 0 || row >= global_display_matrix->rows) {
        return;
    }
    
    for (int col = 0; col < global_display_matrix->cols; col++) {
        display_matrix_clear_cell(row, col);
    }
}

// Clear all cells
void display_matrix_clear_all(void) {
    if (!global_display_matrix) return;
    
    for (int row = 0; row < global_display_matrix->rows; row++) {
        for (int col = 0; col < global_display_matrix->cols; col++) {
            struct display_cell *cell = &global_display_matrix->cells[row * global_display_matrix->cols + col];
            cell->codepoint = ' ';
            cell->attr = ATTR_NORMAL;
            cell->fg_color = COLOR_DEFAULT;
            cell->bg_color = COLOR_DEFAULT;
            cell->flags = CELL_DIRTY;
        }
    }
    
    display_matrix_mark_all_dirty();
}

// Mark cell as dirty
void display_matrix_mark_dirty(int row, int col) {
    if (!global_display_matrix || row < 0 || row >= global_display_matrix->rows) {
        return;
    }
    
    // Mark line as dirty
    global_display_matrix->line_dirty[row] = true;
    
    // Update dirty line range
    if (global_display_matrix->first_dirty_line < 0 || row < global_display_matrix->first_dirty_line) {
        global_display_matrix->first_dirty_line = row;
    }
    if (global_display_matrix->last_dirty_line < 0 || row > global_display_matrix->last_dirty_line) {
        global_display_matrix->last_dirty_line = row;
    }
    
    // Mark specific cell
    if (col >= 0 && col < global_display_matrix->cols) {
        global_display_matrix->cells[row * global_display_matrix->cols + col].flags |= CELL_DIRTY;
    }
    
    atomic_fetch_add(&global_display_matrix->generation, 1);
}

// Mark region as dirty
void display_matrix_mark_region_dirty(int start_row, int start_col, int end_row, int end_col) {
    if (!global_display_matrix) return;
    
    // Clamp bounds
    if (start_row < 0) start_row = 0;
    if (start_col < 0) start_col = 0;
    if (end_row >= global_display_matrix->rows) end_row = global_display_matrix->rows - 1;
    if (end_col >= global_display_matrix->cols) end_col = global_display_matrix->cols - 1;
    
    for (int row = start_row; row <= end_row; row++) {
        global_display_matrix->line_dirty[row] = true;
        for (int col = start_col; col <= end_col; col++) {
            global_display_matrix->cells[row * global_display_matrix->cols + col].flags |= CELL_DIRTY;
        }
    }
    
    // Update dirty line range
    if (global_display_matrix->first_dirty_line < 0 || start_row < global_display_matrix->first_dirty_line) {
        global_display_matrix->first_dirty_line = start_row;
    }
    if (global_display_matrix->last_dirty_line < 0 || end_row > global_display_matrix->last_dirty_line) {
        global_display_matrix->last_dirty_line = end_row;
    }
    
    atomic_fetch_add(&global_display_matrix->generation, 1);
}

// Mark all as dirty
void display_matrix_mark_all_dirty(void) {
    if (!global_display_matrix) return;
    
    global_display_matrix->full_redraw_pending = true;
    memset(global_display_matrix->line_dirty, true, global_display_matrix->rows * sizeof(bool));
    global_display_matrix->first_dirty_line = 0;
    global_display_matrix->last_dirty_line = global_display_matrix->rows - 1;
    
    for (int i = 0; i < global_display_matrix->rows * global_display_matrix->cols; i++) {
        global_display_matrix->cells[i].flags |= CELL_DIRTY;
    }
    
    atomic_fetch_add(&global_display_matrix->generation, 1);
    atomic_fetch_add(&global_display_matrix->full_redraws, 1);
}

// Check if cell is dirty
bool display_matrix_is_dirty(int row, int col) {
    if (!global_display_matrix || row < 0 || row >= global_display_matrix->rows ||
        col < 0 || col >= global_display_matrix->cols) {
        return false;
    }
    
    return (global_display_matrix->cells[row * global_display_matrix->cols + col].flags & CELL_DIRTY) != 0;
}

// Check if line is dirty
bool display_matrix_is_line_dirty(int row) {
    if (!global_display_matrix || row < 0 || row >= global_display_matrix->rows) {
        return false;
    }
    
    return global_display_matrix->line_dirty[row];
}

// Set cursor position
void display_matrix_set_cursor(int row, int col, bool visible) {
    if (!global_display_matrix) return;
    
    // Clear old cursor position
    if (global_display_matrix->old_cursor_row >= 0 && global_display_matrix->old_cursor_col >= 0) {
        struct display_cell *old_cell = display_matrix_get_cell(global_display_matrix->old_cursor_row,
                                                               global_display_matrix->old_cursor_col);
        if (old_cell) {
            old_cell->flags &= ~CELL_CURSOR;
            display_matrix_mark_dirty(global_display_matrix->old_cursor_row,
                                    global_display_matrix->old_cursor_col);
        }
    }
    
    global_display_matrix->old_cursor_row = global_display_matrix->cursor_row;
    global_display_matrix->old_cursor_col = global_display_matrix->cursor_col;
    global_display_matrix->cursor_row = row;
    global_display_matrix->cursor_col = col;
    global_display_matrix->cursor_visible = visible;
    
    // Mark new cursor position
    if (visible && row >= 0 && row < global_display_matrix->rows &&
        col >= 0 && col < global_display_matrix->cols) {
        struct display_cell *new_cell = display_matrix_get_cell(row, col);
        if (new_cell) {
            new_cell->flags |= CELL_CURSOR;
            display_matrix_mark_dirty(row, col);
        }
    }
    
    atomic_fetch_add(&display_matrix_global_stats.cursor_moves, 1);
}

// Render text to display matrix
void display_matrix_render_text(int row, int start_col, const char *text,
                               size_t len, uint8_t attr, uint8_t fg, uint8_t bg) {
    if (!global_display_matrix || !text || row < 0 || row >= global_display_matrix->rows) {
        return;
    }
    
    int col = start_col;
    const char *p = text;
    const char *end = text + len;
    
    while (p < end && col < global_display_matrix->cols) {
        unicode_t codepoint;
        int char_len = utf8_to_unicode(p, 0, end - p, &codepoint);
        
        if (char_len <= 0) {
            // Invalid UTF-8, show replacement character
            codepoint = 0xFFFD;
            char_len = 1;
        }
        
        // Handle special characters
        if (codepoint == '\t') {
            // Tab expansion
            int tab_width = 8 - (col % 8);
            for (int i = 0; i < tab_width && col < global_display_matrix->cols; i++) {
                display_matrix_set_cell(row, col++, ' ', attr, fg, bg);
            }
        } else if (codepoint < 32) {
            // Control character - show as ^X
            if (col < global_display_matrix->cols) {
                display_matrix_set_cell(row, col++, '^', attr | ATTR_REVERSE, fg, bg);
            }
            if (col < global_display_matrix->cols) {
                display_matrix_set_cell(row, col++, codepoint + '@', attr | ATTR_REVERSE, fg, bg);
            }
        } else {
            // Normal character
            display_matrix_set_cell(row, col++, codepoint, attr, fg, bg);
        }
        
        p += char_len;
    }
}

// Check if matrix needs update
bool display_matrix_needs_update(void) {
    if (!global_display_matrix) return false;
    
    return global_display_matrix->full_redraw_pending ||
           global_display_matrix->first_dirty_line >= 0;
}

// Commit updates (clear dirty flags)
void display_matrix_commit_updates(void) {
    if (!global_display_matrix) return;
    
    // Clear dirty flags
    for (int i = 0; i < global_display_matrix->rows * global_display_matrix->cols; i++) {
        global_display_matrix->cells[i].flags &= ~CELL_DIRTY;
    }
    
    memset(global_display_matrix->line_dirty, false, global_display_matrix->rows * sizeof(bool));
    global_display_matrix->first_dirty_line = -1;
    global_display_matrix->last_dirty_line = -1;
    global_display_matrix->full_redraw_pending = false;
    
    if (global_display_matrix->full_redraw_pending) {
        atomic_fetch_add(&global_display_matrix->full_redraws, 1);
    } else {
        atomic_fetch_add(&global_display_matrix->partial_redraws, 1);
    }
}

// Scroll region up
void display_matrix_scroll_up(int start_row, int end_row, int lines) {
    perf_start_timing("scroll");
    if (!global_display_matrix || lines <= 0 || start_row >= end_row) {
        perf_end_timing("scroll");
        return;
    }
    // Move cells up
    for (int dst_row = start_row; dst_row <= end_row - lines; dst_row++) {
        int src_row = dst_row + lines;
        for (int col = 0; col < global_display_matrix->cols; col++) {
            global_display_matrix->cells[dst_row * global_display_matrix->cols + col] =
                global_display_matrix->cells[src_row * global_display_matrix->cols + col];
        }
        global_display_matrix->line_dirty[dst_row] = true;
    }
    // Clear bottom lines
    for (int row = end_row - lines + 1; row <= end_row; row++) {
        display_matrix_clear_line(row);
    }
    display_matrix_mark_region_dirty(start_row, 0, end_row, global_display_matrix->cols - 1);
    atomic_fetch_add(&display_matrix_global_stats.scroll_operations, 1);
    perf_end_timing("scroll");
}

// Statistics update
void display_matrix_stats_update(size_t cells_updated, uint64_t update_time_ns) {
    atomic_fetch_add(&display_matrix_global_stats.total_updates, 1);
    atomic_fetch_add(&display_matrix_global_stats.cell_updates, cells_updated);
    atomic_fetch_add(&display_matrix_global_stats.update_time_ns, update_time_ns);
}

#ifdef DEBUG
// Dump statistics
void display_matrix_dump_stats(void) {
    if (!global_display_matrix) return;
    
    mlwrite("Display Matrix Statistics:");
    mlwrite("  Dimensions: %dx%d", global_display_matrix->rows, global_display_matrix->cols);
    mlwrite("  Cells updated: %zu", atomic_load(&global_display_matrix->cells_updated));
    mlwrite("  Full redraws: %zu", atomic_load(&global_display_matrix->full_redraws));
    mlwrite("  Partial redraws: %zu", atomic_load(&global_display_matrix->partial_redraws));
    mlwrite("  Generation: %u", atomic_load(&global_display_matrix->generation));
    printf("  Dirty lines: %d to %d\n", 
           global_display_matrix->first_dirty_line, global_display_matrix->last_dirty_line);
    printf("  Cursor: (%d,%d) visible=%s\n",
           global_display_matrix->cursor_row, global_display_matrix->cursor_col,
           global_display_matrix->cursor_visible ? "true" : "false");
    
    mlwrite("Global Statistics:");
    mlwrite("  Total updates: %zu", atomic_load(&display_matrix_global_stats.total_updates));
    mlwrite("  Cell updates: %zu", atomic_load(&display_matrix_global_stats.cell_updates));
    mlwrite("  Scroll operations: %zu", atomic_load(&display_matrix_global_stats.scroll_operations));
    mlwrite("  Update time: %lu ns", atomic_load(&display_matrix_global_stats.update_time_ns));
}

// Validate matrix consistency
void display_matrix_check_consistency(void) {
    if (!global_display_matrix) return;
    
    // Check bounds
    if (global_display_matrix->rows <= 0 || global_display_matrix->cols <= 0) {
        mlwrite("Invalid matrix dimensions: %dx%d", 
                global_display_matrix->rows, global_display_matrix->cols);
        return;
    }
    
    // Check cursor bounds
    if (global_display_matrix->cursor_row < 0 || 
        global_display_matrix->cursor_row >= global_display_matrix->rows ||
        global_display_matrix->cursor_col < 0 || 
        global_display_matrix->cursor_col >= global_display_matrix->cols) {
        mlwrite("Cursor out of bounds: (%d,%d)",
                global_display_matrix->cursor_row, global_display_matrix->cursor_col);
    }
    
    mlwrite("Display matrix consistency check: PASSED");
}
#endif
