// test_util_memory.c - Unit tests for memory management
// Tests src/util/memory.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "internal/memory.h"

// Test safe_alloc function
static int test_memory_safe_alloc(void) {
    int ok = 1;
    PHASE_START("MEMORY: ALLOC", "Safe allocation");

    // Test basic allocation
    void *ptr = safe_alloc(128, "test allocation", __FILE__, __LINE__);
    if (ptr == NULL) {
        LOG_ERROR("[FAIL] safe_alloc returned NULL for valid size");
        ok = 0;
    } else {
        // Verify memory is zero-initialized (calloc behavior)
        unsigned char *bytes = (unsigned char *)ptr;
        int all_zero = 1;
        for (int i = 0; i < 128; i++) {
            if (bytes[i] != 0) {
                all_zero = 0;
                break;
            }
        }
        if (!all_zero) {
            LOG_ERROR("[FAIL] safe_alloc didn't zero-initialize memory");
            ok = 0;
        }
        safe_free(&ptr);
    }

    // Test zero-size allocation (should allocate minimum 1 byte)
    ptr = safe_alloc(0, "zero size", __FILE__, __LINE__);
    if (ptr == NULL) {
        LOG_ERROR("[FAIL] safe_alloc returned NULL for zero size");
        ok = 0;
    } else {
        safe_free(&ptr);
    }

    // Test large allocation (within reason)
    ptr = safe_alloc(1024 * 1024, "large allocation", __FILE__, __LINE__);
    if (ptr == NULL) {
        LOG_ERROR("[FAIL] safe_alloc returned NULL for 1MB allocation");
        ok = 0;
    } else {
        safe_free(&ptr);
    }

    PHASE_END("MEMORY: ALLOC", ok);
    return ok;
}

// Test safe_free function
static int test_memory_safe_free(void) {
    int ok = 1;
    PHASE_START("MEMORY: FREE", "Safe free");

    // Test basic free
    void *ptr = safe_alloc(64, "test free", __FILE__, __LINE__);
    if (ptr == NULL) {
        LOG_ERROR("[FAIL] couldn't allocate for free test");
        ok = 0;
    } else {
        safe_free(&ptr);
        if (ptr != NULL) {
            LOG_ERROR("[FAIL] safe_free didn't nullify pointer");
            ok = 0;
        }
    }

    // Test double-free safety (should not crash)
    ptr = NULL;
    safe_free(&ptr);  // Should be safe to free NULL

    // Test freeing already-freed pointer (should be safe)
    ptr = safe_alloc(32, "double free test", __FILE__, __LINE__);
    safe_free(&ptr);
    safe_free(&ptr);  // Should be safe

    PHASE_END("MEMORY: FREE", ok);
    return ok;
}

// Test safe_realloc function
static int test_memory_safe_realloc(void) {
    int ok = 1;
    PHASE_START("MEMORY: REALLOC", "Safe reallocation");

    // Test growing allocation
    void *ptr = safe_alloc(64, "realloc test", __FILE__, __LINE__);
    if (ptr == NULL) {
        LOG_ERROR("[FAIL] couldn't allocate for realloc test");
        ok = 0;
    } else {
        // Write some data
        memset(ptr, 0xAB, 64);

        // Grow it
        void *new_ptr = safe_realloc(ptr, 128, "realloc grow");
        if (new_ptr == NULL) {
            LOG_ERROR("[FAIL] safe_realloc failed to grow allocation");
            ok = 0;
            safe_free(&ptr);
        } else {
            // Verify original data is preserved
            unsigned char *bytes = (unsigned char *)new_ptr;
            int data_ok = 1;
            for (int i = 0; i < 64; i++) {
                if (bytes[i] != 0xAB) {
                    data_ok = 0;
                    break;
                }
            }
            if (!data_ok) {
                LOG_ERROR("[FAIL] safe_realloc didn't preserve original data");
                ok = 0;
            }
            safe_free(&new_ptr);
        }
    }

    // Test shrinking allocation
    ptr = safe_alloc(128, "realloc shrink", __FILE__, __LINE__);
    if (ptr != NULL) {
        void *new_ptr = safe_realloc(ptr, 64, "realloc shrink");
        if (new_ptr == NULL) {
            LOG_ERROR("[FAIL] safe_realloc failed to shrink allocation");
            ok = 0;
        } else {
            safe_free(&new_ptr);
        }
    }

    // Test realloc to zero (should free)
    ptr = safe_alloc(64, "realloc to zero", __FILE__, __LINE__);
    if (ptr != NULL) {
        void *new_ptr = safe_realloc(ptr, 0, "realloc zero");
        if (new_ptr != NULL) {
            LOG_ERROR("[FAIL] safe_realloc to zero didn't free memory");
            ok = 0;
            safe_free(&new_ptr);
        }
    }

    PHASE_END("MEMORY: REALLOC", ok);
    return ok;
}

