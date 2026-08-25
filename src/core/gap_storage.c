/*
 * gap_storage.c - Gap buffer adapter for text_storage interface
 *
 * Wraps the existing gap_buffer implementation to conform to the
 * text_storage vtable interface, enabling polymorphic storage backends.
 *
 * C23 compliant
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>

#include "internal/text_storage.h"
#include "internal/piece_table.h"
#include "gapbuffer.h"
#include "memory.h"
#include "utf8.h"
#include "μemacs/utf8_optimized.h"
#include "util/logger.h"

/* Gap Storage Structure
 *
 * Embeds text_storage as first member for polymorphic casting. */

typedef struct gap_storage {
    struct text_storage base;       /* Must be first for casting */
    struct gap_buffer *gb;          /* Underlying gap buffer */
} gap_storage_t;

/* Forward Declarations */

static void gap_storage_destroy(struct text_storage *ts);
static int gap_storage_insert(struct text_storage *ts, size_t pos, const char *text, size_t len);
static int gap_storage_delete(struct text_storage *ts, size_t pos, size_t len);
static int gap_storage_replace(struct text_storage *ts, size_t pos, size_t old_len,
                               const char *new_text, size_t new_len);
static char gap_storage_get_char(struct text_storage *ts, size_t pos);
static size_t gap_storage_get_text(struct text_storage *ts, size_t pos, size_t len,
                                   char *buf, size_t buf_size);
static const char *gap_storage_get_line(struct text_storage *ts, size_t line_num, size_t *length);
static size_t gap_storage_size(struct text_storage *ts);
static int gap_storage_set_cursor(struct text_storage *ts, size_t pos);
static size_t gap_storage_get_cursor(struct text_storage *ts);
static size_t gap_storage_line_count(struct text_storage *ts);
static size_t gap_storage_line_to_offset(struct text_storage *ts, size_t line_num);
static size_t gap_storage_offset_to_line(struct text_storage *ts, size_t offset);
static size_t gap_storage_line_length(struct text_storage *ts, size_t line_num);
static size_t gap_storage_char_to_byte(struct text_storage *ts, size_t line_num, size_t char_pos);
static size_t gap_storage_byte_to_char(struct text_storage *ts, size_t line_num, size_t byte_pos);
static size_t gap_storage_char_count(struct text_storage *ts, size_t line_num);
static uint32_t gap_storage_generation(struct text_storage *ts);
static void gap_storage_invalidate_caches(struct text_storage *ts);
static int gap_storage_compact(struct text_storage *ts);
static int gap_storage_reserve(struct text_storage *ts, size_t additional);
static size_t gap_storage_search_forward(struct text_storage *ts, size_t start,
                                         const char *pattern, size_t pattern_len);
static size_t gap_storage_search_backward(struct text_storage *ts, size_t start,
                                          const char *pattern, size_t pattern_len);
static text_storage_type_t gap_storage_type(struct text_storage *ts);
static struct text_storage *gap_storage_convert_to(struct text_storage *ts, text_storage_type_t target);

/* Operations Table */

static const struct text_storage_ops gap_storage_ops = {
    .destroy            = gap_storage_destroy,
    .insert             = gap_storage_insert,
    .delete             = gap_storage_delete,
    .replace            = gap_storage_replace,
    .get_char           = gap_storage_get_char,
    .get_text           = gap_storage_get_text,
    .get_line           = gap_storage_get_line,
    .size               = gap_storage_size,
    .set_cursor         = gap_storage_set_cursor,
    .get_cursor         = gap_storage_get_cursor,
    .line_count         = gap_storage_line_count,
    .line_to_offset     = gap_storage_line_to_offset,
    .offset_to_line     = gap_storage_offset_to_line,
    .line_length        = gap_storage_line_length,
    .char_to_byte       = gap_storage_char_to_byte,
    .byte_to_char       = gap_storage_byte_to_char,
    .char_count         = gap_storage_char_count,
    .generation         = gap_storage_generation,
    .invalidate_caches  = gap_storage_invalidate_caches,
    .compact            = gap_storage_compact,
    .reserve            = gap_storage_reserve,
    .search_forward     = gap_storage_search_forward,
    .search_backward    = gap_storage_search_backward,
    .type               = gap_storage_type,
    .convert_to         = gap_storage_convert_to,
};

