// test_core_window_hash.c - Unit tests for window hash table system
// Tests src/core/window_hash.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "window_hash.h"
#include "memory.h"
#include <stdlib.h>

// Test window hash initialization
static int test_window_hash_init(void) {
    int ok = 1;
    PHASE_START("WHASH: INIT", "Window hash table initialization");

    // Initialize hash table
    if (window_hash_init() != 0) {
        LOG_ERROR("[FAIL] window_hash_init failed");
        ok = 0;
        PHASE_END("WHASH: INIT", ok);
        return ok;
    }

    // Re-initialize should be safe (idempotent)
    if (window_hash_init() != 0) {
        LOG_ERROR("[FAIL] second window_hash_init failed");
        ok = 0;
    }

    // Verify stats are initialized
    uint64_t lookups = 0, collisions = 0;
    uint32_t generation = 0;
    window_hash_get_stats(&lookups, &collisions, &generation);

    if (lookups != 0) {
        LOG_ERRORF("[FAIL] Initial lookups should be 0, got %lu", (unsigned long)lookups);
        ok = 0;
    }

    if (collisions != 0) {
        LOG_ERRORF("[FAIL] Initial collisions should be 0, got %lu", (unsigned long)collisions);
        ok = 0;
    }

    if (generation == 0) {
        LOG_ERROR("[FAIL] Generation should be initialized to 1");
        ok = 0;
    }

    PHASE_END("WHASH: INIT", ok);
    return ok;
}

// Test window hash add/find operations
static int test_window_hash_add_find(void) {
    int ok = 1;
    PHASE_START("WHASH: ADDFIND", "Add and find operations");

    window_hash_init();

    // Create test buffer and line
    struct buffer *bp = bfind("test-whash", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] Failed to create test buffer");
        ok = 0;
        PHASE_END("WHASH: ADDFIND", ok);
        return ok;
    }

    // Get first line from buffer
    struct line *lp = lforw(bp->b_linep);

    // Create a test window manually (simplified)
    struct window test_window;
    memset(&test_window, 0, sizeof(test_window));
    test_window.w_bufp = bp;
    test_window.w_dotp = lp;

    // Add window-line association
    if (window_hash_add(&test_window, lp) != 0) {
        LOG_ERROR("[FAIL] window_hash_add failed");
        ok = 0;
    }

    // Find windows by line
    int count = 0;
    struct window **windows = window_hash_find_by_line(lp, &count);

    if (!windows) {
        LOG_ERROR("[FAIL] window_hash_find_by_line returned nullptr");
        ok = 0;
    } else {
        if (count != 1) {
            LOG_ERRORF("[FAIL] Expected 1 window, got %d", count);
            ok = 0;
        }
        if (windows[0] != &test_window) {
            LOG_ERROR("[FAIL] Found window doesn't match");
            ok = 0;
        }
        SAFE_FREE(windows);
    }

    // Verify stats updated
    uint64_t lookups = 0;
    window_hash_get_stats(&lookups, nullptr, nullptr);
    if (lookups != 1) {
        LOG_ERRORF("[FAIL] Lookup count should be 1, got %lu", (unsigned long)lookups);
        ok = 0;
    }

    // Cleanup
    window_hash_remove(&test_window, lp);
    bp->b_flag &= ~BFCHG;
    (void)zotbuf(bp);

    PHASE_END("WHASH: ADDFIND", ok);
    return ok;
}

// Test allocation failure handling (simulated by adding to nullptr)
static int test_window_hash_alloc_failure(void) {
    int ok = 1;
    PHASE_START("WHASH: ALLOCFAIL", "Allocation failure handling");

    window_hash_init();

    // Try to add nullptr window - should fail gracefully
    struct buffer *bp = bfind("test-whash-fail", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] Failed to create test buffer");
        ok = 0;
        PHASE_END("WHASH: ALLOCFAIL", ok);
        return ok;
    }

    struct line *lp = lforw(bp->b_linep);

    // nullptr window should return error
    if (window_hash_add(nullptr, lp) != -1) {
        LOG_ERROR("[FAIL] nullptr window should return -1");
        ok = 0;
    }

    // nullptr line should return error
    struct window test_window;
    memset(&test_window, 0, sizeof(test_window));
    if (window_hash_add(&test_window, nullptr) != -1) {
        LOG_ERROR("[FAIL] nullptr line should return -1");
        ok = 0;
    }

    // Cleanup
    bp->b_flag &= ~BFCHG;
    (void)zotbuf(bp);

    PHASE_END("WHASH: ALLOCFAIL", ok);
    return ok;
}