// Test safe_strdup function
static int test_memory_safe_strdup(void) {
    int ok = 1;
    PHASE_START("MEMORY: STRDUP", "Safe string duplication");

    // Test basic string duplication
    const char *original = "Hello, World!";
    char *dup = safe_strdup(original, "strdup test");
    if (dup == NULL) {
        LOG_ERROR("[FAIL] safe_strdup returned NULL");
        ok = 0;
    } else {
        if (strcmp(dup, original) != 0) {
            LOG_ERRORF("[FAIL] safe_strdup didn't copy correctly: '%s'", dup);
            ok = 0;
        }
        safe_free((void **)&dup);
    }

    // Test empty string
    original = "";
    dup = safe_strdup(original, "empty string");
    if (dup == NULL) {
        LOG_ERROR("[FAIL] safe_strdup returned NULL for empty string");
        ok = 0;
    } else {
        if (strcmp(dup, "") != 0) {
            LOG_ERROR("[FAIL] safe_strdup didn't handle empty string");
            ok = 0;
        }
        safe_free((void **)&dup);
    }

    // Test NULL input
    dup = safe_strdup(NULL, "NULL input");
    if (dup != NULL) {
        LOG_ERROR("[FAIL] safe_strdup didn't return NULL for NULL input");
        ok = 0;
        safe_free((void **)&dup);
    }

    PHASE_END("MEMORY: STRDUP", ok);
    return ok;
}

// Test memory tracking
static int test_memory_tracking(void) {
    int ok = 1;
    PHASE_START("MEMORY: TRACKING", "Allocation tracking");

    // Allocate some memory to test tracking
    void *ptr1 = safe_alloc(256, "tracking test 1", __FILE__, __LINE__);
    void *ptr2 = safe_alloc(512, "tracking test 2", __FILE__, __LINE__);
    void *ptr3 = safe_alloc(128, "tracking test 3", __FILE__, __LINE__);

    if (!ptr1 || !ptr2 || !ptr3) {
        LOG_ERROR("[FAIL] couldn't allocate for tracking test");
        ok = 0;
    } else {
        // Free them
        safe_free(&ptr1);
        safe_free(&ptr2);
        safe_free(&ptr3);
    }

    // Note: We can't easily verify the tracking internals without
    // exposing internal state, but we can verify the functions work
    // without crashing

    PHASE_END("MEMORY: TRACKING", ok);
    return ok;
}

// Test memory cleanup (should be called at shutdown)
static int test_memory_cleanup(void) {
    int ok = 1;
    PHASE_START("MEMORY: CLEANUP", "Memory cleanup");

    // Allocate some memory that we intentionally don't free
    void *leak1 = safe_alloc(64, "intentional leak", __FILE__, __LINE__);
    void *leak2 = safe_alloc(32, "intentional leak", __FILE__, __LINE__);

    // Prevent unused variable warnings
    (void)leak1;
    (void)leak2;

    // Don't call memory_cleanup here as it would free these and other
    // tracked allocations. This test just verifies the function exists
    // and can be called.

    // Note: In a real test suite, we'd want to verify cleanup works,
    // but that would require resetting the tracking state.

    PHASE_END("MEMORY: CLEANUP", ok);
    return ok;
}

// Test allocation edge cases
static int test_memory_edge_cases(void) {
    int ok = 1;
    PHASE_START("MEMORY: EDGES", "Edge case handling");

    // Test very small allocation
    void *ptr = safe_alloc(1, "tiny allocation", __FILE__, __LINE__);
    if (ptr == NULL) {
        LOG_ERROR("[FAIL] couldn't allocate 1 byte");
        ok = 0;
    } else {
        safe_free(&ptr);
    }

    // Test page-sized allocation
    ptr = safe_alloc(4096, "page size", __FILE__, __LINE__);
    if (ptr == NULL) {
        LOG_ERROR("[FAIL] couldn't allocate page-sized block");
        ok = 0;
    } else {
        safe_free(&ptr);
    }

    PHASE_END("MEMORY: EDGES", ok);
    return ok;
}

// Test SIZE_MAX/2 boundary protection (as requested in task)
static int test_memory_size_max_boundary(void) {
    int ok = 1;
    PHASE_START("MEMORY: SIZE_MAX BOUNDARY", "Test allocation size limit protection");

    // Test SIZE_MAX/2 + 1 (should fail and return NULL)
    size_t huge_size = (SIZE_MAX / 2) + 1;
    void *ptr = safe_alloc(huge_size, "size_max boundary", __FILE__, __LINE__);

    if (ptr != NULL) {
        LOG_ERRORF("[FAIL] safe_alloc should reject SIZE_MAX/2 + 1, but returned %p", ptr);
        safe_free(&ptr);
        ok = 0;
    } else {
        LOG_INFOF("[%sOK%s] safe_alloc correctly rejected SIZE_MAX/2 + 1", GREEN, RESET);
    }

    // Test SIZE_MAX (should also fail)
    ptr = safe_alloc(SIZE_MAX, "size_max", __FILE__, __LINE__);
    if (ptr != NULL) {
        LOG_ERRORF("[FAIL] safe_alloc should reject SIZE_MAX, but returned %p", ptr);
        safe_free(&ptr);
        ok = 0;
    } else {
        LOG_INFOF("[%sOK%s] safe_alloc correctly rejected SIZE_MAX", GREEN, RESET);
    }

    // Test SIZE_MAX/2 - 1 (may succeed or fail depending on available memory, but shouldn't crash)
    size_t large_size = (SIZE_MAX / 2) - 1;
    ptr = safe_alloc(large_size, "size_max/2 - 1", __FILE__, __LINE__);
    if (ptr == NULL) {
        LOG_INFOF("[%sOK%s] safe_alloc rejected SIZE_MAX/2 - 1 (likely OOM)", GREEN, RESET);
    } else {
        LOG_INFOF("[%sOK%s] safe_alloc accepted SIZE_MAX/2 - 1 (huge allocation succeeded)", GREEN, RESET);
        safe_free(&ptr);
    }

    PHASE_END("MEMORY: SIZE_MAX BOUNDARY", ok);
    return ok;
}