/* Helper Macros */

#define TO_GAP_STORAGE(ts) ((gap_storage_t *)(ts))
#define GET_GB(ts)         (TO_GAP_STORAGE(ts)->gb)

/* Creation Functions */

/*
 * Create gap storage with given initial capacity.
 */
struct text_storage *gap_storage_create(size_t initial_capacity) {
    gap_storage_t *gs = safe_alloc(sizeof(gap_storage_t), "gap storage", __FILE__, __LINE__);
    if (!gs) return nullptr;

    gs->gb = gap_buffer_create(initial_capacity);
    if (!gs->gb) {
        SAFE_FREE(gs);
        return nullptr;
    }

    gs->base.ops = &gap_storage_ops;
    atomic_init(&gs->base.generation, 0);
    atomic_init(&gs->base.edit_count, 0);
    atomic_init(&gs->base.total_chars_edited, 0);
    gs->base.original_size = 0;

    return &gs->base;
}

/*
 * Wrap existing gap buffer in storage interface (takes ownership).
 */
struct text_storage *text_storage_from_gap_buffer(struct gap_buffer *gb) {
    if (!gb) return nullptr;

    gap_storage_t *gs = safe_alloc(sizeof(gap_storage_t), "gap storage wrapper", __FILE__, __LINE__);
    if (!gs) return nullptr;

    gs->gb = gb;
    gs->base.ops = &gap_storage_ops;
    atomic_init(&gs->base.generation, atomic_load(&gb->generation));
    atomic_init(&gs->base.edit_count, 0);
    atomic_init(&gs->base.total_chars_edited, 0);
    gs->base.original_size = gap_buffer_size(gb);

    return &gs->base;
}

/*
 * Extract underlying gap buffer (transfers ownership back to caller).
 */
struct gap_buffer *gap_storage_unwrap(struct text_storage *ts) {
    if (!ts || ts->ops != &gap_storage_ops) return nullptr;

    gap_storage_t *gs = TO_GAP_STORAGE(ts);
    struct gap_buffer *gb = gs->gb;
    gs->gb = nullptr;  /* Prevent double-free */
    SAFE_FREE(gs);
    return gb;
}

/* Lifecycle Operations */

static void gap_storage_destroy(struct text_storage *ts) {
    if (!ts) return;
    gap_storage_t *gs = TO_GAP_STORAGE(ts);
    gap_buffer_destroy(gs->gb);
    SAFE_FREE(gs);
}

/* Core Text Operations */

static int gap_storage_insert(struct text_storage *ts, size_t pos, const char *text, size_t len) {
    int result = gap_buffer_insert(GET_GB(ts), pos, text, len);
    if (result == GAP_BUFFER_SUCCESS) {
        text_storage_record_edit(ts, len);
    }
    return result;
}

static int gap_storage_delete(struct text_storage *ts, size_t pos, size_t len) {
    int result = gap_buffer_delete(GET_GB(ts), pos, len);
    if (result == GAP_BUFFER_SUCCESS) {
        text_storage_record_edit(ts, len);
    }
    return result;
}

static int gap_storage_replace(struct text_storage *ts, size_t pos, size_t old_len,
                               const char *new_text, size_t new_len) {
    struct gap_buffer *gb = GET_GB(ts);

    /* Delete old text, then insert new */
    if (old_len > 0) {
        int del_result = gap_buffer_delete(gb, pos, old_len);
        if (del_result != GAP_BUFFER_SUCCESS) return del_result;
    }

    if (new_len > 0) {
        int ins_result = gap_buffer_insert(gb, pos, new_text, new_len);
        if (ins_result != GAP_BUFFER_SUCCESS) return ins_result;
    }

    text_storage_record_edit(ts, old_len + new_len);
    return TS_SUCCESS;
}

/* Text Access Operations */

