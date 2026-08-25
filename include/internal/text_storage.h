/*
 * text_storage.h - Abstract text storage interface for μEmacs
 *
 * Provides a vtable-based abstraction allowing multiple storage backends:
 *   - Gap buffer: O(1) at cursor, optimal for small files with high edit locality
 *   - Piece table: O(1) load via mmap, optimal for large files with sparse edits
 *
 * Heuristic selection based on file size and usage patterns.
 *
 * C23 compliant
 */

#ifndef UEMACS_TEXT_STORAGE_H
#define UEMACS_TEXT_STORAGE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

/* Storage Backend Types */

typedef enum {
    STORAGE_GAP_BUFFER  = 0,    /* Traditional gap buffer (default) */
    STORAGE_PIECE_TABLE = 1,    /* Piece table with mmap */
    STORAGE_AUTO        = 2,    /* Heuristic selection based on file size */
} text_storage_type_t;

/* Heuristic Thresholds
 *
 * Derived from crossover analysis:
 *   - Gap buffer: O(n) load, O(1) cursor edit, excellent cache locality
 *   - Piece table: O(1) load via mmap, O(log p) edit, minimal memory for viewing
 *
 * Crossover point ~10MB where mmap lazy loading advantage outweighs
 * gap buffer's locality benefits. */

#define STORAGE_THRESHOLD_DEFAULT     (2UL * 1024 * 1024)    /* 2 MB - lowered for faster mmap loading */
#define STORAGE_THRESHOLD_READONLY    (1UL * 1024 * 1024)    /* 1 MB for viewing */
#define STORAGE_THRESHOLD_HEAVY_EDIT  (50UL * 1024 * 1024)   /* 50 MB */
#define STORAGE_FORCE_PIECE_TABLE     (100UL * 1024 * 1024)  /* 100 MB+ always piece table */
#define STORAGE_EDIT_DENSITY_CONVERT  0.20                   /* 20% triggers conversion */
#define STORAGE_MAX_PIECE_COUNT       10000                  /* Fragmentation threshold */

/* Storage Hints
 *
 * Hints modify the default heuristic behavior. */

typedef enum {
    STORAGE_HINT_DEFAULT     = 0,   /* Use size-based heuristics */
    STORAGE_HINT_READONLY    = 1,   /* Viewing logs, prefer piece table earlier */
    STORAGE_HINT_HEAVY_EDIT  = 2,   /* Heavy editing, prefer gap buffer longer */
    STORAGE_HINT_FORCE_GAP   = 3,   /* Always use gap buffer */
    STORAGE_HINT_FORCE_PIECE = 4,   /* Always use piece table */
} text_storage_hint_t;

/* Forward Declarations */

struct text_storage;
struct text_storage_ops;
struct gap_buffer;

/* Storage Operations Interface (vtable)
 *
 * All operations return 0 on success, negative on error unless specified. */

struct text_storage_ops {
    /* Lifecycle */
    void (*destroy)(struct text_storage *ts);

    /* Core text operations */
    int (*insert)(struct text_storage *ts, size_t pos, const char *text, size_t len);
    int (*delete)(struct text_storage *ts, size_t pos, size_t len);
    int (*replace)(struct text_storage *ts, size_t pos, size_t old_len,
                   const char *new_text, size_t new_len);

    /* Text access */
    char (*get_char)(struct text_storage *ts, size_t pos);
    size_t (*get_text)(struct text_storage *ts, size_t pos, size_t len,
                       char *buf, size_t buf_size);
    const char *(*get_line)(struct text_storage *ts, size_t line_num, size_t *length);

    /* Size and position */
    size_t (*size)(struct text_storage *ts);
    int (*set_cursor)(struct text_storage *ts, size_t pos);
    size_t (*get_cursor)(struct text_storage *ts);

    /* Line navigation */
    size_t (*line_count)(struct text_storage *ts);
    size_t (*line_to_offset)(struct text_storage *ts, size_t line_num);
    size_t (*offset_to_line)(struct text_storage *ts, size_t offset);
    size_t (*line_length)(struct text_storage *ts, size_t line_num);

    /* UTF-8 aware operations */
    size_t (*char_to_byte)(struct text_storage *ts, size_t line_num, size_t char_pos);
    size_t (*byte_to_char)(struct text_storage *ts, size_t line_num, size_t byte_pos);
    size_t (*char_count)(struct text_storage *ts, size_t line_num);

    /* Change tracking */
    uint32_t (*generation)(struct text_storage *ts);
    void (*invalidate_caches)(struct text_storage *ts);

    /* Memory management */
    int (*compact)(struct text_storage *ts);
    int (*reserve)(struct text_storage *ts, size_t additional);

    /* Search */
    size_t (*search_forward)(struct text_storage *ts, size_t start,
                             const char *pattern, size_t pattern_len);
    size_t (*search_backward)(struct text_storage *ts, size_t start,
                              const char *pattern, size_t pattern_len);

    /* Type information */
    text_storage_type_t (*type)(struct text_storage *ts);

    /* Conversion (returns new storage, caller must free old) */
    struct text_storage *(*convert_to)(struct text_storage *ts, text_storage_type_t target);
};

/* Base Storage Structure
 *
 * All backends embed this as their first member for polymorphism via casting. */

struct text_storage {
    const struct text_storage_ops *ops;

    /* Change tracking for display caching */
    _Atomic uint32_t generation;