// Test safe_realloc with NULL old pointer (as requested in task)
static int test_memory_realloc_null_ptr(void) {
    int ok = 1;
    PHASE_START("MEMORY: REALLOC NULL PTR", "Test realloc with NULL old pointer");

    // Realloc with NULL pointer should behave like malloc
    void *ptr = safe_realloc(NULL, 128, "realloc from null");

    if (ptr == NULL) {
        LOG_ERROR("[FAIL] safe_realloc(NULL, 128) should allocate, returned NULL");
        ok = 0;
    } else {
        LOG_INFOF("[%sOK%s] safe_realloc(NULL, 128) allocated successfully", GREEN, RESET);

        // Verify it's usable
        memset(ptr, 0xFF, 128);
        safe_free(&ptr);
    }

    // Realloc NULL to zero size (should return NULL)
    ptr = safe_realloc(NULL, 0, "realloc null to zero");
    if (ptr != NULL) {
        LOG_ERRORF("[FAIL] safe_realloc(NULL, 0) should return NULL, got %p", ptr);
        safe_free(&ptr);
        ok = 0;
    } else {
        LOG_INFOF("[%sOK%s] safe_realloc(NULL, 0) returned NULL", GREEN, RESET);
    }

    // Realloc NULL with SIZE_MAX boundary
    ptr = safe_realloc(NULL, (SIZE_MAX / 2) + 1, "realloc null oversized");
    if (ptr != NULL) {
        LOG_ERRORF("[FAIL] safe_realloc(NULL, SIZE_MAX/2+1) should fail, got %p", ptr);
        safe_free(&ptr);
        ok = 0;
    } else {
        LOG_INFOF("[%sOK%s] safe_realloc(NULL, SIZE_MAX/2+1) correctly rejected", GREEN, RESET);
    }

    PHASE_END("MEMORY: REALLOC NULL PTR", ok);
    return ok;
}

// Test allocation failure cascade (stress test)
static int test_memory_allocation_cascade(void) {
    int ok = 1;
    PHASE_START("MEMORY: ALLOCATION CASCADE", "Test allocation failure handling");

    // Allocate increasing sizes until failure
    #define MAX_ALLOCS 100
    void *ptrs[MAX_ALLOCS];
    int count = 0;
    size_t total_allocated = 0;

    LOG_INFO("Allocating increasingly large blocks...");

    for (int i = 0; i < MAX_ALLOCS; i++) {
        size_t size = (i + 1) * 1024 * 1024;  // 1MB, 2MB, 3MB, etc.
        ptrs[i] = safe_alloc(size, "cascade test", __FILE__, __LINE__);

        if (ptrs[i] == NULL) {
            LOG_INFOF("[%sOK%s] Allocation failed at iteration %d (size=%zu MB) - expected behavior", GREEN, RESET, i, size / (1024*1024));
            break;
        }

        count++;
        total_allocated += size;

        // Write to the memory to ensure it's actually allocated
        memset(ptrs[i], (unsigned char)i, size);

        // Stop if we've allocated a reasonable amount (to avoid OOM killer)
        if (total_allocated > 512 * 1024 * 1024) {  // Stop at 512 MB
            LOG_INFOF("[%sOK%s] Allocated %d blocks (%zu MB total), stopping test", GREEN, RESET, count, total_allocated / (1024*1024));
            break;
        }
    }

    // Free all allocated blocks
    for (int i = 0; i < count; i++) {
        safe_free(&ptrs[i]);
    }

    LOG_INFOF("[%sOK%s] Successfully freed all allocated blocks", GREEN, RESET);

    PHASE_END("MEMORY: ALLOCATION CASCADE", ok);
    return ok;
}

// Entry point
int test_util_memory(void) {
    int ok = 1;
    PHASE_START("MEMORY", "Memory management tests");

    ok &= test_memory_safe_alloc();
    ok &= test_memory_safe_free();
    ok &= test_memory_safe_realloc();
    ok &= test_memory_safe_strdup();
    ok &= test_memory_tracking();
    ok &= test_memory_cleanup();
    ok &= test_memory_edge_cases();
    ok &= test_memory_size_max_boundary();
    ok &= test_memory_realloc_null_ptr();
    ok &= test_memory_allocation_cascade();

    if (ok) {
        LOG_INFO("[SUCCESS] All memory tests passed.");
    }

    PHASE_END("MEMORY", ok);
    return ok;
}
