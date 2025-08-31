// gapbuffer.c - Gap buffer implementation for efficient text storage

#include "μemacs/gapbuffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "memory.h"
#include "utf8.h"
#include "../text/boyer_moore.h"

// Global statistics
struct gap_buffer_stats gap_buffer_global_stats = {0};

// Create a new gap buffer with initial capacity
struct gap_buffer *gap_buffer_create(size_t initial_capacity) {
    if (initial_capacity < GAP_BUFFER_MIN_SIZE) {
        initial_capacity = GAP_BUFFER_MIN_SIZE;
    }
    
    struct gap_buffer *gb = safe_alloc(sizeof(struct gap_buffer), 
                                      "gap buffer", __FILE__, __LINE__);
    if (!gb) return NULL;
    
    gb->data = safe_alloc(initial_capacity, "gap buffer data", __FILE__, __LINE__);
    if (!gb->data) {
        SAFE_FREE(gb);
        return NULL;
    }
    
    gb->capacity = initial_capacity;
    gb->gap_start = 0;
    gb->gap_end = initial_capacity;
    gb->logical_size = 0;
    atomic_init(&gb->generation, 0);
    
    // Initialize line index
    gb->line_idx.offsets = safe_alloc(LINE_INDEX_CHUNK * sizeof(size_t),
                                     "line index", __FILE__, __LINE__);
    if (!gb->line_idx.offsets) {
        SAFE_FREE(gb->data);
        SAFE_FREE(gb);
        return NULL;
    }
    gb->line_idx.count = 1;
    gb->line_idx.capacity = LINE_INDEX_CHUNK;
    gb->line_idx.offsets[0] = 0; // First line starts at offset 0
    atomic_init(&gb->line_idx.dirty, false);
    
    // Initialize character cache
    gb->char_cache.byte_offset = 0;
    gb->char_cache.char_offset = 0;
    gb->char_cache.line_num = 0;
    atomic_init(&gb->char_cache.valid, true);
    
    return gb;
}

// Destroy gap buffer and free all resources
void gap_buffer_destroy(struct gap_buffer *gb) {
    if (!gb) return;
    
    SAFE_FREE(gb->data);
    SAFE_FREE(gb->line_idx.offsets);
    SAFE_FREE(gb);
}

// Move gap to specified position - O(n) worst case, but amortized O(1)
static int move_gap_to(struct gap_buffer *gb, size_t pos) {
    if (pos == gb->gap_start) return GAP_BUFFER_SUCCESS;
    
    size_t gap_size = gb->gap_end - gb->gap_start;
    
    if (pos < gb->gap_start) {
        // Move gap left: shift text right
        size_t move_size = gb->gap_start - pos;
        memmove(&gb->data[pos + gap_size], &gb->data[pos], move_size);
        gb->gap_start = pos;
        gb->gap_end = pos + gap_size;
    } else {
        // Move gap right: shift text left
        size_t move_size = pos - gb->gap_start;
        memmove(&gb->data[gb->gap_start], &gb->data[gb->gap_end], move_size);
        gb->gap_start = pos;
        gb->gap_end = pos + gap_size;
    }
    
    atomic_fetch_add(&gap_buffer_global_stats.cursor_moves, 1);
    atomic_fetch_add(&gb->generation, 1);
    return GAP_BUFFER_SUCCESS;
}

// Expand gap buffer capacity
static int expand_gap_buffer(struct gap_buffer *gb, size_t min_additional) {
    size_t new_capacity = gb->capacity;
    
    // Calculate new capacity using growth factor
    while (new_capacity - gb->logical_size < min_additional) {
        new_capacity = (size_t)(new_capacity * GAP_BUFFER_GROW_FACTOR);
    }
    
    char *new_data = safe_alloc(new_capacity, "gap buffer expansion", __FILE__, __LINE__);
    if (!new_data) return GAP_BUFFER_OUT_OF_MEM;
    
    // Copy data before gap
    memcpy(new_data, gb->data, gb->gap_start);
    
    // Copy data after gap
    size_t after_gap_size = gb->capacity - gb->gap_end;
    memcpy(&new_data[new_capacity - after_gap_size], 
           &gb->data[gb->gap_end], after_gap_size);
    
    SAFE_FREE(gb->data);
    gb->data = new_data;
    gb->gap_end = new_capacity - after_gap_size;
    gb->capacity = new_capacity;
    
    atomic_fetch_add(&gap_buffer_global_stats.expansions, 1);
    atomic_fetch_add(&gb->generation, 1);
    return GAP_BUFFER_SUCCESS;
}

