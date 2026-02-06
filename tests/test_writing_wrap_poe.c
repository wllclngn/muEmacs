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

/* Check if line is effectively blank (empty or whitespace only) */
static int is_blank_line(struct line* lp) {
    if (!lp) return 1;
    int len = llength(lp);
    if (len == 0) return 1;
    /* Check if all chars are whitespace */
    for (int i = 0; i < len; i++) {
        char c = lgetc(lp, i);
        if (c != ' ' && c != '\t' && c != '\r') return 0;
    }
    return 1;
}

int test_poe_gutenberg_wrap(void)
{
    int ok = 1;
    PHASE_START("POE: WRAP", "Wrap paragraph to fill column on Gutenberg text");

    init_editor_minimal("poe-wrap");
    unmark(0,0);
    (void)bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Try multiple paths since tests can run from different directories
    const char* paths[] = {
        "tests/data/poe-collected-fictions.txt",      // From project root
        "../tests/data/poe-collected-fictions.txt",   // From build/
        nullptr
    };
    const char* path = nullptr;
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

    // Find first non-blank paragraph (skip header lines, find actual prose)
    struct line* lp = lforw(curbp->b_linep);
    // Skip initial blank lines
    while (lp != curbp->b_linep && is_blank_line(lp)) lp = lforw(lp);
    // Skip short header lines (looking for a multi-line paragraph)
    int para_lines = 0;
    struct line* para_start = lp;
    while (lp != curbp->b_linep) {
        if (is_blank_line(lp)) {
            if (para_lines >= 3) break;  // Found a good paragraph
            // Not enough lines, skip and try next paragraph
            while (lp != curbp->b_linep && is_blank_line(lp)) lp = lforw(lp);
            para_start = lp;
            para_lines = 0;
        } else {
            para_lines++;
            lp = lforw(lp);
        }
    }
    if (para_lines < 3) {
        LOG_ERROR("[FAIL] Could not find a multi-line paragraph");
        ok = 0;
        PHASE_END("POE: WRAP", ok);
        return ok;
    }
    struct line* start = para_start;
    // Limit paragraph scan to a reasonable number of lines to avoid huge checks
    int scanned = 0;
    lp = start;
    while (lp != curbp->b_linep && !is_blank_line(lp) && scanned < 200) {
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
