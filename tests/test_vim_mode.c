/*
 * test_vim_mode.c - Tests for Vim Mode Implementation
 *
 * Tests operators (d/c/y), motions (h/j/k/l/w/b/e/0/$), visual mode (v/V),
 * and mode switching.
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

/* Test vim mode toggle */
static int test_vim_mode_toggle(void) {
    int result = 1;

    LOG_INFO("  Testing evil-mode toggle...");

    /* Ensure vim mode starts disabled */
    vim_mode_active = 0;
    atomic_store(&g_vim_state.current_mode, MODE_INSERT);

    /* Toggle on */
    evil_mode(0, 1);
    if (!vim_mode_active) {
        LOG_ERROR("[FAIL] evil-mode did not enable vim_mode_active");
        result = 0;
    }
    if (atomic_load(&g_vim_state.current_mode) != MODE_NORMAL) {
        LOG_ERROR("[FAIL] evil-mode did not set MODE_NORMAL");
        result = 0;
    }

    /* Toggle off */
    evil_mode(0, 1);
    if (vim_mode_active) {
        LOG_ERROR("[FAIL] evil-mode did not disable vim_mode_active");
        result = 0;
    }
    if (atomic_load(&g_vim_state.current_mode) != MODE_INSERT) {
        LOG_ERROR("[FAIL] evil-mode did not reset to MODE_INSERT");
        result = 0;
    }

    if (result) {
        LOG_INFO("[SUCCESS] evil-mode toggle works correctly");
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
        LOG_ERROR("[FAIL] vim_normal_keymap is NULL after init");
        result = 0;
    }

    if (!vkm) {
        LOG_ERROR("[FAIL] vim_visual_keymap is NULL after init");
        result = 0;
    }

    if (result) {
        LOG_INFO("[SUCCESS] vim keymaps initialized");
    }

    return result;
}

/* Test navigation bindings exist in normal mode keymap */
static int test_vim_navigation_bindings(void) {
    int result = 1;

    LOG_INFO("  Testing vim navigation bindings...");

    struct keymap *nkm = atomic_load(&vim_normal_keymap);
    if (!nkm) {
        LOG_ERROR("[FAIL] vim_normal_keymap is NULL");
        return 0;
    }

    /* Check h/j/k/l bindings */
    struct keymap_entry *h = km_lookup_char(nkm, 'h');
    struct keymap_entry *j = km_lookup_char(nkm, 'j');
    struct keymap_entry *k = km_lookup_char(nkm, 'k');
    struct keymap_entry *l = km_lookup_char(nkm, 'l');

    if (!h || h->is_prefix || !KM_GET_CMD(h)) {
        LOG_ERROR("[FAIL] 'h' not bound in normal mode");
        result = 0;
    }
    if (!j || j->is_prefix || !KM_GET_CMD(j)) {
        LOG_ERROR("[FAIL] 'j' not bound in normal mode");
        result = 0;
    }
    if (!k || k->is_prefix || !KM_GET_CMD(k)) {
        LOG_ERROR("[FAIL] 'k' not bound in normal mode");
        result = 0;
    }
    if (!l || l->is_prefix || !KM_GET_CMD(l)) {
        LOG_ERROR("[FAIL] 'l' not bound in normal mode");
        result = 0;
    }

    /* Check word motion bindings */
    struct keymap_entry *b = km_lookup_char(nkm, 'b');
    struct keymap_entry *e = km_lookup_char(nkm, 'e');

    if (!b || b->is_prefix || !KM_GET_CMD(b)) {
        LOG_ERROR("[FAIL] 'b' (backward word) not bound");
        result = 0;
    }
    if (!e || e->is_prefix || !KM_GET_CMD(e)) {
        LOG_ERROR("[FAIL] 'e' (end of word) not bound");
        result = 0;
    }

    /* Check line boundary bindings */
    struct keymap_entry *zero = km_lookup_char(nkm, '0');
    struct keymap_entry *dollar = km_lookup_char(nkm, '$');
    struct keymap_entry *caret = km_lookup_char(nkm, '^');

    if (!zero || zero->is_prefix || !KM_GET_CMD(zero)) {
        LOG_ERROR("[FAIL] '0' (beginning of line) not bound");
        result = 0;
    }
    if (!dollar || dollar->is_prefix || !KM_GET_CMD(dollar)) {
        LOG_ERROR("[FAIL] '$' (end of line) not bound");
        result = 0;
    }
    if (!caret || caret->is_prefix || !KM_GET_CMD(caret)) {
        LOG_ERROR("[FAIL] '^' (first non-blank) not bound");
        result = 0;
    }

    if (result) {
        LOG_INFO("[SUCCESS] All navigation bindings present");
    }

    return result;
}

/* Test operator bindings (d/c/y) */
static int test_vim_operator_bindings(void) {
    int result = 1;

    LOG_INFO("  Testing vim operator bindings...");

    struct keymap *nkm = atomic_load(&vim_normal_keymap);
    if (!nkm) {
        LOG_ERROR("[FAIL] vim_normal_keymap is NULL");
        return 0;
    }

    /* Check d/c/y operator bindings */
    struct keymap_entry *d = km_lookup_char(nkm, 'd');
    struct keymap_entry *c = km_lookup_char(nkm, 'c');
    struct keymap_entry *y = km_lookup_char(nkm, 'y');

    if (!d || d->is_prefix || !KM_GET_CMD(d)) {
        LOG_ERROR("[FAIL] 'd' (delete operator) not bound");
        result = 0;
    }
    if (!c || c->is_prefix || !KM_GET_CMD(c)) {
        LOG_ERROR("[FAIL] 'c' (change operator) not bound");
        result = 0;
    }
    if (!y || y->is_prefix || !KM_GET_CMD(y)) {
        LOG_ERROR("[FAIL] 'y' (yank operator) not bound");
        result = 0;
    }

    if (result) {
        LOG_INFO("[SUCCESS] All operator bindings present");
    }

    return result;
}

