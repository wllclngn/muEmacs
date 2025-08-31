/*
 * window_hash.c - O(1) atomic window lookup system
 * Replaces O(n) window iteration with hash table for line operations
 */

#include "μemacs/window_hash.h"
#include "estruct.h"
#include "edef.h"
#include "memory.h"
#include <string.h>

/* Global hash table instance */
static struct window_hash_table window_hash_global = {0};
static _Atomic bool hash_initialized = false;

/* Fast hash function for line pointers */
static inline uint32_t hash_line_ptr(struct line *lp)
{
    uintptr_t addr = (uintptr_t)lp;
    /* MurmurHash-inspired pointer hashing */
    addr ^= addr >> 16;
    addr *= 0x85ebca6b;
    addr ^= addr >> 13;
    addr *= 0xc2b2ae35;
    addr ^= addr >> 16;
    return (uint32_t)(addr & WINDOW_HASH_MASK);
}

/* Initialize window hash table */
int window_hash_init(void)
{
    bool expected = false;
    if (atomic_compare_exchange_strong(&hash_initialized, &expected, true)) {
        memset(&window_hash_global, 0, sizeof(window_hash_global));
        atomic_init(&window_hash_global.lookups, 0);
        atomic_init(&window_hash_global.collisions, 0);
        atomic_init(&window_hash_global.generation, 1);
        return 0;
    }
    return 0; /* Already initialized */
}

/* Add window-line association - O(1) atomic operation */
int window_hash_add(struct window *wp, struct line *lp)
{
    if (!wp || !lp) return -1;
    
    uint32_t hash = hash_line_ptr(lp);
    
    /* Allocate new hash entry */
    struct window_hash_entry *entry = safe_alloc(sizeof(struct window_hash_entry),
                                                 "window hash entry", __FILE__, __LINE__);
    if (!entry) return -1;
    
    entry->window = wp;
    entry->line = lp;
    
    /* Atomic insertion at head of collision chain */
    struct window_hash_entry *old_head = window_hash_global.buckets[hash];
    atomic_init(&entry->next, old_head);
    
    /* Update collision counter if bucket wasn't empty */
    if (old_head) {
        atomic_fetch_add_explicit(&window_hash_global.collisions, 1, memory_order_relaxed);
    }
    
    window_hash_global.buckets[hash] = entry;
    atomic_fetch_add_explicit(&window_hash_global.generation, 1, memory_order_release);
    
    return 0;
}

/* Remove window-line association - O(1) average */
void window_hash_remove(struct window *wp, struct line *lp)
{
    if (!wp || !lp) return;
    
    uint32_t hash = hash_line_ptr(lp);
    struct window_hash_entry *entry = window_hash_global.buckets[hash];
    struct window_hash_entry *prev = NULL;
    
    /* Search collision chain */
    while (entry) {
        struct window_hash_entry *next_entry = atomic_load_explicit(&entry->next, memory_order_acquire);
        
        if (entry->window == wp && entry->line == lp) {
            /* Remove from chain */
            if (prev) {
                atomic_store_explicit(&prev->next, next_entry, memory_order_release);
            } else {
                window_hash_global.buckets[hash] = next_entry;
            }
            
            SAFE_FREE(entry);
            atomic_fetch_add_explicit(&window_hash_global.generation, 1, memory_order_release);
            return;
        }
        
        prev = entry;
        entry = next_entry;
    }
}

/* Find all windows for a line - O(1) average case */
struct window **window_hash_find_by_line(struct line *lp, int *count)
{
    if (!lp || !count) return NULL;
    
    *count = 0;
    uint32_t hash = hash_line_ptr(lp);
    struct window_hash_entry *entry = window_hash_global.buckets[hash];
    
    atomic_fetch_add_explicit(&window_hash_global.lookups, 1, memory_order_relaxed);
    
    /* Count matching windows first */
    int window_count = 0;
    struct window_hash_entry *scan = entry;
    while (scan) {
        if (scan->line == lp) {
            window_count++;
        }
        scan = atomic_load_explicit(&scan->next, memory_order_acquire);
    }
    
    if (window_count == 0) return NULL;
    
    /* Allocate result array */
    struct window **windows = safe_alloc(window_count * sizeof(struct window*),
                                        "window lookup result", __FILE__, __LINE__);
    if (!windows) return NULL;
    
    /* Populate result array */
    int index = 0;
    entry = window_hash_global.buckets[hash];
    while (entry && index < window_count) {
        if (entry->line == lp) {
            windows[index++] = entry->window;
        }
        entry = atomic_load_explicit(&entry->next, memory_order_acquire);
    }
    
    *count = index;
    return windows;
}

/* Update line pointer atomically - for line operations */
void window_hash_update_line(struct window *wp, struct line *old_line, struct line *new_line)
{
    if (!wp || !old_line || !new_line) return;
    
    /* Remove old association */
    window_hash_remove(wp, old_line);
    
    /* Add new association */
    window_hash_add(wp, new_line);
}

/* Clear all entries for a window - for window cleanup */
void window_hash_clear_window(struct window *wp)
{
    if (!wp) return;
    
    /* Scan all buckets for this window */
    for (int i = 0; i < WINDOW_HASH_SIZE; i++) {
        struct window_hash_entry *entry = window_hash_global.buckets[i];
        struct window_hash_entry *prev = NULL;
        
        while (entry) {
            struct window_hash_entry *next_entry = atomic_load_explicit(&entry->next, memory_order_acquire);
            
            if (entry->window == wp) {
                /* Remove from chain */
                if (prev) {
                    atomic_store_explicit(&prev->next, next_entry, memory_order_release);
                } else {
                    window_hash_global.buckets[i] = next_entry;
                }
                
                SAFE_FREE(entry);
                entry = next_entry; /* Continue with next */
            } else {
                prev = entry;
                entry = next_entry;
            }
        }
    }
    
    atomic_fetch_add_explicit(&window_hash_global.generation, 1, memory_order_release);
}

/* Get performance statistics */
void window_hash_get_stats(uint64_t *lookups, uint64_t *collisions, uint32_t *generation)
{
    if (lookups) {
        *lookups = atomic_load_explicit(&window_hash_global.lookups, memory_order_relaxed);
    }
    if (collisions) {
        *collisions = atomic_load_explicit(&window_hash_global.collisions, memory_order_relaxed);
    }
    if (generation) {
        *generation = atomic_load_explicit(&window_hash_global.generation, memory_order_relaxed);
    }
}

/* Cleanup hash table resources */
void window_hash_cleanup(void)
{
    for (int i = 0; i < WINDOW_HASH_SIZE; i++) {
        struct window_hash_entry *entry = window_hash_global.buckets[i];
        while (entry) {
            struct window_hash_entry *next_entry = atomic_load_explicit(&entry->next, memory_order_acquire);
            SAFE_FREE(entry);
            entry = next_entry;
        }
        window_hash_global.buckets[i] = NULL;
    }
    
    atomic_store_explicit(&hash_initialized, false, memory_order_release);
}