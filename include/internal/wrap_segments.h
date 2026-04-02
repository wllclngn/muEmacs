#ifndef WRAP_SEGMENTS_H_
#define WRAP_SEGMENTS_H_

#include <stdint.h>
#include <stdbool.h>

struct line;

// Segment break classification (Pretext-inspired)
// Determines where the line-breaker can split
typedef enum {
	SEG_TEXT  = 0,  // Non-breaking text run (letters, digits, punctuation)
	SEG_SPACE = 1,  // Breakable whitespace run
	SEG_TAB   = 2,  // Tab character (display width depends on column)
	SEG_CTRL  = 3,  // Control character (renders as ^X, 2 display columns)
} wrap_seg_kind_t;

// A single wrap segment: a contiguous run of same-kind characters
typedef struct {
	uint16_t byte_offset;     // Start byte position within the line
	uint16_t byte_len;        // Length in bytes
	uint16_t display_width;   // Total display columns (tab: unwrapped width)
	uint16_t char_count;      // Logical character count (for syntax face lookup)
	wrap_seg_kind_t kind;     // Break classification
} wrap_segment_t;

// Fast-path detection flags
#define WRAP_SIMPLE   0x01    // No tabs, wide chars, or control chars
#define WRAP_HAS_TAB  0x02    // Contains at least one tab
#define WRAP_HAS_WIDE 0x04    // Contains at least one double-width char
#define WRAP_HAS_CTRL 0x08    // Contains at least one control char

// Per-line wrap segment cache
typedef struct wrap_cache {
	wrap_segment_t *segments;       // Heap-allocated segment array
	uint16_t count;                 // Number of segments
	uint16_t capacity;              // Allocated capacity
	int total_display_width;        // Sum of all segment display_widths
	uint8_t flags;                  // WRAP_* bitmask
} wrap_cache_t;

// Unified layout result
typedef struct {
	int row_count;    // Total display rows this line occupies
	int cursor_row;   // Row the cursor falls on (0-indexed from line start)
	int cursor_col;   // Display column within cursor_row
} wrap_result_t;

// Build segment cache for a line. Reads via lgetc(), no size limit.
// Stores result in lp->l_wrap_cache. Returns the cache (never nullptr on success).
wrap_cache_t *wrap_prepare(struct line *lp, int tab_width);

// Free a line's segment cache and set l_wrap_cache to nullptr.
void wrap_invalidate(struct line *lp);

// Count display rows for a wrapped line.
// Fast path: lines fitting in one row return 1 immediately.
int wrap_line_rows(struct line *lp, int wrap_col, int tab_width);

// Find cursor screen position within a wrapped line.
// Returns extra rows (wrap-induced), sets *out_col to column within that row.
int wrap_cursor_pos(struct line *lp, int byte_offset, int wrap_col,
                    int tab_width, int *out_col);

// Full unified layout producing both row count and cursor position.
void wrap_layout(struct line *lp, int wrap_col, int tab_width,
                 int cursor_byte, wrap_result_t *result);

#endif // WRAP_SEGMENTS_H_
