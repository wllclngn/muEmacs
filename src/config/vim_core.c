/*
 * vim_core.c - Vim mode infrastructure for μEmacs
 *
 * Provides the core symbols needed by bind.c, main.c, and names.c
 * for vim/evil mode support. The actual vim commands (motions, operators,
 * text objects) are implemented in the c_evil extension, loaded via UEP.
 *
 * C23 compliant
 */

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "μemacs/keymap.h"

/*
 * vim_init_keymaps - called from main.c at startup.
 * Creates empty keymaps that the c_evil extension will populate.
 * The keymaps must exist for the dispatch code in bind.c to work.
 */
void vim_init_keymaps(void) {
    struct keymap *nkm = keymap_create("vim_normal");
    if (nkm) atomic_store(&vim_normal_keymap, nkm);

    struct keymap *vkm = keymap_create("vim_visual");
    if (vkm) atomic_store(&vim_visual_keymap, vkm);
}

/*
 * evil_mode - toggle vim mode on/off.
 * When vim is an extension, the extension overrides this via register_command().
 * This stub provides a minimal toggle for the names[] table entry.
 */
int evil_mode(int f, int n) {
    (void)f; (void)n;
    vim_mode_active = !vim_mode_active;
    if (vim_mode_active) {
        atomic_store(&g_vim_state.current_mode, MODE_NORMAL);
        mlwrite("[EVIL: Enabled - load c_evil extension for full vim mode]");
    } else {
        atomic_store(&g_vim_state.current_mode, MODE_INSERT);
        mlwrite("[EVIL: Disabled]");
    }
    return true;
}

/*
 * vim_enter_normal_mode_external - ESC handler for bind.c.
 * When vim is an extension, the extension sets vim_esc_handler to its
 * own implementation. This stub handles the case where the extension
 * hasn't loaded but vim_mode_active was somehow set.
 */
int vim_enter_normal_mode_external(int f, int n) {
    (void)f; (void)n;
    atomic_store(&g_vim_state.current_mode, MODE_NORMAL);
    return true;
}
