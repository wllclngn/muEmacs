/*
 * piece_table.c - Piece table storage backend for μEmacs
 *
 * Implements the text_storage vtable interface using piece table algorithm.
 *
 * Key characteristics:
 *   - Original file mmap'd read-only (never modified)
 *   - Edits append to add buffer, pieces track spans
 *   - O(1) file load, O(log p) edits, O(n) sequential access
 *
 * C23 compliant
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "internal/piece_table.h"
#include "internal/text_storage.h"
#include "memory.h"
#include "utf8.h"
#include "μemacs/utf8_optimized.h"
#include "util/logger.h"
#include <sublimation_text.h>

/* Constants */

#define ADD_BUFFER_INITIAL    4096
#define ADD_BUFFER_GROW_FACTOR 2
#define LINE_INDEX_INITIAL    256

/* Forward Declarations */

static void pt_destroy(struct text_storage *ts);
static int pt_insert(struct text_storage *ts, size_t pos, const char *text, size_t len);
static int pt_delete(struct text_storage *ts, size_t pos, size_t len);
static int pt_replace(struct text_storage *ts, size_t pos, size_t old_len,
                      const char *new_text, size_t new_len);
static char pt_get_char(struct text_storage *ts, size_t pos);
static size_t pt_get_text(struct text_storage *ts, size_t pos, size_t len,
                          char *buf, size_t buf_size);
static const char *pt_get_line(struct text_storage *ts, size_t line_num, size_t *length);
static size_t pt_size(struct text_storage *ts);
static int pt_set_cursor(struct text_storage *ts, size_t pos);
static size_t pt_get_cursor(struct text_storage *ts);
static size_t pt_line_count(struct text_storage *ts);
static size_t pt_line_to_offset(struct text_storage *ts, size_t line_num);
static size_t pt_offset_to_line(struct text_storage *ts, size_t offset);
static size_t pt_line_length(struct text_storage *ts, size_t line_num);
static size_t pt_char_to_byte(struct text_storage *ts, size_t line_num, size_t char_pos);
static size_t pt_byte_to_char(struct text_storage *ts, size_t line_num, size_t byte_pos);
static size_t pt_char_count(struct text_storage *ts, size_t line_num);
static uint32_t pt_generation(struct text_storage *ts);
static void pt_invalidate_caches(struct text_storage *ts);
static int pt_compact(struct text_storage *ts);
static int pt_reserve(struct text_storage *ts, size_t additional);
static size_t pt_search_forward(struct text_storage *ts, size_t start,
                                const char *pattern, size_t pattern_len);
static size_t pt_search_backward(struct text_storage *ts, size_t start,
                                 const char *pattern, size_t pattern_len);
static text_storage_type_t pt_type(struct text_storage *ts);
static struct text_storage *pt_convert_to(struct text_storage *ts, text_storage_type_t target);

/* Operations Table */

static const struct text_storage_ops piece_table_ops = {
    .destroy            = pt_destroy,
    .insert             = pt_insert,
    .delete             = pt_delete,
    .replace            = pt_replace,
    .get_char           = pt_get_char,
    .get_text           = pt_get_text,
    .get_line           = pt_get_line,
    .size               = pt_size,
    .set_cursor         = pt_set_cursor,
    .get_cursor         = pt_get_cursor,
    .line_count         = pt_line_count,
    .line_to_offset     = pt_line_to_offset,
    .offset_to_line     = pt_offset_to_line,
    .line_length        = pt_line_length,
    .char_to_byte       = pt_char_to_byte,
    .byte_to_char       = pt_byte_to_char,
    .char_count         = pt_char_count,
    .generation         = pt_generation,
    .invalidate_caches  = pt_invalidate_caches,
    .compact            = pt_compact,
    .reserve            = pt_reserve,
    .search_forward     = pt_search_forward,
    .search_backward    = pt_search_backward,
    .type               = pt_type,
    .convert_to         = pt_convert_to,
};

/* Helper Macros */

#define TO_PT(ts) ((piece_table_t *)(ts))

/* Internal Helpers */

/*
 * Get source pointer for a piece.
 */
