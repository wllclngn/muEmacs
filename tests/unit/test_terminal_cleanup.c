// test_terminal_cleanup.c - Unit tests for terminal cleanup
// Tests src/terminal/cleanup.c cleanup and state management functions

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include <unistd.h>
#include <string.h>

// External cleanup functions from cleanup.c
extern void terminal_cleanup_init(void);
extern void terminal_set_alt_screen(bool active);
extern void terminal_set_cursor_visible(bool visible);
extern bool terminal_should_exit(void);
extern void terminal_force_exit(void);

// ============================================================================
// Cleanup Initialization Tests
// ============================================================================

static int test_cleanup_init(void) {
    int ok = 1;
    PHASE_START("CLEANUP: INIT", "Cleanup initialization");

    // Should not crash
    terminal_cleanup_init();

    // Multiple calls should be idempotent
    terminal_cleanup_init();
    terminal_cleanup_init();

    PHASE_END("CLEANUP: INIT", ok);
    return ok;
}

// ============================================================================
// Alt Screen State Tests
// ============================================================================

static int test_alt_screen_activate(void) {
    int ok = 1;
    PHASE_START("CLEANUP: ALT-ON", "Alternate screen activation");

    // Activate alt screen (should write escape sequence)
    terminal_set_alt_screen(true);

    // Multiple activations should be safe
    terminal_set_alt_screen(true);

    PHASE_END("CLEANUP: ALT-ON", ok);
    return ok;
}

static int test_alt_screen_deactivate(void) {
    int ok = 1;
    PHASE_START("CLEANUP: ALT-OFF", "Alternate screen deactivation");

    // Deactivate alt screen
    terminal_set_alt_screen(false);

    // Multiple deactivations should be safe
    terminal_set_alt_screen(false);

    PHASE_END("CLEANUP: ALT-OFF", ok);
    return ok;
}

static int test_alt_screen_transitions(void) {
    int ok = 1;
    PHASE_START("CLEANUP: ALT-TRANS", "Alternate screen state transitions");

    // Test multiple transitions
    terminal_set_alt_screen(true);
    terminal_set_alt_screen(false);
    terminal_set_alt_screen(true);
    terminal_set_alt_screen(false);

    PHASE_END("CLEANUP: ALT-TRANS", ok);
    return ok;
}

// ============================================================================
// Cursor Visibility Tests
// ============================================================================

static int test_cursor_hide(void) {
    int ok = 1;
    PHASE_START("CLEANUP: CURSOR-HIDE", "Cursor hiding");

    // Hide cursor
    terminal_set_cursor_visible(false);

    // Multiple hides should be safe
    terminal_set_cursor_visible(false);

    PHASE_END("CLEANUP: CURSOR-HIDE", ok);
    return ok;
}

static int test_cursor_show(void) {
    int ok = 1;
    PHASE_START("CLEANUP: CURSOR-SHOW", "Cursor showing");

    // Show cursor
    terminal_set_cursor_visible(true);

    // Multiple shows should be safe
    terminal_set_cursor_visible(true);

    PHASE_END("CLEANUP: CURSOR-SHOW", ok);
    return ok;
}

static int test_cursor_transitions(void) {
    int ok = 1;
    PHASE_START("CLEANUP: CURSOR-TRANS", "Cursor visibility transitions");

    // Test multiple transitions
    terminal_set_cursor_visible(false);
    terminal_set_cursor_visible(true);
    terminal_set_cursor_visible(false);
    terminal_set_cursor_visible(true);

    PHASE_END("CLEANUP: CURSOR-TRANS", ok);
    return ok;
}

// ============================================================================
// Exit Flag Tests
// ============================================================================

static int test_exit_flag_initial(void) {
    int ok = 1;
    PHASE_START("CLEANUP: EXIT-INIT", "Initial exit flag state");

    // Should initially be false (unless set by previous test)
    // This is a best-effort test since state is global
    bool should_exit = terminal_should_exit();

    // Just verify the call succeeds
    (void)should_exit;

    PHASE_END("CLEANUP: EXIT-INIT", ok);
    return ok;
}

static int test_exit_flag_force(void) {
    int ok = 1;
    PHASE_START("CLEANUP: EXIT-FORCE", "Force exit flag");

    // Force exit
    terminal_force_exit();

    // Should now return true
    if (!terminal_should_exit()) {
        LOG_ERROR("[FAIL] terminal_should_exit false after force_exit");
        ok = 0;
    }

    // Multiple force calls should be safe
    terminal_force_exit();
    terminal_force_exit();

    PHASE_END("CLEANUP: EXIT-FORCE", ok);
    return ok;
}

// ============================================================================
// Idempotency Tests
// ============================================================================

static int test_cleanup_idempotent(void) {
    int ok = 1;
    PHASE_START("CLEANUP: IDEMPOTENT", "Cleanup idempotency");

    // Test that multiple cleanup operations are safe
    terminal_set_alt_screen(false);
    terminal_set_alt_screen(false);

    terminal_set_cursor_visible(true);
    terminal_set_cursor_visible(true);

    // Mix operations
    terminal_set_alt_screen(true);
    terminal_set_cursor_visible(false);
    terminal_set_alt_screen(true);
    terminal_set_cursor_visible(false);

    PHASE_END("CLEANUP: IDEMPOTENT", ok);
    return ok;
}

// ============================================================================
// Combined State Tests
// ============================================================================

static int test_cleanup_combined_states(void) {
    int ok = 1;
    PHASE_START("CLEANUP: COMBINED", "Combined state management");

    // Test setting multiple states in sequence
    terminal_set_alt_screen(true);
    terminal_set_cursor_visible(false);

    // Reverse them
    terminal_set_cursor_visible(true);
    terminal_set_alt_screen(false);

    // Random pattern
    terminal_set_alt_screen(true);
    terminal_set_alt_screen(false);
    terminal_set_cursor_visible(false);
    terminal_set_cursor_visible(true);

    PHASE_END("CLEANUP: COMBINED", ok);
    return ok;
}

// ============================================================================
// Entry Point
// ============================================================================

int test_terminal_cleanup(void) {
    int result = 1;

    result &= test_cleanup_init();
    result &= test_alt_screen_activate();
    result &= test_alt_screen_deactivate();
    result &= test_alt_screen_transitions();
    result &= test_cursor_hide();
    result &= test_cursor_show();
    result &= test_cursor_transitions();
    result &= test_exit_flag_initial();
    result &= test_exit_flag_force();
    result &= test_cleanup_idempotent();
    result &= test_cleanup_combined_states();

    return result;
}