// Insert text at specified position
int gap_buffer_insert(struct gap_buffer *gb, size_t pos, const char *text, size_t len) {
    if (!gb || !text || pos > gb->logical_size) {
        return GAP_BUFFER_INVALID;
    }
    
    // Ensure gap has enough space
    size_t gap_size = gb->gap_end - gb->gap_start;
    if (gap_size < len) {
        if (expand_gap_buffer(gb, len - gap_size) != GAP_BUFFER_SUCCESS) {
            return GAP_BUFFER_OUT_OF_MEM;
        }
    }
    
    // Move gap to insertion position
    if (move_gap_to(gb, pos) != GAP_BUFFER_SUCCESS) {
        return GAP_BUFFER_ERROR;
    }
    
    // Insert text into gap
    memcpy(&gb->data[gb->gap_start], text, len);
    gb->gap_start += len;
    gb->logical_size += len;
    
    // Invalidate caches
    atomic_store(&gb->line_idx.dirty, true);
    atomic_store(&gb->char_cache.valid, false);
    
    atomic_fetch_add(&gap_buffer_global_stats.insertions, 1);
    atomic_fetch_add(&gb->generation, 1);
    return GAP_BUFFER_SUCCESS;
}

// Delete text at specified position
int gap_buffer_delete(struct gap_buffer *gb, size_t pos, size_t len) {
    if (!gb || pos > gb->logical_size || pos + len > gb->logical_size) {
        return GAP_BUFFER_INVALID;
    }
    
    // Move gap to deletion position
    if (move_gap_to(gb, pos) != GAP_BUFFER_SUCCESS) {
        return GAP_BUFFER_ERROR;
    }
    
    // Extend gap to include deleted text
    gb->gap_end += len;
    gb->logical_size -= len;
    
    // Compact gap if it becomes too large
    if (gb->gap_end - gb->gap_start > GAP_BUFFER_MAX_GAP) {
        gap_buffer_compact(gb);
    }
    
    // Invalidate caches
    atomic_store(&gb->line_idx.dirty, true);
    atomic_store(&gb->char_cache.valid, false);
    
    atomic_fetch_add(&gap_buffer_global_stats.deletions, 1);
    atomic_fetch_add(&gb->generation, 1);
    return GAP_BUFFER_SUCCESS;
}

// Set cursor position
int gap_buffer_set_cursor(struct gap_buffer *gb, size_t pos) {
    if (!gb || pos > gb->logical_size) {
        return GAP_BUFFER_INVALID;
    }
    
    return move_gap_to(gb, pos);
}

// Get current cursor position
size_t gap_buffer_get_cursor(struct gap_buffer *gb) {
    return gb ? gb->gap_start : 0;
}

// Get character at position
char gap_buffer_get_char(struct gap_buffer *gb, size_t pos) {
    if (!gb || pos >= gb->logical_size) {
        return '\0';
    }
    
    if (pos < gb->gap_start) {
        return gb->data[pos];
    } else {
        return gb->data[pos + (gb->gap_end - gb->gap_start)];
    }
}

// Get text range
size_t gap_buffer_get_text(struct gap_buffer *gb, size_t pos, size_t len,
                          char *buffer, size_t buffer_size) {
    if (!gb || !buffer || pos > gb->logical_size) {
        return 0;
    }
    
    if (pos + len > gb->logical_size) {
        len = gb->logical_size - pos;
    }
    if (len > buffer_size) {
        len = buffer_size;
    }
    
    size_t copied = 0;
    size_t gap_size = gb->gap_end - gb->gap_start;
    
    if (pos < gb->gap_start) {
        // Copy from before gap
        size_t before_gap = gb->gap_start - pos;
        size_t copy_before = (before_gap < len) ? before_gap : len;
        memcpy(buffer, &gb->data[pos], copy_before);
        copied += copy_before;
        len -= copy_before;
        pos = gb->gap_start;
    }
    
    if (len > 0 && pos >= gb->gap_start) {
        // Copy from after gap
        size_t actual_pos = pos + gap_size;
        memcpy(&buffer[copied], &gb->data[actual_pos], len);
        copied += len;
    }
    
    return copied;
}