static inline const char *piece_source(piece_table_t *pt, piece_t *p) {
    return p->is_original ? pt->original : pt->add_buffer;
}

/*
 * Allocate a new piece.
 */
static piece_t *piece_alloc(bool is_original, size_t start, size_t length) {
    piece_t *p = safe_alloc(sizeof(piece_t), "piece", __FILE__, __LINE__);
    if (!p) return nullptr;
    p->is_original = is_original;
    p->start = start;
    p->length = length;
    p->next = p->prev = nullptr;
    return p;
}

/*
 * Free a piece.
 */
static void piece_free(piece_t *p) {
    SAFE_FREE(p);
}

/*
 * Insert piece after given piece (or at head if after is nullptr).
 */
static void piece_insert_after(piece_table_t *pt, piece_t *after, piece_t *p) {
    if (!after) {
        /* Insert at head */
        p->next = pt->head;
        p->prev = nullptr;
        if (pt->head) pt->head->prev = p;
        pt->head = p;
        if (!pt->tail) pt->tail = p;
    } else {
        p->next = after->next;
        p->prev = after;
        if (after->next) after->next->prev = p;
        after->next = p;
        if (pt->tail == after) pt->tail = p;
    }
    pt->piece_count++;
}

/*
 * Remove piece from list.
 */
static void piece_remove(piece_table_t *pt, piece_t *p) {
    if (p->prev) p->prev->next = p->next;
    else pt->head = p->next;
    if (p->next) p->next->prev = p->prev;
    else pt->tail = p->prev;
    pt->piece_count--;
}

/*
 * Ensure add buffer has room for additional bytes.
 */
static int add_buffer_ensure(piece_table_t *pt, size_t additional) {
    size_t needed = pt->add_size + additional;
    if (needed <= pt->add_capacity) return TS_SUCCESS;

    size_t new_cap = pt->add_capacity ? pt->add_capacity : ADD_BUFFER_INITIAL;
    while (new_cap < needed) new_cap *= ADD_BUFFER_GROW_FACTOR;

    char *new_buf = SAFE_REALLOC(pt->add_buffer, new_cap, "pt add buffer");
    if (!new_buf) return TS_OUT_OF_MEM;

    pt->add_buffer = new_buf;
    pt->add_capacity = new_cap;
    return TS_SUCCESS;
}

/*
 * Append text to add buffer, return start offset.
 */
static size_t add_buffer_append(piece_table_t *pt, const char *text, size_t len) {
    size_t start = pt->add_size;
    memcpy(pt->add_buffer + start, text, len);
    pt->add_size += len;
    return start;
}

/*
 * Find piece containing logical position and offset within it.
 */
piece_t *piece_table_find_piece(piece_table_t *pt, size_t logical_pos,
                                 size_t *offset_in_piece) {
    size_t pos = 0;
    for (piece_t *p = pt->head; p; p = p->next) {
        if (pos + p->length > logical_pos) {
            if (offset_in_piece) *offset_in_piece = logical_pos - pos;
            return p;
        }
        pos += p->length;
    }

    /* Position at end */
    if (offset_in_piece) *offset_in_piece = 0;
    return pt->tail;
}

/* Creation Functions */

/*
 * Initialize base storage fields.
 */
static void pt_init_base(piece_table_t *pt) {
    pt->base.ops = &piece_table_ops;
    atomic_init(&pt->base.generation, 0);
    atomic_init(&pt->base.edit_count, 0);
    atomic_init(&pt->base.total_chars_edited, 0);
}

struct text_storage *piece_table_create_from_mmap(int fd, size_t file_size) {
    piece_table_t *pt = safe_alloc(sizeof(piece_table_t), "piece_table", __FILE__, __LINE__);
    if (!pt) return nullptr;

    memset(pt, 0, sizeof(*pt));
    pt_init_base(pt);

