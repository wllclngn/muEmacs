#include "test_utils.h"
#include "μemacs/keymap.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "terminal/input_state.h"

// Explicitly declare functions that might be missing from efunc.h headers in test context
extern int yank(int f, int n);
extern int yankpop(int f, int n);
extern int undo_cmd(int f, int n);
extern int redo_cmd(int f, int n);
extern int fileread(int f, int n);

// Direct keymap lookup helper for tests - uses modern keymap_key_t
static fn_t test_getbind_key(struct keymap *map, keymap_key_t key)
{
    if (!map) return nullptr;

    struct keymap_entry *entry = keymap_lookup(map, key);
    if (entry && !entry->is_prefix) {
        return atomic_load_explicit(&entry->binding.cmd, memory_order_relaxed);
    }
    return nullptr;
}

// Verify that default key bindings match expected μEmacs behaviors
int test_keymap_defaults() {
    int result = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("keymap-defaults");

    PHASE_START("Keymap Defaults", "Verify default key actions match expectations");

    // Ensure modern keymaps are initialized from legacy table
    keymap_init_defaults();

    // Initialize vim keymaps before loading settings (required for vim bindings)
    extern void vim_init_keymaps(void);
    vim_init_keymaps();

    // Load settings from TOML - this is where all keybindings come from now
    extern int settings_load(int f, int n);
    settings_load(false, 0);

    // Get keymaps
    struct keymap *gkm = atomic_load(&global_keymap);
    struct keymap *ckm = atomic_load(&ctlx_keymap);
    struct keymap *mkm = atomic_load(&meta_keymap);

    // Helper macro for checking global keymap bindings
    #define CHECK_GLOBAL(CODE, MODS, FN) \
        do { \
            keymap_key_t k = keymap_key_make((CODE), (MODS)); \
            fn_t f = test_getbind_key(gkm, k); \
            if (f != (FN)) { \
                LOG_INFOF("[%sFAIL%s] Global binding mismatch for '%c' mods=0x%X: got %p expected %p (%s)", \
                       RED, RESET, (char)(CODE), (MODS), (void*)(uintptr_t)f, (void*)(uintptr_t)(FN), #FN); \
                result = 0; \
            } else { \
                LOG_INFOF("[%sOK%s] Global '%c' mods=0x%X -> %s", GREEN, RESET, (char)(CODE), (MODS), #FN); \
            } \
        } while (0)

    // Helper macro for C-x keymap bindings
    #define CHECK_CTLX(CODE, MODS, FN) \
        do { \
            keymap_key_t k = keymap_key_make((CODE), (MODS)); \
            fn_t f = test_getbind_key(ckm, k); \
            if (f != (FN)) { \
                LOG_INFOF("[%sFAIL%s] C-x binding mismatch for '%c' mods=0x%X: got %p expected %p (%s)", \
                       RED, RESET, (char)(CODE), (MODS), (void*)(uintptr_t)f, (void*)(uintptr_t)(FN), #FN); \
                result = 0; \
            } else { \
                LOG_INFOF("[%sOK%s] C-x '%c' mods=0x%X -> %s", GREEN, RESET, (char)(CODE), (MODS), #FN); \
            } \
        } while (0)

    // Helper macro for Meta keymap bindings
    #define CHECK_META(CODE, MODS, FN) \
        do { \
            keymap_key_t k = keymap_key_make((CODE), (MODS)); \
            fn_t f = test_getbind_key(mkm, k); \
            if (f != (FN)) { \
                LOG_INFOF("[%sFAIL%s] Meta binding mismatch for '%c' mods=0x%X: got %p expected %p (%s)", \
                       RED, RESET, (char)(CODE), (MODS), (void*)(uintptr_t)f, (void*)(uintptr_t)(FN), #FN); \
                result = 0; \
            } else { \
                LOG_INFOF("[%sOK%s] Meta '%c' mods=0x%X -> %s", GREEN, RESET, (char)(CODE), (MODS), #FN); \
            } \
        } while (0)

    LOG_INFO("\n--- 1. Essential Operations ---");
    CHECK_CTLX('C', MOD_CTRL, quit);        // C-x C-c: Exit editor
    CHECK_CTLX('S', MOD_CTRL, filesave);    // C-x C-s: Save file
    CHECK_CTLX('F', MOD_CTRL, filefind);    // C-x C-f: Find/open file
    CHECK_GLOBAL('G', MOD_CTRL, ctrlg);     // C-g: Abort command
    CHECK_META('X', 0, namedcmd);           // M-x: Execute named command
    CHECK_GLOBAL('W', MOD_CTRL, region_kill);// C-w: Kill region (cut)
    CHECK_META('W', 0, region_copy);         // M-w: Copy region
    CHECK_GLOBAL('Y', MOD_CTRL, yank);      // C-y: Yank (paste)
    CHECK_META('Y', 0, yankpop);            // M-y: Yank pop

    LOG_INFO("\n--- 2. Navigation ---");
    CHECK_GLOBAL('A', MOD_CTRL, goto_line_start);   // C-a: Beginning of line
    CHECK_GLOBAL('E', MOD_CTRL, goto_line_end);   // C-e: End of line
    CHECK_GLOBAL('F', MOD_CTRL, move_char_forward);  // C-f: Forward char
    CHECK_GLOBAL('B', MOD_CTRL, move_char_backward);  // C-b: Backward char
    CHECK_GLOBAL('N', MOD_CTRL, cursor_down);  // C-n: Next line
    CHECK_GLOBAL('P', MOD_CTRL, cursor_up);  // C-p: Previous line
    CHECK_GLOBAL('V', MOD_CTRL, move_page_down);  // C-v: Next page
    CHECK_GLOBAL('Z', MOD_CTRL, move_page_up);  // C-z: Previous page
    CHECK_META('<', 0, goto_buffer_start);            // M-<: Beginning of file
    CHECK_META('>', 0, goto_buffer_end);            // M->: End of file
    CHECK_META('F', 0, move_word_forward);           // M-f: Forward word
    CHECK_META('B', 0, move_word_backward);           // M-b: Backward word
    CHECK_META('G', 0, gotoline);           // M-g: Go to line

    LOG_INFO("\n--- 3. Editing ---");
    CHECK_GLOBAL('D', MOD_CTRL, delete_char_forward);   // C-d: Delete char forward
    CHECK_GLOBAL('H', MOD_CTRL, delete_char_backward);   // C-h: Delete char backward (Backspace)
    CHECK_GLOBAL('K', MOD_CTRL, kill_to_eol);  // C-k: Kill to end of line
    CHECK_GLOBAL('T', MOD_CTRL, twiddle);   // C-t: Transpose characters
    CHECK_GLOBAL('O', MOD_CTRL, openline);  // C-o: Open line
    CHECK_META(' ', 0, setmark);            // M-SPC: Set mark

    LOG_INFO("\n--- 4. Undo/Redo ---");
    CHECK_GLOBAL('_', MOD_CTRL, undo_cmd);  // C-_: Undo
    CHECK_META('_', 0, redo_cmd);           // M-_: Redo
    CHECK_CTLX('R', MOD_CTRL, fileread);    // C-x C-r: Read file

    LOG_INFO("\n--- 5. Search & Replace ---");
    // Note: C-s may be overridden by user (insert mode save-file), skip check
    CHECK_GLOBAL('R', MOD_CTRL, backsearch);// C-r: Incremental search backward
    CHECK_META('R', 0, sreplace);           // M-r: Replace string
    CHECK_META('R', MOD_CTRL, qreplace);    // M-C-r: Query replace

    LOG_INFO("\n--- 6. Windows & Buffers ---");
    CHECK_CTLX('0', 0, window_delete);            // C-x 0: Delete window
    CHECK_CTLX('1', 0, window_only);           // C-x 1: Delete other windows
    CHECK_CTLX('2', 0, window_split);          // C-x 2: Split window
    CHECK_CTLX('O', 0, window_next);           // C-x o: Next window
    CHECK_CTLX('B', 0, usebuffer);          // C-x b: Select buffer
    CHECK_CTLX('K', 0, killbuffer);         // C-x k: Kill buffer
    CHECK_CTLX('B', MOD_CTRL, listbuffers); // C-x C-b: List buffers

    LOG_INFO("\n--- 7. Macros ---");
    CHECK_CTLX('(', 0, ctlxlp);             // C-x (: Begin macro
    CHECK_CTLX(')', 0, ctlxrp);             // C-x ): End macro
    CHECK_CTLX('E', 0, ctlxe);              // C-x e: Execute macro

    LOG_INFO("\n--- 8. Misc / System ---");
    CHECK_CTLX('=', 0, showcpos);           // C-x =: Show cursor position
    CHECK_GLOBAL('L', MOD_CTRL, redraw);    // C-l: Redraw screen
    CHECK_META('X', 0, namedcmd);           // M-x: Named command

    // Critical negative check - C-h should be delete_char_backward, not help prefix
    keymap_key_t ctrl_h = keymap_key_make('H', MOD_CTRL);
    fn_t ch_binding = test_getbind_key(gkm, ctrl_h);
    if (ch_binding == help) {
        LOG_ERROR("[FAIL] C-h incorrectly bound to help prefix");
        result = 0;
    }

    PHASE_END("Keymap Defaults", result);
    return result;
}
