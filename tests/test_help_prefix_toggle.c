#include "test_utils.h"
#include "μemacs/keymap.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

int test_help_prefix_toggle() {
    int result = 1;
    PHASE_START("Help Prefix Toggle", "Enable/disable C-h help prefix without breaking defaults");

    // Ensure legacy defaults loaded
    keymap_init_from_legacy();

    struct keymap *gkm = atomic_load(&global_keymap);
    struct keymap *hkm = atomic_load(&help_keymap);

    // 1) By default, C-h must be backdel (not a prefix)
    struct keymap_entry *e = keymap_lookup(gkm, CONTROL | 'H');
    if (!e || e->is_prefix || e->binding.cmd != backdel) {
        printf("[%sFAIL%s] Default C-h is not backdel\n", RED, RESET);
        result = 0;
    }

    // 2) Enable help prefix and verify C-h becomes a prefix
    if (!help_prefix_enable(0, 0)) {
        printf("[%sFAIL%s] Failed to enable help prefix\n", RED, RESET);
        result = 0;
    } else {
        e = keymap_lookup(gkm, CONTROL | 'H');
        if (!e || !e->is_prefix || e->binding.map != hkm) {
            printf("[%sFAIL%s] C-h did not become help prefix after enabling\n", RED, RESET);
            result = 0;
        }
        // Ensure help map has useful entries
        struct keymap_entry *hk = keymap_lookup(hkm, 'k');
        struct keymap_entry *hb = keymap_lookup(hkm, 'b');
        if (!hk || hk->is_prefix || hk->binding.cmd != deskey) {
            printf("[%sFAIL%s] Help map missing 'k' -> deskey\n", RED, RESET);
            result = 0;
        }
        if (!hb || hb->is_prefix || hb->binding.cmd != desbind) {
            printf("[%sFAIL%s] Help map missing 'b' -> desbind\n", RED, RESET);
            result = 0;
        }
    }

    // 3) Disable help prefix and verify Backspace restored
    if (!help_prefix_disable(0, 0)) {
        printf("[%sFAIL%s] Failed to disable help prefix\n", RED, RESET);
        result = 0;
    } else {
        e = keymap_lookup(gkm, CONTROL | 'H');
        if (!e || e->is_prefix || e->binding.cmd != backdel) {
            printf("[%sFAIL%s] C-h backdel not restored after disabling\n", RED, RESET);
            result = 0;
        }
    }

    PHASE_END("Help Prefix Toggle", result);
    return result;
}
