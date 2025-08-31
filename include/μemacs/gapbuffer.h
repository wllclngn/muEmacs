// gapbuffer.h - Gap buffer implementation for efficient text storage
// Replaces traditional line-based storage with modern gap buffer architecture

#ifndef UEMACS_GAPBUFFER_H
#define UEMACS_GAPBUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

// Forward declarations
struct buffer;
struct line;

// Gap buffer structure with O(1) insertion/deletion at cursor
struct gap_buffer {
    char *data;              // Actual text storage
    size_t capacity;         // Total buffer capacity
    size_t gap_start;        // Start of gap (cursor position)
    size_t gap_end;          // End of gap (exclusive)
    size_t logical_size;     // Size of text (excluding gap)
    _Atomic uint32_t generation; // Change tracking for display caching
    
    // Line index cache for O(log n) line navigation
    struct line_index {
        size_t *offsets;     // Byte offsets of line starts
        size_t count;        // Number of lines
        size_t capacity;     // Allocated line capacity
        _Atomic bool dirty;  // Needs rebuilding
    } line_idx;
    
    // UTF-8 character cache for O(1) column positioning
    struct char_cache {
        size_t byte_offset;  // Cached byte position
        size_t char_offset;  // Corresponding character position
        size_t line_num;     // Line number for this cache
        _Atomic bool valid;  // Cache validity flag
    } char_cache;
};

// Gap buffer configuration
#define GAP_BUFFER_MIN_SIZE    1024      // Minimum buffer size
#define GAP_BUFFER_GROW_FACTOR 1.5       // Growth factor for expansion
#define GAP_BUFFER_MAX_GAP     4096      // Maximum gap size before compaction
#define LINE_INDEX_CHUNK       128       // Line index growth chunk size

// Gap buffer management
struct gap_buffer *gap_buffer_create(size_t initial_capacity);
void gap_buffer_destroy(struct gap_buffer *gb);

// Core text operations - O(1) at cursor, O(n) elsewhere
int gap_buffer_insert(struct gap_buffer *gb, size_t pos, const char *text, size_t len);
int gap_buffer_delete(struct gap_buffer *gb, size_t pos, size_t len);
int gap_buffer_replace(struct gap_buffer *gb, size_t pos, size_t old_len, 
                       const char *new_text, size_t new_len);

// Cursor positioning - O(1) when moving cursor, O(log n) for random access
int gap_buffer_set_cursor(struct gap_buffer *gb, size_t pos);
size_t gap_buffer_get_cursor(struct gap_buffer *gb);

// Text access - O(1) for sequential, O(n) for scattered access
char gap_buffer_get_char(struct gap_buffer *gb, size_t pos);
size_t gap_buffer_get_text(struct gap_buffer *gb, size_t pos, size_t len, 
                           char *buffer, size_t buffer_size);
const char *gap_buffer_get_line(struct gap_buffer *gb, size_t line_num, size_t *length);

// Line navigation - O(log n) with cached line index
size_t gap_buffer_line_count(struct gap_buffer *gb);
size_t gap_buffer_line_to_offset(struct gap_buffer *gb, size_t line_num);
size_t gap_buffer_offset_to_line(struct gap_buffer *gb, size_t offset);
size_t gap_buffer_line_length(struct gap_buffer *gb, size_t line_num);

// UTF-8 aware operations - O(1) with character cache
size_t gap_buffer_char_to_byte(struct gap_buffer *gb, size_t line_num, size_t char_pos);
size_t gap_buffer_byte_to_char(struct gap_buffer *gb, size_t line_num, size_t byte_pos);
size_t gap_buffer_char_count(struct gap_buffer *gb, size_t line_num);

// Buffer statistics
size_t gap_buffer_size(struct gap_buffer *gb);
size_t gap_buffer_capacity(struct gap_buffer *gb);
size_t gap_buffer_gap_size(struct gap_buffer *gb);
double gap_buffer_fragmentation(struct gap_buffer *gb);

// Search operations - Boyer-Moore for efficiency
size_t gap_buffer_search_forward(struct gap_buffer *gb, size_t start_pos,
                                 const char *pattern, size_t pattern_len);
size_t gap_buffer_search_backward(struct gap_buffer *gb, size_t start_pos,
                                  const char *pattern, size_t pattern_len);

// Batch operations for efficiency
int gap_buffer_insert_lines(struct gap_buffer *gb, size_t line_num,
                           const char **lines, size_t line_count);
int gap_buffer_delete_lines(struct gap_buffer *gb, size_t start_line, size_t line_count);

// Memory management and optimization
int gap_buffer_compact(struct gap_buffer *gb);
int gap_buffer_reserve(struct gap_buffer *gb, size_t additional_capacity);
void gap_buffer_trim(struct gap_buffer *gb);

// Cache management
void gap_buffer_invalidate_caches(struct gap_buffer *gb);
void gap_buffer_rebuild_line_index(struct gap_buffer *gb);
void gap_buffer_update_char_cache(struct gap_buffer *gb, size_t line_num, size_t byte_pos);

// Legacy compatibility for line-based interface
struct line *gap_buffer_get_line_struct(struct gap_buffer *gb, size_t line_num);
int gap_buffer_sync_to_lines(struct gap_buffer *gb, struct line **head_line);
int gap_buffer_sync_from_lines(struct gap_buffer *gb, struct line *head_line);

// Debug and statistics
#ifdef DEBUG
void gap_buffer_dump_stats(struct gap_buffer *gb);
void gap_buffer_validate(struct gap_buffer *gb);
void gap_buffer_dump_structure(struct gap_buffer *gb);
#endif

// Performance monitoring
struct gap_buffer_stats {
    _Atomic size_t insertions;
    _Atomic size_t deletions;
    _Atomic size_t cursor_moves;
    _Atomic size_t cache_hits;
    _Atomic size_t cache_misses;
    _Atomic size_t compactions;
    _Atomic size_t expansions;
};

extern struct gap_buffer_stats gap_buffer_global_stats;

// Utility macros for gap buffer positioning
#define GAP_BUFFER_BEFORE_GAP(gb, pos) ((pos) < (gb)->gap_start)
#define GAP_BUFFER_AFTER_GAP(gb, pos)  ((pos) >= (gb)->gap_start)
#define GAP_BUFFER_ACTUAL_POS(gb, pos) \
    (GAP_BUFFER_BEFORE_GAP(gb, pos) ? (pos) : (pos) + ((gb)->gap_end - (gb)->gap_start))

// Error codes
#define GAP_BUFFER_SUCCESS     0
#define GAP_BUFFER_ERROR      -1
#define GAP_BUFFER_OUT_OF_MEM -2
#define GAP_BUFFER_INVALID    -3
#define GAP_BUFFER_RANGE      -4

#endif // UEMACS_GAPBUFFER_H