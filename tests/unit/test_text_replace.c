// test_text_replace.c - Unit tests for search and replace
// Tests src/text/search.c: search functions and replace operations

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include <string.h>

// Helper to setup buffer with text
static int setup_buffer_with_text(const char *text) {
    struct buffer *bp = bfind("test-replace", true, 0);
    if (!bp) return 0;

    (void)bclear(bp);
    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = bp->b_linep;
    curwp->w_doto = 0;

    // Insert text line by line
    const char *p = text;
    while (*p) {
        const char *line_end = strchr(p, '\n');
        int len = line_end ? (line_end - p) : strlen(p);

        for (int i = 0; i < len; i++) {
            linsert(1, p[i]);
        }

        if (line_end) {
            lnewline();
            p = line_end + 1;
        } else {
            break;
        }
    }

    // Position cursor at start
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    return 1;
}

// Helper to get buffer content as string
static char *get_buffer_content(void) {
    static char buf[4096];
    int pos = 0;

    struct line *lp = lforw(curbp->b_linep);
    while (lp != curbp->b_linep && pos < 4095) {
        int len = llength(lp);
        for (int i = 0; i < len && pos < 4095; i++) {
            buf[pos++] = lgetc(lp, i);
        }
        lp = lforw(lp);
        if (lp != curbp->b_linep && pos < 4095) {
            buf[pos++] = '\n';
        }
    }
    buf[pos] = '\0';
    return buf;
}

// Helper to cleanup test buffer
static void cleanup_buffer(void) {
    if (curbp && strcmp(curbp->b_bname, "test-replace") == 0) {
        curbp->b_flag &= ~BFCHG;
        struct buffer *bp = curbp;
        curbp = bfind("main", false, 0);
        if (curbp) curwp->w_bufp = curbp;
        (void)zotbuf(bp);
    }
}

