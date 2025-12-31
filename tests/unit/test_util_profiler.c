// test_util_profiler.c - Unit tests for profiler system
// Tests src/util/profiler.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "profiler.h"

#include <time.h>
#include <unistd.h>

// Test profiler initialization and shutdown
static int test_profiler_init(void) {
    int ok = 1;
    PHASE_START("PROFILER: INIT", "Profiler initialization");

    perf_init();
    perf_shutdown();

    PHASE_END("PROFILER: INIT", ok);
    return ok;
}

// Test memory allocation tracking
static int test_profiler_memory(void) {
    int ok = 1;
    PHASE_START("PROFILER: MEMORY", "Memory tracking");

    perf_init();

    // Track allocations
    perf_count_allocation(1024);
    perf_count_allocation(2048);
    perf_count_allocation(512);

    // Track deallocations
    perf_count_deallocation(512);
    perf_count_deallocation(1024);

    perf_shutdown();

    PHASE_END("PROFILER: MEMORY", ok);
    return ok;
}

// Test buffer allocation counting
static int test_profiler_buffers(void) {
    int ok = 1;
    PHASE_START("PROFILER: BUFFERS", "Buffer allocation tracking");

    perf_init();

    perf_count_buffer_alloc();
    perf_count_buffer_alloc();
    perf_count_buffer_alloc();

    perf_shutdown();

    PHASE_END("PROFILER: BUFFERS", ok);
    return ok;
}

// Test line allocation counting
static int test_profiler_lines(void) {
    int ok = 1;
    PHASE_START("PROFILER: LINES", "Line allocation tracking");

    perf_init();

    for (int i = 0; i < 10; i++) {
        perf_count_line_alloc();
    }

    perf_shutdown();

    PHASE_END("PROFILER: LINES", ok);
    return ok;
}

// Test operation counters
static int test_profiler_operations(void) {
    int ok = 1;
    PHASE_START("PROFILER: OPS", "Operation counters");

    perf_init();

    perf_count_key_lookup();
    perf_count_key_lookup();
    perf_count_display_update();
    perf_count_file_read();
    perf_count_file_write();

    perf_shutdown();

    PHASE_END("PROFILER: OPS", ok);
    return ok;
}

// Test timing functionality
static int test_profiler_timing(void) {
    int ok = 1;
    PHASE_START("PROFILER: TIMING", "Timer operations");

    perf_init();

    // Start and stop a timer
    perf_start_timing("test_operation");

    // Simulate some work
    struct timespec ts = {0, 10000000}; // 10ms
    nanosleep(&ts, NULL);

    perf_end_timing("test_operation");

    // Nested timers
    perf_start_timing("outer_op");
    perf_start_timing("inner_op");
    perf_end_timing("inner_op");
    perf_end_timing("outer_op");

    perf_shutdown();

    PHASE_END("PROFILER: TIMING", ok);
    return ok;
}

// Test profiler when disabled
static int test_profiler_disabled(void) {
    int ok = 1;
    PHASE_START("PROFILER: DISABLED", "Operations when disabled");

    // Don't call perf_init(), so profiler should be disabled
    perf_count_allocation(1024);
    perf_count_buffer_alloc();
    perf_count_key_lookup();
    perf_start_timing("disabled_op");
    perf_end_timing("disabled_op");

    // These should be no-ops and not crash

    PHASE_END("PROFILER: DISABLED", ok);
    return ok;
}

// Test peak memory tracking
static int test_profiler_peak(void) {
    int ok = 1;
    PHASE_START("PROFILER: PEAK", "Peak memory tracking");

    perf_init();

    // Allocate increasing amounts
    perf_count_allocation(100);
    perf_count_allocation(200);
    perf_count_allocation(300);

    // Peak should now be 600

    // Deallocate some
    perf_count_deallocation(200);

    // Peak should still be 600, current should be 400

    perf_shutdown();

    PHASE_END("PROFILER: PEAK", ok);
    return ok;
}

// Entry point
int test_util_profiler(void) {
    int ok = 1;
    PHASE_START("PROFILER", "Profiler system tests");

    ok &= test_profiler_init();
    ok &= test_profiler_memory();
    ok &= test_profiler_buffers();
    ok &= test_profiler_lines();
    ok &= test_profiler_operations();
    ok &= test_profiler_timing();
    ok &= test_profiler_disabled();
    ok &= test_profiler_peak();

    if (ok) {
        LOG_INFO("[SUCCESS] All profiler tests passed.");
    }

    PHASE_END("PROFILER", ok);
    return ok;
}