static char gap_storage_get_char(struct text_storage *ts, size_t pos) {
    return gap_buffer_get_char(GET_GB(ts), pos);
}

static size_t gap_storage_get_text(struct text_storage *ts, size_t pos, size_t len,
                                   char *buf, size_t buf_size) {
    return gap_buffer_get_text(GET_GB(ts), pos, len, buf, buf_size);
}

/*
 * Get line content as contiguous string.
 * Uses thread-local buffer since gap buffer data may span the gap.
 */
static _Thread_local char line_buf[8192];
static _Thread_local size_t line_buf_capacity = sizeof(line_buf);

static const char *gap_storage_get_line(struct text_storage *ts, size_t line_num, size_t *length) {
    struct gap_buffer *gb = GET_GB(ts);
    size_t line_count = gap_buffer_line_count(gb);

    if (line_num >= line_count) {
        if (length) *length = 0;
        return "";
    }

    size_t start = gap_buffer_line_to_offset(gb, line_num);
    size_t end;

    if (line_num + 1 < line_count) {
        end = gap_buffer_line_to_offset(gb, line_num + 1);
        /* Exclude newline */
        if (end > start && gap_buffer_get_char(gb, end - 1) == '\n') {
            end--;
        }
    } else {
        end = gap_buffer_size(gb);
    }

    size_t len = end - start;
    if (len >= line_buf_capacity) {
        len = line_buf_capacity - 1;  /* Truncate to buffer size */
    }

    size_t copied = gap_buffer_get_text(gb, start, len, line_buf, line_buf_capacity);
    line_buf[copied] = '\0';

    if (length) *length = copied;
    return line_buf;
}

/* Size and Position Operations */

static size_t gap_storage_size(struct text_storage *ts) {
    return gap_buffer_size(GET_GB(ts));
}

static int gap_storage_set_cursor(struct text_storage *ts, size_t pos) {
    return gap_buffer_set_cursor(GET_GB(ts), pos);
}

static size_t gap_storage_get_cursor(struct text_storage *ts) {
    return gap_buffer_get_cursor(GET_GB(ts));
}

/* Line Navigation Operations */

static size_t gap_storage_line_count(struct text_storage *ts) {
    return gap_buffer_line_count(GET_GB(ts));
}

static size_t gap_storage_line_to_offset(struct text_storage *ts, size_t line_num) {
    return gap_buffer_line_to_offset(GET_GB(ts), line_num);
}

static size_t gap_storage_offset_to_line(struct text_storage *ts, size_t offset) {
    return gap_buffer_offset_to_line(GET_GB(ts), offset);
}

static size_t gap_storage_line_length(struct text_storage *ts, size_t line_num) {
    struct gap_buffer *gb = GET_GB(ts);
    size_t line_count = gap_buffer_line_count(gb);
    if (line_num >= line_count) return 0;

    size_t start = gap_buffer_line_to_offset(gb, line_num);
    size_t end;
    if (line_num + 1 < line_count) {
        end = gap_buffer_line_to_offset(gb, line_num + 1);
        /* Exclude newline from length */
        if (end > start) end--;
    } else {
        end = gap_buffer_size(gb);
    }
    return end - start;
}

/* UTF-8 Aware Operations */

static size_t gap_storage_char_to_byte(struct text_storage *ts, size_t line_num, size_t char_pos) {
    struct gap_buffer *gb = GET_GB(ts);
    size_t line_start = gap_buffer_line_to_offset(gb, line_num);
    size_t line_len = gap_storage_line_length(ts, line_num);

    /* Walk UTF-8 characters to find byte offset */
    size_t byte_pos = 0;
    size_t char_count = 0;

    while (byte_pos < line_len && char_count < char_pos) {
        char c = gap_buffer_get_char(gb, line_start + byte_pos);
        byte_pos += (size_t)utf8_byte_length_fast((unsigned char)c);
        char_count++;
    }

    return byte_pos;
}

