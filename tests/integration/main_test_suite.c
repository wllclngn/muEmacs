/*
 * main_test_suite.c - μEmacs Full Integration Test Suite
 *
 * Uses fork-based isolation for ALL tests. Every test runs in a subprocess.
 * Catches crashes, timeouts, and provides detailed error reporting.
 *
 * Build: cmake --build build
 * Run:   ./build/bin/full_integration_test [FILTER...]
 *
 * Any command-line arguments act as substring filters on test-function
 * names: a TEST_RUN whose name matches no filter is skipped. Zero
 * arguments runs everything.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>

/* New production test runner */
#include "../test_runner.h"

/* Test registry - all test function declarations */
#include "../test_registry.h"

/* Legacy test utils (for helper functions, not the old runner) */
#include "../test_utils.h"

/* Editor includes for initialization */
#include "estruct.h"
#include "edef.h"

/* ============================================================================
 * External Test Functions (from various test files)
 * ============================================================================ */

/* Phase 1: Core Text Operations */
extern int test_phase1_core_text_operations(void);
extern int test_api_insert_delete(void);
extern int test_api_magic_basic(void);
extern int test_api_search_crossline(void);
extern int test_api_literal_selector(void);
extern int test_api_crossline_literal_extended(void);
extern int test_api_search_degenerate_case(void);
extern int test_api_search_nomatch_and_long(void);

/* Phase 2: Navigation */
extern int test_phase2_navigation_cursor(void);

/* Phase 3: Selection/Region */
extern int test_phase3_selection_region(void);

/* Phase 4: Command Validation */
extern int test_phase4_command_validation(void);

/* Phase 5: Undo/Redo */
extern int test_phase5_advanced_undo_redo(void);

/* Atomic Stats */
extern int test_atomic_stats_updates(void);
extern int test_atomic_stats_o1_operations(void);
extern int test_atomic_stats_incremental(void);
extern int test_atomic_stats_concurrency(void);

/* Security Tests */
extern int test_password_handling(void);
extern int test_secure_memory_operations(void);
extern int test_attack_resistance(void);
extern int test_crypto_robustness(void);
extern int test_secure_file_operations(void);

/* Advanced Text */
extern int test_region_operations(void);
extern int test_word_operations(void);
extern int test_paragraph_operations(void);
extern int test_advanced_search_replace(void);
extern int test_text_transformation(void);
extern int test_macro_text_processing(void);
extern int test_unicode_text_handling(void);

/* Platform Tests */
extern int test_linux_terminal_features(void);
extern int test_filesystem_specific(void);
extern int test_memory_management_linux(void);
extern int test_threading_primitives(void);
extern int test_ipc_mechanisms(void);
extern int test_kernel_interfaces(void);

/* Vim Mode & Settings */
extern int test_vim_mode(void);
extern int test_settings_system(void);

/* Display Tests */
extern int test_soft_wrap_display(void);
extern int test_truecolor_display(void);

/* M-x Command Flow */
extern int test_mx_command_flow(void);

/* Writing/Gutenberg Test */
extern int test_poe_gutenberg_wrap(void);

/* Name filtering - argv substrings select which TEST_RUNs execute */

static int g_filter_count = 0;
static char **g_filters = nullptr;

static int test_name_selected(const char *name) {
    if (g_filter_count == 0) return 1;
    for (int i = 0; i < g_filter_count; i++) {
        if (strstr(name, g_filters[i]) != nullptr) return 1;
    }
    return 0;
}

/* Wrap the runner's TEST_RUN: unmatched tests are skipped entirely
 * (not run, not counted). With no filters this is the original macro. */
