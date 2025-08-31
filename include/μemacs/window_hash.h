/*
 * window_hash.h - O(1) window lookup hash table for atomic window management
 * Eliminates O(n) linear window searches during line operations
 */

#ifndef WINDOW_HASH_H
#define WINDOW_HASH_H

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>

// Forward declarations
struct window;
struct line;

/* Hash table size - power of 2 for fast modulo */
#define WINDOW_HASH_SIZE 128
#define WINDOW_HASH_MASK (WINDOW_HASH_SIZE - 1)

/* Window hash entry for collision resolution */
struct window_hash_entry {
    struct window *window;
    struct line *line;  /* Key: line pointer */
    _Atomic(struct window_hash_entry *) next;
};

/* Global window hash table for O(1) window-by-line lookup */
struct window_hash_table {
    struct window_hash_entry *buckets[WINDOW_HASH_SIZE];
    _Atomic uint64_t lookups;      /* Performance counter */
    _Atomic uint64_t collisions;   /* Hash collision counter */
    _Atomic uint32_t generation;   /* Cache invalidation */
};

/* Initialize window hash table system */
int window_hash_init(void);

/* Add window-line association to hash table - O(1) */
int window_hash_add(struct window *wp, struct line *lp);

/* Remove window-line association from hash table - O(1) */
void window_hash_remove(struct window *wp, struct line *lp);

/* Find all windows associated with a line - O(1) average */
struct window **window_hash_find_by_line(struct line *lp, int *count);

/* Update window line pointer atomically - O(1) */
void window_hash_update_line(struct window *wp, struct line *old_line, struct line *new_line);

/* Clear all entries for a window */
void window_hash_clear_window(struct window *wp);

/* Get performance statistics */
void window_hash_get_stats(uint64_t *lookups, uint64_t *collisions, uint32_t *generation);

/* Free window hash table resources */
void window_hash_cleanup(void);

#endif /* WINDOW_HASH_H */