static size_t gap_storage_byte_to_char(struct text_storage *ts, size_t line_num, size_t byte_pos) {
    struct gap_buffer *gb = GET_GB(ts);
    size_t line_start = gap_buffer_line_to_offset(gb, line_num);
    size_t line_len = gap_storage_line_length(ts, line_num);

    if (byte_pos > line_len) byte_pos = line_len;

    /* Count UTF-8 characters up to byte position */
    size_t curr_byte = 0;
    size_t char_count = 0;

    while (curr_byte < byte_pos) {
        char c = gap_buffer_get_char(gb, line_start + curr_byte);
        curr_byte += (size_t)utf8_byte_length_fast((unsigned char)c);
        char_count++;
    }

    return char_count;
}

static size_t gap_storage_char_count(struct text_storage *ts, size_t line_num) {
    struct gap_buffer *gb = GET_GB(ts);
    size_t line_start = gap_buffer_line_to_offset(gb, line_num);
    size_t line_len = gap_storage_line_length(ts, line_num);

    /* Count UTF-8 characters in line */
    size_t byte_pos = 0;
    size_t char_count = 0;

    while (byte_pos < line_len) {
        char c = gap_buffer_get_char(gb, line_start + byte_pos);
        byte_pos += (size_t)utf8_byte_length_fast((unsigned char)c);
        char_count++;
    }

    return char_count;
}

/* Change Tracking Operations */

static uint32_t gap_storage_generation(struct text_storage *ts) {
    return atomic_load(&GET_GB(ts)->generation);
}

static void gap_storage_invalidate_caches(struct text_storage *ts) {
    gap_buffer_invalidate_caches(GET_GB(ts));
}

/* Memory Management Operations */

static int gap_storage_compact(struct text_storage *ts) {
    return gap_buffer_compact(GET_GB(ts));
}

static int gap_storage_reserve(struct text_storage *ts, size_t additional) {
    return gap_buffer_reserve(GET_GB(ts), additional);
}

/* Search Operations */

static size_t gap_storage_search_forward(struct text_storage *ts, size_t start,
                                         const char *pattern, size_t pattern_len) {
    return gap_buffer_search_forward(GET_GB(ts), start, pattern, pattern_len);
}

static size_t gap_storage_search_backward(struct text_storage *ts, size_t start,
                                          const char *pattern, size_t pattern_len) {
    /* gap_buffer_search_backward may not exist - implement here */
    struct gap_buffer *gb = GET_GB(ts);
    if (!pattern || pattern_len == 0 || start == 0) {
        return TS_NOT_FOUND;
    }

    /* Simple backward search - could optimize with Boyer-Moore-Horspool */
    size_t pos = start;
    while (pos >= pattern_len) {
        pos--;
        bool match = true;
        for (size_t i = 0; i < pattern_len && match; i++) {
            if (gap_buffer_get_char(gb, pos + i) != pattern[i]) {
                match = false;
            }
        }
        if (match) return pos;
    }

    return TS_NOT_FOUND;
}

/* Type Information */

static text_storage_type_t gap_storage_type(struct text_storage *ts) {
    (void)ts;
    return STORAGE_GAP_BUFFER;
}

/* Conversion */

static struct text_storage *gap_storage_convert_to(struct text_storage *ts, text_storage_type_t target) {
    if (target == STORAGE_GAP_BUFFER) {
        return ts;  /* Already gap buffer */
    }

    if (target == STORAGE_PIECE_TABLE) {
        /* TODO: Implement conversion to piece table */
        /* For now, return nullptr to indicate conversion not available */
        LOG_WARN("Gap buffer to piece table conversion not yet implemented");
        return nullptr;
    }

    return nullptr;
}

/* Factory Functions (from text_storage.h)
 *
 * These implement the public API. For now, all creation uses gap buffer
 * since piece table isn't implemented yet. When piece_table.c is added,
 * these will be moved to a separate text_storage.c with heuristic selection. */

struct text_storage *text_storage_create(size_t initial_capacity,
                                         text_storage_hint_t hint) {
    (void)hint;  /* TODO: Use hint for piece table selection */
    return gap_storage_create(initial_capacity);
}

