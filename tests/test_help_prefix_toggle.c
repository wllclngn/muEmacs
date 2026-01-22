#include "test_utils.h"
#include "μemacs/keymap.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

// Atomic accessors for binding union (binding.cmd and binding.map are now _Atomic)
#define KM_GET_CMD(entry) atomic_load_explicit(&(entry)->binding.cmd, memory_order_relaxed)
#define KM_GET_MAP(entry) atomic_load_explicit(&(entry)->binding.map, memory_order_relaxed)

int test_help_prefix_toggle() {
    int result = 1;
    PHASE_START("Help Prefix Toggle", "Enable/disable C-h help prefix without breaking defaults");

    // Initialize keymaps and load settings
    keymap_init_defaults();
    extern void vim_init_keymaps(void);
    vim_init_keymaps();
    extern int settings_load(int f, int n);
    settings_load(false, 0);

    struct keymap *gkm = atomic_load(&global_keymap);
    struct keymap *hkm = atomic_load(&help_keymap);

    // Modern key for C-h
    keymap_key_t ctrl_h = keymap_key_make('H', MOD_CTRL);
    keymap_key_t key_k = keymap_key_make('k', 0);
    keymap_key_t key_b = keymap_key_make('b', 0);

    // 1) By default, C-h must be delete_char_backward (not a prefix)
    struct keymap_entry *e = keymap_lookup(gkm, ctrl_h);
    if (!e || e->is_prefix || KM_GET_CMD(e) != delete_char_backward) {
        LOG_ERROR("[FAIL] Default C-h is not delete_char_backward");
        result = 0;
    }

    // 2) Enable help prefix and verify C-h becomes a prefix
    if (!help_prefix_enable(0, 0)) {
        LOG_ERROR("[FAIL] Failed to enable help prefix");
        result = 0;
    } else {
        e = keymap_lookup(gkm, ctrl_h);
        if (!e || !e->is_prefix || KM_GET_MAP(e) != hkm) {
            LOG_ERROR("[FAIL] C-h did not become help prefix after enabling");
            result = 0;
        }
        // Ensure help map has useful entries
        struct keymap_entry *hk = keymap_lookup(hkm, key_k);
        struct keymap_entry *hb = keymap_lookup(hkm, key_b);
        if (!hk || hk->is_prefix || KM_GET_CMD(hk) != describe_key_binding) {
            LOG_ERROR("[FAIL] Help map missing 'k' -> describe_key_binding");
            result = 0;
        }
        if (!hb || hb->is_prefix || KM_GET_CMD(hb) != describe_all_bindings) {
            LOG_ERROR("[FAIL] Help map missing 'b' -> describe_all_bindings");
            result = 0;
        }
    }

    // 3) Disable help prefix and verify Backspace restored
    if (!help_prefix_disable(0, 0)) {
        LOG_ERROR("[FAIL] Failed to disable help prefix");
        result = 0;
    } else {
        e = keymap_lookup(gkm, ctrl_h);
        if (!e || e->is_prefix || KM_GET_CMD(e) != delete_char_backward) {
            LOG_ERROR("[FAIL] C-h delete_char_backward not restored after disabling");
            result = 0;
        }
    }

    PHASE_END("Help Prefix Toggle", result);
    return result;
}
