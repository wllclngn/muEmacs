// test_atomic_cursor.c - Tests for cursor position variables
// Tests that cursor variables (vtrow, vtcol, ttrow, ttcol, currow, curcol)
// are properly accessible and that currow/curcol are atomic.
//
// Note: vtrow, vtcol, ttrow, ttcol are plain int, NOT atomic.
// Only currow and curcol are _Atomic int.

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include <pthread.h>
#include <stdatomic.h>

#define NUM_THREADS 4
#define ITERATIONS_PER_THREAD 10000

// Shared state for thread coordination
static _Atomic int ready_count = 0;
static _Atomic int start_flag = 0;

// Thread function: increment currow/curcol using atomic operations
static void *currowcol_increment_thread(void *arg) {
    int thread_id = *(int *)arg;
    (void)thread_id;

    // Wait for all threads to be ready
    atomic_fetch_add(&ready_count, 1);
    while (!atomic_load(&start_flag)) {
        // Spin wait
    }

    // Perform atomic increments on currow/curcol
    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        atomic_fetch_add(&currow, 1);
        atomic_fetch_add(&curcol, 1);
    }

    return NULL;
}

// Test 1: Verify vtrow/vtcol are accessible (single-threaded)
static int test_vt_cursor_basic(void) {
    int ok = 1;
    PHASE_START("CURSOR: VT-BASIC", "vtrow/vtcol basic access");

    // Test basic read/write
    vtrow = 0;
    vtcol = 0;

    for (int i = 0; i < 1000; i++) {
        vtrow++;
        vtcol++;
    }

    if (vtrow != 1000 || vtcol != 1000) {
        LOG_ERRORF("[FAIL] vtrow/vtcol basic increment failed: got %d/%d", vtrow, vtcol);
        ok = 0;
    } else {
        LOG_INFO("[SUCCESS] vtrow/vtcol basic access works");
    }

    PHASE_END("CURSOR: VT-BASIC", ok);
    return ok;
}

// Test 2: Verify ttrow/ttcol are accessible (single-threaded)
static int test_tt_cursor_basic(void) {
    int ok = 1;
    PHASE_START("CURSOR: TT-BASIC", "ttrow/ttcol basic access");

    // Test basic read/write
    ttrow = 0;
    ttcol = 0;

    for (int i = 0; i < 1000; i++) {
        ttrow++;
        ttcol++;
    }

    if (ttrow != 1000 || ttcol != 1000) {
        LOG_ERRORF("[FAIL] ttrow/ttcol basic increment failed: got %d/%d", ttrow, ttcol);
        ok = 0;
    } else {
        LOG_INFO("[SUCCESS] ttrow/ttcol basic access works");
    }

    PHASE_END("CURSOR: TT-BASIC", ok);
    return ok;
}

// Test 3: Verify currow/curcol are atomic (concurrent test)
static int test_currowcol_atomic(void) {
    int ok = 1;
    PHASE_START("CURSOR: ATOMIC", "currow/curcol concurrent atomicity");

    // Reset state
    atomic_store(&currow, 0);
    atomic_store(&curcol, 0);
    atomic_store(&ready_count, 0);
    atomic_store(&start_flag, 0);

    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, currowcol_increment_thread, &thread_ids[i]) != 0) {
            LOG_ERRORF("[FAIL] Failed to create thread %d", i);
            ok = 0;
            PHASE_END("CURSOR: ATOMIC", ok);
            return ok;
        }
    }

    // Wait for all threads to be ready
    while (atomic_load(&ready_count) < NUM_THREADS) {
        // Spin wait
    }

    // Start all threads simultaneously
    atomic_store(&start_flag, 1);

    // Wait for all threads to complete
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Verify results - should be exact with atomic operations
    int expected = NUM_THREADS * ITERATIONS_PER_THREAD;
    int actual_row = atomic_load(&currow);
    int actual_col = atomic_load(&curcol);

    LOG_INFOF("[INFO] currow: expected=%d, actual=%d", expected, actual_row);
    LOG_INFOF("[INFO] curcol: expected=%d, actual=%d", expected, actual_col);

    if (actual_row != expected) {
        LOG_ERRORF("[FAIL] currow mismatch: expected %d, got %d", expected, actual_row);
        ok = 0;
    }

    if (actual_col != expected) {
        LOG_ERRORF("[FAIL] curcol mismatch: expected %d, got %d", expected, actual_col);
        ok = 0;
    }

    if (ok) {
        LOG_INFO("[SUCCESS] currow/curcol atomic operations verified");
    }

    PHASE_END("CURSOR: ATOMIC", ok);
    return ok;
}

// Test 4: Verify cursor range operations
static int test_cursor_range(void) {
    int ok = 1;
    PHASE_START("CURSOR: RANGE", "Cursor variable range test");

    // Test that cursor values can span typical terminal dimensions
    vtrow = 0;
    vtcol = 0;

    // Simulate moving through a 24x80 terminal multiple times
    for (int screen = 0; screen < 100; screen++) {
        for (int row = 0; row < 24; row++) {
            vtrow = row;
            for (int col = 0; col < 80; col++) {
                vtcol = col;
            }
        }
    }

    // Final position should be (23, 79)
    if (vtrow != 23 || vtcol != 79) {
        LOG_ERRORF("[FAIL] Cursor range test failed: expected (23,79), got (%d,%d)", vtrow, vtcol);
        ok = 0;
    } else {
        LOG_INFO("[SUCCESS] Cursor range test passed");
    }

    PHASE_END("CURSOR: RANGE", ok);
    return ok;
}

// Main entry point for integration test
int test_atomic_cursor(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("cursor");

    ok &= test_vt_cursor_basic();
    ok &= test_tt_cursor_basic();
    ok &= test_currowcol_atomic();
    ok &= test_cursor_range();

    return ok;
}
