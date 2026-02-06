// test_text_isearch.c - Unit tests for incremental search
// Tests src/text/isearch.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"

// Helper: setup a test buffer with content
static struct buffer *setup_search_buffer(const char *name, const char *content[]) {
    struct buffer *bp = bfind((char *)name, true, 0);
    if (!bp) return nullptr;

    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Add content lines
    for (int i = 0; content[i]; i++) {
        linstr(content[i]);
        lnewline();
    }

    // Go back to beginning
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    return bp;
}

// Helper: cleanup
static void cleanup_buffer(struct buffer *bp, struct buffer *oldbp) {
    if (oldbp) {
        curbp = oldbp;
        curwp->w_bufp = oldbp;
    }
    if (bp) {
        bp->b_flag &= ~BFCHG;
        (void)zotbuf(bp);
    }
}

// Test scanner function (internal search engine)
static int test_isearch_scanner(void) {
    int ok = 1;
    PHASE_START("ISEARCH: SCANNER", "Scanner function");

    const char *content[] = {
        "Hello World",
        "This is a test",
        "Hello again",
        nullptr
    };

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_search_buffer("test-scanner", content);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("ISEARCH: SCANNER", ok);
        return ok;
    }

    // Search for "Hello"
    int found = scanner("Hello", DIR_FORWARD, POS_END);
    if (!found) {
        LOG_ERROR("[FAIL] scanner didn't find 'Hello'");
        ok = 0;
    }

    // Search for non-existent
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;
    found = scanner("NOTFOUND", DIR_FORWARD, POS_END);
    if (found) {
        LOG_ERROR("[FAIL] scanner found non-existent pattern");
        ok = 0;
    }

    cleanup_buffer(bp, oldbp);
    PHASE_END("ISEARCH: SCANNER", ok);
    return ok;
}

// Test case sensitivity
static int test_isearch_case(void) {
    int ok = 1;
    PHASE_START("ISEARCH: CASE", "Case sensitivity");

    const char *content[] = {
        "Hello World",
        "hello world",
        "HELLO WORLD",
        nullptr
    };

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_search_buffer("test-case", content);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("ISEARCH: CASE", ok);
        return ok;
    }

    // Without MDEXACT, should find case-insensitive
    bp->b_mode &= ~MDEXACT;
    int found = scanner("hello", DIR_FORWARD, POS_END);
    if (!found) {
        LOG_ERROR("[FAIL] case-insensitive search failed");
        ok = 0;
    }

    // With MDEXACT, should be case-sensitive
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;
    bp->b_mode |= MDEXACT;
    found = scanner("HELLO", DIR_FORWARD, POS_END);
    if (!found) {
        LOG_ERROR("[FAIL] case-sensitive search failed");
        ok = 0;
    }

    cleanup_buffer(bp, oldbp);
    PHASE_END("ISEARCH: CASE", ok);
    return ok;
}

// Test backward search
static int test_isearch_backward(void) {
    int ok = 1;
    PHASE_START("ISEARCH: BACKWARD", "Backward search");

    const char *content[] = {
        "First line",
        "Second line",
        "Third line",
        nullptr
    };

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_search_buffer("test-backward", content);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("ISEARCH: BACKWARD", ok);
        return ok;
    }

    // Go to end of buffer
    curwp->w_dotp = lback(bp->b_linep);
    curwp->w_doto = llength(curwp->w_dotp);

    // Search backward for "First"
    int found = scanner("First", DIR_REVERSE, POS_BEGIN);
    if (!found) {
        LOG_ERROR("[FAIL] backward search failed");
        ok = 0;
    }

    cleanup_buffer(bp, oldbp);
    PHASE_END("ISEARCH: BACKWARD", ok);
    return ok;
}

// Test empty pattern
static int test_isearch_empty(void) {
    int ok = 1;
    PHASE_START("ISEARCH: EMPTY", "Empty pattern handling");

    const char *content[] = {
        "Some text",
        nullptr
    };

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_search_buffer("test-empty", content);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("ISEARCH: EMPTY", ok);
        return ok;
    }

    // Empty pattern should fail gracefully
    int found = scanner("", DIR_FORWARD, POS_END);
    // Empty patterns typically return false or are handled specially
    (void)found;  // Result depends on implementation

    cleanup_buffer(bp, oldbp);
    PHASE_END("ISEARCH: EMPTY", ok);
    return ok;
}

// Entry point
int test_text_isearch(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("isearch");

    PHASE_START("ISEARCH", "Incremental search tests");

    ok &= test_isearch_scanner();
    ok &= test_isearch_case();
    ok &= test_isearch_backward();
    ok &= test_isearch_empty();

    if (ok) {
        LOG_INFO("[SUCCESS] All isearch tests passed.");
    }

    PHASE_END("ISEARCH", ok);
    return ok;
}
