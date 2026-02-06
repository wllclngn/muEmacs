// test_config_vim.c - Unit tests for vim mode
// Tests src/config/vim_bindings.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "editor_mode.h"
#include "line.h"

// Test vim mode toggle
static int test_vim_toggle(void) {
    int ok = 1;
    PHASE_START("VIM: TOGGLE", "Vim mode toggle");

    int orig_vim_active = vim_mode_active;
    enum editor_mode orig_mode = atomic_load(&g_vim_state.current_mode);

    // Enable vim mode
    vim_mode_active = 1;
    atomic_store(&g_vim_state.current_mode, MODE_NORMAL);

    if (!vim_mode_active) {
        LOG_ERROR("[FAIL] vim mode not enabled");
        ok = 0;
    }

    if (atomic_load(&g_vim_state.current_mode) != MODE_NORMAL) {
        LOG_ERROR("[FAIL] mode not set to NORMAL");
        ok = 0;
    }

    // Disable vim mode
    vim_mode_active = 0;
    atomic_store(&g_vim_state.current_mode, MODE_INSERT);

    if (vim_mode_active) {
        LOG_ERROR("[FAIL] vim mode not disabled");
        ok = 0;
    }

    // Restore
    vim_mode_active = orig_vim_active;
    atomic_store(&g_vim_state.current_mode, orig_mode);

    PHASE_END("VIM: TOGGLE", ok);
    return ok;
}

// Test editor mode values
static int test_vim_modes(void) {
    int ok = 1;
    PHASE_START("VIM: MODES", "Editor mode values");

    enum editor_mode orig_mode = atomic_load(&g_vim_state.current_mode);

    // Test normal mode
    atomic_store(&g_vim_state.current_mode, MODE_NORMAL);
    if (atomic_load(&g_vim_state.current_mode) != MODE_NORMAL) {
        LOG_ERROR("[FAIL] normal mode not set");
        ok = 0;
    }

    // Test insert mode
    atomic_store(&g_vim_state.current_mode, MODE_INSERT);
    if (atomic_load(&g_vim_state.current_mode) != MODE_INSERT) {
        LOG_ERROR("[FAIL] insert mode not set");
        ok = 0;
    }

    // Test visual mode
    atomic_store(&g_vim_state.current_mode, MODE_VISUAL);
    if (atomic_load(&g_vim_state.current_mode) != MODE_VISUAL) {
        LOG_ERROR("[FAIL] visual mode not set");
        ok = 0;
    }

    // Test visual line mode
    atomic_store(&g_vim_state.current_mode, MODE_VISUAL_LINE);
    if (atomic_load(&g_vim_state.current_mode) != MODE_VISUAL_LINE) {
        LOG_ERROR("[FAIL] visual line mode not set");
        ok = 0;
    }

    // Test replace mode
    atomic_store(&g_vim_state.current_mode, MODE_REPLACE);
    if (atomic_load(&g_vim_state.current_mode) != MODE_REPLACE) {
        LOG_ERROR("[FAIL] replace mode not set");
        ok = 0;
    }

    // Restore
    atomic_store(&g_vim_state.current_mode, orig_mode);

    PHASE_END("VIM: MODES", ok);
    return ok;
}

// Test vim mode with buffer
static int test_vim_buffer(void) {
    int ok = 1;
    PHASE_START("VIM: BUFFER", "Vim mode with buffer");

    int orig_vim = vim_mode_active;
    enum editor_mode orig_mode = atomic_load(&g_vim_state.current_mode);

    // Create test buffer
    struct buffer *bp = bfind("test-vim", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("VIM: BUFFER", ok);
        return ok;
    }

    struct buffer *oldbp = curbp;
    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Enable vim mode
    vim_mode_active = 1;
    atomic_store(&g_vim_state.current_mode, MODE_NORMAL);

    // Add some text
    linstr("Test text for vim");

    // Verify text added
    if (llength(curwp->w_dotp) == 0) {
        LOG_ERROR("[FAIL] text not inserted");
        ok = 0;
    }

    // Restore
    vim_mode_active = orig_vim;
    atomic_store(&g_vim_state.current_mode, orig_mode);
    curbp = oldbp;
    curwp->w_bufp = oldbp;
    bp->b_flag &= ~BFCHG;
    (void)zotbuf(bp);

    PHASE_END("VIM: BUFFER", ok);
    return ok;
}

// Entry point
int test_config_vim(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("vim");

    PHASE_START("VIM", "Vim mode tests");

    ok &= test_vim_toggle();
    ok &= test_vim_modes();
    ok &= test_vim_buffer();

    if (ok) {
        LOG_INFO("[SUCCESS] All vim mode tests passed.");
    }

    PHASE_END("VIM", ok);
    return ok;
}
