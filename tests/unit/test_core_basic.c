// test_core_basic.c - Unit tests for basic cursor movement operations
// Tests src/core/basic.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"

// Helper: setup a test buffer with content
static struct buffer *setup_test_buffer_with_lines(const char *name, int num_lines) {
    struct buffer *bp = bfind((char *)name, true, 0);
    if (!bp) return NULL;

    struct buffer *oldbp = curbp;
    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = bp->b_linep;
    curwp->w_doto = 0;

    // Add content
    for (int i = 1; i <= num_lines; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Line %d content here", i);

        curwp->w_dotp = lforw(bp->b_linep);
        curwp->w_doto = 0;
        linstr(buf);
        if (i < num_lines) lnewline();
    }

    // Position at start
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    curbp = oldbp;
    return bp;
}

// Helper: cleanup test buffer
static void cleanup_test_buffer(struct buffer *bp, struct buffer *oldbp) {
    if (oldbp) {
        curbp = oldbp;
        curwp->w_bufp = oldbp;
    }
    if (bp) {
        bp->b_flag &= ~BFCHG;
        zotbuf(bp);
    }
}

// Test goto_line_start - go to beginning of line
static int test_gotobol(void) {
    int ok = 1;
    PHASE_START("BASIC: GOTOBOL", "Go to beginning of line");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer_with_lines("test-goto_line_start", 3);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BASIC: GOTOBOL", ok);
        return ok;
    }

    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = lforw(bp->b_linep);

    // Move to middle of line
    curwp->w_doto = 5;

    // Go to beginning
    if (goto_line_start(false, 1) != true) {
        LOG_ERROR("[FAIL] goto_line_start failed");
        ok = 0;
    }

    // Should be at column 0
    if (curwp->w_doto != 0) {
        LOG_ERRORF("[FAIL] not at beginning: doto=%d", curwp->w_doto);
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("BASIC: GOTOBOL", ok);
    return ok;
}

// Test goto_line_end - go to end of line
static int test_gotoeol(void) {
    int ok = 1;
    PHASE_START("BASIC: GOTOEOL", "Go to end of line");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer_with_lines("test-goto_line_end", 3);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BASIC: GOTOEOL", ok);
        return ok;
    }

    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    int expected_len = llength(curwp->w_dotp);

    // Go to end
    if (goto_line_end(false, 1) != true) {
        LOG_ERROR("[FAIL] goto_line_end failed");
        ok = 0;
    }

    // Should be at end of line
    if (curwp->w_doto != expected_len) {
        LOG_ERRORF("[FAIL] not at end: doto=%d, expected=%d", curwp->w_doto, expected_len);
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("BASIC: GOTOEOL", ok);
    return ok;
}

// Test move_char_forward - move forward by characters
static int test_forwchar(void) {
    int ok = 1;
    PHASE_START("BASIC: FORWCHAR", "Move forward by characters");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer_with_lines("test-move_char_forward", 2);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BASIC: FORWCHAR", ok);
        return ok;
    }

    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Move forward 3 characters
    if (move_char_forward(false, 3) != true) {
        LOG_ERROR("[FAIL] move_char_forward(3) failed");
        ok = 0;
    }

    if (curwp->w_doto != 3) {
        LOG_ERRORF("[FAIL] position wrong: %d != 3", curwp->w_doto);
        ok = 0;
    }

    // Move to end of line and try to go to next line
    goto_line_end(false, 1);
    struct line *first_line = curwp->w_dotp;

    if (move_char_forward(false, 1) != true) {
        LOG_ERROR("[FAIL] move_char_forward to next line failed");
        ok = 0;
    }

    // Should be on second line, position 0
    if (curwp->w_dotp == first_line) {
        LOG_ERROR("[FAIL] did not move to next line");
        ok = 0;
    }

    if (curwp->w_doto != 0) {
        LOG_ERRORF("[FAIL] not at beginning of next line: %d", curwp->w_doto);
        ok = 0;
    }

    // Test negative n (should call move_char_backward)
    if (move_char_forward(false, -2) != true) {
        LOG_ERROR("[FAIL] move_char_forward with negative n failed");
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("BASIC: FORWCHAR", ok);
    return ok;
}

