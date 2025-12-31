// test_text_region.c - Unit tests for region operations
// Tests src/text/region.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"

// Helper: setup a test buffer with content
static struct buffer *setup_region_buffer(const char *name) {
    struct buffer *bp = bfind((char *)name, true, 0);
    if (!bp) return NULL;

    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Add content
    linstr("Line one text");
    lnewline();
    linstr("Line two text");
    lnewline();
    linstr("Line three text");

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
        zotbuf(bp);
    }
}

// Test mark setting
static int test_region_setmark(void) {
    int ok = 1;
    PHASE_START("REGION: SETMARK", "Set mark");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_region_buffer("test-setmark");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("REGION: SETMARK", ok);
        return ok;
    }

    // Set mark
    if (setmark(false, 1) != true) {
        LOG_ERROR("[FAIL] setmark failed");
        ok = 0;
    }

    // Mark should be at current position
    if (curwp->w_markp != curwp->w_dotp) {
        LOG_ERROR("[FAIL] mark line mismatch");
        ok = 0;
    }

    if (curwp->w_marko != curwp->w_doto) {
        LOG_ERROR("[FAIL] mark offset mismatch");
        ok = 0;
    }

    cleanup_buffer(bp, oldbp);
    PHASE_END("REGION: SETMARK", ok);
    return ok;
}

// Test exchange point and mark
static int test_region_exchange(void) {
    int ok = 1;
    PHASE_START("REGION: EXCHANGE", "Exchange point and mark");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_region_buffer("test-exchange");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("REGION: EXCHANGE", ok);
        return ok;
    }

    // Set mark at beginning
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;
    setmark(false, 1);

    // Move to a different position
    curwp->w_doto = 5;

    struct line *orig_dotp = curwp->w_dotp;
    int orig_doto = curwp->w_doto;
    struct line *orig_markp = curwp->w_markp;
    int orig_marko = curwp->w_marko;

    // Exchange
    if (swapmark(false, 1) != true) {
        LOG_ERROR("[FAIL] swapmark failed");
        ok = 0;
    }

    // Point should now be where mark was
    if (curwp->w_dotp != orig_markp || curwp->w_doto != orig_marko) {
        LOG_ERROR("[FAIL] point not at old mark position");
        ok = 0;
    }

    // Mark should now be where point was
    if (curwp->w_markp != orig_dotp || curwp->w_marko != orig_doto) {
        LOG_ERROR("[FAIL] mark not at old point position");
        ok = 0;
    }

    cleanup_buffer(bp, oldbp);
    PHASE_END("REGION: EXCHANGE", ok);
    return ok;
}

// Test getregion function
static int test_region_getregion(void) {
    int ok = 1;
    PHASE_START("REGION: GETREGION", "Get region");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_region_buffer("test-getregion");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("REGION: GETREGION", ok);
        return ok;
    }

    // Set mark at beginning of line
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;
    setmark(false, 1);

    // Move forward
    curwp->w_doto = 8;

    // Get region
    struct region r;
    if (getregion(&r) != true) {
        LOG_ERROR("[FAIL] getregion failed");
        ok = 0;
    } else {
        // Region size should be 8 characters
        if (r.r_size != 8) {
            LOG_ERRORF("[FAIL] region size: %ld != 8", r.r_size);
            ok = 0;
        }
    }

    cleanup_buffer(bp, oldbp);
    PHASE_END("REGION: GETREGION", ok);
    return ok;
}

// Test kill region
static int test_region_kill(void) {
    int ok = 1;
    PHASE_START("REGION: KILL", "Kill region");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_region_buffer("test-region_kill");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("REGION: KILL", ok);
        return ok;
    }

    // Set mark at beginning
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;
    setmark(false, 1);

    // Move to end of line
    curwp->w_doto = llength(curwp->w_dotp);

    int orig_len = llength(curwp->w_dotp);

    // Kill region
    if (region_kill(false, 1) != true) {
        LOG_ERROR("[FAIL] region_kill failed");
        ok = 0;
    }

    // Line should be empty now
    if (llength(curwp->w_dotp) != 0) {
        LOG_ERRORF("[FAIL] line not empty after kill: %d", llength(curwp->w_dotp));
        ok = 0;
    }

    cleanup_buffer(bp, oldbp);
    PHASE_END("REGION: KILL", ok);
    return ok;
}

