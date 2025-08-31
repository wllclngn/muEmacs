// display_matrix.h - Display matrix with dirty region tracking
// Optimizes screen updates through intelligent damage tracking

#ifndef UEMACS_DISPLAY_MATRIX_H
#define UEMACS_DISPLAY_MATRIX_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

// Forward declarations
struct window;
struct buffer;

// Display cell structure
struct display_cell {
    uint32_t codepoint;         // Unicode codepoint
    uint8_t attr;               // Text attributes (bold, reverse, etc.)
    uint8_t fg_color;           // Foreground color
    uint8_t bg_color;           // Background color
    uint8_t flags;              // Cell flags (dirty, selected, etc.)
};

// Cell attribute flags
#define ATTR_NORMAL     0x00
#define ATTR_BOLD       0x01
#define ATTR_UNDERLINE  0x02
#define ATTR_REVERSE    0x04
#define ATTR_STANDOUT   0x08
#define ATTR_DIM        0x10
#define ATTR_ITALIC     0x20
#define ATTR_BLINK      0x40

// Cell flags
#define CELL_DIRTY      0x01    // Cell needs redraw
#define CELL_SELECTED   0x02    // Cell in selection
#define CELL_CURSOR     0x04    // Cursor position
#define CELL_WRAPPED    0x08    // Line wrap continuation
#define CELL_TAB        0x10    // Tab expansion
#define CELL_CONTROL    0x20    // Control character

// Dirty region structure
struct dirty_region {
    int start_row, start_col;   // Top-left corner
    int end_row, end_col;       // Bottom-right corner (exclusive)
    bool valid;                 // Region is valid
    struct dirty_region *next;  // Linked list for merging
};

// Display matrix structure
struct display_matrix {
    struct display_cell *cells; // 2D array of display cells
    int rows, cols;             // Matrix dimensions
    int capacity_rows;          // Allocated rows
    int capacity_cols;          // Allocated columns
    
    // Dirty region tracking
    struct dirty_region *dirty_regions; // List of dirty regions
    bool full_redraw_pending;    // Force complete redraw
    _Atomic uint32_t generation; // Change generation counter
    
    // Line tracking for efficiency
    bool *line_dirty;           // Per-line dirty flags
    int first_dirty_line;       // First dirty line (-1 if none)
    int last_dirty_line;        // Last dirty line (-1 if none)
    
    // Cursor tracking
    int cursor_row, cursor_col; // Current cursor position
    int old_cursor_row;         // Previous cursor position
    int old_cursor_col;
    bool cursor_visible;        // Cursor visibility
    
    // Selection tracking
    int sel_start_row, sel_start_col; // Selection start
    int sel_end_row, sel_end_col;     // Selection end
    bool selection_active;            // Selection is active
    
    // Performance optimization
    _Atomic size_t cells_updated;     // Statistics
    _Atomic size_t regions_merged;
    _Atomic size_t full_redraws;
    _Atomic size_t partial_redraws;
};

// Global display matrix
extern struct display_matrix *global_display_matrix;

// Display matrix management
int display_matrix_init(int initial_rows, int initial_cols);
void display_matrix_destroy(void);
int display_matrix_resize(int new_rows, int new_cols);

// Cell manipulation
void display_matrix_set_cell(int row, int col, uint32_t codepoint, 
                            uint8_t attr, uint8_t fg, uint8_t bg);
void display_matrix_set_cell_attr(int row, int col, uint8_t attr);
void display_matrix_set_cell_color(int row, int col, uint8_t fg, uint8_t bg);
struct display_cell *display_matrix_get_cell(int row, int col);
void display_matrix_clear_cell(int row, int col);
void display_matrix_clear_line(int row);
void display_matrix_clear_all(void);

// Dirty region management
void display_matrix_mark_dirty(int row, int col);
void display_matrix_mark_region_dirty(int start_row, int start_col, 
                                     int end_row, int end_col);
void display_matrix_mark_line_dirty(int row);
void display_matrix_mark_all_dirty(void);
bool display_matrix_is_dirty(int row, int col);
bool display_matrix_is_line_dirty(int row);

// Region operations
struct dirty_region *dirty_region_create(int start_row, int start_col,
                                        int end_row, int end_col);
void dirty_region_destroy(struct dirty_region *region);
void dirty_region_merge_overlapping(void);
void dirty_region_clear_all(void);
int dirty_region_count(void);