    /* mmap the file read-only */
    if (file_size > 0) {
        pt->original = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (pt->original == MAP_FAILED) {
            SAFE_FREE(pt);
            return nullptr;
        }
        pt->original_size = file_size;
        pt->original_fd = fd;

        /* Advise kernel about access pattern */
        madvise(pt->original, file_size, MADV_SEQUENTIAL);

        /* Create single piece for entire file */
        piece_t *p = piece_alloc(true, 0, file_size);
        if (!p) {
            munmap(pt->original, file_size);
            SAFE_FREE(pt);
            return nullptr;
        }
        pt->head = pt->tail = p;
        pt->piece_count = 1;
        pt->logical_size = file_size;
    } else {
        pt->original_fd = -1;
    }

    pt->base.original_size = file_size;
    atomic_init(&pt->line_idx.dirty, true);

    return &pt->base;
}

struct text_storage *piece_table_create_empty(size_t initial_capacity) {
    piece_table_t *pt = safe_alloc(sizeof(piece_table_t), "piece_table", __FILE__, __LINE__);
    if (!pt) return nullptr;

    memset(pt, 0, sizeof(*pt));
    pt_init_base(pt);
    pt->original_fd = -1;

    /* Pre-allocate add buffer */
    if (initial_capacity > 0) {
        pt->add_buffer = safe_alloc(initial_capacity, "pt add buffer", __FILE__, __LINE__);
        if (!pt->add_buffer) {
            SAFE_FREE(pt);
            return nullptr;
        }
        pt->add_capacity = initial_capacity;
    }

    atomic_init(&pt->line_idx.dirty, true);
    return &pt->base;
}

struct text_storage *piece_table_create_from_buffer(const char *data, size_t len) {
    struct text_storage *ts = piece_table_create_empty(len);
    if (!ts) return nullptr;

    if (len > 0 && data) {
        if (pt_insert(ts, 0, data, len) != TS_SUCCESS) {
            pt_destroy(ts);
            return nullptr;
        }
    }

    return ts;
}

/* Lifecycle Operations */

static void pt_destroy(struct text_storage *ts) {
    if (!ts) return;
    piece_table_t *pt = TO_PT(ts);

    /* Free pieces */
    piece_t *p = pt->head;
    while (p) {
        piece_t *next = p->next;
        piece_free(p);
        p = next;
    }

    /* Unmap original file */
    if (pt->original && pt->original_size > 0) {
        munmap(pt->original, pt->original_size);
    }

    /* Close file descriptor */
    if (pt->original_fd >= 0) {
        close(pt->original_fd);
    }

    /* Free add buffer */
    SAFE_FREE(pt->add_buffer);

    /* Free line index */
    SAFE_FREE(pt->line_idx.offsets);

    SAFE_FREE(pt);
}

/* Core Text Operations */

static int pt_insert(struct text_storage *ts, size_t pos, const char *text, size_t len) {
    if (!text || len == 0) return TS_SUCCESS;
    piece_table_t *pt = TO_PT(ts);

    if (pos > pt->logical_size) return TS_RANGE;

    /* Ensure add buffer space */
    if (add_buffer_ensure(pt, len) != TS_SUCCESS) {
        return TS_OUT_OF_MEM;
    }

    /* Append text to add buffer */
    size_t add_start = add_buffer_append(pt, text, len);

    /* Create piece for new text */
    piece_t *new_piece = piece_alloc(false, add_start, len);
    if (!new_piece) return TS_OUT_OF_MEM;

    if (pt->head == nullptr) {
        /* Empty document */
        pt->head = pt->tail = new_piece;
        pt->piece_count = 1;
    } else if (pos == 0) {
        /* Insert at beginning */
        piece_insert_after(pt, nullptr, new_piece);
    } else if (pos == pt->logical_size) {
        /* Append at end */
        piece_insert_after(pt, pt->tail, new_piece);
    } else {
        /* Split existing piece */
        size_t offset_in_piece;
        piece_t *p = piece_table_find_piece(pt, pos, &offset_in_piece);

        if (offset_in_piece == 0) {
            /* Insert before this piece */
            piece_insert_after(pt, p->prev, new_piece);
        } else if (offset_in_piece == p->length) {
            /* Insert after this piece */
            piece_insert_after(pt, p, new_piece);
        } else {
            /* Split piece: [0..offset) + new + [offset..length) */
            piece_t *right = piece_alloc(p->is_original, p->start + offset_in_piece,
                                          p->length - offset_in_piece);
            if (!right) {
                piece_free(new_piece);
                return TS_OUT_OF_MEM;
            }

            /* Truncate left piece */
            p->length = offset_in_piece;

            /* Insert new piece and right piece */
            piece_insert_after(pt, p, new_piece);
            piece_insert_after(pt, new_piece, right);
        }
    }

    pt->logical_size += len;
    atomic_store(&pt->line_idx.dirty, true);
    text_storage_record_edit(ts, len);

    return TS_SUCCESS;
}

