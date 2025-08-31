#include "test_utils.h"
#include "test_undo_capacity.h"

#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/efunc.h"
#include "internal/line.h"
#include "internal/undo.h"

static void init_editor_minimal(const char* name) {
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    edinit((char*)(name ? name : "undo-cap"));
    varinit();
}

int test_undo_capacity_wrap() {
    int ok = 1;
    PHASE_START("UNDO: CAP", "Capacity growth/wrap under many edits");

    init_editor_minimal("undo-capacity");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Ensure a real line exists
    curwp->w_dotp = curbp->b_linep;
    curwp->w_doto = 0;
    lnewline();
    curwp->w_dotp = lforw(curbp->b_linep);
    curwp->w_doto = 0;

    // Insert many single characters to exceed initial capacity (100)
    const char* stress = getenv("STRESS");
    int total = 300; // default triggers several resizes and wraps
    if (stress && strcmp(stress, "1") == 0) {
        total = 3000; // heavier pressure under stress mode
        printf("[INFO] STRESS=1: undo capacity total edits=%d\n", total);
    }
    for (int i = 0; i < total; ++i) {
        if (!linsert(1, 'a' + (i % 26))) { ok = 0; break; }
    }
    if (llength(curwp->w_dotp) != total) {
        ok = 0; printf("[%sFAIL%s] insert count mismatch\n", RED, RESET);
    }

    // Undo all operations; should restore to empty line
    int safety = total + 10;
    while (safety-- > 0 && llength(curwp->w_dotp) > 0) {
        if (!undo_cmd(0,0)) break;
    }
    if (llength(curwp->w_dotp) != 0) {
        ok = 0; printf("[%sFAIL%s] did not undo to empty after many edits\n", RED, RESET);
    }

    PHASE_END("UNDO: CAP", ok);
    return ok;
}
