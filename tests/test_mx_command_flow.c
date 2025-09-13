#include "test_utils.h"
#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/efunc.h"
#include "internal/line.h"
#include "μemacs/keymap.h"

static void init_editor_minimal(const char* name) {
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    edinit((char*)(name ? name : "mx"));
    varinit();
}

int test_mx_command_flow(void)
{
    int ok = 1;
    PHASE_START("M-X FLOW", "execute-command-line (docmd) path with args");

    init_editor_minimal("mx-flow");
    unmark(0,0);
    bclear(curbp);
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
        printf("[%sFAIL%s] docmd forward-character failed\n", RED, RESET);
        ok = 0;
    } else {
        if (curwp->w_doto != (len >= 10 ? 10 : len)) {
            printf("[%sFAIL%s] forward-character did not move expected distance (got %d)\n", RED, RESET, curwp->w_doto);
            ok = 0;
        }
    }

    // Enable help prefix and validate via keymap
    if (!docmd("enable-help-prefix")) {
        printf("[%sFAIL%s] enable-help-prefix via docmd failed\n", RED, RESET);
        ok = 0;
    } else {
        struct keymap *gkm = atomic_load(&global_keymap);
        struct keymap *hkm = atomic_load(&help_keymap);
        struct keymap_entry *e = keymap_lookup(gkm, CONTROL | 'H');
        if (!e || !e->is_prefix || e->binding.map != hkm) {
            printf("[%sFAIL%s] C-h did not become help prefix after docmd\n", RED, RESET);
            ok = 0;
        }
    }

    // Disable help prefix and validate backdel restored
    if (!docmd("disable-help-prefix")) {
        printf("[%sFAIL%s] disable-help-prefix via docmd failed\n", RED, RESET);
        ok = 0;
    } else {
        struct keymap *gkm = atomic_load(&global_keymap);
        struct keymap_entry *e = keymap_lookup(gkm, CONTROL | 'H');
        if (!e || e->is_prefix || e->binding.cmd != backdel) {
            printf("[%sFAIL%s] C-h backdel not restored after docmd\n", RED, RESET);
            ok = 0;
        }
    }

    // Invalid command should fail but not crash
    if (docmd("this-command-does-not-exist")) {
        printf("[%sFAIL%s] invalid command unexpectedly succeeded\n", RED, RESET);
        ok = 0;
    }

    PHASE_END("M-X FLOW", ok);
    return ok;
}

