// test_core_line.c - Unit tests for line operations
// Tests src/core/line.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "gapbuffer.h"

// Helper: setup a test buffer
static struct buffer *setup_test_buffer(const char *name) {
    struct buffer *bp = bfind((char *)name, true, 0);
    if (bp) {
        curbp = bp;
        curwp->w_bufp = bp;
        curwp->w_dotp = bp->b_linep;
        curwp->w_doto = 0;
    }
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
        (void)zotbuf(bp);
    }
}

// Test line allocation
static int test_line_alloc(void) {
    int ok = 1;
    PHASE_START("LINE: ALLOC", "Line allocation");

    struct line *lp = lalloc(32);
    if (!lp) {
        LOG_ERROR("[FAIL] lalloc returned nullptr");
        ok = 0;
        PHASE_END("LINE: ALLOC", ok);
        return ok;
    }

    // Verify storage was created
    if (!lp->storage) {
        LOG_ERROR("[FAIL] line has no storage backend");
        ok = 0;
    }

    // Initially empty
    if (llength(lp) != 0) {
        LOG_ERRORF("[FAIL] new line not empty: %d", llength(lp));
        ok = 0;
    }

    lfree(lp);

    PHASE_END("LINE: ALLOC", ok);
    return ok;
}

// Test linsert - insert characters
static int test_line_insert(void) {
    int ok = 1;
    PHASE_START("LINE: INSERT", "Character insertion");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer("test-linsert");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("LINE: INSERT", ok);
        return ok;
    }

    // Move to first real line
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Insert single character
    if (linsert(1, 'A') != true) {
        LOG_ERROR("[FAIL] linsert failed");
        ok = 0;
    }

    // Verify
    if (llength(curwp->w_dotp) != 1) {
        LOG_ERRORF("[FAIL] line length wrong: %d", llength(curwp->w_dotp));
        ok = 0;
    }

    if (lgetc(curwp->w_dotp, 0) != 'A') {
        LOG_ERRORF("[FAIL] wrong char: '%c'", lgetc(curwp->w_dotp, 0));
        ok = 0;
    }

    // Insert multiple
    if (linsert(3, 'B') != true) {
        LOG_ERROR("[FAIL] linsert(3) failed");
        ok = 0;
    }

    if (llength(curwp->w_dotp) != 4) {
        LOG_ERRORF("[FAIL] line length after multi-insert: %d", llength(curwp->w_dotp));
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("LINE: INSERT", ok);
    return ok;
}

// Test linstr - insert string
static int test_line_insert_string(void) {
    int ok = 1;
    PHASE_START("LINE: LINSTR", "String insertion");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer("test-linstr");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("LINE: LINSTR", ok);
        return ok;
    }

    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Insert string
    if (linstr("Hello World") != true) {
        LOG_ERROR("[FAIL] linstr failed");
        ok = 0;
    }

    int len = llength(curwp->w_dotp);
    if (len != 11) {
        LOG_ERRORF("[FAIL] line length: %d != 11", len);
        ok = 0;
    }

    // Verify content
    if (lgetc(curwp->w_dotp, 0) != 'H' || lgetc(curwp->w_dotp, 6) != 'W') {
        LOG_ERROR("[FAIL] content mismatch");
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("LINE: LINSTR", ok);
    return ok;
}

// Test lnewline - create new line
static int test_line_newline(void) {
    int ok = 1;
    PHASE_START("LINE: NEWLINE", "Newline insertion");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer("test-newline");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("LINE: NEWLINE", ok);
        return ok;
    }

    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Insert text then newline
    linstr("Line 1");
    struct line *original_line = curwp->w_dotp;
    int original_len = llength(original_line);

    if (lnewline() != true) {
        LOG_ERROR("[FAIL] lnewline failed");
        ok = 0;
    }

    // lnewline splits the line: content before cursor goes to NEW line above,
    // cursor stays on current line (now empty after the split).
    // Original line struct is now the "after" portion (empty in this case).

    // Current line should now be empty (content moved to line above)
    if (llength(curwp->w_dotp) != 0) {
        LOG_ERRORF("[FAIL] current line not empty after newline: %d bytes", llength(curwp->w_dotp));
        ok = 0;
    }

    // The line BEFORE current should contain the original content
    struct line *prev_line = lback(curwp->w_dotp);
    if (prev_line == curbp->b_linep || llength(prev_line) != original_len) {
        LOG_ERROR("[FAIL] previous line doesn't have original content");
        ok = 0;
    }

    // Cursor should be at beginning of current line
    if (curwp->w_doto != 0) {
        LOG_ERRORF("[FAIL] cursor not at beginning: %d", curwp->w_doto);
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("LINE: NEWLINE", ok);
    return ok;
}