static int pt_delete(struct text_storage *ts, size_t pos, size_t len) {
    if (len == 0) return TS_SUCCESS;
    piece_table_t *pt = TO_PT(ts);

    if (pos + len > pt->logical_size) return TS_RANGE;

    size_t end = pos + len;
    size_t curr_pos = 0;

    /* Find and modify pieces in the delete range */
    piece_t *p = pt->head;
    while (p && curr_pos < end) {
        size_t piece_end = curr_pos + p->length;
        piece_t *next = p->next;

        if (piece_end <= pos) {
            /* Piece entirely before delete range */
            curr_pos = piece_end;
            p = next;
            continue;
        }

        if (curr_pos >= end) {
            /* Piece entirely after delete range */
            break;
        }

        /* Piece overlaps delete range */
        size_t del_start = (pos > curr_pos) ? pos - curr_pos : 0;
        size_t del_end = (end < piece_end) ? end - curr_pos : p->length;

        if (del_start == 0 && del_end == p->length) {
            /* Delete entire piece */
            piece_remove(pt, p);
            piece_free(p);
        } else if (del_start == 0) {
            /* Delete from start of piece */
            p->start += del_end;
            p->length -= del_end;
        } else if (del_end == p->length) {
            /* Delete to end of piece */
            p->length = del_start;
        } else {
            /* Delete from middle - split piece */
            piece_t *right = piece_alloc(p->is_original, p->start + del_end,
                                          p->length - del_end);
            if (!right) return TS_OUT_OF_MEM;

            p->length = del_start;
            piece_insert_after(pt, p, right);
        }

        curr_pos = piece_end;
        p = next;
    }

    pt->logical_size -= len;
    atomic_store(&pt->line_idx.dirty, true);
    text_storage_record_edit(ts, len);

    return TS_SUCCESS;
}

static int pt_replace(struct text_storage *ts, size_t pos, size_t old_len,
                      const char *new_text, size_t new_len) {
    int rc;
    if (old_len > 0) {
        rc = pt_delete(ts, pos, old_len);
        if (rc != TS_SUCCESS) return rc;
    }
    if (new_len > 0) {
        rc = pt_insert(ts, pos, new_text, new_len);
        if (rc != TS_SUCCESS) return rc;
    }
    return TS_SUCCESS;
}

/* Text Access Operations */

static char pt_get_char(struct text_storage *ts, size_t pos) {
    piece_table_t *pt = TO_PT(ts);
    if (pos >= pt->logical_size) return '\0';

    size_t offset;
    piece_t *p = piece_table_find_piece(pt, pos, &offset);
    if (!p) return '\0';

    const char *src = piece_source(pt, p);
    return src[p->start + offset];
}

static size_t pt_get_text(struct text_storage *ts, size_t pos, size_t len,
                          char *buf, size_t buf_size) {
    piece_table_t *pt = TO_PT(ts);

    if (pos >= pt->logical_size) return 0;
    if (pos + len > pt->logical_size) len = pt->logical_size - pos;
    if (len > buf_size) len = buf_size;

    size_t copied = 0;
    size_t curr_pos = 0;

    for (piece_t *p = pt->head; p && copied < len; p = p->next) {
        size_t piece_end = curr_pos + p->length;

        if (piece_end <= pos) {
            curr_pos = piece_end;
            continue;
        }

        size_t start_in_piece = (pos > curr_pos) ? pos - curr_pos : 0;
        size_t end_in_piece = p->length;

        if (curr_pos + end_in_piece > pos + len) {
            end_in_piece = pos + len - curr_pos;
        }

        size_t copy_len = end_in_piece - start_in_piece;
        if (copied + copy_len > len) copy_len = len - copied;

        const char *src = piece_source(pt, p);
        memcpy(buf + copied, src + p->start + start_in_piece, copy_len);
        copied += copy_len;
        curr_pos = piece_end;
    }

    return copied;
}

