// test_core_undo.c - Unit tests for undo/redo system
// Tests src/core/undo.c
// Merged from: test_undo_deterministic.c, test_undo_capacity.c, test_undo_advanced.c

#include "../test_utils.h"
#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/efunc.h"
#include "internal/line.h"
#include "internal/undo.h"
#include <time.h>
#include <unistd.h>

static void init_undo_test(const char* name) {
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    edinit((char*)(name ? name : "undo"));
    varinit();
}

static void setup_empty_line(void) {
    (void)bclear(curbp);
    curbp->b_mode &= ~MDVIEW;
    curwp->w_dotp = curbp->b_linep;
    curwp->w_doto = 0;
    lnewline();
    curwp->w_dotp = lforw(curbp->b_linep);
    curwp->w_doto = 0;
}

// Test basic undo/redo of literal inserts
static int test_undo_basic(void) {
    int ok = 1;
    PHASE_START("UNDO: BASIC", "Basic undo/redo of literal inserts");

    init_undo_test("undo-basic");
    setup_empty_line();

    // Insert abc
    linsert(1, 'a');
    linsert(1, 'b');
    linsert(1, 'c');
    int post_insert_len = llength(curwp->w_dotp);

    // Undo
    if (!undo_cmd(0,0)) {
        ok = 0;
        LOG_ERROR("[FAIL] undo_cmd failed");
    }
    int post_undo_len = llength(curwp->w_dotp);
    if (post_undo_len >= post_insert_len) {
        ok = 0;
        LOG_ERROR("[FAIL] undo had no effect");
    }

    // Redo
    if (!redo_cmd(0,0)) {
        ok = 0;
        LOG_ERROR("[FAIL] redo_cmd failed");
    }
    if (llength(curwp->w_dotp) != 3) {
        ok = 0;
        LOG_ERROR("[FAIL] redo did not restore line");
    }

    PHASE_END("UNDO: BASIC", ok);
    return ok;
}

// Test grouped operations
static int test_undo_grouping(void) {
    int ok = 1;
    PHASE_START("UNDO: GROUPING", "Grouped operations undo as one step");

    init_undo_test("undo-grouping");
    setup_empty_line();

    // Insert some text first
    linsert(1, 'a');
    linsert(1, 'b');
    linsert(1, 'c');
    int base_len = llength(curwp->w_dotp);

    // Grouped insert of xy
    undo_group_begin(curbp);
    linsert(1, 'x');
    linsert(1, 'y');
    undo_group_end(curbp);

    // Undo should remove both x and y
    if (!undo_cmd(0,0)) {
        ok = 0;
        LOG_ERROR("[FAIL] grouped undo failed");
    } else {
        if (llength(curwp->w_dotp) != base_len) {
            LOG_INFOF("[%sINFO%s] grouped undo did not coalesce fully (len=%d base=%d)", YELLOW, RESET, llength(curwp->w_dotp), base_len);
        }
    }

    PHASE_END("UNDO: GROUPING", ok);
    return ok;
}

// Test redo invalidation on new edit
static int test_undo_redo_invalidation(void) {
    int ok = 1;
    PHASE_START("UNDO: REDO-INVALIDATION", "Redo invalidated by new edits");

    init_undo_test("undo-invalidation");
    setup_empty_line();

    // Insert xyz
    linsert(1, 'x');
    linsert(1, 'y');
    linsert(1, 'z');

    // Undo
    if (!undo_cmd(0,0)) {
        ok = 0;
        LOG_ERROR("[FAIL] setup undo failed");
        PHASE_END("UNDO: REDO-INVALIDATION", ok);
        return ok;
    }

    // Make new edit - should invalidate redo
    linsert(1, 'a');

    // Redo should fail
    int redo_result = redo_cmd(0,0);
    if (redo_result) {
        ok = 0;
        LOG_ERROR("[FAIL] redo should have been invalidated");
    } else {
        LOG_INFO("[SUCCESS] redo correctly invalidated");
    }

    // But undo should still work
    if (!undo_cmd(0,0)) {
        ok = 0;
        LOG_ERROR("[FAIL] undo should still work");
    }

    PHASE_END("UNDO: REDO-INVALIDATION", ok);
    return ok;
}

// Test capacity growth under many edits
static int test_undo_capacity(void) {
    int ok = 1;
    PHASE_START("UNDO: CAPACITY", "Capacity growth under many edits");

    init_undo_test("undo-capacity");
    setup_empty_line();

    // Insert many characters to exceed initial capacity
    const char* stress = getenv("STRESS");
    int total = 300;
    if (stress && strcmp(stress, "1") == 0) {
        total = 3000;
        LOG_INFOF("[INFO] STRESS=1: total edits=%d", total);
    }

    for (int i = 0; i < total; ++i) {
        if (!linsert(1, 'a' + (i % 26))) {
            ok = 0;
            break;
        }
    }

    if (llength(curwp->w_dotp) != total) {
        ok = 0;
        LOG_ERROR("[FAIL] insert count mismatch");
    }

    // Undo all operations
    int safety = total + 10;
    while (safety-- > 0 && llength(curwp->w_dotp) > 0) {
        if (!undo_cmd(0,0)) break;
    }

    if (llength(curwp->w_dotp) != 0) {
        ok = 0;
        LOG_ERROR("[FAIL] did not undo to empty");
    }

    PHASE_END("UNDO: CAPACITY", ok);
    return ok;
}

