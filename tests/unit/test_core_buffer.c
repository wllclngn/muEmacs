// test_core_buffer.c - Unit tests for buffer management
// Tests src/core/buffer.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"

// Test buffer creation via bfind
static int test_buffer_create(void) {
    int ok = 1;
    PHASE_START("BUFFER: CREATE", "Buffer creation and lookup");

    // Create a new buffer
    struct buffer *bp = bfind("test-buffer-1", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] bfind returned nullptr for new buffer");
        ok = 0;
        PHASE_END("BUFFER: CREATE", ok);
        return ok;
    }

    // Verify buffer name
    if (strcmp(bp->b_bname, "test-buffer-1") != 0) {
        LOG_ERRORF("[FAIL] buffer name mismatch: '%s'", bp->b_bname);
        ok = 0;
    }

    // Verify buffer is in buffer list
    struct buffer *found = bfind("test-buffer-1", false, 0);
    if (found != bp) {
        LOG_ERROR("[FAIL] bfind didn't find existing buffer");
        ok = 0;
    }

    // Create another buffer
    struct buffer *bp2 = bfind("test-buffer-2", true, 0);
    if (!bp2 || bp2 == bp) {
        LOG_ERROR("[FAIL] second buffer creation failed");
        ok = 0;
    }

    // Cleanup - clear BFCHG to prevent "Discard changes?" prompt
    bp->b_flag &= ~BFCHG;
    bp2->b_flag &= ~BFCHG;
    (void)zotbuf(bp);
    (void)zotbuf(bp2);

    PHASE_END("BUFFER: CREATE", ok);
    return ok;
}

// Test buffer clearing
static int test_buffer_clear(void) {
    int ok = 1;
    PHASE_START("BUFFER: CLEAR", "Buffer clearing operations");

    struct buffer *bp = bfind("test-clear", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BUFFER: CLEAR", ok);
        return ok;
    }

    // Add some content to the buffer
    struct buffer *oldbp = curbp;
    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = bp->b_linep;
    curwp->w_doto = 0;

    // Insert text
    linstr("Line one");
    lnewline();
    linstr("Line two");
    lnewline();
    linstr("Line three");

    // Verify content was added
    int line_count = 0;
    struct line *lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        line_count++;
        lp = lforw(lp);
    }

    if (line_count < 3) {
        LOG_ERRORF("[FAIL] content not added: %d lines", line_count);
        ok = 0;
    }

    // Clear the buffer
    if (bclear(bp) != true) {
        LOG_ERROR("[FAIL] bclear failed");
        ok = 0;
    }

    // Verify buffer is empty (only header line)
    lp = lforw(bp->b_linep);
    if (lp != bp->b_linep) {
        LOG_ERROR("[FAIL] buffer not empty after clear");
        ok = 0;
    }

    // Restore and cleanup
    curbp = oldbp;
    curwp->w_bufp = oldbp;
    bp->b_flag &= ~BFCHG;
    (void)zotbuf(bp);

    PHASE_END("BUFFER: CLEAR", ok);
    return ok;
}

// Test buffer statistics
static int test_buffer_stats(void) {
    int ok = 1;
    PHASE_START("BUFFER: STATS", "Buffer statistics tracking");

    struct buffer *bp = bfind("test-stats", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BUFFER: STATS", ok);
        return ok;
    }

    // Setup
    struct buffer *oldbp = curbp;
    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = bp->b_linep;
    curwp->w_doto = 0;

    // Initially empty
    int lines, words;
    long bytes;
    buffer_get_stats_fast(bp, &lines, &bytes, &words);

    // Add content
    linstr("Hello world");
    lnewline();
    linstr("Testing stats");

    // Mark stats dirty and recompute
    buffer_mark_stats_dirty(bp);
    buffer_get_stats_fast(bp, &lines, &bytes, &words);

    if (lines < 2) {
        LOG_ERRORF("[FAIL] line count wrong: %d", lines);
        ok = 0;
    }

    if (bytes < 20) {
        LOG_ERRORF("[FAIL] byte count too low: %ld", bytes);
        ok = 0;
    }

    // Restore and cleanup
    curbp = oldbp;
    curwp->w_bufp = oldbp;
    bp->b_flag &= ~BFCHG;
    (void)zotbuf(bp);

    PHASE_END("BUFFER: STATS", ok);
    return ok;
}

