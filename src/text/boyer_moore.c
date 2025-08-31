/*
 * boyer_moore.c - Boyer–Moore–Horspool (BMH) literal search
 * Sublinear average-case performance with O(1) extra space.
 */

#include "boyer_moore.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
/* No dynamic memory required */

/* Fast character normalization for case-insensitive search */
static inline unsigned char normalize_char(unsigned char c, bool case_sensitive)
{
    return case_sensitive ? c : (unsigned char)tolower(c);
}

/* Precompute bad character rule (BMH variant) */
static void compute_bad_char(struct boyer_moore_context *ctx)
{
    int i;
    
    /* Initialize all characters to pattern length (maximum shift) */
    for (i = 0; i < BM_ALPHABET_SIZE; i++) {
        ctx->bad_char[i] = ctx->pattern_len;
    }
    
    /* Set actual shift distances for characters in pattern (except last) */
    for (i = 0; i < ctx->pattern_len - 1; i++) {
        unsigned char c = normalize_char(ctx->pattern[i], ctx->case_sensitive);
        ctx->bad_char[c] = ctx->pattern_len - 1 - i;
    }
}

/* Initialize Boyer-Moore context */
int bm_init(struct boyer_moore_context *ctx, const unsigned char *pattern, 
           int pattern_len, bool case_sensitive)
{
    if (!ctx || !pattern || pattern_len <= 0 || pattern_len > BM_MAX_PATTERN) {
        return -1;
    }
    ctx->pattern = pattern;
    ctx->pattern_len = pattern_len;
    ctx->case_sensitive = case_sensitive;
    
    /* Precompute search tables */
    compute_bad_char(ctx);
    return 0;
}

/* Boyer-Moore forward search */
int bm_search(struct boyer_moore_context *ctx, const unsigned char *text, 
             int text_len, int start_pos)
{
    int i, j;
    int shift;
    int m = ctx->pattern_len;
    int n = text_len;
    
    if (!ctx || !text || start_pos < 0 || start_pos >= n || m > n) {
        return -1;
    }
    
    /* Start at first window that fits */
    i = start_pos;
    while (i <= n - m) {
        j = m - 1;
        while (j >= 0 && normalize_char(text[i + j], ctx->case_sensitive) ==
                         normalize_char(ctx->pattern[j], ctx->case_sensitive)) {
            j--;
        }
        if (j < 0) return i;
        unsigned char bad = normalize_char(text[i + m - 1], ctx->case_sensitive);
        int bad_shift = ctx->bad_char[bad];
        shift = bad_shift > 0 ? bad_shift : 1;
        i += shift;
    }
    
    return -1; /* Pattern not found */
}

/* Boyer-Moore reverse search */
int bm_search_reverse(struct boyer_moore_context *ctx, const unsigned char *text, 
                     int text_len, int start_pos)
{
    int i, j;
    int shift;
    int m = ctx->pattern_len;
    
    if (!ctx || !text || start_pos >= text_len || start_pos < 0) {
        return -1;
    }
    int last = -1;
    int end = start_pos - m + 1;
    if (end > text_len - m) end = text_len - m;
    if (end < 0) end = 0;
    i = 0;
    while (i <= end) {
        j = m - 1;
        while (j >= 0 && normalize_char(text[i + j], ctx->case_sensitive) ==
                         normalize_char(ctx->pattern[j], ctx->case_sensitive)) {
            j--;
        }
        if (j < 0) { last = i; i += 1; continue; }
        unsigned char bad = normalize_char(text[i + m - 1], ctx->case_sensitive);
        int bad_shift = ctx->bad_char[bad];
        shift = bad_shift > 0 ? bad_shift : 1;
        i += shift;
    }
    return last;
}

/* Free Boyer-Moore context */
/* bm_free is a no-op for BMH (declared inline in header) */
