#include "test_utils.h"
#include "test_boyer_moore.h"

#include "../src/text/boyer_moore.h"

static int expect_match(const unsigned char* text, int text_len,
                        const char* pat, int start, int want, bool cs) {
    struct boyer_moore_context ctx;
    if (bm_init(&ctx, (const unsigned char*)pat, (int)strlen(pat), cs) != 0) {
        printf("[%sFAIL%s] bm_init failed for pattern '%s'\n", RED, RESET, pat);
        return 0;
    }
    int got = bm_search(&ctx, text, text_len, start);
    if (got != want) {
        printf("[%sFAIL%s] bm_search '%s' got %d want %d\n", RED, RESET, pat, got, want);
        return 0;
    }
    return 1;
}

static int expect_rmatch(const unsigned char* text, int text_len,
                         const char* pat, int start, int want, bool cs) {
    struct boyer_moore_context ctx;
    if (bm_init(&ctx, (const unsigned char*)pat, (int)strlen(pat), cs) != 0) {
        printf("[%sFAIL%s] bm_init failed for pattern '%s' (reverse)\n", RED, RESET, pat);
        return 0;
    }
    int got = bm_search_reverse(&ctx, text, text_len, start);
    if (got != want) {
        printf("[%sFAIL%s] bm_search_reverse '%s' got %d want %d\n", RED, RESET, pat, got, want);
        return 0;
    }
    return 1;
}

int test_bmh_literals() {
    int ok = 1;
    PHASE_START("BMH: LITERALS", "Boyer–Moore–Horspool forward/reverse");

    const unsigned char* text = (const unsigned char*)"Hello world, HELLO WORLD";
    int n = (int)strlen((const char*)text);

    // Forward, case-sensitive
    ok &= expect_match(text, n, "Hello", 0, 0, true);
    ok &= expect_match(text, n, "world", 0, 6, true);
    ok &= expect_match(text, n, "WORLD", 0, 19, true);

    // Forward, case-insensitive (first occurrence)
    ok &= expect_match(text, n, "HELLO", 0, 0, false);
    ok &= expect_match(text, n, "WORLD", 0, 6, false);

    // Reverse, case-insensitive (last occurrence)
    ok &= expect_rmatch(text, n, "hello", n-1, 13, false);
    ok &= expect_rmatch(text, n, "world", n-1, 19, false);

    // Not found
    ok &= expect_match(text, n, "xyz", 0, -1, true);

    PHASE_END("BMH: LITERALS", ok);
    return ok;
}

int test_bmh_edge_cases() {
    int ok = 1;
    PHASE_START("BMH: EDGES", "Boundary conditions and edge cases");

    // Empty pattern should fail to initialize
    struct boyer_moore_context ctx;
    if (bm_init(&ctx, (const unsigned char*)"", 0, true) == 0) {
        printf("[%sFAIL%s] bm_init unexpectedly succeeded for empty pattern\n", RED, RESET);
        ok = 0;
    }

    const unsigned char* t1 = (const unsigned char*)"abc";
    int n1 = 3;

    // Pattern longer than text → not found
    ok &= expect_match(t1, n1, "abcd", 0, -1, true);

    // start_pos beyond end → not found
    {
        struct boyer_moore_context c2;
        bm_init(&c2, (const unsigned char*)"a", 1, true);
        int got = bm_search(&c2, t1, n1, 3);
        if (got != -1) {
            printf("[%sFAIL%s] start_pos==n expected -1, got %d\n", RED, RESET, got);
            ok = 0;
        }
    }

    // start_pos in middle should find the first subsequent match (index 4)
    ok &= expect_match((const unsigned char*)"aaabaa", 6, "aa", 2, 4, true);

    // Reverse search: find last occurrence <= start_pos
    ok &= expect_rmatch((const unsigned char*)"abc abc abc", 11, "abc", 10, 8, true);
    ok &= expect_rmatch((const unsigned char*)"abc abc abc", 11, "abc", 2, 0, true);

    // UTF-8 bytes: case-insensitive is ASCII-only; non-ASCII should not fold
    const unsigned char* utf8 = (const unsigned char*)"GrüßGott"; // ü = 0xC3 0xBC
    int n2 = (int)strlen((const char*)utf8);
    ok &= expect_match(utf8, n2, "Grü", 0, 0, true);   // exact bytes
    // Uppercase pattern with Ü should NOT match case-insensitive due to ASCII fold only
    ok &= expect_match(utf8, n2, "GRÜ", 0, -1, false);

    PHASE_END("BMH: EDGES", ok);
    return ok;
}

