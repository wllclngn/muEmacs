/*
 * piece_table.h - Piece table storage backend for μEmacs
 *
 * Implements the text_storage interface using piece table algorithm.
 * Optimal for large files with sparse edits:
 *   - O(1) load via mmap (no copying)
 *   - O(log p) edit where p is piece count
 *   - Append-only add buffer - original file never modified
 *
 * C23 compliant
 */

#ifndef UEMACS_PIECE_TABLE_H
#define UEMACS_PIECE_TABLE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "internal/text_storage.h"

/* Piece Structure
 *
 * Each piece references a span in either the original file (mmap'd) or
 * the append-only add buffer. */

typedef struct piece {
    bool is_original;      /* true = original mmap, false = add buffer */
    size_t start;          /* Byte offset in source */
    size_t length;         /* Byte length of this piece */
    struct piece *next;
    struct piece *prev;
} piece_t;

/* Piece Table Structure
 *
 * Embeds text_storage as first member for polymorphic casting. */

typedef struct piece_table {
    struct text_storage base;   /* Must be first for casting */

    /* Original file (read-only, mmap'd) */
    char *original;             /* mmap'd original file content */
    size_t original_size;       /* Size of original file */
    int original_fd;            /* File descriptor (-1 if not from file) */

    /* Add buffer (append-only edits) */
    char *add_buffer;           /* Dynamically grown buffer for new text */
    size_t add_size;            /* Current used size */
    size_t add_capacity;        /* Allocated capacity */

    /* Piece list */
    piece_t *head;              /* First piece (sentinel or real) */
    piece_t *tail;              /* Last piece */
    size_t piece_count;         /* Number of pieces */

    /* Logical document state */
    size_t logical_size;        /* Total document size */
    size_t cursor_pos;          /* Current cursor position */

    /* Line index cache (lazy-built) */
    struct {
        size_t *offsets;        /* Array of line start offsets */
        size_t count;           /* Number of lines */
        size_t capacity;        /* Allocated slots */
        _Atomic bool dirty;     /* Needs rebuild */
    } line_idx;

    /* Generation for cache invalidation */
    _Atomic uint32_t generation;
} piece_table_t;

/* Creation Functions */

/*
 * Create piece table from mmap'd file.
 * Takes ownership of the file descriptor.
 */
struct text_storage *piece_table_create_from_mmap(int fd, size_t file_size);

/*
 * Create empty piece table with initial add buffer capacity.
 */
struct text_storage *piece_table_create_empty(size_t initial_capacity);

/*
 * Create piece table from memory buffer (copies data).
 */
struct text_storage *piece_table_create_from_buffer(const char *data, size_t len);

/* Utility Functions */

/*
 * Get piece containing logical position.
 * Returns piece and sets offset_in_piece to position within piece.
 */
piece_t *piece_table_find_piece(piece_table_t *pt, size_t logical_pos,
                                 size_t *offset_in_piece);

/*
 * Get raw pointer to text at position (may span pieces - use carefully).
 * Only valid until next edit. Returns nullptr if pos spans pieces.
 */
const char *piece_table_get_ptr(piece_table_t *pt, size_t pos, size_t *max_len);

/*
 * Rebuild line index from scratch.
 */
void piece_table_rebuild_line_index(piece_table_t *pt);

/*
 * Convert piece table to gap buffer (for heavy editing).
 * Returns new gap-based storage, caller must free this piece table.
 */
struct text_storage *piece_table_to_gap_buffer(piece_table_t *pt);

#endif /* UEMACS_PIECE_TABLE_H */
