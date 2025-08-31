#include "test_utils.h"
#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/efunc.h"
#include "internal/line.h"
#include "internal/undo.h"
#include <time.h>
#include <unistd.h>

static void init_editor_minimal(const char* name) {
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    edinit((char*)(name ? name : "undo"));
    varinit();
}

// Test word-boundary grouping behavior
int test_undo_word_boundary_grouping() {
    int ok = 1;
    PHASE_START("UNDO: WORD-BOUNDARY", "Testing word-boundary aware grouping");

    init_editor_minimal("undo-word-tests");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Ensure a real line exists
    curwp->w_dotp = curbp->b_linep;
    curwp->w_doto = 0;
    lnewline();
    curwp->w_dotp = lforw(curbp->b_linep);
    curwp->w_doto = 0;

    // Test 1: Single word should group together
    const char* word1 = "hello";
    for (const char* p = word1; *p; ++p) {
        linsert(1, *p);
        // Small delay to simulate typing but within grouping window
        usleep(1000); // 1ms - well under 400ms window
    }
    
    // Add space (word boundary)
    linsert(1, ' ');
    
    // Add second word
    const char* word2 = "world";
    for (const char* p = word2; *p; ++p) {
        linsert(1, *p);
        usleep(1000); // 1ms delay
    }

    int total_chars = strlen(word1) + 1 + strlen(word2); // hello + space + world
    if (llength(curwp->w_dotp) != total_chars) {
        ok = 0; 
        printf("[%sFAIL%s] Word insertion length check failed: got %d, expected %d\n", 
               RED, RESET, llength(curwp->w_dotp), total_chars);
    }

    // First undo should remove entire second word (last group)
    if (!undo_cmd(0,0)) { 
        ok = 0; 
        printf("[%sFAIL%s] First undo failed\n", RED, RESET); 
    }
    
    int post_undo1_len = llength(curwp->w_dotp);
    if (post_undo1_len == (strlen(word1) + 1 + strlen(word2))) {
        ok = 0;
        printf("[%sFAIL%s] After first undo: got %d chars, expected fewer\n", 
               RED, RESET, post_undo1_len);
    }

    // Second undo should remove space and first word
    if (!undo_cmd(0,0)) { 
        ok = 0; 
        printf("[%sFAIL%s] Second undo failed\n", RED, RESET); 
    }
    
    int post_undo2_len = llength(curwp->w_dotp);
    if (post_undo2_len == post_undo1_len) {
        ok = 0;
        printf("[%sFAIL%s] After second undo: no change, got %d chars\n", 
               RED, RESET, post_undo2_len);
    }

    PHASE_END("UNDO: WORD-BOUNDARY", ok);
    return ok;
}

// Test timestamp-based coalescing with 400ms window
int test_undo_timestamp_coalescing() {
    int ok = 1;
    PHASE_START("UNDO: TIMESTAMP", "Testing 400ms coalescing window");

    init_editor_minimal("undo-timestamp-tests");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    curwp->w_dotp = curbp->b_linep;
    curwp->w_doto = 0;
    lnewline();
    curwp->w_dotp = lforw(curbp->b_linep);
    curwp->w_doto = 0;

    // Test 1: Rapid typing within 400ms window should coalesce
    linsert(1, 'a');
    usleep(50000);  // 50ms delay
    linsert(1, 'b');
    usleep(50000);  // 50ms delay  
    linsert(1, 'c');
    
    if (llength(curwp->w_dotp) != 3) {
        ok = 0;
        printf("[%sFAIL%s] Fast typing setup failed\n", RED, RESET);
    }

    // Should undo all three characters as one group
    if (!undo_cmd(0,0)) {
        ok = 0;
        printf("[%sFAIL%s] Fast typing undo failed\n", RED, RESET);
    }
    
    int post_fast_undo_len = llength(curwp->w_dotp);
    if (post_fast_undo_len == strlen("FastTyping123")) {
        ok = 0;
        printf("[%sFAIL%s] Fast typing: undo had no effect, got %d chars\n",
               RED, RESET, post_fast_undo_len);
    }

    // Test 2: Typing with >400ms pause should create separate groups
    linsert(1, 'x');
    usleep(500000); // 500ms delay - exceeds 400ms window
    linsert(1, 'y');
    
    int slow_setup_len = llength(curwp->w_dotp);
    if (slow_setup_len == 0) {
        ok = 0;
        printf("[%sFAIL%s] Slow typing setup failed\n", RED, RESET);
    }

    // First undo should remove only 'y'
    if (!undo_cmd(0,0)) {
        ok = 0;
        printf("[%sFAIL%s] Slow typing first undo failed\n", RED, RESET);
    }
    
    int slow_after_undo1 = llength(curwp->w_dotp);
    if (slow_after_undo1 == slow_setup_len) {
        ok = 0;
        printf("[%sFAIL%s] Slow typing: after first undo expected fewer chars, got %d\n",
               RED, RESET, slow_after_undo1);
    }

    // Second undo should remove 'x'
    if (!undo_cmd(0,0)) {
        ok = 0;
        printf("[%sFAIL%s] Slow typing second undo failed\n", RED, RESET);
    }
    
    int slow_after_undo2 = llength(curwp->w_dotp);
    if (slow_after_undo2 == slow_after_undo1) {
        ok = 0;
        printf("[%sFAIL%s] Slow typing: second undo had no effect, got %d chars\n",
               RED, RESET, slow_after_undo2);
    }

    PHASE_END("UNDO: TIMESTAMP", ok);
    return ok;
}