/* Test visual mode bindings */
static int test_vim_visual_bindings(void) {
    int result = 1;

    LOG_INFO("  Testing vim visual mode bindings...");

    struct keymap *nkm = atomic_load(&vim_normal_keymap);
    struct keymap *vkm = atomic_load(&vim_visual_keymap);

    if (!nkm || !vkm) {
        LOG_ERROR("[FAIL] Keymaps are NULL");
        return 0;
    }

    /* Check v/V bindings in normal mode */
    struct keymap_entry *v = km_lookup_char(nkm, 'v');
    struct keymap_entry *V = km_lookup_char(nkm, 'V');

    if (!v || v->is_prefix || !KM_GET_CMD(v)) {
        LOG_ERROR("[FAIL] 'v' (visual mode) not bound in normal mode");
        result = 0;
    }
    if (!V || V->is_prefix || !KM_GET_CMD(V)) {
        LOG_ERROR("[FAIL] 'V' (visual line mode) not bound in normal mode");
        result = 0;
    }

    /* Check operations in visual mode keymap */
    struct keymap_entry *vd = km_lookup_char(vkm, 'd');
    struct keymap_entry *vc = km_lookup_char(vkm, 'c');
    struct keymap_entry *vy = km_lookup_char(vkm, 'y');

    if (!vd || vd->is_prefix || !KM_GET_CMD(vd)) {
        LOG_ERROR("[FAIL] 'd' not bound in visual mode");
        result = 0;
    }
    if (!vc || vc->is_prefix || !KM_GET_CMD(vc)) {
        LOG_ERROR("[FAIL] 'c' not bound in visual mode");
        result = 0;
    }
    if (!vy || vy->is_prefix || !KM_GET_CMD(vy)) {
        LOG_ERROR("[FAIL] 'y' not bound in visual mode");
        result = 0;
    }

    if (result) {
        LOG_INFO("[SUCCESS] All visual mode bindings present");
    }

    return result;
}

/* Test user preference bindings from .vimrc */
static int test_vim_user_bindings(void) {
    int result = 1;

    LOG_INFO("  Testing user preference bindings...");

    struct keymap *nkm = atomic_load(&vim_normal_keymap);
    if (!nkm) {
        LOG_ERROR("[FAIL] vim_normal_keymap is NULL");
        return 0;
    }

    /* Check user bindings: H/L/K/J/s/S/q/w/r/u/o/O/p/x */
    const char *user_keys = "HLKJsSquropPx";
    for (int i = 0; user_keys[i]; i++) {
        struct keymap_entry *e = km_lookup_char(nkm, user_keys[i]);
        if (!e || e->is_prefix || !KM_GET_CMD(e)) {
            LOG_ERRORF("[FAIL] '%c' user binding not present", user_keys[i]);
            result = 0;
        }
    }

    if (result) {
        LOG_INFO("[SUCCESS] All user preference bindings present");
    }

    return result;
}

/* Test mode switching commands */
static int test_vim_mode_switching(void) {
    int result = 1;

    LOG_INFO("  Testing mode switching bindings...");

    struct keymap *nkm = atomic_load(&vim_normal_keymap);
    if (!nkm) {
        LOG_ERROR("[FAIL] vim_normal_keymap is NULL");
        return 0;
    }

    /* Check 'i' for insert mode */
    struct keymap_entry *i = km_lookup_char(nkm, 'i');
    if (!i || i->is_prefix || !KM_GET_CMD(i)) {
        LOG_ERROR("[FAIL] 'i' (insert mode) not bound");
        result = 0;
    }

    /* Check ESC for normal mode (TOML stores ^[ as raw byte 0x1B, not '[' + MOD_CTRL) */
    struct keymap_entry *esc = keymap_lookup(nkm, keymap_key_make(0x1B, 0));
    if (!esc || esc->is_prefix || !KM_GET_CMD(esc)) {
        LOG_ERROR("[FAIL] ESC (0x1B) not bound");
        result = 0;
    }

    if (result) {
        LOG_INFO("[SUCCESS] Mode switching bindings present");
    }

    return result;
}

/* Main test function */
int test_vim_mode(void) {
    int result = 1;
    int sub_result;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("vim-mode");

    // Load settings from TOML - this is where vim keybindings come from
    extern int settings_load(int f, int n);
    settings_load(false, 0);

    PHASE_START("Vim Mode Tests", "Testing vim mode implementation");

    /* Test 1: Vim state structure */
    sub_result = test_vim_state_init();
    if (!sub_result) result = 0;

    /* Test 2: Evil mode toggle */
    sub_result = test_vim_mode_toggle();
    if (!sub_result) result = 0;

    /* Test 3: Keymap initialization */
    sub_result = test_vim_keymap_init();
    if (!sub_result) result = 0;

    /* Test 4: Navigation bindings */
    sub_result = test_vim_navigation_bindings();
    if (!sub_result) result = 0;

    /* Test 5: Operator bindings */
    sub_result = test_vim_operator_bindings();
    if (!sub_result) result = 0;

    /* Test 6: Visual mode bindings */
    sub_result = test_vim_visual_bindings();
    if (!sub_result) result = 0;

    /* Test 7: User preference bindings */
    sub_result = test_vim_user_bindings();
    if (!sub_result) result = 0;

    /* Test 8: Mode switching */
    sub_result = test_vim_mode_switching();
    if (!sub_result) result = 0;

    PHASE_END("Vim Mode Tests", result);

    return result;
}