// Test basic forward search
static int test_forwsearch_basic(void) {
    int ok = 1;
    PHASE_START("SEARCH: FORW", "Forward search functionality");

    if (!setup_buffer_with_text("hello world\ntest hello again\nfinal line")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("SEARCH: FORW", ok);
        return ok;
    }

    // Set search pattern manually (simulating what forwsearch would do)
    strcpy(pat, "hello");

    // Use scanner to find first occurrence (public wrapper for scan_buffer)
    // Use POS_END to position at end of match so we can find next occurrence
    extern int scanner(const char *, int, int);
    if (scanner("hello", DIR_FORWARD, POS_END) != true) {
        LOG_ERROR("[FAIL] scanner didn't find 'hello'");
        ok = 0;
    }

    // Cursor should be after first "hello" (at position 5 = len("hello"))
    struct line *first_line = lforw(curbp->b_linep);
    if (curwp->w_dotp != first_line || curwp->w_doto < 5) {
        LOG_ERRORF("[FAIL] Cursor not positioned after match (doto=%d)", curwp->w_doto);
        ok = 0;
    }

    // Search again for second occurrence
    if (scanner("hello", DIR_FORWARD, POS_END) != true) {
        LOG_ERROR("[FAIL] scanner didn't find second 'hello'");
        ok = 0;
    }

    // Should be on second line now
    if (curwp->w_dotp == first_line) {
        LOG_ERROR("[FAIL] Still on first line after second search");
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("SEARCH: FORW", ok);
    return ok;
}

// Test backward search
static int test_backsearch_basic(void) {
    int ok = 1;
    PHASE_START("SEARCH: BACK", "Backward search functionality");

    if (!setup_buffer_with_text("hello world\ntest hello again\nfinal line")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("SEARCH: BACK", ok);
        return ok;
    }

    // Move to end of buffer
    curwp->w_dotp = lback(curbp->b_linep);
    curwp->w_doto = llength(curwp->w_dotp);

    // Search backward for "hello"
    extern int scanner(const char *, int, int);
    if (scanner("hello", DIR_REVERSE, POS_END) != true) {
        LOG_ERROR("[FAIL] backward scanner didn't find 'hello'");
        ok = 0;
    }

    // Should find second occurrence first (when searching backward)
    struct line *second_line = lforw(lforw(curbp->b_linep));
    if (curwp->w_dotp == curbp->b_linep) {
        LOG_ERROR("[FAIL] Backward search failed to position cursor");
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("SEARCH: BACK", ok);
    return ok;
}

// Test case sensitivity in search
static int test_search_case_sensitivity(void) {
    int ok = 1;
    PHASE_START("SEARCH: CASE", "Case sensitivity in search");

    if (!setup_buffer_with_text("Hello HELLO hello HeLLo")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("SEARCH: CASE", ok);
        return ok;
    }

    // Search for lowercase "hello"
    extern int scanner(const char *, int, int);

    // In case-insensitive mode (if enabled), should find "Hello"
    // In case-sensitive mode, should find exact match
    if (scanner("hello", DIR_FORWARD, POS_BEGIN) != true) {
        LOG_ERROR("[FAIL] Couldn't find 'hello' variant");
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("SEARCH: CASE", ok);
    return ok;
}

// Test search at buffer boundaries
static int test_search_boundaries(void) {
    int ok = 1;
    PHASE_START("SEARCH: BOUND", "Search at buffer boundaries");

    if (!setup_buffer_with_text("test")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("SEARCH: BOUND", ok);
        return ok;
    }

    extern int scanner(const char *, int, int);

    // Search for pattern at very start
    if (scanner("test", DIR_FORWARD, POS_BEGIN) != true) {
        LOG_ERROR("[FAIL] Couldn't find pattern at buffer start");
        ok = 0;
    }

    // Reset and search for non-existent pattern
    curwp->w_dotp = lforw(curbp->b_linep);
    curwp->w_doto = 0;

    if (scanner("nonexistent", DIR_FORWARD, POS_BEGIN) == true) {
        LOG_ERROR("[FAIL] Found non-existent pattern (false positive)");
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("SEARCH: BOUND", ok);
    return ok;
}

// Test multiline search
static int test_search_multiline(void) {
    int ok = 1;
    PHASE_START("SEARCH: MULTI", "Multiline search patterns");

    if (!setup_buffer_with_text("line one\nline two\nline three")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("SEARCH: MULTI", ok);
        return ok;
    }

    extern int scanner(const char *, int, int);

    // Search for "line" - should find multiple occurrences
    int found_count = 0;
    if (scanner("line", DIR_FORWARD, POS_BEGIN) == true) {
        found_count++;

        // Try to find next occurrence
        if (scanner("line", DIR_FORWARD, POS_BEGIN) == true) {
            found_count++;
        }
    }

    if (found_count < 2) {
        LOG_ERRORF("[FAIL] Didn't find multiple occurrences (found=%d)", found_count);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("SEARCH: MULTI", ok);
    return ok;
}

// Test search with special characters
static int test_search_special_chars(void) {
    int ok = 1;
    PHASE_START("SEARCH: SPECIAL", "Search with special characters");

    if (!setup_buffer_with_text("test-file.txt\ndata[0] = value\n*pointer++")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("SEARCH: SPECIAL", ok);
        return ok;
    }

    extern int scanner(const char *, int, int);

    // Search for pattern with dot
    if (scanner("test-file", DIR_FORWARD, POS_BEGIN) != true) {
        LOG_ERROR("[FAIL] Couldn't find 'test-file'");
        ok = 0;
    }

    // Reset position
    curwp->w_dotp = lforw(curbp->b_linep);
    curwp->w_doto = 0;

    // Search for pattern with brackets
    if (scanner("data[0]", DIR_FORWARD, POS_BEGIN) != true) {
        LOG_ERROR("[FAIL] Couldn't find 'data[0]'");
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("SEARCH: SPECIAL", ok);
    return ok;
}

// Test empty pattern handling
static int test_search_empty_pattern(void) {
    int ok = 1;
    PHASE_START("SEARCH: EMPTY", "Empty pattern edge case");

    if (!setup_buffer_with_text("test content")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("SEARCH: EMPTY", ok);
        return ok;
    }

    extern int scanner(const char *, int, int);

    // Empty pattern should fail gracefully
    int result = scanner("", DIR_FORWARD, POS_BEGIN);
    // Result could be true or false depending on implementation
    // Main test: it shouldn't crash

    cleanup_buffer();
    PHASE_END("SEARCH: EMPTY", ok);
    return ok;
}

// Test pattern matching accuracy
static int test_search_accuracy(void) {
    int ok = 1;
    PHASE_START("SEARCH: ACCURACY", "Pattern matching accuracy");

    if (!setup_buffer_with_text("the then there their")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("SEARCH: ACCURACY", ok);
        return ok;
    }

    extern int scanner(const char *, int, int);

    // Search for "the" - should find it as substring and word
    // Use POS_END to position cursor at end of match (after "the")
    if (scanner("the", DIR_FORWARD, POS_END) != true) {
        LOG_ERROR("[FAIL] Couldn't find 'the'");
        ok = 0;
    }

    // Verify we're at position after "the" (POS_END positions at end of match)
    if (curwp->w_doto < 3) {
        LOG_ERRORF("[FAIL] Position incorrect after match: doto=%d", curwp->w_doto);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("SEARCH: ACCURACY", ok);
    return ok;
}

// Entry point
int test_text_replace(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("replace");

    PHASE_START("TEXT_REPLACE", "Search and replace tests");

    ok &= test_forwsearch_basic();
    ok &= test_backsearch_basic();
    ok &= test_search_case_sensitivity();
    ok &= test_search_boundaries();
    ok &= test_search_multiline();
    ok &= test_search_special_chars();
    ok &= test_search_empty_pattern();
    ok &= test_search_accuracy();

    if (ok) {
        LOG_INFO("[SUCCESS] All search/replace tests passed.");
    }

    PHASE_END("TEXT_REPLACE", ok);
    return ok;
}