// Test move_char_backward - move backward by characters
static int test_backchar(void) {
    int ok = 1;
    PHASE_START("BASIC: BACKCHAR", "Move backward by characters");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer_with_lines("test-move_char_backward", 2);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BASIC: BACKCHAR", ok);
        return ok;
    }

    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 5;

    // Move backward 2 characters
    if (move_char_backward(false, 2) != true) {
        LOG_ERROR("[FAIL] move_char_backward(2) failed");
        ok = 0;
    }

    if (curwp->w_doto != 3) {
        LOG_ERRORF("[FAIL] position wrong: %d != 3", curwp->w_doto);
        ok = 0;
    }

    // Move to beginning and try to go to previous line (should fail at buffer start)
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    if (move_char_backward(false, 1) == true) {
        LOG_ERROR("[FAIL] move_char_backward past buffer start should fail");
        ok = 0;
    }

    // Test negative n (should call move_char_forward)
    curwp->w_doto = 0;
    if (move_char_backward(false, -3) != true) {
        LOG_ERROR("[FAIL] move_char_backward with negative n failed");
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("BASIC: BACKCHAR", ok);
    return ok;
}

// Test cursor_down - move forward by lines
static int test_forwline(void) {
    int ok = 1;
    PHASE_START("BASIC: FORWLINE", "Move forward by lines");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer_with_lines("test-cursor_down", 5);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BASIC: FORWLINE", ok);
        return ok;
    }

    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    struct line *first_line = curwp->w_dotp;

    // Move forward 2 lines
    if (cursor_down(false, 2) != true) {
        LOG_ERROR("[FAIL] cursor_down(2) failed");
        ok = 0;
    }

    // Should be on third line
    if (curwp->w_dotp == first_line) {
        LOG_ERROR("[FAIL] still on first line");
        ok = 0;
    }

    // Count how many lines we moved
    int moved = 0;
    struct line *lp = first_line;
    while (lp != curwp->w_dotp && lp != bp->b_linep) {
        moved++;
        lp = lforw(lp);
    }

    if (moved != 2) {
        LOG_ERRORF("[FAIL] moved %d lines, expected 2", moved);
        ok = 0;
    }

    // Test negative n (should call cursor_up)
    if (cursor_down(false, -1) != true) {
        LOG_ERROR("[FAIL] cursor_down with negative n failed");
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("BASIC: FORWLINE", ok);
    return ok;
}

// Test cursor_up - move backward by lines
static int test_backline(void) {
    int ok = 1;
    PHASE_START("BASIC: BACKLINE", "Move backward by lines");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer_with_lines("test-cursor_up", 5);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BASIC: BACKLINE", ok);
        return ok;
    }

    curbp = bp;
    curwp->w_bufp = bp;

    // Move to third line
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_dotp = lforw(curwp->w_dotp);
    curwp->w_dotp = lforw(curwp->w_dotp);
    curwp->w_doto = 0;

    struct line *third_line = curwp->w_dotp;

    // Move back 1 line
    if (cursor_up(false, 1) != true) {
        LOG_ERROR("[FAIL] cursor_up(1) failed");
        ok = 0;
    }

    // Should be on second line
    if (curwp->w_dotp == third_line) {
        LOG_ERROR("[FAIL] still on third line");
        ok = 0;
    }

    // Test negative n (should call cursor_down)
    if (cursor_up(false, -2) != true) {
        LOG_ERROR("[FAIL] cursor_up with negative n failed");
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("BASIC: BACKLINE", ok);
    return ok;
}

// Test goto_buffer_start - go to beginning of buffer
static int test_gotobob(void) {
    int ok = 1;
    PHASE_START("BASIC: GOTOBOB", "Go to beginning of buffer");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer_with_lines("test-goto_buffer_start", 5);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BASIC: GOTOBOB", ok);
        return ok;
    }

    curbp = bp;
    curwp->w_bufp = bp;

    // Move to middle of buffer
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_dotp = lforw(curwp->w_dotp);
    curwp->w_doto = 5;

    // Go to beginning
    if (goto_buffer_start(false, 1) != true) {
        LOG_ERROR("[FAIL] goto_buffer_start failed");
        ok = 0;
    }

    // Should be at first line, column 0
    if (curwp->w_dotp != lforw(bp->b_linep)) {
        LOG_ERROR("[FAIL] not at first line");
        ok = 0;
    }

    if (curwp->w_doto != 0) {
        LOG_ERRORF("[FAIL] not at column 0: %d", curwp->w_doto);
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("BASIC: GOTOBOB", ok);
    return ok;
}

