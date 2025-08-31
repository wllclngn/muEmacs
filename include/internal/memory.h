/*
 * memory.h - Safe memory management interface for μEmacs
 */

#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <assert.h>

/* Safe allocation functions */
void* safe_alloc(size_t size, const char* context, const char* file, int line);
void* safe_realloc(void* old_ptr, size_t new_size, const char* context);
void safe_free(void** ptr);
char* safe_strdup(const char* str, const char* context);

/* Memory tracking and debugging */
void memory_report(void);
void memory_cleanup(void);

/* C23 compile-time validation for memory safety */
static_assert(sizeof(void*) >= 4, "Pointer size must be at least 32-bit");
static_assert(sizeof(size_t) >= sizeof(void*), "size_t must be able to represent pointer differences");

/* Convenient macros for type-safe allocation */
#define SAFE_ALLOC(type, ctx) \
    (type*)safe_alloc(sizeof(type), ctx, __FILE__, __LINE__)

#define SAFE_ALLOC_SIZED(type, size, ctx) \
    (type*)safe_alloc((size), ctx, __FILE__, __LINE__)

#define SAFE_ARRAY(type, count, ctx) \
    (type*)safe_alloc(sizeof(type) * (count), ctx, __FILE__, __LINE__)

#define SAFE_REALLOC(ptr, new_size, ctx) \
    safe_realloc(ptr, new_size, ctx)

#define SAFE_FREE(ptr) \
    safe_free((void**)&(ptr))

#define SAFE_STRDUP(str, ctx) \
    safe_strdup(str, ctx)

#endif /* MEMORY_H */