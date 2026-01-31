#ifndef MUEMACS_TEST_UTILS_H
#define MUEMACS_TEST_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <locale.h>
#include <errno.h>
#include <time.h>
#include <sys/resource.h>

// Logging support - logs to /tmp/muemacs_debug.log when MUEMACS_DEBUG_LOG is enabled
#include "util/logger.h"

#define RED     "\x1b[31m"
#define GREEN   "\x1b[32m"
#define YELLOW  "\x1b[33m"
#define BLUE    "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN    "\x1b[36m"
#define RESET   "\x1b[0m"

// Test statistics
struct stats_struct {
    unsigned long operations_completed;
    unsigned long test_failures;
    unsigned long test_successes;
    unsigned long commands_tested;
    unsigned long memory_peak_kb;
};
typedef struct stats_struct stats_t;

extern stats_t stats;

// Global path to uemacs binary
extern const char* uemacs_path;

// Modern timeout-based error handling
extern volatile int test_timeout_occurred;
extern volatile int current_test_pid;

void timeout_handler(int sig);

#define PHASE_TIMEOUT_SECONDS 180  // 3 minutes per phase

void setup_phase_timeout();
void clear_phase_timeout();

#define PHASE_START(phase_name, description) \
    LOG_INFOF("======================================== %s: %s ========================================", phase_name, description); \
    setup_phase_timeout();

#define PHASE_END(phase_name, result) \
    clear_phase_timeout(); \
    if (test_timeout_occurred) { \
        LOG_WARNF("[TIMEOUT] %s exceeded time limit", phase_name); \
        stats.test_failures++; \
        result = 0; \
    } else if (result) { \
        LOG_INFOF("[SUCCESS] %s completed successfully", phase_name); \
        stats.test_successes++; \
    } else { \
        LOG_ERRORF("[FAIL] %s failed", phase_name); \
        stats.test_failures++; \
    }

void log_memory_usage();

// Expect script helper
int run_expect_script(const char* script_name, const char* test_file);

// Function to create expect scripts if they don't exist
void create_expect_scripts();

// Keymap validation test function
int test_keymap_validation();

// New Arrow Key Validation
int run_arrow_keys_test(void);

// =============================================================================
// Shared Buffer/Line Test Helpers (E3: consolidate duplicate setup code)
// =============================================================================

struct buffer;  // forward declaration
struct line;

// Buffer helpers - create and manage test buffers
struct buffer *test_setup_buffer(const char *name);
struct buffer *test_setup_buffer_with_lines(const char *name, int num_lines);
void test_cleanup_buffer(struct buffer *bp, struct buffer *oldbp);

// Line helpers
struct line *test_make_line(const char *text);

// Editor initialization - call at start of tests that use editor globals
void test_init_editor(const char *name);

// =============================================================================
// Assertion Macros
// =============================================================================

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        LOG_ERRORF("[FAIL] %s (at %s:%d)", (msg), __FILE__, __LINE__); \
        return 0; \
    } \
} while (0)

#define TEST_ASSERT_EQ(got, expected) do { \
    if ((got) != (expected)) { \
        LOG_ERRORF("[FAIL] Expected %ld, got %ld (at %s:%d)", \
                   (long)(expected), (long)(got), __FILE__, __LINE__); \
        return 0; \
    } \
} while (0)

#define TEST_ASSERT_STR_EQ(got, expected) do { \
    if (strcmp((got), (expected)) != 0) { \
        LOG_ERRORF("[FAIL] Expected \"%s\", got \"%s\" (at %s:%d)", \
                   (expected), (got), __FILE__, __LINE__); \
        return 0; \
    } \
} while (0)

#define TEST_ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        LOG_ERRORF("[FAIL] Expected non-NULL pointer (at %s:%d)", \
                   __FILE__, __LINE__); \
        return 0; \
    } \
} while (0)

#endif // MUEMACS_TEST_UTILS_H
