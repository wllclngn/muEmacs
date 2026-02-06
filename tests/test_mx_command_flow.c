#include "test_utils.h"
#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/efunc.h"
#include "internal/line.h"
#include "μemacs/keymap.h"

// Atomic accessors for binding union (binding.cmd and binding.map are now _Atomic)
#define KM_GET_CMD(entry) atomic_load_explicit(&(entry)->binding.cmd, memory_order_relaxed)
#define KM_GET_MAP(entry) atomic_load_explicit(&(entry)->binding.map, memory_order_relaxed)

int test_mx_command_flow(void)
{
    int ok = 1;
    PHASE_START("M-X FLOW", "execute-command-line (docmd) path with args");

    test_init_editor("mx-flow");

    // Defensive checks - ensure editor initialized properly
    if (!curbp || !curwp) {
        LOG_ERRORF("[FAIL] Editor not initialized (curbp=%p, curwp=%p)", (void*)curbp, (void*)curwp);
        ok = 0;
        PHASE_END("M-X FLOW", ok);
        return 0;
    }

    unmark(0, 0);
    (void)bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Prepare a simple buffer line: "HelloWorld"
    curwp->w_dotp = curbp->b_linep;
    curwp->w_doto = 0;
    lnewline();
    curwp->w_dotp = lforw(curbp->b_linep);
    curwp->w_doto = 0;
    const char* line = "HelloWorld";
    for (const char* p = line; *p; ++p) linsert(1, *p);

    // Move to BOL and run a numeric-arg forward-character: "10 forward-character"
    curwp->w_doto = 0;
    int len = llength(curwp->w_dotp);
    if (!docmd("10 forward-character")) {
        LOG_ERROR("[FAIL] docmd forward-character failed");
        ok = 0;
    } else {
        if (curwp->w_doto != (len >= 10 ? 10 : len)) {
            LOG_ERRORF("[FAIL] forward-character did not move expected distance (got %d)", curwp->w_doto);
            ok = 0;
        }
    }

    // Enable help prefix and validate via keymap
    keymap_key_t ctrl_h = keymap_key_make('H', MOD_CTRL);
    if (!docmd("enable-help-prefix")) {
        LOG_ERROR("[FAIL] enable-help-prefix via docmd failed");
        ok = 0;
    } else {
        struct keymap *gkm = atomic_load(&global_keymap);
        struct keymap *hkm = atomic_load(&help_keymap);
        struct keymap_entry *e = keymap_lookup(gkm, ctrl_h);
        if (!e || !e->is_prefix || KM_GET_MAP(e) != hkm) {
            LOG_ERROR("[FAIL] C-h did not become help prefix after docmd");
            ok = 0;
        }
    }

    // Disable help prefix and validate delete_char_backward restored
    if (!docmd("disable-help-prefix")) {
        LOG_ERROR("[FAIL] disable-help-prefix via docmd failed");
        ok = 0;
    } else {
        struct keymap *gkm = atomic_load(&global_keymap);
        struct keymap_entry *e = keymap_lookup(gkm, ctrl_h);
        if (!e || e->is_prefix || KM_GET_CMD(e) != delete_char_backward) {
            LOG_ERROR("[FAIL] C-h delete_char_backward not restored after docmd");
            ok = 0;
        }
    }

    // Invalid command should fail but not crash
    if (docmd("this-command-does-not-exist")) {
        LOG_ERROR("[FAIL] invalid command unexpectedly succeeded");
        ok = 0;
    }

    PHASE_END("M-X FLOW", ok);
    return ok;
}

