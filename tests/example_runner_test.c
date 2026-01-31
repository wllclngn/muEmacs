/*
 * example_runner_test.c - Example using the fork-based test runner
 *
 * Demonstrates the TEST_SUITE_BEGIN/TEST_RUN/TEST_SUITE_END macros.
 * Each test runs in a forked subprocess for safety.
 *
 * Build: gcc -o test_example example_runner_test.c
 * Run: ./test_example
 */

#include "test_runner.h"

/* Example test that passes */
int test_addition(void) {
    int result = 2 + 2;
    if (result != 4) {
        fprintf(stderr, "Expected 4, got %d\n", result);
        return 0;  /* FAIL */
    }
    return 1;  /* PASS */
}

/* Example test that fails */
int test_deliberate_fail(void) {
    fprintf(stderr, "This test intentionally fails\n");
    return 0;  /* FAIL */
}

/* Example test that crashes (SIGSEGV) */
int test_crash_segfault(void) {
    int *ptr = NULL;
    *ptr = 42;  /* BOOM */
    return 1;
}

/* Example test that times out */
int test_infinite_loop(void) {
    while (1) {
        /* Never returns */
    }
    return 1;
}

/* Example test that's skipped */
int test_skip_example(void) {
    fprintf(stderr, "Feature not implemented\n");
    _exit(77);  /* Convention for SKIP */
    return 1;
}

int main(void) {
    LOG_INFO("");
    LOG_INFO("================================================================");
    LOG_INFO("       Example Test Runner Demonstration");
    LOG_INFO("       Each test runs in a forked subprocess for safety");
    LOG_INFO("================================================================");

    /* Basic math tests */
    TEST_SUITE_BEGIN("Basic Math");
    TEST_RUN(test_addition, "Basic arithmetic", 5);
    TEST_SUITE_END();

    /* Failure handling demonstration */
    TEST_SUITE_BEGIN("Failure Handling");
    TEST_RUN(test_deliberate_fail, "Intentional failure", 5);
    TEST_RUN(test_crash_segfault, "Crash handling", 5);
    TEST_RUN(test_infinite_loop, "Timeout handling", 2);  /* 2 second timeout */
    TEST_RUN(test_skip_example, "Skip demonstration", 5);
    TEST_SUITE_END();

    LOG_INFO("");
    LOG_INFO("================================================================");
    LOG_INFO("                    DEMO COMPLETE");
    LOG_INFO("================================================================");

    /* Return non-zero if any failures */
    test_suite_stats_t *stats = test_get_stats();
    int failures = stats->failed + stats->errors + stats->crashes + stats->timeouts;
    return failures > 0 ? 1 : 0;
}