/*
 * Get line content as contiguous string.
 * Uses thread-local buffer since pieces may be non-contiguous.
 */
static _Thread_local char pt_line_buf[8192];
static _Thread_local size_t pt_line_buf_capacity = sizeof(pt_line_buf);

static const char *pt_get_line(struct text_storage *ts, size_t line_num, size_t *length) {
    piece_table_t *pt = TO_PT(ts);

    /* Ensure line index is up to date */
    if (atomic_load(&pt->line_idx.dirty)) {
        piece_table_rebuild_line_index(pt);
    }

    if (line_num >= pt->line_idx.count) {
        if (length) *length = 0;
        return "";
    }

    size_t start = pt->line_idx.offsets[line_num];
    size_t end;

    if (line_num + 1 < pt->line_idx.count) {
        end = pt->line_idx.offsets[line_num + 1];
        /* Exclude newline */
        if (end > start) {
            char c = pt_get_char(ts, end - 1);
            if (c == '\n') end--;
        }
    } else {
        end = pt->logical_size;
    }

    size_t len = end - start;
    if (len >= pt_line_buf_capacity) {
        len = pt_line_buf_capacity - 1;
    }

    size_t copied = pt_get_text(ts, start, len, pt_line_buf, pt_line_buf_capacity);
    pt_line_buf[copied] = '\0';

    if (length) *length = copied;
    return pt_line_buf;
}

/* Size and Position Operations */

static size_t pt_size(struct text_storage *ts) {
    return TO_PT(ts)->logical_size;
}

static int pt_set_cursor(struct text_storage *ts, size_t pos) {
    piece_table_t *pt = TO_PT(ts);
    if (pos > pt->logical_size) return TS_RANGE;
    pt->cursor_pos = pos;
    return TS_SUCCESS;
}

static size_t pt_get_cursor(struct text_storage *ts) {
    return TO_PT(ts)->cursor_pos;
}

/* Line Navigation Operations */

/*
 * Rebuild line index by scanning for newlines.
 */
void piece_table_rebuild_line_index(piece_table_t *pt) {
    /* Count lines first */
    size_t line_count = 1;  /* At least one line */
    size_t pos = 0;

    for (piece_t *p = pt->head; p; p = p->next) {
        const char *src = piece_source(pt, p);
        for (size_t i = 0; i < p->length; i++) {
            if (src[p->start + i] == '\n') {
                line_count++;
            }
        }
        pos += p->length;
    }

    /* Allocate/grow index */
    if (line_count > pt->line_idx.capacity) {
        size_t new_cap = line_count + LINE_INDEX_INITIAL;
        size_t *new_offsets = SAFE_REALLOC(pt->line_idx.offsets, new_cap * sizeof(size_t), "pt line index");
        if (!new_offsets) return;  /* Leave dirty */
        pt->line_idx.offsets = new_offsets;
        pt->line_idx.capacity = new_cap;
    }

    /* Build index */
    pt->line_idx.offsets[0] = 0;
    size_t line = 0;
    pos = 0;

    for (piece_t *p = pt->head; p; p = p->next) {
        const char *src = piece_source(pt, p);
        for (size_t i = 0; i < p->length; i++) {
            if (src[p->start + i] == '\n' && line + 1 < line_count) {
                line++;
                pt->line_idx.offsets[line] = pos + i + 1;
            }
        }
        pos += p->length;
    }

    pt->line_idx.count = line_count;
    atomic_store(&pt->line_idx.dirty, false);
}

static size_t pt_line_count(struct text_storage *ts) {
    piece_table_t *pt = TO_PT(ts);
    if (atomic_load(&pt->line_idx.dirty)) {
        piece_table_rebuild_line_index(pt);
    }
    return pt->line_idx.count;
}