// Test copy region
static int test_region_copy(void) {
    int ok = 1;
    PHASE_START("REGION: COPY", "Copy region");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_region_buffer("test-region_copy");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("REGION: COPY", ok);
        return ok;
    }

    // Set mark at beginning
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;
    setmark(false, 1);

    // Move to position 5
    curwp->w_doto = 5;

    int orig_len = llength(curwp->w_dotp);

    // Copy region (should not modify buffer)
    if (region_copy(false, 1) != true) {
        LOG_ERROR("[FAIL] region_copy failed");
        ok = 0;
    }

    // Line length should be unchanged
    if (llength(curwp->w_dotp) != orig_len) {
        LOG_ERRORF("[FAIL] line modified by copy: %d != %d", llength(curwp->w_dotp), orig_len);
        ok = 0;
    }

    cleanup_buffer(bp, oldbp);
    PHASE_END("REGION: COPY", ok);
    return ok;
}

// Test lower region
static int test_region_lower(void) {
    int ok = 1;
    PHASE_START("REGION: LOWER", "Convert region to lowercase");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_region_buffer("test-lowerregion");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("REGION: LOWER", ok);
        return ok;
    }

    // Replace content with mixed case
    bclear(bp);
    linstr("HELLO WORLD");
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Set mark at beginning
    setmark(false, 1);

    // Move to end of "HELLO"
    curwp->w_doto = 5;

    // Lower the region
    if (lowerregion(false, 1) != true) {
        LOG_ERROR("[FAIL] lowerregion failed");
        ok = 0;
    }

    // Check that "HELLO" became "hello" but "WORLD" unchanged
    char buf[20];
    for (int i = 0; i < 11 && i < llength(curwp->w_dotp); i++) {
        buf[i] = lgetc(curwp->w_dotp, i);
    }
    buf[11] = '\0';

    if (strncmp(buf, "hello WORLD", 11) != 0) {
        LOG_ERRORF("[FAIL] Expected 'hello WORLD', got '%s'", buf);
        ok = 0;
    }

    cleanup_buffer(bp, oldbp);
    PHASE_END("REGION: LOWER", ok);
    return ok;
}

// Test upper region
static int test_region_upper(void) {
    int ok = 1;
    PHASE_START("REGION: UPPER", "Convert region to uppercase");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_region_buffer("test-upperregion");
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("REGION: UPPER", ok);
        return ok;
    }

    // Replace content with lowercase
    bclear(bp);
    linstr("hello world");
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Set mark at beginning
    setmark(false, 1);

    // Move to end of "hello"
    curwp->w_doto = 5;

    // Upper the region
    if (upperregion(false, 1) != true) {
        LOG_ERROR("[FAIL] upperregion failed");
        ok = 0;
    }

    // Check that "hello" became "HELLO" but "world" unchanged
    char buf[20];
    for (int i = 0; i < 11 && i < llength(curwp->w_dotp); i++) {
        buf[i] = lgetc(curwp->w_dotp, i);
    }
    buf[11] = '\0';

    if (strncmp(buf, "HELLO world", 11) != 0) {
        LOG_ERRORF("[FAIL] Expected 'HELLO world', got '%s'", buf);
        ok = 0;
    }

    cleanup_buffer(bp, oldbp);
    PHASE_END("REGION: UPPER", ok);
    return ok;
}

// Entry point
int test_text_region(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("region");

    PHASE_START("REGION", "Region operation tests");

    ok &= test_region_setmark();
    ok &= test_region_exchange();
    ok &= test_region_getregion();
    ok &= test_region_kill();
    ok &= test_region_copy();
    ok &= test_region_lower();
    ok &= test_region_upper();

    if (ok) {
        LOG_INFO("[SUCCESS] All region tests passed.");
    }

    PHASE_END("REGION", ok);
    return ok;
}
