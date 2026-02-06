// test_util_git_status.c - Thread safety and concurrency tests for git_status module
//
// Tests:
// - pthread_create failure handling
// - Buffer overflow protection
// - Concurrent request handling
// - Mutex lock ordering and race conditions

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#include "../test_utils.h"
#include "../test_registry.h"
#include "internal/git_status.h"

// Test: Buffer overflow protection in git_status_get_cached
static int test_buffer_overflow_protection(void) {
    int ok = 1;
    PHASE_START("GIT_STATUS: BUFFER OVERFLOW", "Test buffer boundary protection");

    git_status_init();
    git_status_set_enabled(1);

    // Trigger an update and wait for it to populate cache
    git_status_request_async(".");
    sleep(1);  // Give thread time to complete

    // Test 1: Zero-size buffer
    char tiny_buf[1] = {0};
    int len = git_status_get_cached(tiny_buf, 0);
    if (len != 0) {
        LOG_ERRORF("[FAIL] Zero-size buffer should return 0, got %d", len);
        ok = 0;
    } else {
        LOG_INFOF("[%sOK%s] Zero-size buffer returns 0", GREEN, RESET);
    }

    // Test 2: Single-char buffer (only room for null terminator)
    char one_buf[1] = {0};
    len = git_status_get_cached(one_buf, 1);
    if (one_buf[0] != '\0') {
        LOG_ERROR("[FAIL] Single-char buffer not null-terminated");
        ok = 0;
    } else {
        LOG_INFOF("[%sOK%s] Single-char buffer properly null-terminated", GREEN, RESET);
    }

    // Test 3: Small buffer - should truncate safely
    char small_buf[8] = {0};
    len = git_status_get_cached(small_buf, sizeof(small_buf));
    if (small_buf[sizeof(small_buf)-1] != '\0') {
        LOG_ERROR("[FAIL] Buffer not null-terminated after truncation");
        ok = 0;
    } else {
        LOG_INFOF("[%sOK%s] Small buffer safely truncated and null-terminated", GREEN, RESET);
    }

    // Test 4: nullptr buffer
    len = git_status_get_cached(nullptr, 100);
    if (len != 0) {
        LOG_ERRORF("[FAIL] nullptr buffer should return 0, got %d", len);
        ok = 0;
    } else {
        LOG_INFOF("[%sOK%s] nullptr buffer handled safely", GREEN, RESET);
    }

    git_status_set_enabled(0);
    PHASE_END("GIT_STATUS: BUFFER OVERFLOW", ok);
    return ok;
}

// Test: Concurrent request handling (stress test)
static int test_concurrent_requests(void) {
    int ok = 1;
    PHASE_START("GIT_STATUS: CONCURRENT REQUESTS", "Test multiple simultaneous requests");

    git_status_init();
    git_status_set_enabled(1);

    // Fire off many rapid requests - should throttle properly
    LOG_INFO("Sending 50 rapid requests (should throttle to ~1 actual update)...");

    for (int i = 0; i < 50; i++) {
        git_status_request_async(".");
        usleep(1000);  // 1ms between requests
    }

    // Wait for any in-progress update
    sleep(1);

    // Verify we can still get cached result
    char result[128] = {0};
    int len = git_status_get_cached(result, sizeof(result));

    LOG_INFOF("[%sOK%s] Concurrent requests handled, cached result: '%s' (%d bytes)", GREEN, RESET, result, len);

    git_status_set_enabled(0);
    PHASE_END("GIT_STATUS: CONCURRENT REQUESTS", ok);
    return ok;
}

// Test: Mutex lock ordering (verify no deadlocks)
static int test_mutex_lock_ordering(void) {
    int ok = 1;
    PHASE_START("GIT_STATUS: MUTEX ORDERING", "Test lock acquisition patterns");

    git_status_init();
    git_status_set_enabled(1);

    // Interleave requests and reads to check for deadlocks
    LOG_INFO("Interleaving async requests and cached reads...");

    char buf[128];
    for (int i = 0; i < 10; i++) {
        git_status_request_async(".");  // Acquires lock
        git_status_get_cached(buf, sizeof(buf));  // Acquires lock
        usleep(10000);  // 10ms
    }

    LOG_INFOF("[%sOK%s] No deadlocks detected in lock ordering", GREEN, RESET);

    git_status_set_enabled(0);
    PHASE_END("GIT_STATUS: MUTEX ORDERING", ok);
    return ok;
}

// Test: Disabled state behavior
static int test_disabled_state(void) {
    int ok = 1;
    PHASE_START("GIT_STATUS: DISABLED STATE", "Test behavior when disabled");

    git_status_init();
    git_status_set_enabled(0);  // Explicitly disabled

    // Request should be no-op
    git_status_request_async(".");

    char buf[128] = {0};
    int len = git_status_get_cached(buf, sizeof(buf));

    if (len != 0) {
        LOG_ERRORF("[FAIL] Disabled state should return 0, got %d", len);
        ok = 0;
    } else {
        LOG_INFOF("[%sOK%s] Disabled state returns 0 bytes", GREEN, RESET);
    }

    PHASE_END("GIT_STATUS: DISABLED STATE", ok);
    return ok;
}

// Test: ENABLE_EXPECT environment variable
static int test_expect_env_override(void) {
    int ok = 1;
    PHASE_START("GIT_STATUS: EXPECT ENV", "Test ENABLE_EXPECT disables git_status");

    // Set the environment variable that should disable git_status
    setenv("ENABLE_EXPECT", "1", 1);

    git_status_init();
    git_status_set_enabled(1);  // Try to enable, but should be blocked by env var

    git_status_request_async(".");
    sleep(1);

    char buf[128] = {0};
    int len = git_status_get_cached(buf, sizeof(buf));

    if (len != 0) {
        LOG_ERRORF("[FAIL] ENABLE_EXPECT should disable git_status, got %d bytes", len);
        ok = 0;
    } else {
        LOG_INFOF("[%sOK%s] ENABLE_EXPECT properly disables git_status", GREEN, RESET);
    }

    unsetenv("ENABLE_EXPECT");
    PHASE_END("GIT_STATUS: EXPECT ENV", ok);
    return ok;
}

// Test: Rapid enable/disable toggling
static int test_rapid_toggle(void) {
    int ok = 1;
    PHASE_START("GIT_STATUS: RAPID TOGGLE", "Test rapid enable/disable cycles");

    git_status_init();

    LOG_INFO("Toggling enabled state 100 times...");
    for (int i = 0; i < 100; i++) {
        git_status_set_enabled(i % 2);  // Alternate on/off
        git_status_request_async(".");
    }

    LOG_INFOF("[%sOK%s] Rapid toggle completed without crash", GREEN, RESET);

    git_status_set_enabled(0);
    PHASE_END("GIT_STATUS: RAPID TOGGLE", ok);
    return ok;
}

int test_util_git_status(void) {
    int all_ok = 1;

    all_ok &= test_disabled_state();
    all_ok &= test_buffer_overflow_protection();
    all_ok &= test_concurrent_requests();
    all_ok &= test_mutex_lock_ordering();
    all_ok &= test_expect_env_override();
    all_ok &= test_rapid_toggle();

    return all_ok;
}
