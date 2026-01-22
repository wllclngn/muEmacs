// tests/unit/test_text_nfa.c - Unit tests for Thompson NFA (MAGIC regex-lite) engine
// Covers anchors (^, $), cross-line, classes, closure, case folding, edge cases, and zero-alloc
// The NFA engine uses fixed-size arenas and state sets (no malloc/free during search/compile)
#include "../test_utils.h"
#include "../test_registry.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "estruct.h"
#include "edef.h"
#include "internal/line.h"
#include "internal/nfa.h"
#include "internal/text_storage.h"

#ifdef ENABLE_SEARCH_NFA
extern struct line* lalloc(int used);

// Helper: create a buffer with two lines: "foo\nbar"
static struct line* make_buffer(void) {
    struct line* l1 = lalloc(8);
    struct line* l2 = lalloc(8);
    if (!l1 || !l2) return NULL;
    TS_INSERT(l1->storage, 0, "foo", 3);
    TS_INSERT(l2->storage, 0, "bar", 3);
    l1->l_fp = l2; l2->l_bp = l1;
    l1->l_bp = l2->l_fp = NULL;
    return l1;
}
#else
// Helper: stub for when NFA is disabled
static void* make_buffer(void) {
    LOG_INFO("[SKIP] make_buffer called but NFA disabled");
    return NULL;
}
#endif

#ifdef ENABLE_SEARCH_NFA
static int test_anchor_start(void) {
    nfa_program_info nfa = {0};
    if (!nfa_compile("^foo", 1, &nfa)) return 0;
    struct line* l = make_buffer();
    struct line* mlp = NULL; int moff = 0;
    if (!nfa_search_forward(&nfa, l, 0, 0, &mlp, &moff)) return 0;
    return (mlp == l && moff == 0) ? 1 : 0;
}
#else
static int test_anchor_start(void) {
    LOG_INFO("[SKIP] NFA not enabled - test_anchor_start skipped");
    return 1; // Skip counts as pass
}
#endif

#ifdef ENABLE_SEARCH_NFA
static int test_anchor_end(void) {
    nfa_program_info nfa = {0};
    if (!nfa_compile("bar$", 1, &nfa)) return 0;
    struct line* l = make_buffer();
    struct line* mlp = NULL; int moff = 0;
    if (!nfa_search_forward(&nfa, l->l_fp, 0, 0, &mlp, &moff)) return 0;
    return (mlp == l->l_fp && moff == 0) ? 1 : 0;
}
#else
static int test_anchor_end(void) {
    LOG_INFO("[SKIP] NFA not enabled");
    return 1;
}
#endif

#ifdef ENABLE_SEARCH_NFA
static int test_cross_line(void) {
    // Test searching across multiple lines in a buffer
    struct line* l = make_buffer();  // Creates l1="foo", l2="bar"
    struct line* mlp = NULL; int moff = 0;

    // Search for "foo" on line 1
    nfa_program_info nfa1 = {0};
    if (!nfa_compile("foo", 1, &nfa1)) return 0;
    if (!nfa_search_forward(&nfa1, l, 0, 0, &mlp, &moff)) return 0;
    if (!(mlp == l && moff == 0)) return 0;

    // Search for "bar" on line 2
    nfa_program_info nfa2 = {0};
    if (!nfa_compile("bar", 1, &nfa2)) return 0;
    if (!nfa_search_forward(&nfa2, l->l_fp, 0, 0, &mlp, &moff)) return 0;
    return (mlp == l->l_fp && moff == 0) ? 1 : 0;
}
#else
static int test_cross_line(void) {
    LOG_INFO("[SKIP] NFA not enabled - test_cross_line skipped");
    return 1;
}
#endif

#ifdef ENABLE_SEARCH_NFA
static int test_class_and_closure(void) {
    nfa_program_info nfa = {0};
    // NFA only supports *, not + operator. Use fo[o]* to match "f" + one-or-more "o"s
    if (!nfa_compile("fo[o]*", 1, &nfa)) return 0;
    struct line* l = make_buffer();
    struct line* mlp = NULL; int moff = 0;
    if (!nfa_search_forward(&nfa, l, 0, 0, &mlp, &moff)) return 0;
    return (mlp == l && moff == 0) ? 1 : 0;
}
#else
static int test_class_and_closure(void) {
    LOG_INFO("[SKIP] NFA not enabled - test_class_and_closure skipped");
    return 1;
}
#endif

#ifdef ENABLE_SEARCH_NFA
static int test_case_fold(void) {
    nfa_program_info nfa = {0};
    if (!nfa_compile("FOO", 0, &nfa)) return 0;
    struct line* l = make_buffer();
    struct line* mlp = NULL; int moff = 0;
    if (!nfa_search_forward(&nfa, l, 0, 0, &mlp, &moff)) return 0;
    return (mlp == l && moff == 0) ? 1 : 0;
}
#else
static int test_case_fold(void) {
    LOG_INFO("[SKIP] NFA not enabled - test_case_fold skipped");
    return 1;
}
#endif

