#ifndef UEMACS_TEST_UTILS_H
#define UEMACS_TEST_UTILS_H

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

#define RED   "\x1b[31m"
#define GREEN "\x1b[32m" 
#define YELLOW "\x1b[33m"
#define BLUE  "\x1b[34m"
#define RESET "\x1b[0m"

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
    printf("\n%s========================================%s\n", BLUE, RESET); \
    printf("%s        %s%s\n", BLUE, phase_name, RESET); \
    printf("%s        %s%s\n", BLUE, description, RESET); \
    printf("%s========================================%s\n", BLUE, RESET); \
    setup_phase_timeout();

#define PHASE_END(phase_name, result) \
    clear_phase_timeout(); \
    if (test_timeout_occurred) { \
        printf("\n[%sTIMEOUT%s] Test exceeded time limit\n", YELLOW, RESET); \
        stats.test_failures++; \
        result = 0; \
    } else if (result) { \
        printf("\n[%sSUCCESS%s] %s completed successfully\n", GREEN, RESET, phase_name); \
        stats.test_successes++; \
    } else { \
        printf("\n[%sFAIL%s] %s failed\n", RED, RESET, phase_name); \
        stats.test_failures++; \
    }

void log_memory_usage();

// Expect script helper
int run_expect_script(const char* script_name, const char* test_file);

// Function to create expect scripts if they don't exist
void create_expect_scripts();

// Keymap validation test function
int test_keymap_validation();

#endif // UEMACS_TEST_UTILS_H