static size_t pt_line_to_offset(struct text_storage *ts, size_t line_num) {
    piece_table_t *pt = TO_PT(ts);
    if (atomic_load(&pt->line_idx.dirty)) {
        piece_table_rebuild_line_index(pt);
    }
    if (line_num >= pt->line_idx.count) {
        return pt->logical_size;
    }
    return pt->line_idx.offsets[line_num];
}

static size_t pt_offset_to_line(struct text_storage *ts, size_t offset) {
    piece_table_t *pt = TO_PT(ts);
    if (atomic_load(&pt->line_idx.dirty)) {
        piece_table_rebuild_line_index(pt);
    }

    /* Binary search */
    size_t lo = 0, hi = pt->line_idx.count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (pt->line_idx.offsets[mid] <= offset) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo > 0 ? lo - 1 : 0;
}

static size_t pt_line_length(struct text_storage *ts, size_t line_num) {
    piece_table_t *pt = TO_PT(ts);
    if (atomic_load(&pt->line_idx.dirty)) {
        piece_table_rebuild_line_index(pt);
    }

    if (line_num >= pt->line_idx.count) return 0;

    size_t start = pt->line_idx.offsets[line_num];
    size_t end;

    if (line_num + 1 < pt->line_idx.count) {
        end = pt->line_idx.offsets[line_num + 1];
        if (end > start) end--;  /* Exclude newline */
    } else {
        end = pt->logical_size;
    }

    return end - start;
}

/* UTF-8 Aware Operations */

static size_t pt_char_to_byte(struct text_storage *ts, size_t line_num, size_t char_pos) {
    size_t line_start = pt_line_to_offset(ts, line_num);
    size_t line_len = pt_line_length(ts, line_num);

    size_t byte_pos = 0;
    size_t char_count = 0;

    while (byte_pos < line_len && char_count < char_pos) {
        char c = pt_get_char(ts, line_start + byte_pos);
        byte_pos += (size_t)utf8_byte_length_fast((unsigned char)c);
        char_count++;
    }

    return byte_pos;
}

static size_t pt_byte_to_char(struct text_storage *ts, size_t line_num, size_t byte_pos) {
    size_t line_start = pt_line_to_offset(ts, line_num);
    size_t line_len = pt_line_length(ts, line_num);

    if (byte_pos > line_len) byte_pos = line_len;

    size_t curr_byte = 0;
    size_t char_count = 0;

    while (curr_byte < byte_pos) {
        char c = pt_get_char(ts, line_start + curr_byte);
        curr_byte += (size_t)utf8_byte_length_fast((unsigned char)c);
        char_count++;
    }

    return char_count;
}

static size_t pt_char_count(struct text_storage *ts, size_t line_num) {
    size_t line_start = pt_line_to_offset(ts, line_num);
    size_t line_len = pt_line_length(ts, line_num);

    size_t byte_pos = 0;
    size_t char_count = 0;

    while (byte_pos < line_len) {
        char c = pt_get_char(ts, line_start + byte_pos);
        byte_pos += (size_t)utf8_byte_length_fast((unsigned char)c);
        char_count++;
    }

    return char_count;
}

/* Change Tracking Operations */

static uint32_t pt_generation(struct text_storage *ts) {
    return atomic_load(&TO_PT(ts)->generation);
}

static void pt_invalidate_caches(struct text_storage *ts) {
    atomic_store(&TO_PT(ts)->line_idx.dirty, true);
    atomic_fetch_add(&TO_PT(ts)->generation, 1);
}

/* Memory Management Operations */

static int pt_compact(struct text_storage *ts) {
    /* Piece tables don't compact - consider converting to gap buffer */
    (void)ts;
    return TS_SUCCESS;
}

static int pt_reserve(struct text_storage *ts, size_t additional) {
    return add_buffer_ensure(TO_PT(ts), additional);
}

/* Search Operations */