// Test ldelete - delete characters
static int test_line_delete(void) {
    int ok = 1;
    PHASE_START("LINE: DELETE", "Character deletion");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer("test-delete");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("LINE: DELETE", ok);
        return ok;
    }

    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Insert text
    linstr("ABCDEF");
    curwp->w_doto = 0;  // Go back to start

    // Delete 3 characters (no kill)
    if (ldelete(3, false) != true) {
        LOG_ERROR("[FAIL] ldelete failed");
        ok = 0;
    }

    // Should have "DEF" left
    int len = llength(curwp->w_dotp);
    if (len != 3) {
        LOG_ERRORF("[FAIL] length after delete: %d != 3", len);
        ok = 0;
    }

    if (lgetc(curwp->w_dotp, 0) != 'D') {
        LOG_ERRORF("[FAIL] first char after delete: '%c'", lgetc(curwp->w_dotp, 0));
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("LINE: DELETE", ok);
    return ok;
}

// Test ldelnewline - join lines
static int test_line_delnewline(void) {
    int ok = 1;
    PHASE_START("LINE: DELNEWLINE", "Join lines");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer("test-delnewline");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("LINE: DELNEWLINE", ok);
        return ok;
    }

    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Create two lines
    linstr("Hello");
    lnewline();
    linstr("World");

    // Go back to first line, end
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = llength(curwp->w_dotp);

    // Delete the newline (join lines)
    if (ldelnewline() != true) {
        LOG_ERROR("[FAIL] ldelnewline failed");
        ok = 0;
    }

    // Should now be "HelloWorld" on one line
    int len = llength(curwp->w_dotp);
    if (len != 10) {
        LOG_ERRORF("[FAIL] joined line length: %d != 10", len);
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("LINE: DELNEWLINE", ok);
    return ok;
}

// Test kill buffer operations
static int test_line_kill(void) {
    int ok = 1;
    PHASE_START("LINE: KILL", "Kill buffer operations");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer("test-kill");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("LINE: KILL", ok);
        return ok;
    }

    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Clear kill buffer
    kdelete();

    // Insert to kill buffer
    if (kinsert('A') != true) {
        LOG_ERROR("[FAIL] kinsert failed");
        ok = 0;
    }
    kinsert('B');
    kinsert('C');

    // Delete with kill flag
    linstr("XYZ");
    curwp->w_doto = 0;
    ldelete(3, true);  // Kill "XYZ"

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("LINE: KILL", ok);
    return ok;
}

// Test yank - yank text from kill buffer
static int test_line_yank(void) {
    int ok = 1;
    PHASE_START("LINE: YANK", "Yank from kill buffer");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer("test-yank");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("LINE: YANK", ok);
        return ok;
    }

    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Clear kill buffer and add some content
    kdelete();
    kinsert('T');
    kinsert('E');
    kinsert('S');
    kinsert('T');

    // Yank the content
    int initial_len = llength(curwp->w_dotp);
    if (yank(false, 1) != true) {
        LOG_ERROR("[FAIL] yank failed");
        ok = 0;
    }

    // Line should now have the yanked content
    int new_len = llength(curwp->w_dotp);
    if (new_len != initial_len + 4) {
        LOG_ERRORF("[FAIL] yank didn't insert content: %d != %d", new_len, initial_len + 4);
        ok = 0;
    }

    // Verify content
    if (lgetc(curwp->w_dotp, 0) != 'T' || lgetc(curwp->w_dotp, 3) != 'T') {
        LOG_ERROR("[FAIL] yanked content incorrect");
        ok = 0;
    }

    // Test yank with count
    (void)bclear(bp);
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Yank 3 times
    if (yank(false, 3) != true) {
        LOG_ERROR("[FAIL] yank with count failed");
        ok = 0;
    }

    // Should have "TESTTESTTEST" (4 * 3 = 12 chars)
    if (llength(curwp->w_dotp) != 12) {
        LOG_ERRORF("[FAIL] yank count didn't work: %d != 12", llength(curwp->w_dotp));
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("LINE: YANK", ok);
    return ok;
}

