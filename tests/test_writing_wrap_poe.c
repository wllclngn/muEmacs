#include "test_utils.h"
#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/efunc.h"
#include "internal/line.h"

static void init_editor_minimal(const char* name) {
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    edinit((char*)(name ? name : "poe-wrap"));
    varinit();
}

static int line_len(struct line* lp) {
    return lp ? llength(lp) : 0;
}

int test_poe_gutenberg_wrap(void)
{
    int ok = 1;
    PHASE_START("POE: WRAP", "Wrap paragraph to fill column on Gutenberg text");

    init_editor_minimal("poe-wrap");
    unmark(0,0);
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Try multiple paths since tests can run from different directories
    const char* paths[] = {
        "tests/data/poe-collected-works.txt",      // From project root
        "../tests/data/poe-collected-works.txt",   // From build/
        NULL
    };
    const char* path = NULL;
    for (int i = 0; paths[i]; i++) {
        if (access(paths[i], R_OK) == 0) {
            path = paths[i];
            break;
        }
    }
    if (!path) path = paths[0];  // Fallback for error message
    if (access(path, R_OK) != 0) {
        LOG_ERRORF("[FAIL] Missing Gutenberg text at %s", path);
        ok = 0;
        PHASE_END("POE: WRAP", ok);
        return ok;
    }

    // Read file into current buffer
    if (!readin(path, false)) {
        LOG_ERRORF("[FAIL] readin failed for %s", path);
        ok = 0;
        PHASE_END("POE: WRAP", ok);
        return ok;
    }

    // Enable writing mode at column 80 (also turns on wrap)
    writing_mode_enable(true, 80);

    // Find first non-empty paragraph
    struct line* lp = lforw(curbp->b_linep);
    while (lp != curbp->b_linep && line_len(lp) == 0) lp = lforw(lp);
    if (lp == curbp->b_linep) {
        LOG_ERROR("[FAIL] Could not find a non-empty paragraph");
        ok = 0;
        PHASE_END("POE: WRAP", ok);
        return ok;
    }
    struct line* start = lp;
    // Limit paragraph scan to a reasonable number of lines to avoid huge checks
    int scanned = 0;
    while (lp != curbp->b_linep && line_len(lp) > 0 && scanned < 200) {
        scanned++;
        lp = lforw(lp);
    }
    struct line* end_after = lp; // first blank after paragraph (or limit)

    // Place point within the paragraph and fill it
    curwp->w_dotp = start;
    curwp->w_doto = 0;
    if (!fillpara(false, 1)) {
        LOG_ERROR("[FAIL] fill-paragraph failed");
        ok = 0;
    } else {
        // Verify lines up to first blank are <= fillcol
        int violations = 0;
        int checked = 0;
        int maxlen = 0;
        int fcol = fillcol > 0 ? fillcol : 80;
        struct line* cur = start;
        while (cur != end_after && cur != curbp->b_linep) {
            int ll = line_len(cur);
            if (ll > maxlen) maxlen = ll;
            if (ll > fcol) violations++;
            checked++;
            cur = lforw(cur);
        }
        if (checked == 0) {
            LOG_ERROR("[FAIL] No lines checked in paragraph");
            ok = 0;
        } else if (violations > 0) {
            LOG_ERRORF("[FAIL] %d/%d lines exceed fill column (max=%d, fillcol=%d)", violations, checked, maxlen, fcol);
            ok = 0;
        } else {
            LOG_INFOF("[SUCCESS] Paragraph reflowed within fill column (max=%d, fillcol=%d)", maxlen, fcol);
        }
    }

    PHASE_END("POE: WRAP", ok);
    return ok;
}