// Rebuild line index for O(log n) line navigation
void gap_buffer_rebuild_line_index(struct gap_buffer *gb) {
    if (!gb) return;
    
    gb->line_idx.count = 0;
    size_t gap_size = gb->gap_end - gb->gap_start;
    
    // Scan text and record line starts
    gb->line_idx.offsets[0] = 0;
    gb->line_idx.count = 1;
    
    // Scan before gap
    for (size_t i = 0; i < gb->gap_start; i++) {
        if (gb->data[i] == '\n') {
            // Ensure capacity
            if (gb->line_idx.count >= gb->line_idx.capacity) {
                size_t new_capacity = gb->line_idx.capacity + LINE_INDEX_CHUNK;
                size_t *new_offsets = SAFE_REALLOC(gb->line_idx.offsets, 
                                            new_capacity * sizeof(size_t), "gapbuffer");
                if (!new_offsets) return; // Keep old index
                gb->line_idx.offsets = new_offsets;
                gb->line_idx.capacity = new_capacity;
            }
            gb->line_idx.offsets[gb->line_idx.count++] = i + 1;
        }
    }
    
    // Scan after gap
    for (size_t i = gb->gap_end; i < gb->capacity; i++) {
        if (gb->data[i] == '\n') {
            // Ensure capacity
            if (gb->line_idx.count >= gb->line_idx.capacity) {
                size_t new_capacity = gb->line_idx.capacity + LINE_INDEX_CHUNK;
                size_t *new_offsets = SAFE_REALLOC(gb->line_idx.offsets,
                                            new_capacity * sizeof(size_t), "gapbuffer");
                if (!new_offsets) return; // Keep old index
                gb->line_idx.offsets = new_offsets;
                gb->line_idx.capacity = new_capacity;
            }
            // Adjust offset to account for gap
            gb->line_idx.offsets[gb->line_idx.count++] = i - gap_size + 1;
        }
    }
    
    atomic_store(&gb->line_idx.dirty, false);
}

// Get line count
size_t gap_buffer_line_count(struct gap_buffer *gb) {
    if (!gb) return 0;
    
    if (atomic_load(&gb->line_idx.dirty)) {
        gap_buffer_rebuild_line_index(gb);
    }
    
    return gb->line_idx.count;
}

// Convert line number to byte offset
size_t gap_buffer_line_to_offset(struct gap_buffer *gb, size_t line_num) {
    if (!gb || line_num >= gap_buffer_line_count(gb)) {
        return gb ? gb->logical_size : 0;
    }
    
    return gb->line_idx.offsets[line_num];
}

// Convert byte offset to line number - O(log n) binary search
size_t gap_buffer_offset_to_line(struct gap_buffer *gb, size_t offset) {
    if (!gb || offset > gb->logical_size) {
        return 0;
    }
    
    if (atomic_load(&gb->line_idx.dirty)) {
        gap_buffer_rebuild_line_index(gb);
    }
    
    // Binary search in line index
    size_t left = 0;
    size_t right = gb->line_idx.count - 1;
    
    while (left < right) {
        size_t mid = left + (right - left + 1) / 2;
        if (gb->line_idx.offsets[mid] <= offset) {
            left = mid;
        } else {
            right = mid - 1;
        }
    }
    
    return left;
}

// Get buffer size (excluding gap)
size_t gap_buffer_size(struct gap_buffer *gb) {
    return gb ? gb->logical_size : 0;
}

// Get buffer capacity
size_t gap_buffer_capacity(struct gap_buffer *gb) {
    return gb ? gb->capacity : 0;
}

// Get current gap size
size_t gap_buffer_gap_size(struct gap_buffer *gb) {
    return gb ? (gb->gap_end - gb->gap_start) : 0;
}

// Calculate fragmentation ratio
double gap_buffer_fragmentation(struct gap_buffer *gb) {
    if (!gb || gb->capacity == 0) return 0.0;
    return (double)(gb->gap_end - gb->gap_start) / gb->capacity;
}

