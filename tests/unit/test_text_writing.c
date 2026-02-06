// test_text_writing.c - Unit tests for writing mode functionality
// Tests src/config/writing.c

#include "../test_utils.h"
#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/efunc.h"
#include <string.h>
#include <stdbool.h>

static void init_editor_minimal(const char* name) {
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    edinit((char*)(name ? name : "writing"));
    varinit();
    writing_mode_reset();  /* Clear any leftover state from previous tests */
    fillcol = 72;  /* Reset to default to avoid polluting other tests */
}

// Test basic writing mode enable/disable
int test_writing_mode_toggle() {
    int ok = 1;
    PHASE_START("WRITING: TOGGLE", "Testing writing mode enable/disable");

    init_editor_minimal("writing-toggle");
    (void)bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Save initial state
    int initial_fillcol = fillcol;
    int initial_wrap = (curbp->b_mode & MDWRAP) ? 1 : 0;

    // Test 1: Enable writing mode with default column (80)
    int result = writing_mode_enable(false, 0);

    if (result != true) {
        ok = 0;
        LOG_ERROR("[FAIL] writing_mode_enable should return true");
    }

    if (fillcol != 80) {
        ok = 0;
        LOG_ERRORF("[FAIL] Default fill column should be 80, got %d", fillcol);
    } else {
        LOG_INFO("[SUCCESS] Fill column set to default 80");
    }

    if (!(curbp->b_mode & MDWRAP)) {
        ok = 0;
        LOG_ERROR("[FAIL] MDWRAP should be enabled");
    } else {
        LOG_INFO("[SUCCESS] MDWRAP enabled correctly");
    }

    // Test 2: Disable writing mode
    result = writing_mode_disable(false, 0);

    if (result != true) {
        ok = 0;
        LOG_ERROR("[FAIL] writing_mode_disable should return true");
    }

    if (fillcol != initial_fillcol) {
        ok = 0;
        LOG_ERRORF("[FAIL] Fill column not restored (expected %d, got %d)", initial_fillcol, fillcol);
    } else {
        LOG_INFO("[SUCCESS] Fill column restored to initial value");
    }

    int final_wrap = (curbp->b_mode & MDWRAP) ? 1 : 0;
    if (final_wrap != initial_wrap) {
        ok = 0;
        LOG_ERRORF("[FAIL] MDWRAP not restored (expected %d, got %d)", initial_wrap, final_wrap);
    } else {
        LOG_INFO("[SUCCESS] MDWRAP restored to initial state");
    }

    PHASE_END("WRITING: TOGGLE", ok);
    return ok;
}

// Test custom column width
int test_writing_mode_custom_column() {
    int ok = 1;
    PHASE_START("WRITING: CUSTOM-COLUMN", "Testing writing mode with custom column width");

    init_editor_minimal("writing-custom");
    (void)bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Save original fillcol to restore at end
    int orig_fillcol = fillcol;

    // Test 1: Set custom column width (72)
    int result = writing_mode_enable(true, 72);

    if (result != true) {
        ok = 0;
        LOG_ERROR("[FAIL] writing_mode_enable with argument should return true");
    }

    if (fillcol != 72) {
        ok = 0;
        LOG_ERRORF("[FAIL] Custom fill column should be 72, got %d", fillcol);
    } else {
        LOG_INFO("[SUCCESS] Fill column set to custom value 72");
    }

    // Test 2: Set another custom width (100)
    result = writing_mode_enable(true, 100);

    if (fillcol != 100) {
        ok = 0;
        LOG_ERRORF("[FAIL] Custom fill column should be 100, got %d", fillcol);
    } else {
        LOG_INFO("[SUCCESS] Fill column updated to 100");
    }

    // Disable writing mode and restore original fillcol
    writing_mode_disable(false, 0);
    fillcol = orig_fillcol;

    PHASE_END("WRITING: CUSTOM-COLUMN", ok);
    return ok;
}

// Test column width bounds checking
int test_writing_mode_bounds() {
    int ok = 1;
    PHASE_START("WRITING: BOUNDS", "Testing writing mode column width bounds");

    init_editor_minimal("writing-bounds");
    (void)bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Save original fillcol to restore at end
    int orig_fillcol = fillcol;

    // Test 1: Very small column width (should clamp to 20)
    int result = writing_mode_enable(true, 10);

    if (result != true) {
        ok = 0;
        LOG_ERROR("[FAIL] writing_mode_enable should return true");
    }

    if (fillcol < 20) {
        ok = 0;
        LOG_ERRORF("[FAIL] Fill column should be clamped to minimum 20, got %d", fillcol);
    } else {
        LOG_INFOF("[SUCCESS] Fill column clamped to minimum (got %d)", fillcol);
    }

    // Test 2: Very large column width (should clamp to 200)
    result = writing_mode_enable(true, 500);

    if (fillcol > 200) {
        ok = 0;
        LOG_ERRORF("[FAIL] Fill column should be clamped to maximum 200, got %d", fillcol);
    } else {
        LOG_INFOF("[SUCCESS] Fill column clamped to maximum (got %d)", fillcol);
    }

    // Test 3: Negative column width (should clamp to 20)
    result = writing_mode_enable(true, -50);

    if (fillcol < 20) {
        ok = 0;
        LOG_ERRORF("[FAIL] Negative fill column should be clamped to 20, got %d", fillcol);
    } else {
        LOG_INFOF("[SUCCESS] Negative value clamped to minimum (got %d)", fillcol);
    }

    // Disable writing mode and restore original fillcol
    writing_mode_disable(false, 0);
    fillcol = orig_fillcol;

    PHASE_END("WRITING: BOUNDS", ok);
    return ok;
}