// Test goto_buffer_end - go to end of buffer
static int test_gotoeob(void) {
    int ok = 1;
    PHASE_START("BASIC: GOTOEOB", "Go to end of buffer");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer_with_lines("test-goto_buffer_end", 5);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BASIC: GOTOEOB", ok);
        return ok;
    }

    curbp = bp;
    curwp->w_bufp = bp;

    // Start at beginning
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Go to end
    if (goto_buffer_end(false, 1) != true) {
        LOG_ERROR("[FAIL] goto_buffer_end failed");
        ok = 0;
    }

    // Should be at buffer header line (end of buffer marker)
    if (curwp->w_dotp != bp->b_linep) {
        LOG_ERROR("[FAIL] not at end of buffer");
        ok = 0;
    }

    if (curwp->w_doto != 0) {
        LOG_ERRORF("[FAIL] not at column 0: %d", curwp->w_doto);
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("BASIC: GOTOEOB", ok);
    return ok;
}

// Test gotoline - go to specific line number
static int test_gotoline(void) {
    int ok = 1;
    PHASE_START("BASIC: GOTOLINE", "Go to specific line");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer_with_lines("test-gotoline", 10);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BASIC: GOTOLINE", ok);
        return ok;
    }

    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Go to line 5
    if (gotoline(true, 5) != true) {
        LOG_ERROR("[FAIL] gotoline(5) failed");
        ok = 0;
    }

    // Count which line we're on
    int line_num = 1;
    struct line *lp = lforw(bp->b_linep);
    while (lp != curwp->w_dotp && lp != bp->b_linep) {
        line_num++;
        lp = lforw(lp);
    }

    if (line_num != 5) {
        LOG_ERRORF("[FAIL] on line %d, expected 5", line_num);
        ok = 0;
    }

    // Test line 0 (should go to end)
    if (gotoline(true, 0) != true) {
        LOG_ERROR("[FAIL] gotoline(0) failed");
        ok = 0;
    }

    // Should be at end of buffer
    if (curwp->w_dotp != bp->b_linep) {
        LOG_ERROR("[FAIL] gotoline(0) did not go to end");
        ok = 0;
    }

    // Test negative line (should fail)
    if (gotoline(true, -5) == true) {
        LOG_ERROR("[FAIL] gotoline(-5) should fail");
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("BASIC: GOTOLINE", ok);
    return ok;
}

// Test move_page_down/move_page_up - page up/down
static int test_paging(void) {
    int ok = 1;
    PHASE_START("BASIC: PAGING", "Page forward and backward");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer_with_lines("test-paging", 50);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BASIC: PAGING", ok);
        return ok;
    }

    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_linep = lforw(bp->b_linep);
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Ensure window has reasonable size
    if (curwp->w_ntrows < 5) {
        curwp->w_ntrows = 20;
    }

    struct line *first_linep = curwp->w_linep;

    // Page forward
    if (move_page_down(false, 1) != true) {
        LOG_ERROR("[FAIL] move_page_down failed");
        ok = 0;
    }

    // w_linep should have moved
    if (curwp->w_linep == first_linep) {
        LOG_ERROR("[FAIL] window did not scroll forward");
        ok = 0;
    }

    struct line *scrolled_linep = curwp->w_linep;

    // Page backward
    if (move_page_up(false, 1) != true) {
        LOG_ERROR("[FAIL] move_page_up failed");
        ok = 0;
    }

    // Should have scrolled back
    if (curwp->w_linep == scrolled_linep) {
        LOG_ERROR("[FAIL] window did not scroll backward");
        ok = 0;
    }

    // Test with explicit count
    if (move_page_down(true, 2) != true) {
        LOG_ERROR("[FAIL] move_page_down(2) failed");
        ok = 0;
    }

    // Test negative n
    if (move_page_down(true, -1) != true) {
        LOG_ERROR("[FAIL] move_page_down(-1) failed");
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("BASIC: PAGING", ok);
    return ok;
}