static size_t pt_search_forward(struct text_storage *ts, size_t start,
                                const char *pattern, size_t pattern_len) {
    piece_table_t *pt = TO_PT(ts);

    if (!pattern || pattern_len == 0 || start >= pt->logical_size)
        return TS_NOT_FOUND;

    size_t remaining = pt->logical_size - start;
    if (remaining < pattern_len)
        return TS_NOT_FOUND;

    // Materialize contiguous text for the matcher
    unsigned char *text = SAFE_ARRAY(unsigned char, remaining, "pt search fwd");
    if (!text) return TS_NOT_FOUND;

    pt_get_text(ts, start, remaining, (char *)text, remaining);

    sublimation_search prog;
    sublimation_search_compile(&prog, pattern, pattern_len,
                               SUBLIMATION_SEARCH_FIXED, 0);
    if (!sublimation_search_valid(&prog)) {
        SAFE_FREE(text);
        return TS_NOT_FOUND;
    }

    long pos = sublimation_search_find(&prog, (const char *)text,
                                       remaining, nullptr);
    SAFE_FREE(text);

    return (pos >= 0) ? start + (size_t)pos : TS_NOT_FOUND;
}

static size_t pt_search_backward(struct text_storage *ts, size_t start,
                                 const char *pattern, size_t pattern_len) {
    piece_table_t *pt = TO_PT(ts);

    if (!pattern || pattern_len == 0 || start == 0)
        return TS_NOT_FOUND;

    if (start > pt->logical_size) start = pt->logical_size;
    if (start < pattern_len)
        return TS_NOT_FOUND;

    // Materialize text from 0..start for reverse search
    unsigned char *text = SAFE_ARRAY(unsigned char, start, "pt search bwd");
    if (!text) return TS_NOT_FOUND;

    pt_get_text(ts, 0, start, (char *)text, start);

    sublimation_search prog;
    sublimation_search_compile(&prog, pattern, pattern_len,
                               SUBLIMATION_SEARCH_FIXED, 0);
    if (!sublimation_search_valid(&prog)) {
        SAFE_FREE(text);
        return TS_NOT_FOUND;
    }

    /* Last match at or before start: walk find_from keeping the final hit. */
    long best = -1;
    size_t from = 0;
    for (;;) {
        long idx = sublimation_search_find_from(&prog, (const char *)text,
                                                start, from, nullptr);
        if (idx < 0) break;
        best = idx;
        from = (size_t)idx + 1;
    }
    SAFE_FREE(text);

    return (best >= 0) ? (size_t)best : TS_NOT_FOUND;
}

/* Type Information */

static text_storage_type_t pt_type(struct text_storage *ts) {
    (void)ts;
    return STORAGE_PIECE_TABLE;
}

/* Conversion */

/* Forward declaration - defined in gap_storage.c */
extern struct text_storage *gap_storage_create(size_t initial_capacity);

struct text_storage *piece_table_to_gap_buffer(piece_table_t *pt) {
    struct text_storage *gap = gap_storage_create(pt->logical_size);
    if (!gap) return nullptr;

    /* Copy all text to gap buffer */
    size_t pos = 0;
    for (piece_t *p = pt->head; p; p = p->next) {
        const char *src = piece_source(pt, p);
        if (TS_INSERT(gap, pos, src + p->start, p->length) != TS_SUCCESS) {
            TS_DESTROY(gap);
            return nullptr;
        }
        pos += p->length;
    }

    return gap;
}

static struct text_storage *pt_convert_to(struct text_storage *ts, text_storage_type_t target) {
    if (target == STORAGE_PIECE_TABLE) {
        return ts;  /* Already piece table */
    }

    if (target == STORAGE_GAP_BUFFER) {
        return piece_table_to_gap_buffer(TO_PT(ts));
    }

    return nullptr;
}

/* Utility Functions */

const char *piece_table_get_ptr(piece_table_t *pt, size_t pos, size_t *max_len) {
    size_t offset;
    piece_t *p = piece_table_find_piece(pt, pos, &offset);
    if (!p) {
        if (max_len) *max_len = 0;
        return nullptr;
    }

    const char *src = piece_source(pt, p);
    if (max_len) *max_len = p->length - offset;
    return src + p->start + offset;
}
