// test_text_word.c - Unit tests for word operations
// Tests src/text/word.c: upperword, lowerword, capword, fillpara

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"

// Helper to setup buffer with text
static int setup_buffer_with_text(const char *text) {
    struct buffer *oldbp = curbp;

    struct buffer *bp = bfind("test-word", true, 0);
    if (!bp) return 0;

    (void)bclear(bp);
    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = bp->b_linep;
    curwp->w_doto = 0;

    linstr(text);

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
    if (curbp && strcmp(curbp->b_bname, "test-word") == 0) {
        curbp->b_flag &= ~BFCHG;
        struct buffer *bp = curbp;
        curbp = bfind("main", false, 0);
        if (curbp) curwp->w_bufp = curbp;
        (void)zotbuf(bp);
    }
}

// Test upperword function
static int test_upperword(void) {
    int ok = 1;
    PHASE_START("WORD: UPPER", "Convert words to uppercase");

    if (!setup_buffer_with_text("hello world test")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("WORD: UPPER", ok);
        return ok;
    }

    // Convert first word to upper
    if (upperword(false, 1) != true) {
        LOG_ERROR("[FAIL] upperword failed");
        ok = 0;
    }

    const char *content = get_buffer_content();
    if (strncmp(content, "HELLO world", 11) != 0) {
        LOG_ERRORF("[FAIL] Expected 'HELLO world', got '%s'", content);
        ok = 0;
    }

    // Convert second word
    if (upperword(false, 1) != true) {
        LOG_ERROR("[FAIL] upperword second call failed");
        ok = 0;
    }

    content = get_buffer_content();
    if (strncmp(content, "HELLO WORLD test", 16) != 0) {
        LOG_ERRORF("[FAIL] Expected 'HELLO WORLD test', got '%s'", content);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("WORD: UPPER", ok);
    return ok;
}

// Test lowerword function
static int test_lowerword(void) {
    int ok = 1;
    PHASE_START("WORD: LOWER", "Convert words to lowercase");

    if (!setup_buffer_with_text("HELLO WORLD TEST")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("WORD: LOWER", ok);
        return ok;
    }

    // Convert first word to lower
    if (lowerword(false, 1) != true) {
        LOG_ERROR("[FAIL] lowerword failed");
        ok = 0;
    }

    const char *content = get_buffer_content();
    if (strncmp(content, "hello WORLD", 11) != 0) {
        LOG_ERRORF("[FAIL] Expected 'hello WORLD', got '%s'", content);
        ok = 0;
    }

    // Convert multiple words with n=2
    cleanup_buffer();
    setup_buffer_with_text("FOO BAR BAZ");
    if (lowerword(false, 2) != true) {
        LOG_ERROR("[FAIL] lowerword with n=2 failed");
        ok = 0;
    }

    content = get_buffer_content();
    if (strncmp(content, "foo bar BAZ", 11) != 0) {
        LOG_ERRORF("[FAIL] Expected 'foo bar BAZ', got '%s'", content);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("WORD: LOWER", ok);
    return ok;
}

// Test capword function
static int test_capword(void) {
    int ok = 1;
    PHASE_START("WORD: CAP", "Capitalize words");

    if (!setup_buffer_with_text("hello world test")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("WORD: CAP", ok);
        return ok;
    }

    // Capitalize first word
    if (capword(false, 1) != true) {
        LOG_ERROR("[FAIL] capword failed");
        ok = 0;
    }

    const char *content = get_buffer_content();
    if (strncmp(content, "Hello world", 11) != 0) {
        LOG_ERRORF("[FAIL] Expected 'Hello world', got '%s'", content);
        ok = 0;
    }

    // Test with mixed case input
    cleanup_buffer();
    setup_buffer_with_text("hELLo WoRLd");
    if (capword(false, 2) != true) {
        LOG_ERROR("[FAIL] capword with mixed case failed");
        ok = 0;
    }

    content = get_buffer_content();
    if (strncmp(content, "Hello World", 11) != 0) {
        LOG_ERRORF("[FAIL] Expected 'Hello World', got '%s'", content);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("WORD: CAP", ok);
    return ok;
}

// Test move_word_backward function
static int test_backword(void) {
    int ok = 1;
    PHASE_START("WORD: BACK", "Move backward by words");

    if (!setup_buffer_with_text("one two three four")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("WORD: BACK", ok);
        return ok;
    }

    // Move to end of line
    goto_line_end(false, 0);
    int end_pos = curwp->w_doto;

    // Move back one word
    if (move_word_backward(false, 1) != true) {
        LOG_ERROR("[FAIL] move_word_backward failed");
        ok = 0;
    }

    // Should be at start of "four"
    if (curwp->w_doto >= end_pos) {
        LOG_ERROR("[FAIL] move_word_backward didn't move cursor back");
        ok = 0;
    }

    // Move back two more words
    if (move_word_backward(false, 2) != true) {
        LOG_ERROR("[FAIL] move_word_backward n=2 failed");
        ok = 0;
    }

    // Should be near start of buffer
    if (curwp->w_doto > 5) {
        LOG_ERROR("[FAIL] move_word_backward didn't reach expected position");
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("WORD: BACK", ok);
    return ok;
}

// Test move_word_forward function
static int test_forwword(void) {
    int ok = 1;
    PHASE_START("WORD: FORW", "Move forward by words");

    if (!setup_buffer_with_text("one two three four")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("WORD: FORW", ok);
        return ok;
    }

    int start_pos = curwp->w_doto;

    // Move forward one word
    if (move_word_forward(false, 1) != true) {
        LOG_ERROR("[FAIL] move_word_forward failed");
        ok = 0;
    }

    // Should have moved forward
    if (curwp->w_doto <= start_pos) {
        LOG_ERROR("[FAIL] move_word_forward didn't move cursor forward");
        ok = 0;
    }

    // Move forward two more words
    int mid_pos = curwp->w_doto;
    if (move_word_forward(false, 2) != true) {
        LOG_ERROR("[FAIL] move_word_forward n=2 failed");
        ok = 0;
    }

    if (curwp->w_doto <= mid_pos) {
        LOG_ERROR("[FAIL] move_word_forward n=2 didn't move forward enough");
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("WORD: FORW", ok);
    return ok;
}

// Test fillpara function
static int test_fillpara(void) {
    int ok = 1;
    PHASE_START("WORD: FILLPARA", "Fill paragraph to column width");

    // Set fill column
    fillcol = 40;

    // Create long paragraph
    const char *long_text = "This is a very long line that should be wrapped when we call fillpara because it exceeds the fill column width.";

    if (!setup_buffer_with_text(long_text)) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("WORD: FILLPARA", ok);
        return ok;
    }

    // Call fillpara
    if (fillpara(false, 1) != true) {
        LOG_ERROR("[FAIL] fillpara failed");
        ok = 0;
    }

    // Check that text was wrapped
    const char *content = get_buffer_content();
    int newline_count = 0;
    for (const char *p = content; *p; p++) {
        if (*p == '\n') newline_count++;
    }

    if (newline_count < 1) {
        LOG_ERRORF("[FAIL] fillpara didn't wrap long line (newlines=%d)", newline_count);
        ok = 0;
    }

    // Verify no line exceeds fill column (approximately)
    const char *line_start = content;
    for (const char *p = content; *p; p++) {
        if (*p == '\n' || *(p+1) == '\0') {
            int line_len = p - line_start;
            // Allow some overage for words that don't fit
            if (line_len > fillcol + 20) {
                LOG_ERRORF("[FAIL] Line too long: %d chars (fillcol=%d)", line_len, fillcol);
                ok = 0;
            }
            line_start = p + 1;
        }
    }

    cleanup_buffer();
    PHASE_END("WORD: FILLPARA", ok);
    return ok;
}

// Test delete_word_forward - delete forward word
static int test_delfword(void) {
    int ok = 1;
    PHASE_START("WORD: DELFWORD", "Delete forward word");

    if (!setup_buffer_with_text("hello world test")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("WORD: DELFWORD", ok);
        return ok;
    }

    // Delete first word from beginning
    if (delete_word_forward(false, 1) != true) {
        LOG_ERROR("[FAIL] delete_word_forward failed");
        ok = 0;
    }

    const char *content = get_buffer_content();
    // Should have deleted "hello" (and possibly space)
    if (strstr(content, "hello") != nullptr) {
        LOG_ERRORF("[FAIL] Word not deleted: '%s'", content);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("WORD: DELFWORD", ok);
    return ok;
}

// Test delete_word_backward - delete backward word
static int test_delbword(void) {
    int ok = 1;
    PHASE_START("WORD: DELBWORD", "Delete backward word");

    if (!setup_buffer_with_text("hello world test")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("WORD: DELBWORD", ok);
        return ok;
    }

    // Move to end
    goto_line_end(false, 0);

    // Delete backward word
    if (delete_word_backward(false, 1) != true) {
        LOG_ERROR("[FAIL] delete_word_backward failed");
        ok = 0;
    }

    const char *content = get_buffer_content();
    // Should have deleted "test"
    if (strstr(content, "test") != nullptr) {
        LOG_ERRORF("[FAIL] Word not deleted: '%s'", content);
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("WORD: DELBWORD", ok);
    return ok;
}

// Test wordcount
static int test_wordcount(void) {
    int ok = 1;
    PHASE_START("WORD: WORDCOUNT", "Count words in buffer");

    if (!setup_buffer_with_text("one two three four five")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("WORD: WORDCOUNT", ok);
        return ok;
    }

    // wordcount requires a region (mark to point), so set mark at start
    setmark(false, 0);
    // Move to end of buffer to define region spanning all text
    curwp->w_dotp = lback(curbp->b_linep);
    curwp->w_doto = llength(curwp->w_dotp);

    // wordcount should succeed (displays result in message line)
    if (wordcount(false, 1) != true) {
        LOG_ERROR("[FAIL] wordcount failed");
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("WORD: WORDCOUNT", ok);
    return ok;
}

// Test inword helper
static int test_inword(void) {
    int ok = 1;
    PHASE_START("WORD: INWORD", "Check if cursor is in a word");

    if (!setup_buffer_with_text("hello world")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("WORD: INWORD", ok);
        return ok;
    }

    // At start of word
    if (!inword()) {
        LOG_ERROR("[FAIL] Should be in word at start");
        ok = 0;
    }

    // Move to space
    curwp->w_doto = 5;
    if (inword()) {
        LOG_ERROR("[FAIL] Should not be in word at space");
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("WORD: INWORD", ok);
    return ok;
}

// Test edge cases
static int test_word_edge_cases(void) {
    int ok = 1;
    PHASE_START("WORD: EDGE", "Edge cases and boundary conditions");

    // Test empty buffer
    if (!setup_buffer_with_text("")) {
        LOG_ERROR("[FAIL] Failed to setup buffer");
        ok = 0;
        PHASE_END("WORD: EDGE", ok);
        return ok;
    }

    // upperword on empty should not crash
    if (upperword(false, 1) == true) {
        // Expected to return false or handle gracefully
    }

    // Test single character
    cleanup_buffer();
    setup_buffer_with_text("a");
    if (upperword(false, 1) != true) {
        LOG_ERROR("[FAIL] upperword on single char failed");
        ok = 0;
    }

    const char *content = get_buffer_content();
    if (content[0] != 'A') {
        LOG_ERRORF("[FAIL] Single char not uppercased: '%c'", content[0]);
        ok = 0;
    }

    // Test word at end of buffer
    cleanup_buffer();
    setup_buffer_with_text("test");
    if (lowerword(false, 1) != true) {
        LOG_ERROR("[FAIL] lowerword at buffer end failed");
        ok = 0;
    }

    // Test negative count (should return false)
    cleanup_buffer();
    setup_buffer_with_text("TEST");
    if (upperword(false, -1) != false) {
        LOG_ERROR("[FAIL] upperword should reject negative count");
        ok = 0;
    }

    cleanup_buffer();
    PHASE_END("WORD: EDGE", ok);
    return ok;
}

// Entry point
int test_text_word(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("word");

    PHASE_START("TEXT_WORD", "Word operation tests");

    ok &= test_upperword();
    ok &= test_lowerword();
    ok &= test_capword();
    ok &= test_backword();
    ok &= test_forwword();
    ok &= test_fillpara();
    ok &= test_delfword();
    ok &= test_delbword();
    ok &= test_wordcount();
    ok &= test_inword();
    ok &= test_word_edge_cases();

    if (ok) {
        LOG_INFO("[SUCCESS] All word operation tests passed.");
    }

    PHASE_END("TEXT_WORD", ok);
    return ok;
}
