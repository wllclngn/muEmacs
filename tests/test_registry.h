// test_registry.h - Central registry of all unit test entry points
// Each unit test module declares its entry point functions here
// The integration test includes this header to call all tests

#ifndef MUEMACS_TEST_REGISTRY_H
#define MUEMACS_TEST_REGISTRY_H

// Util module tests
int test_utf8_invalid_sequences(void);
int test_utf8_randomized_sanity(void);
int test_util_profiler(void);       // Profiler system tests

// Text module tests
int test_text_search(void);  // Search functionality tests

// Core module tests
int test_keymap_functionality(void);
int test_keymap_defaults(void);
int test_help_prefix_toggle(void);
int test_core_gapbuffer(void);  // Gap buffer data structure tests
int test_core_undo(void);       // Undo/redo system tests

// I/O module tests
int test_io_input(void);        // Input handling tests
int test_io_file(void);         // File I/O tests
int test_io_fileio(void);       // Low-level file I/O tests

// Core module tests (new modular tests)
int test_core_buffer(void);     // Buffer management tests
int test_core_line(void);       // Line operations tests
int test_core_window(void);     // Window management tests
int test_core_basic(void);      // Basic cursor movement tests

// Text module tests (new modular tests)
int test_text_isearch(void);    // Incremental search tests
int test_text_region(void);     // Region operations tests
int test_text_word(void);       // Word operations tests
int test_text_random(void);     // Random text operations tests
int test_text_replace(void);    // Replace operations tests
int test_text_encrypt(void);    // Encryption functionality tests
int test_text_writing(void);    // Writing mode tests

// Config module tests
int test_config_settings(void); // Settings system tests
int test_config_vim(void);      // Vim mode tests
int test_config_eval(void);     // Evaluation/variable system tests
int test_config_exec(void);     // Macro execution system tests
int test_config_bind(void);     // Key binding system tests
int test_config_names(void);    // Name binding table tests

// Core module tests (additional)
int test_core_globals(void);    // Global variable tests
int test_core_window_hash(void); // Window hash table tests

// Util module tests (additional)
int test_util_memory(void);     // Memory management tests
int test_util_git_status(void); // Git status async/threading tests
int test_util_string(void);     // Safe string function tests

// I/O module tests (additional)
int test_io_lock(void);         // File locking tests

// Terminal integration tests
int test_terminal_pty(void);        // PTY and terminal buffer tests

// Terminal unit tests
int test_terminal_ansi(void);       // ANSI/VT100 parser tests

// Platform module tests
int test_platform_spawn(void);      // Spawn/shell command tests

// Additional core display tests
int test_core_display(void);        // Display system tests

// Terminal signal/cleanup/input tests
int test_terminal_signal(void);     // Signal handler tests
int test_terminal_cleanup(void);    // Cleanup and state management tests
int test_terminal_input_state(void); // Input state machine tests

// Atomic cursor tests (Phase D)
int test_atomic_cursor(void);       // Concurrent cursor variable tests

// Wrap segment cache tests (Pretext-inspired)
int test_wrap_segments(void);

#endif // MUEMACS_TEST_REGISTRY_H