struct text_storage *text_storage_create_from_file(int fd, size_t file_size,
                                                   text_storage_hint_t hint) {
    text_storage_type_t selected = text_storage_select_type(file_size, hint);

    if (selected == STORAGE_PIECE_TABLE) {
        /* Use piece table with mmap for large files */
        struct text_storage *ts = piece_table_create_from_mmap(fd, file_size);
        if (ts) {
            LOG_DEBUGF("Using piece table for %zu byte file", file_size);
            return ts;
        }
        /* Fall through to gap buffer on mmap failure */
        LOG_WARN("Piece table creation failed, falling back to gap buffer");
    }

    /* Use gap buffer for small files or as fallback */
    LOG_DEBUGF("Using gap buffer for %zu byte file", file_size);
    return gap_storage_create(file_size > 0 ? file_size : 1024);
}

text_storage_type_t text_storage_select_type(size_t file_size,
                                             text_storage_hint_t hint) {
    /* Heuristic selection based on file size and hint */
    if (hint == STORAGE_HINT_FORCE_GAP) {
        return STORAGE_GAP_BUFFER;
    }
    if (hint == STORAGE_HINT_FORCE_PIECE) {
        return STORAGE_PIECE_TABLE;
    }

    size_t threshold = STORAGE_THRESHOLD_DEFAULT;
    if (hint == STORAGE_HINT_READONLY) {
        threshold = STORAGE_THRESHOLD_READONLY;
    } else if (hint == STORAGE_HINT_HEAVY_EDIT) {
        threshold = STORAGE_THRESHOLD_HEAVY_EDIT;
    }

    if (file_size >= STORAGE_FORCE_PIECE_TABLE) {
        return STORAGE_PIECE_TABLE;
    }

    return (file_size >= threshold) ? STORAGE_PIECE_TABLE : STORAGE_GAP_BUFFER;
}

bool text_storage_should_convert(struct text_storage *ts) {
    if (!ts) return false;

    text_storage_type_t current_type = TS_TYPE(ts);
    size_t edit_count = atomic_load(&ts->edit_count);
    size_t total_edited = atomic_load(&ts->total_chars_edited);
    size_t original_size = ts->original_size;

    if (current_type == STORAGE_PIECE_TABLE && original_size > 0) {
        /* Check edit density: if > 20%, consider converting to gap buffer */
        double density = (double)total_edited / (double)original_size;
        if (density > STORAGE_EDIT_DENSITY_CONVERT) {
            return true;
        }

        /* Check fragmentation */
        /* TODO: Get piece count from piece table implementation */
    }

    /* Gap buffer doesn't need conversion unless file grows huge */
    if (current_type == STORAGE_GAP_BUFFER) {
        size_t current_size = TS_SIZE(ts);
        if (current_size >= STORAGE_FORCE_PIECE_TABLE && edit_count < 100) {
            /* Large file with few edits - might benefit from piece table */
            return true;
        }
    }

    return false;
}

struct text_storage *text_storage_maybe_convert(struct text_storage *ts) {
    if (!text_storage_should_convert(ts)) {
        return ts;
    }

    text_storage_type_t current = TS_TYPE(ts);
    text_storage_type_t target = (current == STORAGE_GAP_BUFFER)
                                 ? STORAGE_PIECE_TABLE
                                 : STORAGE_GAP_BUFFER;

    return TS_CONVERT(ts, target);
}

/* Debug Support */

#ifdef UEMACS_DEBUG_LOG
void text_storage_dump_stats(struct text_storage *ts) {
    if (!ts) return;
    LOG_DEBUGF("text_storage: type=%s size=%zu edits=%zu total_edited=%zu",
               text_storage_type_name(TS_TYPE(ts)),
               TS_SIZE(ts),
               atomic_load(&ts->edit_count),
               atomic_load(&ts->total_chars_edited));
}

const char *text_storage_type_name(text_storage_type_t type) {
    switch (type) {
        case STORAGE_GAP_BUFFER:  return "gap_buffer";
        case STORAGE_PIECE_TABLE: return "piece_table";
        case STORAGE_AUTO:        return "auto";
        default:                  return "unknown";
    }
}
#endif