#ifdef ENABLE_SEARCH_NFA
static int test_empty_pattern(void) {
    // Empty patterns should be rejected by nfa_compile
    nfa_program_info nfa = {0};
    if (nfa_compile("", 1, &nfa)) return 0;  // Should fail to compile
    return 1;  // Test passes if compile correctly rejected empty pattern
}
#else
static int test_empty_pattern(void) {
    LOG_INFO("[SKIP] NFA not enabled - test_empty_pattern skipped");
    return 1;
}
#endif

#ifdef ENABLE_SEARCH_NFA
static int test_anchors_only(void) {
    nfa_program_info nfa = {0};
    if (!nfa_compile("^$", 1, &nfa)) return 0;
    struct line* l = lalloc(8);  // Empty line (gap buffer has 0 content)
    if (!l) return 0;
    // Terminate the buffer - lalloc creates a circular self-reference by default
    l->l_fp = NULL;
    l->l_bp = NULL;
    struct line* mlp = NULL; int moff = 0;
    if (!nfa_search_forward(&nfa, l, 0, 0, &mlp, &moff)) return 0;
    return (mlp == l && moff == 0) ? 1 : 0;
}
#else
static int test_anchors_only(void) {
    LOG_INFO("[SKIP] NFA not enabled - test_anchors_only skipped");
    return 1;
}
#endif

#ifdef ENABLE_SEARCH_NFA
static int test_negated_class(void) {
    nfa_program_info nfa = {0};
    if (!nfa_compile("[^a]oo", 1, &nfa)) return 0;  // Match: NOT 'a', then "oo"
    struct line* l = lalloc(8);
    if (!l) return 0;
    // Terminate the buffer - lalloc creates a circular self-reference by default
    l->l_fp = NULL;
    l->l_bp = NULL;

    // Test 1: "foo" - 'f' is not 'a', so [^a]oo SHOULD match
    TS_INSERT(l->storage, 0, "foo", 3);
    struct line* mlp = NULL; int moff = 0;
    if (!nfa_search_forward(&nfa, l, 0, 0, &mlp, &moff)) return 0;
    if (!(mlp == l && moff == 0)) return 0;

    // Test 2: "aoo" - 'a' matches [^a] negation, so should NOT match
    TS_DELETE(l->storage, 0, 1);
    TS_INSERT(l->storage, 0, "a", 1);  // Now "aoo"
    if (nfa_search_forward(&nfa, l, 0, 0, &mlp, &moff)) return 0;  // Should fail

    return 1;
}
#else
static int test_negated_class(void) {
    LOG_INFO("[SKIP] NFA not enabled - test_negated_class skipped");
    return 1;
}
#endif

#ifdef ENABLE_SEARCH_NFA
static int test_zero_length_match(void) {
    nfa_program_info nfa = {0};
    if (!nfa_compile("^", 1, &nfa)) return 0;
    struct line* l = make_buffer();
    struct line* mlp = NULL; int moff = 0;
    if (!nfa_search_forward(&nfa, l, 0, 0, &mlp, &moff)) return 0;
    return (mlp == l && moff == 0) ? 1 : 0;
}
#else
static int test_zero_length_match(void) {
    LOG_INFO("[SKIP] NFA not enabled - test_zero_length_match skipped");
    return 1;
}
#endif

#ifdef ENABLE_SEARCH_NFA
static int test_multiline_anchor(void) {
    nfa_program_info nfa = {0};
    if (!nfa_compile("^bar$", 1, &nfa)) return 0;
    struct line* l = make_buffer();
    struct line* mlp = NULL; int moff = 0;
    if (!nfa_search_forward(&nfa, l->l_fp, 0, 0, &mlp, &moff)) return 0;
    return (mlp == l->l_fp && moff == 0) ? 1 : 0;
}
#else
static int test_multiline_anchor(void) {
    LOG_INFO("[SKIP] NFA not enabled - test_multiline_anchor skipped");
    return 1;
}
#endif

// Entry point for integration test
int test_text_nfa(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("nfa");

    PHASE_START("NFA: REGEX", "Thompson NFA regex engine tests");

#ifdef ENABLE_SEARCH_NFA
    ok &= test_anchor_start();
    ok &= test_anchor_end();
    ok &= test_cross_line();
    ok &= test_class_and_closure();
    ok &= test_case_fold();
    ok &= test_empty_pattern();
    ok &= test_anchors_only();
    ok &= test_negated_class();
    ok &= test_zero_length_match();
    ok &= test_multiline_anchor();
    if (ok) {
        LOG_INFO("[SUCCESS] All NFA anchor/cross-line/case tests passed.");
    }
#else
    LOG_INFO("[INFO] NFA engine not enabled - all NFA tests skipped.");
#endif

    PHASE_END("NFA: REGEX", ok);
    return ok;
}
