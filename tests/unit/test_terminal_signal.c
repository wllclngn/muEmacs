// test_terminal_signal.c - Unit tests for terminal signal handling
// Tests src/terminal/signal_handler.c signal management functions

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "terminal/signal_handler.h"
#include <unistd.h>
#include <sys/ioctl.h>

// ============================================================================
// Signal Handler Installation Tests
// ============================================================================

static int test_signal_handler_init_basic(void) {
    int ok = 1;
    PHASE_START("SIGNAL: INIT", "Signal handlers initialization");

    // Install handlers
    signal_handlers_init();

    // Verify that calling init again is idempotent (should not crash)
    signal_handlers_init();

    // Cleanup
    signal_handlers_cleanup();

    PHASE_END("SIGNAL: INIT", ok);
    return ok;
}

static int test_signal_handler_cleanup(void) {
    int ok = 1;
    PHASE_START("SIGNAL: CLEANUP", "Signal handlers cleanup");

    // Install and cleanup
    signal_handlers_init();
    signal_handlers_cleanup();

    // Cleanup should be idempotent (should not crash)
    signal_handlers_cleanup();

    PHASE_END("SIGNAL: CLEANUP", ok);
    return ok;
}

// ============================================================================
// SIGWINCH Window Resize Tests
// ============================================================================

static int test_sigwinch_size_update(void) {
    int ok = 1;
    PHASE_START("SIGNAL: WINCH", "SIGWINCH window size update");

    signal_handlers_init();

    // Clear any existing signals
    bool size_changed = false;
    terminal_size_t new_size = {0};
    signal_handle_pending(&size_changed, &new_size);

    // Simulate SIGWINCH by directly setting the flag
    // (we can't actually send signals in unit tests without fork)
    sig_winch_pending = 1;
    sig_new_rows = 50;
    sig_new_cols = 100;

    // Check if signal is handled
    size_changed = false;
    bool handled = signal_handle_pending(&size_changed, &new_size);

    if (!handled) {
        LOG_ERROR("[FAIL] signal_handle_pending did not handle SIGWINCH");
        ok = 0;
    }

    if (!size_changed) {
        LOG_ERROR("[FAIL] size_changed not set after SIGWINCH");
        ok = 0;
    }

    // Note: new_size will be updated by fresh ioctl in signal_handle_pending,
    // so we can't reliably test exact values here. Just verify it was handled.

    signal_handlers_cleanup();
    PHASE_END("SIGNAL: WINCH", ok);
    return ok;
}

static int test_sigwinch_no_size_params(void) {
    int ok = 1;
    PHASE_START("SIGNAL: WINCH-NULL", "SIGWINCH with NULL size params");

    signal_handlers_init();

    // Set SIGWINCH flag
    sig_winch_pending = 1;

    // Call without size params (should not crash)
    bool handled = signal_handle_pending(NULL, NULL);

    if (!handled) {
        LOG_ERROR("[FAIL] signal_handle_pending did not handle SIGWINCH");
        ok = 0;
    }

    signal_handlers_cleanup();
    PHASE_END("SIGNAL: WINCH-NULL", ok);
    return ok;
}

// ============================================================================
// Exit Signal Tests (SIGINT, SIGTERM, SIGHUP, SIGPIPE)
// ============================================================================

static int test_signal_exit_conditions(void) {
    int ok = 1;
    PHASE_START("SIGNAL: EXIT", "Exit signal detection");

    signal_handlers_init();

    // Initially should not exit
    if (signal_should_exit()) {
        LOG_ERROR("[FAIL] signal_should_exit true before any signals");
        ok = 0;
    }

    // Test SIGHUP
    sig_hup_pending = 1;
    if (!signal_should_exit()) {
        LOG_ERROR("[FAIL] signal_should_exit false after SIGHUP");
        ok = 0;
    }
    signal_clear_exit();

    // Test SIGTERM
    sig_term_pending = 1;
    if (!signal_should_exit()) {
        LOG_ERROR("[FAIL] signal_should_exit false after SIGTERM");
        ok = 0;
    }
    signal_clear_exit();

    // Test SIGPIPE
    sig_pipe_pending = 1;
    if (!signal_should_exit()) {
        LOG_ERROR("[FAIL] signal_should_exit false after SIGPIPE");
        ok = 0;
    }
    signal_clear_exit();

    // After clear, should not exit
    if (signal_should_exit()) {
        LOG_ERROR("[FAIL] signal_should_exit true after clear_exit");
        ok = 0;
    }

    signal_handlers_cleanup();
    PHASE_END("SIGNAL: EXIT", ok);
    return ok;
}