// Test setmark/swapmark - mark operations
static int test_mark_operations(void) {
    int ok = 1;
    PHASE_START("BASIC: MARK", "Mark set and swap");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer_with_lines("test-mark", 5);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BASIC: MARK", ok);
        return ok;
    }

    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 5;

    // Initially no mark
    curwp->w_markp = NULL;

    // Set mark
    if (setmark(false, 1) != true) {
        LOG_ERROR("[FAIL] setmark failed");
        ok = 0;
    }

    // Mark should be set to current position
    if (curwp->w_markp != curwp->w_dotp) {
        LOG_ERROR("[FAIL] mark line not set correctly");
        ok = 0;
    }

    if (curwp->w_marko != 5) {
        LOG_ERRORF("[FAIL] mark offset not set: %d != 5", curwp->w_marko);
        ok = 0;
    }

    // Move to different position
    curwp->w_dotp = lforw(curwp->w_dotp);
    curwp->w_doto = 10;

    struct line *new_dotp = curwp->w_dotp;
    int new_doto = curwp->w_doto;

    // Swap mark
    if (swapmark(false, 1) != true) {
        LOG_ERROR("[FAIL] swapmark failed");
        ok = 0;
    }

    // Should be back at mark position
    if (curwp->w_doto != 5) {
        LOG_ERRORF("[FAIL] not at mark offset after swap: %d != 5", curwp->w_doto);
        ok = 0;
    }

    // Mark should now be at previous position
    if (curwp->w_markp != new_dotp || curwp->w_marko != new_doto) {
        LOG_ERROR("[FAIL] mark not updated after swap");
        ok = 0;
    }

    // Test swapmark with no mark set
    curwp->w_markp = NULL;
    if (swapmark(false, 1) == true) {
        LOG_ERROR("[FAIL] swapmark should fail with no mark");
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("BASIC: MARK", ok);
    return ok;
}

// Test goto_para_start/goto_para_end - paragraph navigation
static int test_paragraph_navigation(void) {
    int ok = 1;
    PHASE_START("BASIC: PARAGRAPH", "Paragraph navigation");

    struct buffer *oldbp = curbp;
    struct buffer *bp = bfind("test-para", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BASIC: PARAGRAPH", ok);
        return ok;
    }

    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = bp->b_linep;
    curwp->w_doto = 0;

    // Create paragraphs separated by blank lines
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    linstr("First paragraph line one.");
    lnewline();
    linstr("First paragraph line two.");
    lnewline();
    lnewline();  // Blank line
    linstr("Second paragraph line one.");
    lnewline();
    linstr("Second paragraph line two.");
    lnewline();
    lnewline();  // Blank line
    linstr("Third paragraph.");

    // Position in first paragraph
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 5;

    // Go to end of paragraph
    if (goto_para_end(false, 1) != true) {
        LOG_ERROR("[FAIL] goto_para_end failed");
        ok = 0;
    }

    // Should have moved down (basic check)
    // Exact position depends on paragraph detection logic

    // Go to beginning of paragraph
    if (goto_para_start(false, 1) != true) {
        LOG_ERROR("[FAIL] goto_para_start failed");
        ok = 0;
    }

    // Test with negative n
    if (goto_para_end(false, -1) != true) {
        LOG_ERROR("[FAIL] goto_para_end with negative n failed");
        ok = 0;
    }

    if (goto_para_start(false, -1) != true) {
        LOG_ERROR("[FAIL] goto_para_start with negative n failed");
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("BASIC: PARAGRAPH", ok);
    return ok;
}

// Entry point
int test_core_basic(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("basic");

    PHASE_START("BASIC", "Basic cursor movement tests");

    ok &= test_gotobol();
    ok &= test_gotoeol();
    ok &= test_forwchar();
    ok &= test_backchar();
    ok &= test_forwline();
    ok &= test_backline();
    ok &= test_gotobob();
    ok &= test_gotoeob();
    ok &= test_gotoline();
    ok &= test_paging();
    ok &= test_mark_operations();
    ok &= test_paragraph_navigation();

    if (ok) {
        LOG_INFO("[SUCCESS] All basic cursor movement tests passed.");
    }

    PHASE_END("BASIC", ok);
    return ok;
}