// Test collision chain handling
static int test_window_hash_collisions(void) {
    int ok = 1;
    PHASE_START("WHASH: COLLISION", "Hash collision handling");

    window_hash_init();

    // Create multiple buffers/lines to force collisions
    struct buffer *bp1 = bfind("test-collision-1", true, 0);
    struct buffer *bp2 = bfind("test-collision-2", true, 0);
    struct buffer *bp3 = bfind("test-collision-3", true, 0);

    if (!bp1 || !bp2 || !bp3) {
        LOG_ERROR("[FAIL] Failed to create test buffers");
        ok = 0;
        PHASE_END("WHASH: COLLISION", ok);
        return ok;
    }

    struct line *lp1 = lforw(bp1->b_linep);
    struct line *lp2 = lforw(bp2->b_linep);
    struct line *lp3 = lforw(bp3->b_linep);

    // Create test windows
    struct window w1, w2, w3;
    memset(&w1, 0, sizeof(w1));
    memset(&w2, 0, sizeof(w2));
    memset(&w3, 0, sizeof(w3));
    w1.w_bufp = bp1;
    w2.w_bufp = bp2;
    w3.w_bufp = bp3;

    // Add multiple window-line associations
    window_hash_add(&w1, lp1);
    window_hash_add(&w2, lp2);
    window_hash_add(&w3, lp3);

    // Add same window to multiple lines
    window_hash_add(&w1, lp2);
    window_hash_add(&w1, lp3);

    // Find windows for lp1
    int count1 = 0;
    struct window **windows1 = window_hash_find_by_line(lp1, &count1);
    if (count1 != 1 || !windows1) {
        LOG_ERRORF("[FAIL] lp1 should have 1 window, got %d", count1);
        ok = 0;
    } else {
        SAFE_FREE(windows1);
    }

    // Find windows for lp2 (should have 2: w1 and w2)
    int count2 = 0;
    struct window **windows2 = window_hash_find_by_line(lp2, &count2);
    if (count2 != 2 || !windows2) {
        LOG_ERRORF("[FAIL] lp2 should have 2 windows, got %d", count2);
        ok = 0;
    } else {
        SAFE_FREE(windows2);
    }

    // Cleanup
    window_hash_remove(&w1, lp1);
    window_hash_remove(&w2, lp2);
    window_hash_remove(&w3, lp3);
    window_hash_remove(&w1, lp2);
    window_hash_remove(&w1, lp3);

    bp1->b_flag &= ~BFCHG;
    bp2->b_flag &= ~BFCHG;
    bp3->b_flag &= ~BFCHG;
    (void)zotbuf(bp1);
    (void)zotbuf(bp2);
    (void)zotbuf(bp3);

    PHASE_END("WHASH: COLLISION", ok);
    return ok;
}

// Test window_hash_remove with collision chains
static int test_window_hash_remove_chains(void) {
    int ok = 1;
    PHASE_START("WHASH: REMCHAIN", "Remove from collision chains");

    window_hash_init();

    struct buffer *bp = bfind("test-remove-chain", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] Failed to create test buffer");
        ok = 0;
        PHASE_END("WHASH: REMCHAIN", ok);
        return ok;
    }

    struct line *lp = lforw(bp->b_linep);

    // Create multiple windows for same line
    struct window w1, w2, w3;
    memset(&w1, 0, sizeof(w1));
    memset(&w2, 0, sizeof(w2));
    memset(&w3, 0, sizeof(w3));

    window_hash_add(&w1, lp);
    window_hash_add(&w2, lp);
    window_hash_add(&w3, lp);

    // Verify all 3 windows present
    int count = 0;
    struct window **windows = window_hash_find_by_line(lp, &count);
    if (count != 3) {
        LOG_ERRORF("[FAIL] Expected 3 windows, got %d", count);
        ok = 0;
    }
    SAFE_FREE(windows);

    // Remove middle window
    window_hash_remove(&w2, lp);

    // Verify 2 windows remain
    windows = window_hash_find_by_line(lp, &count);
    if (count != 2) {
        LOG_ERRORF("[FAIL] After removing w2, expected 2 windows, got %d", count);
        ok = 0;
    }
    SAFE_FREE(windows);

    // Remove first window
    window_hash_remove(&w1, lp);

    // Verify 1 window remains
    windows = window_hash_find_by_line(lp, &count);
    if (count != 1) {
        LOG_ERRORF("[FAIL] After removing w1, expected 1 window, got %d", count);
        ok = 0;
    }
    SAFE_FREE(windows);

    // Remove last window
    window_hash_remove(&w3, lp);

    // Verify no windows remain
    windows = window_hash_find_by_line(lp, &count);
    if (windows != nullptr) {
        LOG_ERRORF("[FAIL] After removing all, expected nullptr, got %d windows", count);
        ok = 0;
        SAFE_FREE(windows);
    }

    // Cleanup
    bp->b_flag &= ~BFCHG;
    (void)zotbuf(bp);

    PHASE_END("WHASH: REMCHAIN", ok);
    return ok;
}

// Test memory cleanup
static int test_window_hash_cleanup(void) {
    int ok = 1;
    PHASE_START("WHASH: CLEANUP", "Memory cleanup verification");

    window_hash_init();

    // Add some entries
    struct buffer *bp = bfind("test-cleanup", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] Failed to create test buffer");
        ok = 0;
        PHASE_END("WHASH: CLEANUP", ok);
        return ok;
    }

    struct line *lp = lforw(bp->b_linep);
    struct window test_window;
    memset(&test_window, 0, sizeof(test_window));

    window_hash_add(&test_window, lp);

    // Clean up hash table
    window_hash_cleanup();

    // Verify cleanup - re-init should work
    if (window_hash_init() != 0) {
        LOG_ERROR("[FAIL] Re-init after cleanup failed");
        ok = 0;
    }

    // Stats should be reset
    uint64_t lookups = 0;
    window_hash_get_stats(&lookups, nullptr, nullptr);
    if (lookups != 0) {
        LOG_ERRORF("[FAIL] Lookups not reset after cleanup (got %lu)", (unsigned long)lookups);
        ok = 0;
    }

    // Cleanup
    bp->b_flag &= ~BFCHG;
    (void)zotbuf(bp);

    PHASE_END("WHASH: CLEANUP", ok);
    return ok;
}

// Entry point for window hash tests
int test_core_window_hash(void) {
    int result = 1;
    result &= test_window_hash_init();
    result &= test_window_hash_add_find();
    result &= test_window_hash_alloc_failure();
    result &= test_window_hash_collisions();
    result &= test_window_hash_remove_chains();
    result &= test_window_hash_cleanup();
    return result;
}