static int test_signal_interrupt(void) {
    int ok = 1;
    PHASE_START("SIGNAL: INT", "SIGINT interrupt handling");

    signal_handlers_init();

    // Initially no interrupt
    if (signal_check_interrupt()) {
        LOG_ERROR("[FAIL] signal_check_interrupt true before SIGINT");
        ok = 0;
    }

    // Simulate SIGINT
    sig_int_pending = 1;

    // Check and clear
    if (!signal_check_interrupt()) {
        LOG_ERROR("[FAIL] signal_check_interrupt false after SIGINT");
        ok = 0;
    }

    // Should be cleared after check
    if (signal_check_interrupt()) {
        LOG_ERROR("[FAIL] signal_check_interrupt still true after clear");
        ok = 0;
    }

    // Test explicit clear
    sig_int_pending = 1;
    signal_clear_interrupt();
    if (sig_int_pending != 0) {
        LOG_ERROR("[FAIL] signal_clear_interrupt did not clear flag");
        ok = 0;
    }

    signal_handlers_cleanup();
    PHASE_END("SIGNAL: INT", ok);
    return ok;
}

// ============================================================================
// Terminal Size Query Tests
// ============================================================================

static int test_signal_get_terminal_size(void) {
    int ok = 1;
    PHASE_START("SIGNAL: SIZE", "Terminal size query");

    terminal_size_t size = {0};
    bool result = signal_get_terminal_size(&size);

    // Result depends on whether we're running in a real terminal
    // Just verify it doesn't crash and returns sensible values
    if (result) {
        if (size.rows <= 0 || size.cols <= 0) {
            LOG_ERRORF("[FAIL] Invalid terminal size: %dx%d", size.rows, size.cols);
            ok = 0;
        }
    }

    // Test NULL parameter (should return false)
    result = signal_get_terminal_size(NULL);
    if (result) {
        LOG_ERROR("[FAIL] signal_get_terminal_size returned true for NULL");
        ok = 0;
    }

    PHASE_END("SIGNAL: SIZE", ok);
    return ok;
}

// ============================================================================
// Termios Save/Restore Tests
// ============================================================================

static int test_signal_save_termios(void) {
    int ok = 1;
    PHASE_START("SIGNAL: TERMIOS", "Termios save/restore");

    struct termios t;
    memset(&t, 0, sizeof(t));
    t.c_lflag = ECHO | ICANON;  // Some test values

    // Save termios
    signal_save_termios(&t);

    // Saving again should not crash
    signal_save_termios(&t);

    // Save NULL (should not crash)
    signal_save_termios(NULL);

    PHASE_END("SIGNAL: TERMIOS", ok);
    return ok;
}

static int test_signal_restore_terminal_safe(void) {
    int ok = 1;
    PHASE_START("SIGNAL: RESTORE", "Async-signal-safe terminal restore");

    // This function should be async-signal-safe and not crash
    signal_restore_terminal_safe();

    // Multiple calls should be idempotent
    signal_restore_terminal_safe();

    PHASE_END("SIGNAL: RESTORE", ok);
    return ok;
}

// ============================================================================
// Signal Blocking Tests
// ============================================================================

static int test_signal_blocking(void) {
    int ok = 1;
    PHASE_START("SIGNAL: BLOCK", "Signal blocking/unblocking");

    // Block all signals
    sigset_t old_mask = signal_block_all();

    // Verify signals are blocked (we can't easily test this without fork)
    // Just verify the call succeeded

    // Unblock
    signal_unblock(old_mask);

    // Should not crash on repeated calls
    old_mask = signal_block_all();
    signal_unblock(old_mask);

    PHASE_END("SIGNAL: BLOCK", ok);
    return ok;
}

// ============================================================================
// SIGCONT Tests
// ============================================================================

static int test_signal_cont(void) {
    int ok = 1;
    PHASE_START("SIGNAL: CONT", "SIGCONT continue handling");

    signal_handlers_init();

    // Simulate SIGCONT
    sig_cont_pending = 1;

    // Should be handled
    bool handled = signal_handle_pending(NULL, NULL);
    if (!handled) {
        LOG_ERROR("[FAIL] SIGCONT not handled");
        ok = 0;
    }

    // Flag should be cleared
    if (sig_cont_pending != 0) {
        LOG_ERROR("[FAIL] SIGCONT flag not cleared");
        ok = 0;
    }

    signal_handlers_cleanup();
    PHASE_END("SIGNAL: CONT", ok);
    return ok;
}

// ============================================================================
// Entry Point
// ============================================================================

int test_terminal_signal(void) {
    int result = 1;

    result &= test_signal_handler_init_basic();
    result &= test_signal_handler_cleanup();
    result &= test_sigwinch_size_update();
    result &= test_sigwinch_no_size_params();
    result &= test_signal_exit_conditions();
    result &= test_signal_interrupt();
    result &= test_signal_get_terminal_size();
    result &= test_signal_save_termios();
    result &= test_signal_restore_terminal_safe();
    result &= test_signal_blocking();
    result &= test_signal_cont();

    return result;
}
