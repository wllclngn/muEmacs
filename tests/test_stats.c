#include "test_utils.h"
#include "test_stats.h"

#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/efunc.h"
#include "internal/line.h"

static void init_editor_minimal(const char* name) {
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    edinit((char*)(name ? name : "stats"));
    varinit();
}

int test_atomic_stats_updates() {
    int ok = 1;
    PHASE_START("STATS: ATOMIC", "Incremental line/byte/word updates");

    init_editor_minimal("stats");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Ensure a real first line exists
    curwp->w_dotp = curbp->b_linep; curwp->w_doto = 0; lnewline();
    curwp->w_dotp = lforw(curbp->b_linep); curwp->w_doto = 0;

    int lines=0; long bytes=0; int words=0;
    buffer_get_stats_fast(curbp, &lines, &bytes, &words);
    if (lines != 1 || bytes != 0 || words != 0) {
        ok = 0; printf("[%sFAIL%s] initial stats wrong (L=%d B=%ld W=%d)\n", RED, RESET, lines, bytes, words);
    }

    // Insert "hello world" (2 words)
    const char* s = "hello world";
    for (const char* p = s; *p; ++p) linsert(1, *p);
    buffer_get_stats_fast(curbp, &lines, &bytes, &words);
    if (lines != 1 || words < 2) {
        ok = 0; printf("[%sFAIL%s] after insert expected 2 words (got %d)\n", RED, RESET, words);
    }

    // Newline
    lnewline();
    buffer_get_stats_fast(curbp, &lines, &bytes, &words);
    if (lines < 2) {
        ok = 0; printf("[%sFAIL%s] line count did not increase\n", RED, RESET);
    }

    // Delete a char and check bytes change (words heuristic may not change)
    curwp->w_dotp = lforw(curbp->b_linep); curwp->w_doto = 0; // line 1
    long before_bytes = 0; buffer_get_stats_fast(curbp, NULL, &before_bytes, NULL);
    ldelete(1, FALSE);
    long after_bytes = 0; buffer_get_stats_fast(curbp, NULL, &after_bytes, NULL);
    if (!(after_bytes == before_bytes - 1)) {
        ok = 0; printf("[%sFAIL%s] byte count did not decrement\n", RED, RESET);
    }

    PHASE_END("STATS: ATOMIC", ok);
    return ok;
}