// Test state preservation across multiple toggles
int test_writing_mode_state_preservation() {
    int ok = 1;
    PHASE_START("WRITING: STATE-PRESERVATION", "Testing state preservation across toggles");

    init_editor_minimal("writing-state");
    (void)bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Save original fillcol to restore at end
    int orig_fillcol = fillcol;

    // Set initial state
    fillcol = 60;
    curbp->b_mode &= ~MDWRAP;  // Disable wrap

    // Test 1: Enable writing mode (should save current state)
    writing_mode_enable(true, 90);

    if (fillcol != 90) {
        ok = 0;
        LOG_ERRORF("[FAIL] Fill column should be 90, got %d", fillcol);
    }

    if (!(curbp->b_mode & MDWRAP)) {
        ok = 0;
        LOG_ERROR("[FAIL] MDWRAP should be enabled");
    }

    // Test 2: Disable writing mode (should restore to 60, no wrap)
    writing_mode_disable(false, 0);

    if (fillcol != 60) {
        ok = 0;
        LOG_ERRORF("[FAIL] Fill column should be restored to 60, got %d", fillcol);
    } else {
        LOG_INFO("[SUCCESS] Fill column restored to 60");
    }

    if (curbp->b_mode & MDWRAP) {
        ok = 0;
        LOG_ERROR("[FAIL] MDWRAP should be disabled");
    } else {
        LOG_INFO("[SUCCESS] MDWRAP correctly disabled");
    }

    // Restore original fillcol to avoid polluting other tests
    fillcol = orig_fillcol;

    PHASE_END("WRITING: STATE-PRESERVATION", ok);
    return ok;
}

// Test multiple enable/disable cycles
int test_writing_mode_cycles() {
    int ok = 1;
    PHASE_START("WRITING: CYCLES", "Testing multiple enable/disable cycles");

    init_editor_minimal("writing-cycles");
    (void)bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    int initial_fillcol = fillcol;
    int initial_wrap = (curbp->b_mode & MDWRAP) ? 1 : 0;

    // Cycle 1
    writing_mode_enable(true, 70);
    writing_mode_disable(false, 0);

    // Cycle 2
    writing_mode_enable(true, 85);
    writing_mode_disable(false, 0);

    // Cycle 3
    writing_mode_enable(true, 95);
    writing_mode_disable(false, 0);

    // Verify state is correctly restored after all cycles
    if (fillcol != initial_fillcol) {
        ok = 0;
        LOG_ERRORF("[FAIL] Fill column not restored after cycles (expected %d, got %d)", initial_fillcol, fillcol);
    } else {
        LOG_INFO("[SUCCESS] Fill column correctly restored after multiple cycles");
    }

    int final_wrap = (curbp->b_mode & MDWRAP) ? 1 : 0;
    if (final_wrap != initial_wrap) {
        ok = 0;
        LOG_ERROR("[FAIL] MDWRAP not restored after cycles");
    } else {
        LOG_INFO("[SUCCESS] MDWRAP correctly restored after multiple cycles");
    }

    PHASE_END("WRITING: CYCLES", ok);
    return ok;
}

// Test wrap mode interaction
int test_writing_mode_wrap_interaction() {
    int ok = 1;
    PHASE_START("WRITING: WRAP-INTERACTION", "Testing wrap mode interaction");

    init_editor_minimal("writing-wrap");
    (void)bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Save original fillcol to restore at end
    int orig_fillcol = fillcol;

    // Test 1: Start with wrap enabled
    curbp->b_mode |= MDWRAP;
    fillcol = 50;

    writing_mode_enable(true, 75);

    if (!(curbp->b_mode & MDWRAP)) {
        ok = 0;
        LOG_ERROR("[FAIL] MDWRAP should remain enabled");
    } else {
        LOG_INFO("[SUCCESS] MDWRAP maintained while enabled");
    }

    writing_mode_disable(false, 0);

    if (!(curbp->b_mode & MDWRAP)) {
        ok = 0;
        LOG_ERROR("[FAIL] MDWRAP should be restored to enabled");
    } else {
        LOG_INFO("[SUCCESS] MDWRAP correctly restored to enabled state");
    }

    // Test 2: Start with wrap disabled
    curbp->b_mode &= ~MDWRAP;
    fillcol = 50;

    writing_mode_enable(true, 75);

    if (!(curbp->b_mode & MDWRAP)) {
        ok = 0;
        LOG_ERROR("[FAIL] MDWRAP should be enabled by writing mode");
    }

    writing_mode_disable(false, 0);

    if (curbp->b_mode & MDWRAP) {
        ok = 0;
        LOG_ERROR("[FAIL] MDWRAP should be restored to disabled");
    } else {
        LOG_INFO("[SUCCESS] MDWRAP correctly restored to disabled state");
    }

    // Restore original fillcol to avoid polluting other tests
    fillcol = orig_fillcol;

    PHASE_END("WRITING: WRAP-INTERACTION", ok);
    return ok;
}

// Entry point for writing mode tests
int test_text_writing(void) {
    int all_passed = 1;

    all_passed &= test_writing_mode_toggle();
    all_passed &= test_writing_mode_custom_column();
    all_passed &= test_writing_mode_bounds();
    all_passed &= test_writing_mode_state_preservation();
    all_passed &= test_writing_mode_cycles();
    all_passed &= test_writing_mode_wrap_interaction();

    return all_passed;
}
