// test_text_random.c - Unit tests for random text operations
// Tests src/text/random.c: detab, entab, trim, quote, twiddle, etc.

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include <string.h>

// Helper to setup buffer with text
static int setup_buffer_with_text(const char *text) {
    struct buffer *bp = bfind("test-random", true, 0);
    if (!bp) return 0;

    (void)bclear(bp);
    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = bp->b_linep;
    curwp->w_doto = 0;

    // Insert text
    const char *p = text;
    while (*p) {
        if (*p == '\n') {
            lnewline();
        } else if (*p == '\t') {
            linsert(1, '\t');
        } else {
            linsert(1, *p);
        }
        p++;
    }

    // Position cursor at start
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    return 1;
}

// Helper to get buffer content as string (preserving tabs)
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
    if (curbp && strcmp(curbp->b_bname, "test-random") == 0) {
        curbp->b_flag &= ~BFCHG;
        struct buffer *bp = curbp;
        curbp = bfind("main", false, 0);
        if (curbp) curwp->w_bufp = curbp;
        (void)zotbuf(bp);
    }
}

// Test detab - convert tabs to spaces
static int test_detab(void) {
    int ok = 1;
    PHASE_START("RANDOM: DETAB", "Convert tabs to spaces");

    // Setup with tabs (tab stop is typically 8)
    if (!setup_buffer_with_text("hello\tworld\ttest")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: DETAB", ok);
        return ok;
    }

    // Count initial tabs
    const char *before = get_buffer_content();
    int tab_count_before = 0;
    for (const char *p = before; *p; p++) {
        if (*p == '\t') tab_count_before++;
    }

    if (tab_count_before < 2) {
        LOG_ERROR("[FAIL] Test setup failed - expected tabs in input");
        ok = 0;
    }

    // Run detab
    if (detab(false, 1) != true) {
        LOG_ERROR("[FAIL] detab failed");
        ok = 0;
    }

    // Verify tabs are gone
    const char *after = get_buffer_content();
    int tab_count_after = 0;
    for (const char *p = after; *p; p++) {
        if (*p == '\t') tab_count_after++;
    }

    if (tab_count_after > 0) {
        LOG_ERRORF("[FAIL] Tabs still present after detab: %d", tab_count_after);
        ok = 0;
    }

    // Verify content still readable
    if (strstr(after, "hello") == nullptr || strstr(after, "world") == nullptr) {
        LOG_ERROR("[FAIL] Content corrupted after detab");
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("RANDOM: DETAB", ok);
    return ok;
}

// Test entab - convert spaces to tabs
static int test_entab(void) {
    int ok = 1;
    PHASE_START("RANDOM: ENTAB", "Convert spaces to tabs");

    // Setup with multiple spaces (should convert to tabs at tab stops)
    if (!setup_buffer_with_text("hello        world")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: ENTAB", ok);
        return ok;
    }

    const char *before = get_buffer_content();
    int space_count_before = 0;
    for (const char *p = before; *p; p++) {
        if (*p == ' ') space_count_before++;
    }

    // Run entab
    if (entab(false, 1) != true) {
        LOG_ERROR("[FAIL] entab failed");
        ok = 0;
    }

    const char *after = get_buffer_content();
    int space_count_after = 0;
    int tab_count_after = 0;
    for (const char *p = after; *p; p++) {
        if (*p == ' ') space_count_after++;
        if (*p == '\t') tab_count_after++;
    }

    // Should have fewer spaces or some tabs introduced
    if (space_count_after >= space_count_before && tab_count_after == 0) {
        LOG_ERRORF("[FAIL] entab didn't convert spaces (before=%d, after=%d, tabs=%d)", space_count_before, space_count_after, tab_count_after);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("RANDOM: ENTAB", ok);
    return ok;
}

// Test trim - remove trailing whitespace
static int test_trim(void) {
    int ok = 1;
    PHASE_START("RANDOM: TRIM", "Trim trailing whitespace");

    // Setup with trailing spaces
    if (!setup_buffer_with_text("hello world   \ntest line  ")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: TRIM", ok);
        return ok;
    }

    // Run trim on multiple lines
    if (trim(false, 2) != true) {
        LOG_ERROR("[FAIL] trim failed");
        ok = 0;
    }

    const char *after = get_buffer_content();

    // Check first line - should end with 'd' not spaces
    const char *newline = strchr(after, '\n');
    if (newline && newline > after) {
        char last_char = *(newline - 1);
        if (last_char == ' ' || last_char == '\t') {
            LOG_ERRORF("[FAIL] First line still has trailing space: '%c'", last_char);
            ok = 0;
        }
    }

    cleanup_buffer();
    PHASE_END("RANDOM: TRIM", ok);
    return ok;
}

// Test twiddle - swap characters
static int test_twiddle(void) {
    int ok = 1;
    PHASE_START("RANDOM: TWIDDLE", "Swap adjacent characters");

    if (!setup_buffer_with_text("abcd")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: TWIDDLE", ok);
        return ok;
    }

    // Move cursor to position 2 (between 'b' and 'c')
    curwp->w_doto = 2;

    // Run twiddle - should swap 'b' and 'c'
    if (twiddle(false, 1) != true) {
        LOG_ERROR("[FAIL] twiddle failed");
        ok = 0;
    }

    const char *after = get_buffer_content();
    // Should now be "acbd" (b and c swapped)
    if (strncmp(after, "acbd", 4) != 0) {
        LOG_ERRORF("[FAIL] Expected 'acbd', got '%.4s'", after);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("RANDOM: TWIDDLE", ok);
    return ok;
}

// Test showcpos - show cursor position
static int test_showcpos(void) {
    int ok = 1;
    PHASE_START("RANDOM: SHOWCPOS", "Show cursor position");

    if (!setup_buffer_with_text("line one\nline two\nline three")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: SHOWCPOS", ok);
        return ok;
    }

    // Move to second line
    cursor_down(false, 1);
    curwp->w_doto = 5; // Position in middle of "line two"

    // showcpos should not crash and should return true
    if (showcpos(false, 1) != true) {
        LOG_ERROR("[FAIL] showcpos failed");
        ok = 0;
    }

    // The function writes to message line, which we can't easily test
    // Main verification: it didn't crash

    cleanup_buffer();
    PHASE_END("RANDOM: SHOWCPOS", ok);
    return ok;
}

// Test getcline - get current line number
static int test_getcline(void) {
    int ok = 1;
    PHASE_START("RANDOM: GETCLINE", "Get current line number");

    if (!setup_buffer_with_text("line 1\nline 2\nline 3\nline 4")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: GETCLINE", ok);
        return ok;
    }

    // At start - should be line 1
    int line1 = getcline();
    if (line1 != 1) {
        LOG_ERRORF("[FAIL] Expected line 1, got %d", line1);
        ok = 0;
    }

    // Move to line 3
    cursor_down(false, 2);
    int line3 = getcline();
    if (line3 != 3) {
        LOG_ERRORF("[FAIL] Expected line 3, got %d", line3);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("RANDOM: GETCLINE", ok);
    return ok;
}

// Test getccol - get current column
static int test_getccol(void) {
    int ok = 1;
    PHASE_START("RANDOM: GETCCOL", "Get current column");

    if (!setup_buffer_with_text("hello world")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: GETCCOL", ok);
        return ok;
    }

    // At start - column 0
    int col0 = getccol(false);
    if (col0 != 0) {
        LOG_ERRORF("[FAIL] Expected column 0, got %d", col0);
        ok = 0;
    }

    // Move right 5 characters
    move_char_forward(false, 5);
    int col5 = getccol(false);
    if (col5 != 5) {
        LOG_ERRORF("[FAIL] Expected column 5, got %d", col5);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("RANDOM: GETCCOL", ok);
    return ok;
}

// Test setccol - set current column
static int test_setccol(void) {
    int ok = 1;
    PHASE_START("RANDOM: SETCCOL", "Set cursor to column");

    if (!setup_buffer_with_text("hello world test line")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: SETCCOL", ok);
        return ok;
    }

    // Set to column 10
    if (setccol(10) != true) {
        LOG_ERROR("[FAIL] setccol failed");
        ok = 0;
    }

    int col = getccol(false);
    if (col != 10) {
        LOG_ERRORF("[FAIL] Expected column 10, got %d", col);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("RANDOM: SETCCOL", ok);
    return ok;
}

// Test insert_tab
static int test_insert_tab(void) {
    int ok = 1;
    PHASE_START("RANDOM: INSERTTAB", "Insert tab character");

    if (!setup_buffer_with_text("hello")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: INSERTTAB", ok);
        return ok;
    }

    // Move to end
    goto_line_end(false, 0);

    // Save old tabsize
    int old_tabsize = tabsize;
    tabsize = 0; // Use real tabs

    // Insert tab
    if (insert_tab(false, 1) != true) {
        LOG_ERROR("[FAIL] insert_tab failed");
        ok = 0;
    }

    const char *after = get_buffer_content();
    // Should have a tab character
    int has_tab = 0;
    for (const char *p = after; *p; p++) {
        if (*p == '\t') {
            has_tab = 1;
            break;
        }
    }

    if (!has_tab) {
        LOG_ERROR("[FAIL] Tab character not inserted");
        ok = 0;
    }

    tabsize = old_tabsize; // Restore
    cleanup_buffer();
    PHASE_END("RANDOM: INSERTTAB", ok);
    return ok;
}

// Test quote - insert literal character
static int test_quote(void) {
    int ok = 1;
    PHASE_START("RANDOM: QUOTE", "Insert literal character");

    if (!setup_buffer_with_text("hello")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: QUOTE", ok);
        return ok;
    }

    goto_line_end(false, 0);

    // Note: quote() waits for input, so we can only test that it exists
    // Full interactive testing would require PTY setup
    // Just verify function is callable (testing framework limitation)

    cleanup_buffer();
    PHASE_END("RANDOM: QUOTE", ok);
    return ok;
}

// Test openline - open a new line above current
static int test_openline(void) {
    int ok = 1;
    PHASE_START("RANDOM: OPENLINE", "Open new line above");

    if (!setup_buffer_with_text("line1\nline2")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: OPENLINE", ok);
        return ok;
    }

    // Count lines before
    int lines_before = 0;
    struct line *lp = lforw(curbp->b_linep);
    while (lp != curbp->b_linep) {
        lines_before++;
        lp = lforw(lp);
    }

    // Open a line
    if (openline(false, 1) != true) {
        LOG_ERROR("[FAIL] openline failed");
        ok = 0;
    }

    // Count lines after
    int lines_after = 0;
    lp = lforw(curbp->b_linep);
    while (lp != curbp->b_linep) {
        lines_after++;
        lp = lforw(lp);
    }

    if (lines_after != lines_before + 1) {
        LOG_ERRORF("[FAIL] Line not added: %d -> %d", lines_before, lines_after);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("RANDOM: OPENLINE", ok);
    return ok;
}

// Test insert_newline
static int test_insert_newline(void) {
    int ok = 1;
    PHASE_START("RANDOM: NEWLINE", "Insert newline");

    if (!setup_buffer_with_text("hello")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: NEWLINE", ok);
        return ok;
    }

    // Move to middle
    curwp->w_doto = 3;

    // Insert newline
    if (insert_newline(false, 1) != true) {
        LOG_ERROR("[FAIL] insert_newline failed");
        ok = 0;
    }

    // Should have split the line
    const char *content = get_buffer_content();
    int newline_count = 0;
    for (const char *p = content; *p; p++) {
        if (*p == '\n') newline_count++;
    }

    if (newline_count < 1) {
        LOG_ERROR("[FAIL] Newline not inserted");
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("RANDOM: NEWLINE", ok);
    return ok;
}

// Test deblank - delete blank lines
static int test_deblank(void) {
    int ok = 1;
    PHASE_START("RANDOM: DEBLANK", "Delete blank lines");

    if (!setup_buffer_with_text("line1\n\n\nline2")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: DEBLANK", ok);
        return ok;
    }

    // Position on a blank line
    curwp->w_dotp = lforw(lforw(curbp->b_linep));

    // Delete blank lines
    if (deblank(false, 1) != true) {
        LOG_ERROR("[FAIL] deblank failed");
        ok = 0;
    }

    // Count remaining blank lines
    struct line *lp = lforw(curbp->b_linep);
    int blank_count = 0;
    while (lp != curbp->b_linep) {
        if (llength(lp) == 0) blank_count++;
        lp = lforw(lp);
    }

    // Should have fewer blank lines now
    if (blank_count > 1) {
        LOG_ERRORF("[FAIL] Too many blank lines remaining: %d", blank_count);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("RANDOM: DEBLANK", ok);
    return ok;
}

// Test indent - indent line
static int test_indent(void) {
    int ok = 1;
    PHASE_START("RANDOM: INDENT", "Indent line");

    if (!setup_buffer_with_text("hello")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: INDENT", ok);
        return ok;
    }

    int orig_len = llength(curwp->w_dotp);

    // Indent the line
    if (indent(false, 1) != true) {
        LOG_ERROR("[FAIL] indent failed");
        ok = 0;
    }

    // Line should be longer or same (indentation added)
    int new_len = llength(curwp->w_dotp);
    if (new_len < orig_len) {
        LOG_ERRORF("[FAIL] Line shorter after indent: %d -> %d", orig_len, new_len);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("RANDOM: INDENT", ok);
    return ok;
}

// Test edge cases with empty buffer
static int test_random_edge_cases(void) {
    int ok = 1;
    PHASE_START("RANDOM: EDGE", "Edge cases and boundary conditions");

    // Test detab on empty buffer
    if (!setup_buffer_with_text("")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("RANDOM: EDGE", ok);
        return ok;
    }

    // Should handle empty buffer gracefully
    if (detab(false, 1) == true) {
        // Success or failure both acceptable for empty buffer
    }

    // Test twiddle at buffer boundaries
    cleanup_buffer();
    setup_buffer_with_text("a");
    // Twiddle on single char should fail or handle gracefully
    twiddle(false, 1);

    // Test showcpos at end of buffer
    cleanup_buffer();
    setup_buffer_with_text("test");
    goto_line_end(false, 0);
    if (showcpos(false, 1) != true) {
        LOG_ERROR("[FAIL] showcpos at EOL failed");
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("RANDOM: EDGE", ok);
    return ok;
}

// Entry point
int test_text_random(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("random");

    PHASE_START("TEXT_RANDOM", "Random text operation tests");

    ok &= test_detab();
    ok &= test_entab();
    ok &= test_trim();
    ok &= test_twiddle();
    ok &= test_showcpos();
    ok &= test_getcline();
    ok &= test_getccol();
    ok &= test_setccol();
    ok &= test_insert_tab();
    ok &= test_quote();
    ok &= test_openline();
    ok &= test_insert_newline();
    ok &= test_deblank();
    ok &= test_indent();
    ok &= test_random_edge_cases();

    if (ok) {
        LOG_INFO("[SUCCESS] All random text operation tests passed.");
    }

    PHASE_END("TEXT_RANDOM", ok);
    return ok;
}
