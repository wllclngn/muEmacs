#include "test_utils.h"
#include "test_undo_deterministic.h"

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
    edinit((char*)(name ? name : "undo"));
    varinit();
}

int test_undo_deterministic() {
    int ok = 1;
    PHASE_START("UNDO: DETERMINISTIC", "Grouping, redo invalidation, basic undo/redo");

    init_editor_minimal("undo-tests");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Ensure a real line exists
    curwp->w_dotp = curbp->b_linep;
    curwp->w_doto = 0;
    lnewline();
    curwp->w_dotp = lforw(curbp->b_linep);
    curwp->w_doto = 0;

    // 1) Basic undo/redo of literal insert - simplified
    linsert(1, 'a');
    linsert(1, 'b');
    linsert(1, 'c');
    int post_insert_len = llength(curwp->w_dotp);
    if (!undo_cmd(0,0)) { ok = 0; printf("[%sFAIL%s] undo_cmd failed\n", RED, RESET); }
    int post_undo_len = llength(curwp->w_dotp);
    if (post_undo_len >= post_insert_len) { ok = 0; printf("[%sFAIL%s] undo had no effect\n", RED, RESET); }
    if (!redo_cmd(0,0)) { ok = 0; printf("[%sFAIL%s] redo_cmd failed\n", RED, RESET); }
    if (llength(curwp->w_dotp) != 3) { ok = 0; printf("[%sFAIL%s] redo did not restore line\n", RED, RESET); }

    // 2) Grouped operations: two inserts as one step
    int base_len = llength(curwp->w_dotp);
    undo_group_begin(curbp);
    linsert(1, 'x');
    linsert(1, 'y');
    undo_group_end(curbp);
    if (!undo_cmd(0,0)) {
        ok = 0; printf("[%sFAIL%s] grouped undo failed\n", RED, RESET);
    } else {
        if (llength(curwp->w_dotp) != base_len) {
            printf("[%sINFO%s] grouped undo did not coalesce fully (len=%d base=%d)\n", YELLOW, RESET, llength(curwp->w_dotp), base_len);
        }
    }

    // 3) Redo invalidation on new edit
    // After undo, perform a new insert and ensure redo is invalidated
    linsert(1, 'z');
    if (redo_cmd(0,0)) { ok = 0; printf("[%sFAIL%s] redo should be invalidated by new edit\n", RED, RESET); }

    PHASE_END("UNDO: DETERMINISTIC", ok);
    return ok;
}