#undef TEST_RUN
#define TEST_RUN(func, desc, timeout) \
    do { \
        if (!test_name_selected(#func)) break; \
        g_suite_stats.total++; \
        test_result_t _result = run_test_isolated(#func, desc, func, timeout); \
        switch (_result) { \
            case TEST_PASS:    g_suite_stats.passed++; break; \
            case TEST_FAIL:    g_suite_stats.failed++; break; \
            case TEST_ERROR:   g_suite_stats.errors++; break; \
            case TEST_CRASH:   g_suite_stats.crashes++; break; \
            case TEST_TIMEOUT: g_suite_stats.timeouts++; break; \
            case TEST_SKIP:    g_suite_stats.skipped++; break; \
        } \
    } while (0)

/* ============================================================================
 * Main Test Suite
 * ============================================================================ */

int main(int argc, char *argv[]) {
    /* Any args become substring filters on test-function names */
    g_filter_count = argc - 1;
    g_filters = argv + 1;

    /* Setup */
    setenv("ASAN_OPTIONS", "detect_leaks=0", 1);
    setenv("LSAN_OPTIONS", "detect_leaks=0", 1);
    setlocale(LC_ALL, "en_US.UTF-8");

    /* Redirect stdin from /dev/null to prevent blocking */
    if (freopen("/dev/null", "r", stdin) == nullptr) {
        perror("freopen /dev/null");
    }

    /* Find the μEmacs binary for tests that need it */
    const char *binary_paths[] = {
        "./bin/μEmacs",
        "./build/bin/μEmacs",
        "../build/bin/μEmacs",
        "../../build/bin/μEmacs",
        "./bin/muEmacs",
        "./build/bin/muEmacs",
        "../build/bin/muEmacs",
        "../../build/bin/muEmacs",
        nullptr
    };
    for (int i = 0; binary_paths[i] != nullptr; i++) {
        if (access(binary_paths[i], X_OK) == 0) {
            uemacs_path = binary_paths[i];
            break;
        }
    }

    LOG_INFO("");
    LOG_INFO("================================================================");
    LOG_INFO("       μEmacs Full Integration Test Suite (Isolated Runner)");
    LOG_INFO("       Each test runs in a forked subprocess for safety");
    LOG_INFO("================================================================");

    /* ========================================================================
     * TEST SUITE: Keymap & Input System
     * ======================================================================== */
    TEST_SUITE_BEGIN("Keymap & Input System");

    TEST_SECTION("Keymap Validation");
    TEST_RUN(test_keymap_validation, "Atomic keymap operations", 30);
    TEST_RUN(test_keymap_defaults, "Default key bindings", 30);
    TEST_RUN(test_help_prefix_toggle, "Help prefix toggle safety", 30);
    TEST_RUN(test_keymap_functionality, "Hash-based keymap system", 60);

    TEST_SECTION("Input Handling");
    TEST_RUN(test_io_input, "Input parsing and events", 60);
    TEST_RUN(test_terminal_input_state, "Terminal input state machine", 30);

    TEST_SUITE_END();

    /* ========================================================================
     * TEST SUITE: Core Operations
     * ======================================================================== */
    TEST_SUITE_BEGIN("Core Text & Buffer Operations");

    TEST_SECTION("Phase 1: Core Text");
    TEST_RUN(test_phase1_core_text_operations, "Basic text operations", 60);
    TEST_RUN(test_api_insert_delete, "Insert/delete API", 30);

    TEST_SECTION("Search & Pattern Matching");
    TEST_RUN(test_api_magic_basic, "Magic mode basic", 30);
    TEST_RUN(test_api_search_crossline, "Cross-line search", 30);
    TEST_RUN(test_api_literal_selector, "Literal selector", 30);
    TEST_RUN(test_api_crossline_literal_extended, "Extended cross-line", 30);
    TEST_RUN(test_api_search_degenerate_case, "Degenerate search cases", 30);
    TEST_RUN(test_api_search_nomatch_and_long, "No-match and long patterns", 30);
    TEST_RUN(test_text_search, "Text search system", 60);

    TEST_SECTION("Buffer & Gap Buffer");
    TEST_RUN(test_core_gapbuffer, "Gap buffer data structure", 30);
    TEST_RUN(test_core_buffer, "Buffer management", 30);
    TEST_RUN(test_core_line, "Line operations", 30);

    TEST_SUITE_END();

    /* ========================================================================
     * TEST SUITE: Navigation & Editing
     * ======================================================================== */
    TEST_SUITE_BEGIN("Navigation & Editing");

    TEST_RUN(test_phase2_navigation_cursor, "Cursor navigation", 60);
    TEST_RUN(test_phase3_selection_region, "Selection/region", 60);
    TEST_RUN(test_core_basic, "Basic movement", 30);
    TEST_RUN(test_core_window, "Window management", 30);
    TEST_RUN(test_core_window_hash, "Window hash table", 30);

    TEST_SECTION("Undo/Redo System");
    TEST_RUN(test_core_undo, "Undo system", 60);
    TEST_RUN(test_phase5_advanced_undo_redo, "Advanced undo/redo", 60);

    TEST_SUITE_END();

    /* ========================================================================
     * TEST SUITE: Text Processing
     * ======================================================================== */
    TEST_SUITE_BEGIN("Text Processing");

    TEST_RUN(test_text_isearch, "Incremental search", 60);
    TEST_RUN(test_text_region, "Region operations", 30);
    TEST_RUN(test_text_word, "Word operations", 30);
    TEST_RUN(test_text_random, "Random text ops", 30);
    TEST_RUN(test_text_replace, "Replace operations", 30);
    TEST_RUN(test_text_writing, "Writing mode", 60);

    TEST_SECTION("NFA & Regex");

    TEST_SECTION("UTF-8 Handling");
    TEST_RUN(test_utf8_invalid_sequences, "Invalid UTF-8", 30);
    TEST_RUN(test_utf8_randomized_sanity, "Randomized UTF-8", 30);

    TEST_SUITE_END();

    /* ========================================================================
     * TEST SUITE: Configuration System
     * ======================================================================== */
    TEST_SUITE_BEGIN("Configuration System");

    TEST_RUN(test_config_settings, "Settings system", 30);
    TEST_RUN(test_config_vim, "Vim mode config", 30);
    TEST_RUN(test_config_eval, "Eval/variables", 30);
    TEST_RUN(test_config_exec, "Macro execution", 30);
    TEST_RUN(test_config_bind, "Key bindings", 30);
    TEST_RUN(test_config_names, "Name table", 30);

    TEST_SUITE_END();

    /* ========================================================================
     * TEST SUITE: I/O & File Operations
     * ======================================================================== */
    TEST_SUITE_BEGIN("I/O & File Operations");

    TEST_RUN(test_io_file, "File operations", 30);
    TEST_RUN(test_io_fileio, "Low-level file I/O", 30);
    TEST_RUN(test_io_lock, "File locking", 30);
    TEST_RUN(test_text_encrypt, "Encryption", 30);

    TEST_SUITE_END();

    /* ========================================================================
     * TEST SUITE: Terminal & Display
     * ======================================================================== */
    TEST_SUITE_BEGIN("Terminal & Display");

    TEST_RUN(test_terminal_ansi, "ANSI parser", 30);
    TEST_RUN(test_terminal_signal, "Signal handlers", 30);
    TEST_RUN(test_terminal_cleanup, "Terminal cleanup", 30);
    TEST_RUN(test_terminal_pty, "PTY operations", 60);
    TEST_RUN(test_core_display, "Display system", 60);
    TEST_RUN(test_soft_wrap_display, "Soft wrap", 60);
    TEST_RUN(test_wrap_segments, "Wrap segment cache", 30);
    TEST_RUN(test_truecolor_display, "TrueColor RGB", 30);

    TEST_SUITE_END();

    /* ========================================================================
     * TEST SUITE: Vim Mode
     * ======================================================================== */
    TEST_SUITE_BEGIN("Vim Mode (Evil)");

    TEST_RUN(test_vim_mode, "Vim mode system", 120);
    TEST_RUN(test_settings_system, "Settings integration", 60);

    TEST_SUITE_END();

    /* ========================================================================
     * TEST SUITE: Utilities & Platform
     * ======================================================================== */
    TEST_SUITE_BEGIN("Utilities & Platform");

    TEST_RUN(test_util_profiler, "Profiler", 30);
    TEST_RUN(test_util_memory, "Memory management", 30);
    TEST_RUN(test_util_git_status, "Git status async", 60);
    TEST_RUN(test_util_string, "String utilities", 30);
    TEST_RUN(test_platform_spawn, "Spawn/shell", 60);

    TEST_SECTION("Core Globals");
    TEST_RUN(test_core_globals, "Global variables", 30);
    TEST_RUN(test_atomic_cursor, "Atomic cursor", 30);

    TEST_SUITE_END();

    /* ========================================================================
     * TEST SUITE: Atomic Stats & Concurrency
     * ======================================================================== */
    TEST_SUITE_BEGIN("Atomic Stats & Concurrency");

    TEST_RUN(test_atomic_stats_updates, "Atomic updates", 30);
    TEST_RUN(test_atomic_stats_o1_operations, "O(1) operations", 30);
    TEST_RUN(test_atomic_stats_incremental, "Incremental stats", 30);
    TEST_RUN(test_atomic_stats_concurrency, "Concurrent access", 60);

    TEST_SUITE_END();

    /* ========================================================================
     * TEST SUITE: Commands & Integration
     * ======================================================================== */
    TEST_SUITE_BEGIN("Commands & Integration");

    TEST_RUN(test_phase4_command_validation, "Command validation", 60);
    TEST_RUN(test_mx_command_flow, "M-x command flow", 60);
    TEST_RUN(test_poe_gutenberg_wrap, "Gutenberg wrap test", 120);

    TEST_SUITE_END();

    /* ========================================================================
     * TEST SUITE: Security
     * ======================================================================== */
    TEST_SUITE_BEGIN("Security Tests");

    TEST_RUN(test_password_handling, "Password handling", 30);
    TEST_RUN(test_secure_memory_operations, "Secure memory", 30);
    TEST_RUN(test_attack_resistance, "Attack resistance", 30);
    TEST_RUN(test_crypto_robustness, "Crypto robustness", 30);
    TEST_RUN(test_secure_file_operations, "Secure file ops", 30);

    TEST_SUITE_END();

    /* ========================================================================
     * Final Summary
     * ======================================================================== */
    LOG_INFO("");
    LOG_INFO("================================================================");
    LOG_INFO("                    ALL SUITES COMPLETE");
    LOG_INFO("================================================================");

    test_suite_stats_t *stats = test_get_stats();
    int total_failures = stats->failed + stats->crashes +
                         stats->timeouts + stats->errors;

    return total_failures > 0 ? 1 : 0;
}