    /* Conversion heuristics */
    _Atomic size_t edit_count;           /* Number of edit operations */
    _Atomic size_t total_chars_edited;   /* Total bytes inserted + deleted */
    size_t original_size;                /* Size at creation (for density calc) */
};

/* Convenience Macros
 *
 * These provide a clean interface hiding the vtable indirection. */

#define TS_DESTROY(ts)              ((ts)->ops->destroy((ts)))
#define TS_INSERT(ts, pos, txt, ln) ((ts)->ops->insert((ts), (pos), (txt), (ln)))
#define TS_DELETE(ts, pos, len)     ((ts)->ops->delete((ts), (pos), (len)))
#define TS_REPLACE(ts, p, ol, nt, nl) ((ts)->ops->replace((ts), (p), (ol), (nt), (nl)))
#define TS_GET_CHAR(ts, pos)        ((ts)->ops->get_char((ts), (pos)))
#define TS_GET_TEXT(ts, p, l, b, s) ((ts)->ops->get_text((ts), (p), (l), (b), (s)))
#define TS_GET_LINE(ts, ln, len)    ((ts)->ops->get_line((ts), (ln), (len)))
#define TS_SIZE(ts)                 ((ts)->ops->size((ts)))
#define TS_SET_CURSOR(ts, pos)      ((ts)->ops->set_cursor((ts), (pos)))
#define TS_GET_CURSOR(ts)           ((ts)->ops->get_cursor((ts)))
#define TS_LINE_COUNT(ts)           ((ts)->ops->line_count((ts)))
#define TS_LINE_TO_OFFSET(ts, ln)   ((ts)->ops->line_to_offset((ts), (ln)))
#define TS_OFFSET_TO_LINE(ts, off)  ((ts)->ops->offset_to_line((ts), (off)))
#define TS_LINE_LENGTH(ts, ln)      ((ts)->ops->line_length((ts), (ln)))
#define TS_CHAR_TO_BYTE(ts, l, c)   ((ts)->ops->char_to_byte((ts), (l), (c)))
#define TS_BYTE_TO_CHAR(ts, l, b)   ((ts)->ops->byte_to_char((ts), (l), (b)))
#define TS_CHAR_COUNT(ts, ln)       ((ts)->ops->char_count((ts), (ln)))
#define TS_GENERATION(ts)           ((ts)->ops->generation((ts)))
#define TS_INVALIDATE(ts)           ((ts)->ops->invalidate_caches((ts)))
#define TS_COMPACT(ts)              ((ts)->ops->compact((ts)))
#define TS_RESERVE(ts, n)           ((ts)->ops->reserve((ts), (n)))
#define TS_SEARCH_FWD(ts, s, p, l)  ((ts)->ops->search_forward((ts), (s), (p), (l)))
#define TS_SEARCH_BWD(ts, s, p, l)  ((ts)->ops->search_backward((ts), (s), (p), (l)))
#define TS_TYPE(ts)                 ((ts)->ops->type((ts)))
#define TS_CONVERT(ts, type)        ((ts)->ops->convert_to((ts), (type)))

/* Factory Functions */

/*
 * Create empty storage with given initial capacity.
 * Uses heuristic selection if hint is STORAGE_HINT_DEFAULT.
 */
struct text_storage *text_storage_create(size_t initial_capacity,
                                         text_storage_hint_t hint);

/*
 * Create storage from file.
 * Uses file size and hint to select backend:
 *   - < 10MB (default): gap buffer
 *   - >= 10MB: piece table with mmap
 *   - STORAGE_HINT_READONLY lowers threshold to 1MB
 */
struct text_storage *text_storage_create_from_file(int fd, size_t file_size,
                                                   text_storage_hint_t hint);

/*
 * Create storage from existing gap buffer (takes ownership).
 * Wraps the gap buffer in the storage interface.
 */
struct text_storage *text_storage_from_gap_buffer(struct gap_buffer *gb);

/* Heuristic Functions */

/*
 * Determine optimal storage type for given file size and hint.
 */
text_storage_type_t text_storage_select_type(size_t file_size,
                                             text_storage_hint_t hint);

/*
 * Check if storage should be converted based on usage patterns.
 * Returns true if conversion would improve performance.
 */
bool text_storage_should_convert(struct text_storage *ts);

/*
 * Convert storage to optimal type based on current usage.
 * Returns new storage on conversion, or ts if no conversion needed.
 * Caller must check return value and free old storage if different.
 */
struct text_storage *text_storage_maybe_convert(struct text_storage *ts);

/*
 * Update heuristic counters after an edit operation.
 * Called internally by storage backends.
 */
static inline void text_storage_record_edit(struct text_storage *ts, size_t chars) {
    atomic_fetch_add_explicit(&ts->edit_count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&ts->total_chars_edited, chars, memory_order_relaxed);
    atomic_fetch_add_explicit(&ts->generation, 1, memory_order_release);
}

/* Error Codes */

#define TS_SUCCESS      0
#define TS_ERROR       -1
#define TS_OUT_OF_MEM  -2
#define TS_INVALID     -3
#define TS_RANGE       -4
#define TS_NOT_FOUND   SIZE_MAX  /* For search operations */

/* Debug Support */

#ifdef UEMACS_DEBUG_LOG
void text_storage_dump_stats(struct text_storage *ts);
const char *text_storage_type_name(text_storage_type_t type);
#endif

#endif /* UEMACS_TEXT_STORAGE_H */