// Test yank with newlines
static int test_line_yank_multiline(void) {
    int ok = 1;
    PHASE_START("LINE: YANK_MULTI", "Yank multiline content");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer("test-yank-multi");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("LINE: YANK_MULTI", ok);
        return ok;
    }

    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Clear kill buffer and add multiline content
    kdelete();
    kinsert('L');
    kinsert('I');
    kinsert('N');
    kinsert('E');
    kinsert('1');
    kinsert('\n');
    kinsert('L');
    kinsert('I');
    kinsert('N');
    kinsert('E');
    kinsert('2');

    // Yank the multiline content
    if (yank(false, 1) != true) {
        LOG_ERROR("[FAIL] yank multiline failed");
        ok = 0;
    }

    // Should have created a new line
    struct line *first_line = curwp->w_dotp;
    struct line *prev_line = lback(first_line);

    // Previous line should have "LINE1"
    if (prev_line == bp->b_linep || llength(prev_line) != 5) {
        LOG_ERROR("[FAIL] multiline yank first line wrong");
        ok = 0;
    } else {
        if (lgetc(prev_line, 0) != 'L' || lgetc(prev_line, 4) != '1') {
            LOG_ERROR("[FAIL] multiline yank first line content wrong");
            ok = 0;
        }
    }

    // Current line should have "LINE2"
    if (llength(curwp->w_dotp) != 5) {
        LOG_ERRORF("[FAIL] multiline yank second line wrong: %d != 5", llength(curwp->w_dotp));
        ok = 0;
    } else {
        if (lgetc(curwp->w_dotp, 0) != 'L' || lgetc(curwp->w_dotp, 4) != '2') {
            LOG_ERROR("[FAIL] multiline yank second line content wrong");
            ok = 0;
        }
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("LINE: YANK_MULTI", ok);
    return ok;
}

// Test yankpop - cycle through kill ring
static int test_line_yankpop(void) {
    int ok = 1;
    PHASE_START("LINE: YANKPOP", "Yank pop kill ring cycling");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer("test-yankpop");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("LINE: YANKPOP", ok);
        return ok;
    }

    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Test yankpop without prior yank - should fail
    lastflag = 0;  // Clear yank flag
    if (yankpop(false, 1) == true) {
        LOG_ERROR("[FAIL] yankpop succeeded without prior yank");
        ok = 0;
    }

    // Simulate a yank first
    kdelete();
    kinsert('A');
    kinsert('B');
    kinsert('C');

    if (yank(false, 1) != true) {
        LOG_ERROR("[FAIL] initial yank failed");
        ok = 0;
    }

    // Now lastflag should have CFYANK
    // Note: yankpop expects kill ring to have multiple entries
    // For a proper test, we'd need to populate the kill ring,
    // but this tests the basic precondition check

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("LINE: YANKPOP", ok);
    return ok;
}

// Test kill ring edge cases
static int test_kill_ring_edge(void) {
    int ok = 1;
    PHASE_START("LINE: KILLRING_EDGE", "Kill ring edge cases");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer("test-killring");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("LINE: KILLRING_EDGE", ok);
        return ok;
    }

    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Test empty kill buffer yank
    kdelete();
    int len_before = llength(curwp->w_dotp);

    if (yank(false, 1) != true) {
        LOG_ERROR("[FAIL] yank with empty kill buffer failed");
        ok = 0;
    }

    // Should have no effect
    int len_after = llength(curwp->w_dotp);
    if (len_after != len_before) {
        LOG_ERRORF("[FAIL] empty yank changed buffer: %d -> %d", len_before, len_after);
        ok = 0;
    }

    // Test yank with n=0
    kinsert('X');
    if (yank(false, 0) != true) {
        LOG_ERROR("[FAIL] yank with n=0 failed");
        ok = 0;
    }

    // Should have no effect (n=0)
    if (llength(curwp->w_dotp) != len_before) {
        LOG_ERROR("[FAIL] yank n=0 changed buffer");
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("LINE: KILLRING_EDGE", ok);
    return ok;
}

// Entry point
int test_core_line(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("line");

    PHASE_START("LINE", "Line operation tests");

    ok &= test_line_alloc();
    ok &= test_line_insert();
    ok &= test_line_insert_string();
    ok &= test_line_newline();
    ok &= test_line_delete();
    ok &= test_line_delnewline();
    ok &= test_line_kill();
    ok &= test_line_yank();
    ok &= test_line_yank_multiline();
    ok &= test_line_yankpop();
    ok &= test_kill_ring_edge();

    if (ok) {
        LOG_INFO("[SUCCESS] All line tests passed.");
    }

    PHASE_END("LINE", ok);
    return ok;
}
