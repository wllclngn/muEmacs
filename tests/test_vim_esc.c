#include <stdio.h>
#include <stdint.h>
#include <stdatomic.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "μemacs/keymap.h"
#include "editor_mode.h"

#define KM_GET_CMD(entry) atomic_load_explicit(&(entry)->binding.cmd, memory_order_relaxed)

static inline struct keymap_entry *km_lookup_char(struct keymap *km, int ch) {
    return keymap_lookup(km, keymap_key_make((uint32_t)ch, 0));
}

int main() {
    keymap_init_defaults();
    extern void vim_init_keymaps(void);
    vim_init_keymaps();
    extern int settings_load(int f, int n);
    settings_load(0, 0);

    struct keymap *nkm = atomic_load(&vim_normal_keymap);
    struct keymap *vkm = atomic_load(&vim_visual_keymap);

    printf("vim_normal_keymap: %p\n", (void*)nkm);
    printf("vim_visual_keymap: %p\n", (void*)vkm);

    if (!nkm) { printf("[FAIL] vim_normal_keymap is NULL\n"); return 1; }
    if (!vkm) { printf("[FAIL] vim_visual_keymap is NULL\n"); return 1; }

    // Check ESC (Ctrl+[) binding - this is what test_vim_mode_switching checks
    printf("\n--- Mode Switching ---\n");
    struct keymap_entry *i_entry = km_lookup_char(nkm, 'i');
    if (!i_entry || i_entry->is_prefix || !KM_GET_CMD(i_entry)) {
        printf("[FAIL] 'i' (insert mode) not bound\n");
    } else {
        printf("[OK] 'i' bound\n");
    }

    struct keymap_entry *esc = keymap_lookup(nkm, keymap_key_make('[', MOD_CTRL));
    printf("ESC (Ctrl+[) lookup: entry=%p\n", (void*)esc);
    if (esc) {
        printf("  is_prefix=%d\n", esc->is_prefix);
        if (!esc->is_prefix) {
            fn_t f = KM_GET_CMD(esc);
            printf("  cmd=%p\n", (void*)(uintptr_t)f);
        }
    }
    if (!esc || esc->is_prefix || !KM_GET_CMD(esc)) {
        printf("[FAIL] ESC (Ctrl+[) not bound in normal mode\n");
    } else {
        printf("[OK] ESC (Ctrl+[) bound\n");
    }

    // Also check visual mode operations
    printf("\n--- Visual Mode Operations ---\n");
    struct keymap_entry *vd = km_lookup_char(vkm, 'd');
    struct keymap_entry *vc = km_lookup_char(vkm, 'c');
    struct keymap_entry *vy = km_lookup_char(vkm, 'y');

    if (!vd || vd->is_prefix || !KM_GET_CMD(vd)) {
        printf("[FAIL] 'd' not bound in visual mode\n");
    } else {
        printf("[OK] 'd' bound in visual mode\n");
    }
    if (!vc || vc->is_prefix || !KM_GET_CMD(vc)) {
        printf("[FAIL] 'c' not bound in visual mode\n");
    } else {
        printf("[OK] 'c' bound in visual mode\n");
    }
    if (!vy || vy->is_prefix || !KM_GET_CMD(vy)) {
        printf("[FAIL] 'y' not bound in visual mode\n");
    } else {
        printf("[OK] 'y' bound in visual mode\n");
    }

    return 0;
}