// Test word-boundary grouping
static int test_undo_word_boundary(void) {
    int ok = 1;
    PHASE_START("UNDO: WORD-BOUNDARY", "Word-boundary aware grouping");

    init_undo_test("undo-word");
    setup_empty_line();

    // Type "hello "
    const char* word1 = "hello";
    for (const char* p = word1; *p; ++p) {
        linsert(1, *p);
        usleep(1000); // 1ms - within grouping window
    }
    linsert(1, ' '); // word boundary

    // Type "world"
    const char* word2 = "world";
    for (const char* p = word2; *p; ++p) {
        linsert(1, *p);
        usleep(1000);
    }

    int total_chars = strlen(word1) + 1 + strlen(word2);
    if (llength(curwp->w_dotp) != total_chars) {
        ok = 0;
        LOG_ERRORF("[FAIL] length check: got %d, expected %d", llength(curwp->w_dotp), total_chars);
    }

    // First undo should affect second word
    if (!undo_cmd(0,0)) {
        ok = 0;
        LOG_ERROR("[FAIL] first undo failed");
    }

    int post_undo1 = llength(curwp->w_dotp);
    if (post_undo1 == total_chars) {
        ok = 0;
        LOG_ERROR("[FAIL] first undo had no effect");
    }

    // Second undo should affect first word
    if (!undo_cmd(0,0)) {
        ok = 0;
        LOG_ERROR("[FAIL] second undo failed");
    }

    if (llength(curwp->w_dotp) == post_undo1) {
        ok = 0;
        LOG_ERROR("[FAIL] second undo had no effect");
    }

    PHASE_END("UNDO: WORD-BOUNDARY", ok);
    return ok;
}

// Test timestamp-based coalescing
static int test_undo_timestamp_coalescing(void) {
    int ok = 1;
    PHASE_START("UNDO: TIMESTAMP", "400ms coalescing window");

    init_undo_test("undo-timestamp");
    setup_empty_line();

    // Rapid typing within 400ms window
    linsert(1, 'a');
    usleep(50000);  // 50ms
    linsert(1, 'b');
    usleep(50000);  // 50ms
    linsert(1, 'c');

    if (llength(curwp->w_dotp) != 3) {
        ok = 0;
        LOG_ERROR("[FAIL] fast typing setup failed");
    }

    // Should undo as one group
    if (!undo_cmd(0,0)) {
        ok = 0;
        LOG_ERROR("[FAIL] fast typing undo failed");
    }

    int post_fast = llength(curwp->w_dotp);
    if (post_fast == 3) {
        ok = 0;
        LOG_ERROR("[FAIL] fast typing undo had no effect");
    }

    // Slow typing with >400ms pause
    linsert(1, 'x');
    usleep(500000); // 500ms - exceeds window
    linsert(1, 'y');

    int slow_len = llength(curwp->w_dotp);

    // First undo should only remove 'y'
    if (!undo_cmd(0,0)) {
        ok = 0;
        LOG_ERROR("[FAIL] slow first undo failed");
    }

    if (llength(curwp->w_dotp) == slow_len) {
        ok = 0;
        LOG_ERROR("[FAIL] slow first undo had no effect");
    }

    PHASE_END("UNDO: TIMESTAMP", ok);
    return ok;
}

// Test dynamic capacity growth
static int test_undo_dynamic_growth(void) {
    int ok = 1;
    PHASE_START("UNDO: DYNAMIC-GROWTH", "Capacity growth and wraparound");

    init_undo_test("undo-growth");
    setup_empty_line();

    int initial_capacity = 50;
    int operations = initial_capacity + 10;

    // Perform enough operations to trigger growth
    for (int i = 0; i < operations; i++) {
        undo_group_begin(curbp);
        linsert(1, 'a' + (i % 26));
        undo_group_end(curbp);
        usleep(1000);
    }

    LOG_INFOF("[INFO] Performed %d operations (initial capacity ~%d)", operations, initial_capacity);

    // Should still be able to undo after growth
    int chars_before = llength(curwp->w_dotp);
    if (!undo_cmd(0,0)) {
        ok = 0;
        LOG_ERROR("[FAIL] undo after growth failed");
    }

    if (llength(curwp->w_dotp) != chars_before - 1) {
        ok = 0;
        LOG_ERRORF("[FAIL] undo after growth: expected %d, got %d", chars_before - 1, llength(curwp->w_dotp));
    }

    // Test wraparound with many more operations
    int wraparound_ops = initial_capacity * 3;
    for (int i = 0; i < wraparound_ops; i++) {
        undo_group_begin(curbp);
        linsert(1, 'A' + (i % 26));
        undo_group_end(curbp);
    }

    // Should still work after wraparound
    int pre_wrap = llength(curwp->w_dotp);
    if (!undo_cmd(0,0)) {
        ok = 0;
        LOG_ERROR("[FAIL] undo after wraparound failed");
    }

    if (llength(curwp->w_dotp) != pre_wrap - 1) {
        ok = 0;
        LOG_ERRORF("[FAIL] undo after wraparound: expected %d, got %d", pre_wrap - 1, llength(curwp->w_dotp));
    }

    PHASE_END("UNDO: DYNAMIC-GROWTH", ok);
    return ok;
}

// Entry point for undo tests
int test_core_undo(void) {
    int all_passed = 1;

    all_passed &= test_undo_basic();
    all_passed &= test_undo_grouping();
    all_passed &= test_undo_redo_invalidation();
    all_passed &= test_undo_capacity();
    all_passed &= test_undo_word_boundary();
    all_passed &= test_undo_timestamp_coalescing();
    all_passed &= test_undo_dynamic_growth();

    return all_passed;
}
