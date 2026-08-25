/*
 * test_vim_mode.c - Tests for Vim Mode Infrastructure
 *
 * Tests vim state machine, mode toggling, and keymap initialization.
 * Vim command implementations are in the c_evil extension (tested via TUI).
 */

#include "test_utils.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "editor_mode.h"
#include "μemacs/keymap.h"

/* Atomic accessor for binding.cmd (now _Atomic) */
#define KM_GET_CMD(entry) atomic_load_explicit(&(entry)->binding.cmd, memory_order_relaxed)

/* Helper to lookup a simple character key in a keymap */
static inline struct keymap_entry *km_lookup_char(struct keymap *km, int ch) {
    return keymap_lookup(km, keymap_key_make((uint32_t)ch, 0));
}

/* Test the core ESC fallback (the toggle itself is owned by the c_evil
 * extension and covered by its TUI suite) */
static int test_vim_normal_mode_fallback(void) {
    int result = 1;

    LOG_INFO("  Testing vim_enter_normal_mode_external fallback...");

    atomic_store(&g_vim_state.current_mode, MODE_INSERT);
    vim_enter_normal_mode_external(0, 1);
    if (atomic_load(&g_vim_state.current_mode) != MODE_NORMAL) {
        LOG_ERROR("[FAIL] ESC fallback did not set MODE_NORMAL");
        result = 0;
    }

    /* Reset */
    atomic_store(&g_vim_state.current_mode, MODE_INSERT);

    if (result) {
        LOG_INFO("[SUCCESS] ESC fallback works correctly");
    }

    return result;
}

/* Test vim state structure initialization */
static int test_vim_state_init(void) {
    int result = 1;

    LOG_INFO("  Testing vim state structure...");

    /* Check initial state */
    enum editor_mode mode = atomic_load(&g_vim_state.current_mode);
    if (mode != MODE_INSERT && mode != MODE_NORMAL) {
        LOG_ERRORF("[FAIL] g_vim_state.current_mode has invalid initial value: %d", mode);
        result = 0;
    }

    /* Test mode transitions */
    atomic_store(&g_vim_state.current_mode, MODE_NORMAL);
    if (atomic_load(&g_vim_state.current_mode) != MODE_NORMAL) {
        LOG_ERROR("[FAIL] Failed to set MODE_NORMAL");
        result = 0;
    }

    atomic_store(&g_vim_state.current_mode, MODE_VISUAL);
    if (atomic_load(&g_vim_state.current_mode) != MODE_VISUAL) {
        LOG_ERROR("[FAIL] Failed to set MODE_VISUAL");
        result = 0;
    }

    atomic_store(&g_vim_state.current_mode, MODE_VISUAL_LINE);
    if (atomic_load(&g_vim_state.current_mode) != MODE_VISUAL_LINE) {
        LOG_ERROR("[FAIL] Failed to set MODE_VISUAL_LINE");
        result = 0;
    }

    /* Reset to INSERT */
    atomic_store(&g_vim_state.current_mode, MODE_INSERT);

    if (result) {
        LOG_INFO("[SUCCESS] vim state structure works correctly");
    }

    return result;
}

/* Test vim keymap initialization */
static int test_vim_keymap_init(void) {
    int result = 1;

    LOG_INFO("  Testing vim keymap initialization...");

    /* Keymaps are initialized by test_init_editor() */
    struct keymap *nkm = atomic_load(&vim_normal_keymap);
    struct keymap *vkm = atomic_load(&vim_visual_keymap);

    if (!nkm) {
        LOG_ERROR("[FAIL] vim_normal_keymap is nullptr after init");
        result = 0;
    }

    if (!vkm) {
        LOG_ERROR("[FAIL] vim_visual_keymap is nullptr after init");
        result = 0;
    }

    if (result) {
        LOG_INFO("[SUCCESS] vim keymaps initialized");
    }

    return result;
}

/* Main test function */
int test_vim_mode(void) {
    int result = 1;
    int sub_result;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("vim-mode");

    // Load settings (Linus baseline - no vim keybindings in shipped config)
    extern int settings_load(int f, int n);
    settings_load(false, 0);

    PHASE_START("Vim Mode Tests", "Testing vim mode infrastructure");

    /* Test 1: Vim state structure */
    sub_result = test_vim_state_init();
    if (!sub_result) result = 0;

    /* Test 2: Core ESC fallback */
    sub_result = test_vim_normal_mode_fallback();
    if (!sub_result) result = 0;

    /* Test 3: Keymap initialization */
    sub_result = test_vim_keymap_init();
    if (!sub_result) result = 0;

    /* Vim command bindings tested via c_evil extension TUI tests */

    PHASE_END("Vim Mode Tests", result);

    return result;
}