// Cursor management
void display_matrix_set_cursor(int row, int col, bool visible);
void display_matrix_get_cursor(int *row, int *col, bool *visible);
void display_matrix_update_cursor_region(void);

// Selection management
void display_matrix_set_selection(int start_row, int start_col,
                                 int end_row, int end_col);
void display_matrix_clear_selection(void);
void display_matrix_update_selection_region(void);
bool display_matrix_in_selection(int row, int col);

// Text rendering
void display_matrix_render_text(int row, int start_col, const char *text,
                               size_t len, uint8_t attr, uint8_t fg, uint8_t bg);
void display_matrix_render_line(int row, struct buffer *bp, size_t line_offset,
                               int start_col, int width);
void display_matrix_render_buffer(struct window *wp);

// Screen update optimization
int display_matrix_get_dirty_regions(struct dirty_region **regions);
void display_matrix_optimize_updates(void);
bool display_matrix_needs_update(void);
void display_matrix_commit_updates(void);

// Scrolling support
void display_matrix_scroll_up(int start_row, int end_row, int lines);
void display_matrix_scroll_down(int start_row, int end_row, int lines);
void display_matrix_shift_region(int start_row, int start_col,
                                int end_row, int end_col, int row_delta, int col_delta);

// Line operations
void display_matrix_insert_line(int row);
void display_matrix_delete_line(int row);
void display_matrix_copy_line(int src_row, int dst_row);
void display_matrix_fill_line(int row, uint32_t codepoint, uint8_t attr,
                             uint8_t fg, uint8_t bg);

// Attribute helpers
uint8_t attr_combine(uint8_t base_attr, uint8_t overlay_attr);
uint8_t attr_invert(uint8_t attr);
bool attr_equal(uint8_t attr1, uint8_t attr2);

// Color helpers
uint8_t color_from_rgb(uint8_t r, uint8_t g, uint8_t b);
void color_to_rgb(uint8_t color, uint8_t *r, uint8_t *g, uint8_t *b);
uint8_t color_brighten(uint8_t color);
uint8_t color_darken(uint8_t color);

// Performance monitoring
struct display_matrix_stats {
    _Atomic size_t total_updates;
    _Atomic size_t cell_updates;
    _Atomic size_t region_merges;
    _Atomic size_t scroll_operations;
    _Atomic size_t full_redraws;
    _Atomic size_t partial_redraws;
    _Atomic uint64_t update_time_ns;
    _Atomic size_t max_dirty_regions;
    _Atomic size_t cursor_moves;
    _Atomic size_t selection_changes;
};

extern struct display_matrix_stats display_matrix_global_stats;

void display_matrix_stats_reset(void);
void display_matrix_stats_update(size_t cells_updated, uint64_t update_time_ns);

// Validation and debugging
#ifdef DEBUG
void display_matrix_validate(void);
void display_matrix_dump_stats(void);
void display_matrix_dump_dirty_regions(void);
void display_matrix_dump_cells(int start_row, int end_row);
void display_matrix_check_consistency(void);
#endif

// Configuration constants
#define DISPLAY_MATRIX_MIN_ROWS     24
#define DISPLAY_MATRIX_MIN_COLS     80
#define DISPLAY_MATRIX_MAX_ROWS     300
#define DISPLAY_MATRIX_MAX_COLS     500
#define DIRTY_REGION_MERGE_THRESHOLD 3
#define MAX_DIRTY_REGIONS           64

// Color definitions
#define COLOR_BLACK     0
#define COLOR_RED       1
#define COLOR_GREEN     2
#define COLOR_YELLOW    3
#define COLOR_BLUE      4
#define COLOR_MAGENTA   5
#define COLOR_CYAN      6
#define COLOR_WHITE     7
#define COLOR_BRIGHT    8  // Add to base colors for bright variants
#define COLOR_DEFAULT   15

// Error codes
#define DISPLAY_MATRIX_SUCCESS      0
#define DISPLAY_MATRIX_ERROR       -1
#define DISPLAY_MATRIX_OUT_OF_MEM  -2
#define DISPLAY_MATRIX_INVALID     -3
#define DISPLAY_MATRIX_BOUNDS      -4

#endif // UEMACS_DISPLAY_MATRIX_H