// Test buffer modification flag
static int test_buffer_modified(void) {
    int ok = 1;
    PHASE_START("BUFFER: MODIFIED", "Buffer modification tracking");

    struct buffer *bp = bfind("test-modified", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BUFFER: MODIFIED", ok);
        return ok;
    }

    // New buffer should not be modified
    if (bp->b_flag & BFCHG) {
        LOG_ERROR("[FAIL] new buffer has BFCHG flag");
        ok = 0;
    }

    // Setup for editing
    struct buffer *oldbp = curbp;
    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = bp->b_linep;
    curwp->w_doto = 0;

    // Make a change
    linstr("Modified");

    // Should now be modified
    if (!(bp->b_flag & BFCHG)) {
        LOG_ERROR("[FAIL] buffer not marked as changed after edit");
        ok = 0;
    }

    // Restore and cleanup
    curbp = oldbp;
    curwp->w_bufp = oldbp;
    bp->b_flag &= ~BFCHG;
    (void)zotbuf(bp);

    PHASE_END("BUFFER: MODIFIED", ok);
    return ok;
}

// Test anycb function
static int test_buffer_anycb(void) {
    int ok = 1;
    PHASE_START("BUFFER: ANYCB", "Check for changed buffers");

    // Create unmodified buffer
    struct buffer *bp = bfind("test-anycb", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BUFFER: ANYCB", ok);
        return ok;
    }

    // Clear BFCHG flag
    bp->b_flag &= ~BFCHG;

    // anycb should return false if no buffers changed
    // (depends on other buffers in system, so we just test our buffer)

    // Mark as changed
    bp->b_flag |= BFCHG;

    // Now anycb should find it
    if (!anycb()) {
        LOG_ERROR("[FAIL] anycb didn't detect changed buffer");
        ok = 0;
    }

    // Cleanup
    bp->b_flag &= ~BFCHG;  // Clear so zotbuf doesn't complain
    (void)zotbuf(bp);

    PHASE_END("BUFFER: ANYCB", ok);
    return ok;
}

// Test buffer hash lookup and collision handling
static int test_buffer_hash(void) {
    int ok = 1;
    PHASE_START("BUFFER: HASH", "Hash table lookup");

    // Create buffers with distinct names
    struct buffer *bp1 = bfind("hash-test-1", true, 0);
    struct buffer *bp2 = bfind("hash-test-2", true, 0);
    struct buffer *bp3 = bfind("hash-test-3", true, 0);

    if (!bp1 || !bp2 || !bp3) {
        LOG_ERROR("[FAIL] could not create test buffers");
        ok = 0;
        PHASE_END("BUFFER: HASH", ok);
        return ok;
    }

    // Lookup should find correct buffers
    struct buffer *found1 = bfind("hash-test-1", false, 0);
    struct buffer *found2 = bfind("hash-test-2", false, 0);
    struct buffer *found3 = bfind("hash-test-3", false, 0);

    if (found1 != bp1) {
        LOG_ERROR("[FAIL] hash lookup failed for buffer 1");
        ok = 0;
    }
    if (found2 != bp2) {
        LOG_ERROR("[FAIL] hash lookup failed for buffer 2");
        ok = 0;
    }
    if (found3 != bp3) {
        LOG_ERROR("[FAIL] hash lookup failed for buffer 3");
        ok = 0;
    }

    // Non-existent buffer should return nullptr
    struct buffer *notfound = bfind("nonexistent-buffer-xyz", false, 0);
    if (notfound != nullptr) {
        LOG_ERROR("[FAIL] found non-existent buffer");
        ok = 0;
    }

    // Cleanup
    bp1->b_flag &= ~BFCHG;
    bp2->b_flag &= ~BFCHG;
    bp3->b_flag &= ~BFCHG;
    (void)zotbuf(bp1);
    (void)zotbuf(bp2);
    (void)zotbuf(bp3);

    PHASE_END("BUFFER: HASH", ok);
    return ok;
}

