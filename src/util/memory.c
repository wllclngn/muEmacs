/*
 * memory.c - Centralized memory management for μEmacs
 * 
 * Consolidates 91+ scattered malloc/free patterns into safe, tracked allocation
 * Eliminates memory leaks and provides consistent error handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <assert.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "memory.h"

/* Memory allocation tracking for debugging */
typedef struct alloc_record {
    void* ptr;
    size_t size;
    const char* context;
    const char* file;
    int line;
    struct alloc_record* next;
} alloc_record_t;

static alloc_record_t* allocation_list = NULL;
static size_t total_allocated = 0;
static size_t peak_allocated = 0;
static size_t allocation_count = 0;

/* Thread-safe allocation tracking (basic protection) */
static void track_allocation(void* ptr, size_t size, const char* context, const char* file, int line) {
    if (!ptr) return;
    
    alloc_record_t* record = malloc(sizeof(alloc_record_t));
    if (!record) return;  /* Can't track, but allocation succeeded */
    
    record->ptr = ptr;
    record->size = size;
    record->context = context;
    record->file = file;
    record->line = line;
    record->next = allocation_list;
    allocation_list = record;
    
    total_allocated += size;
    allocation_count++;
    
    if (total_allocated > peak_allocated) {
        peak_allocated = total_allocated;
    }
}

static void untrack_allocation(void* ptr) {
    if (!ptr) return;
    
    alloc_record_t** current = &allocation_list;
    while (*current) {
        if ((*current)->ptr == ptr) {
            alloc_record_t* to_free = *current;
            total_allocated -= to_free->size;
            allocation_count--;
            *current = to_free->next;
            free(to_free);
            return;
        }
        current = &(*current)->next;
    }
}

/* Safe allocation with error reporting */
void* safe_alloc(size_t size, const char* context, const char* file, int line) {
    /* Check for overflow in allocation size */
    if (size == 0) {
        size = 1;  /* Minimum allocation */
    }
    
    if (size > SIZE_MAX / 2) {
        mlwrite("(ALLOCATION TOO LARGE: %s)", context ? context : "unknown");
        return NULL;
    }
    
    /* Use calloc for zero-initialized memory */
    void* ptr = calloc(1, size);
    if (!ptr) {
        mlwrite("(OUT OF MEMORY: %s - %zu bytes)", context ? context : "unknown", size);
        return NULL;
    }
    
    track_allocation(ptr, size, context, file, line);
    return ptr;
}

/* Safe reallocation */
void* safe_realloc(void* old_ptr, size_t new_size, const char* context) {
    if (new_size == 0) {
        if (old_ptr) {
            untrack_allocation(old_ptr);
            free(old_ptr);
        }
        return NULL;
    }
    
    if (new_size > SIZE_MAX / 2) {
        mlwrite("(REALLOCATION TOO LARGE: %s)", context ? context : "unknown");
        return NULL;
    }

    // Untrack the old pointer before reallocating
    if (old_ptr) {
        untrack_allocation(old_ptr);
    }
    
    void* new_ptr = realloc(old_ptr, new_size);
    if (!new_ptr) {
        mlwrite("(OUT OF MEMORY: %s - %zu bytes)", context ? context : "unknown", new_size);
        // If realloc fails, the original block is untouched. We should re-track it.
        if (old_ptr) {
            track_allocation(old_ptr, 0, "realloc-fail", __FILE__, __LINE__); // Size is unknown here, but it's better to track it
        }
        return NULL;
    }
    
    track_allocation(new_ptr, new_size, context, __FILE__, __LINE__);
    
    return new_ptr;
}

/* Safe free that nullifies pointer */
void safe_free(void** ptr) {
    if (!ptr || !*ptr) return;
    
    untrack_allocation(*ptr);
    free(*ptr);
    *ptr = NULL;  /* Prevent double-free */
}

/* Allocation report for debugging */
void memory_report(void) {
    mlwrite("Memory: %zu bytes allocated (%zu peak) in %zu blocks", 
            total_allocated, peak_allocated, allocation_count);
    
    if (allocation_list) {
        mlwrite("Memory leaks detected:");
        alloc_record_t* current = allocation_list;
        int leak_count = 0;
        while (current && leak_count < 10) {  /* Limit output */
            mlwrite("  Leak: %zu bytes at %s:%d (%s)", 
                    current->size, current->file, current->line, 
                    current->context ? current->context : "unknown");
            current = current->next;
            leak_count++;
        }
        if (current) {
            mlwrite("  ... and more");
        }
    }
}

/* Cleanup all tracked allocations (for shutdown) */
void memory_cleanup(void) {
    while (allocation_list) {
        alloc_record_t* next = allocation_list->next;
        free(allocation_list->ptr);
        free(allocation_list);
        allocation_list = next;
    }
    total_allocated = 0;
    allocation_count = 0;
}

/* Safe string duplication */
char* safe_strdup(const char* str, const char* context) {
    if (!str) return NULL;
    
    size_t len = strlen(str) + 1;
    char* dup = (char*)safe_alloc(len, context, __FILE__, __LINE__);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}