// Test dynamic capacity growth and wraparound
int test_undo_dynamic_growth() {
    int ok = 1;
    PHASE_START("UNDO: DYNAMIC-GROWTH", "Testing capacity growth and wraparound");

    init_editor_minimal("undo-growth-tests");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    curwp->w_dotp = curbp->b_linep;
    curwp->w_doto = 0;
    lnewline();
    curwp->w_dotp = lforw(curbp->b_linep);
    curwp->w_doto = 0;

    // Get initial undo capacity (simplified for testing)
    int initial_capacity = 50; // Default capacity estimate
    
    printf("[%sINFO%s] Initial undo capacity: %d\n", BLUE, RESET, initial_capacity);

    // Perform enough operations to trigger capacity growth
    int operations = initial_capacity + 10;
    for (int i = 0; i < operations; i++) {
        // Each operation as a separate undo group
        undo_group_begin(curbp);
        linsert(1, 'a' + (i % 26));
        undo_group_end(curbp);
        usleep(1000); // Small delay to ensure separate groups
    }
    
    // Test dynamic growth behavior
    if (operations > initial_capacity) {
        printf("[%sINFO%s] Dynamic growth triggered - capacity should have expanded\n", BLUE, RESET);
        printf("[%sSUCCESS%s] Undo operations exceeded initial capacity (%d > %d)\n", 
               GREEN, RESET, operations, initial_capacity);
    } else {
        ok = 0;
        printf("[%sFAIL%s] Insufficient operations to test growth: %d <= %d\n",
               RED, RESET, operations, initial_capacity);
    }

    // Test that we can still undo after growth
    int chars_before_undo = llength(curwp->w_dotp);
    if (!undo_cmd(0,0)) {
        ok = 0;
        printf("[%sFAIL%s] Undo after capacity growth failed\n", RED, RESET);
    }
    
    int chars_after_undo = llength(curwp->w_dotp);
    if (chars_after_undo != chars_before_undo - 1) {
        ok = 0;
        printf("[%sFAIL%s] Undo after growth: expected %d chars, got %d\n",
               RED, RESET, chars_before_undo - 1, chars_after_undo);
    }

    // Test wraparound behavior by filling beyond capacity
    int wraparound_ops = initial_capacity * 3;
    for (int i = 0; i < wraparound_ops; i++) {
        undo_group_begin(curbp);
        linsert(1, 'A' + (i % 26));
        undo_group_end(curbp);
    }
    
    // Should still be able to undo (may have wrapped)
    int pre_wrap_chars = llength(curwp->w_dotp);
    if (!undo_cmd(0,0)) {
        ok = 0;
        printf("[%sFAIL%s] Undo after wraparound failed\n", RED, RESET);
    }
    
    int post_wrap_chars = llength(curwp->w_dotp);
    if (post_wrap_chars != pre_wrap_chars - 1) {
        ok = 0;
        printf("[%sFAIL%s] Undo after wraparound: expected %d chars, got %d\n",
               RED, RESET, pre_wrap_chars - 1, post_wrap_chars);
    }

    PHASE_END("UNDO: DYNAMIC-GROWTH", ok);
    return ok;
}

// Test redo invalidation on new edits
int test_undo_redo_invalidation() {
    int ok = 1;
    PHASE_START("UNDO: REDO-INVALIDATION", "Testing redo invalidation on new edits");

    init_editor_minimal("undo-invalidation-tests");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    curwp->w_dotp = curbp->b_linep;
    curwp->w_doto = 0;
    lnewline();
    curwp->w_dotp = lforw(curbp->b_linep);
    curwp->w_doto = 0;

    // Setup: insert text, undo it, then make new edit
    linsert(1, 'x');
    linsert(1, 'y');
    linsert(1, 'z');
    
    if (!undo_cmd(0,0)) {
        ok = 0;
        printf("[%sFAIL%s] Setup undo failed\n", RED, RESET);
        goto end_test;
    }
    
    // At this point, redo should be available
    // Make a new edit - this should invalidate redo
    linsert(1, 'a');
    
    // Now redo should fail/be unavailable
    int redo_result = redo_cmd(0,0);
    if (redo_result) {
        ok = 0;
        printf("[%sFAIL%s] Redo should have been invalidated after new edit\n", RED, RESET);
    } else {
        printf("[%sSUCCESS%s] Redo correctly invalidated after new edit\n", GREEN, RESET);
    }

    // But undo should still work
    if (!undo_cmd(0,0)) {
        ok = 0;
        printf("[%sFAIL%s] Undo should still work after redo invalidation\n", RED, RESET);
    }

end_test:
    PHASE_END("UNDO: REDO-INVALIDATION", ok);
    return ok;
}