// Compact gap buffer by removing unused space
int gap_buffer_compact(struct gap_buffer *gb) {
    if (!gb) return GAP_BUFFER_INVALID;
    
    size_t new_capacity = gb->logical_size + GAP_BUFFER_MIN_SIZE;
    if (new_capacity >= gb->capacity) {
        return GAP_BUFFER_SUCCESS; // Already compact enough
    }
    
    char *new_data = safe_alloc(new_capacity, "gap buffer compaction", __FILE__, __LINE__);
    if (!new_data) return GAP_BUFFER_OUT_OF_MEM;
    
    // Copy data before gap
    memcpy(new_data, gb->data, gb->gap_start);
    
    // Copy data after gap
    size_t after_gap_size = gb->capacity - gb->gap_end;
    memcpy(&new_data[gb->gap_start], &gb->data[gb->gap_end], after_gap_size);
    
    SAFE_FREE(gb->data);
    gb->data = new_data;
    gb->capacity = new_capacity;
    gb->gap_start = gb->logical_size;
    gb->gap_end = new_capacity;
    
    atomic_fetch_add(&gap_buffer_global_stats.compactions, 1);
    atomic_fetch_add(&gb->generation, 1);
    return GAP_BUFFER_SUCCESS;
}

// Boyer-Moore search forward
size_t gap_buffer_search_forward(struct gap_buffer *gb, size_t start_pos,
                                const char *pattern, size_t pattern_len) {
    if (!gb || !pattern || pattern_len == 0 || start_pos >= gb->logical_size) {
        return gb ? gb->logical_size : 0; // Not found
    }

    // Materialize a contiguous view from start_pos..end for Boyer-Moore
    size_t remaining = gb->logical_size - start_pos;
    unsigned char *text = SAFE_ARRAY(unsigned char, remaining, "gap buffer search window");
    if (!text) return gb->logical_size;

    // Copy bytes from gap buffer into contiguous array
    for (size_t i = 0; i < remaining; i++) {
        text[i] = (unsigned char)gap_buffer_get_char(gb, start_pos + i);
    }

    struct boyer_moore_context ctx;
    if (bm_init(&ctx, (const unsigned char *)pattern, (int)pattern_len, true) != 0) {
        SAFE_FREE(text);
        return gb->logical_size;
    }

    int pos = bm_search(&ctx, text, (int)remaining, 0);
    bm_free(&ctx);
    SAFE_FREE(text);

    if (pos >= 0) {
        return start_pos + (size_t)pos;
    }
    return gb->logical_size; // Not found
}

// Invalidate all caches
void gap_buffer_invalidate_caches(struct gap_buffer *gb) {
    if (!gb) return;
    
    atomic_store(&gb->line_idx.dirty, true);
    atomic_store(&gb->char_cache.valid, false);
    atomic_fetch_add(&gb->generation, 1);
}

#ifdef DEBUG
// Debug: dump gap buffer statistics
void gap_buffer_dump_stats(struct gap_buffer *gb) {
    if (!gb) return;
    
    mlwrite("Gap Buffer Statistics:");
    mlwrite("  Logical size: %zu bytes", gb->logical_size);
    mlwrite("  Capacity: %zu bytes", gb->capacity);
    mlwrite("  Gap: [%zu, %zu) = %zu bytes", 
           gb->gap_start, gb->gap_end, gb->gap_end - gb->gap_start);
    mlwrite("  Fragmentation: %.2f%%", gap_buffer_fragmentation(gb) * 100.0);
    mlwrite("  Lines: %zu", gap_buffer_line_count(gb));
    mlwrite("  Generation: %u", atomic_load(&gb->generation));
    
    mlwrite("Global Statistics:");
    mlwrite("  Insertions: %zu", atomic_load(&gap_buffer_global_stats.insertions));
    mlwrite("  Deletions: %zu", atomic_load(&gap_buffer_global_stats.deletions));
    mlwrite("  Cursor moves: %zu", atomic_load(&gap_buffer_global_stats.cursor_moves));
    mlwrite("  Expansions: %zu", atomic_load(&gap_buffer_global_stats.expansions));
    mlwrite("  Compactions: %zu", atomic_load(&gap_buffer_global_stats.compactions));
}
// Validate gap buffer integrity
void gap_buffer_validate(struct gap_buffer *gb) {
    if (!gb) return;
    
    assert(gb->gap_start <= gb->gap_end);
    assert(gb->gap_end <= gb->capacity);
    assert(gb->logical_size == gb->gap_start + (gb->capacity - gb->gap_end));
    
    mlwrite("Gap buffer validation: PASSED");
}
#endif