// Test buffer index operations
static int test_buffer_index(void) {
    int ok = 1;
    PHASE_START("BUFFER: INDEX", "Line index operations");

    struct buffer *bp = bfind("test-index", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BUFFER: INDEX", ok);
        return ok;
    }

    // Setup
    struct buffer *oldbp = curbp;
    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = bp->b_linep;
    curwp->w_doto = 0;

    // Add some lines to the buffer
    linstr("Line 1");
    lnewline();
    linstr("Line 2");
    lnewline();
    linstr("Line 3");
    lnewline();
    linstr("Line 4");

    // Rebuild the index
    if (buffer_rebuild_index(bp) != true) {
        LOG_ERROR("[FAIL] buffer_rebuild_index failed");
        ok = 0;
    }

    // Get line count (should be 4 lines + header = 4 data lines)
    int line_count = atomic_load(&bp->b_line_count);
    if (line_count < 4) {
        LOG_ERRORF("[FAIL] line count wrong: %d", line_count);
        ok = 0;
    }

    // Test buffer_index_get - retrieve line by index
    struct line *lp0 = buffer_index_get(bp, 0);
    if (!lp0) {
        LOG_ERROR("[FAIL] could not get line 0");
        ok = 0;
    }

    struct line *lp2 = buffer_index_get(bp, 2);
    if (!lp2) {
        LOG_ERROR("[FAIL] could not get line 2");
        ok = 0;
    }

    // Test invalid index (should return nullptr)
    struct line *lp_invalid = buffer_index_get(bp, 999);
    if (lp_invalid != nullptr) {
        LOG_ERROR("[FAIL] got invalid line at index 999");
        ok = 0;
    }

    // Test negative index
    struct line *lp_neg = buffer_index_get(bp, -1);
    if (lp_neg != nullptr) {
        LOG_ERROR("[FAIL] got line at negative index");
        ok = 0;
    }

    // Restore and cleanup
    curbp = oldbp;
    curwp->w_bufp = oldbp;
    bp->b_flag &= ~BFCHG;
    (void)zotbuf(bp);

    PHASE_END("BUFFER: INDEX", ok);
    return ok;
}

// Test killbuffer function
static int test_killbuffer(void) {
    int ok = 1;
    PHASE_START("BUFFER: KILLBUFFER", "Buffer kill operation");

    // Create a test buffer
    struct buffer *bp = bfind("test-killbuf", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("BUFFER: KILLBUFFER", ok);
        return ok;
    }

    // Add some content
    struct buffer *oldbp = curbp;
    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = bp->b_linep;
    curwp->w_doto = 0;
    linstr("Test content");

    // Restore current buffer before killing
    curbp = oldbp;
    curwp->w_bufp = oldbp;

    // Clear modified flag to avoid prompts
    bp->b_flag &= ~BFCHG;

    // Kill the buffer directly (not via killbuffer() since it needs user input)
    if (zotbuf(bp) != true) {
        LOG_ERROR("[FAIL] zotbuf failed");
        ok = 0;
    }

    // Verify buffer is gone
    struct buffer *found = bfind("test-killbuf", false, 0);
    if (found != nullptr) {
        LOG_ERROR("[FAIL] buffer still exists after kill");
        ok = 0;
    }

    PHASE_END("BUFFER: KILLBUFFER", ok);
    return ok;
}

// Entry point
int test_core_buffer(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("buffer");

    PHASE_START("BUFFER", "Buffer management tests");

    ok &= test_buffer_create();
    ok &= test_buffer_clear();
    ok &= test_buffer_stats();
    ok &= test_buffer_modified();
    ok &= test_buffer_anycb();
    ok &= test_buffer_hash();
    ok &= test_buffer_index();
    ok &= test_killbuffer();

    if (ok) {
        LOG_INFO("[SUCCESS] All buffer tests passed.");
    }

    PHASE_END("BUFFER", ok);
    return ok;
}