int test_bmh_additional_edges() {
    int ok = 1;
    PHASE_START("BMH: MORE", "Additional boundary and start-pos cases");

    // Pattern length equals text length
    {
        const unsigned char* t = (const unsigned char*)"AbCdE";
        int n = 5;
        struct boyer_moore_context ctx;
        // Case-sensitive should match at 0
        bm_init(&ctx, (const unsigned char*)"AbCdE", 5, true);
        int got = bm_search(&ctx, t, n, 0);
        if (got != 0) { printf("[%sFAIL%s] len==len cs match expected 0, got %d\n", RED, RESET, got); ok = 0; }
        // Case-insensitive should also match at 0 for different case
        bm_init(&ctx, (const unsigned char*)"abcde", 5, false);
        got = bm_search(&ctx, t, n, 0);
        if (got != 0) { printf("[%sFAIL%s] len==len ci match expected 0, got %d\n", RED, RESET, got); ok = 0; }
    }

    // start_pos negative / beyond end handling (API guards)
    {
        const unsigned char* t = (const unsigned char*)"abcdef";
        int n = 6;
        struct boyer_moore_context ctx;
        bm_init(&ctx, (const unsigned char*)"def", 3, true);
        int got = bm_search(&ctx, t, n, -1);
        if (got != -1) { printf("[%sFAIL%s] start_pos<0 should yield -1, got %d\n", RED, RESET, got); ok = 0; }
        got = bm_search(&ctx, t, n, 4); // n-m = 3; start beyond first window
        if (got != -1) { printf("[%sFAIL%s] start_pos>n-m should yield -1, got %d\n", RED, RESET, got); ok = 0; }
    }

    // Reverse search boundary: last <= start_pos
    {
        const unsigned char* t = (const unsigned char*)"xxabcxxabc"; // matches at 2 and 7
        int n = 10;
        struct boyer_moore_context ctx;
        bm_init(&ctx, (const unsigned char*)"abc", 3, true);
        int got = bm_search_reverse(&ctx, t, n, 9); // from last index
        if (got != 7) { printf("[%sFAIL%s] reverse from end expected 7, got %d\n", RED, RESET, got); ok = 0; }
        got = bm_search_reverse(&ctx, t, n, 4); // before second match window >=7
        if (got != 2) { printf("[%sFAIL%s] reverse from 4 expected 2, got %d\n", RED, RESET, got); ok = 0; }
        got = bm_search_reverse(&ctx, t, n, 1); // before first match ends
        if (got != -1) { printf("[%sFAIL%s] reverse from 1 expected -1, got %d\n", RED, RESET, got); ok = 0; }
    }

    // UTF-8 multibyte boundaries: match single multibyte codepoint and across
    {
        const unsigned char* t = (const unsigned char*)"Gr\xC3\xBC\xC3\x9Fg"; // Grüßg
        int n = (int)strlen((const char*)t);
        struct boyer_moore_context ctx;
        // Match the byte sequence for 'ü' (C3 BC)
        const unsigned char* u_umlaut = (const unsigned char*)"\xC3\xBC";
        bm_init(&ctx, u_umlaut, 2, true);
        int got = bm_search(&ctx, t, n, 0);
        if (got != 2) { printf("[%sFAIL%s] UTF-8 single codepoint expected 2, got %d\n", RED, RESET, got); ok = 0; }
        // Match spanning üß (4 bytes)
        const unsigned char* uesz = (const unsigned char*)"\xC3\xBC\xC3\x9F";
        bm_init(&ctx, uesz, 4, true);
        got = bm_search(&ctx, t, n, 0);
        if (got != 2) { printf("[%sFAIL%s] UTF-8 span expected 2, got %d\n", RED, RESET, got); ok = 0; }
    }

    PHASE_END("BMH: MORE", ok);
    return ok;
}
