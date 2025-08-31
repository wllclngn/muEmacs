/*
 * boyer_moore.h - Boyer–Moore–Horspool (BMH) literal search (O(1) extra space)
 * Fast average-case intra-line search using only bad-character table.
 */

#ifndef BOYER_MOORE_H
#define BOYER_MOORE_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum pattern length for Boyer-Moore tables */
#define BM_MAX_PATTERN 256
#define BM_ALPHABET_SIZE 256

/* BMH search context (no heap allocations) */
struct boyer_moore_context {
    /* Bad character skip table */
    int bad_char[BM_ALPHABET_SIZE];
    /* Pattern information */
    const unsigned char *pattern;
    int pattern_len;
    bool case_sensitive;
};

/* Initialize Boyer-Moore context with pattern */
int bm_init(struct boyer_moore_context *ctx, const unsigned char *pattern,
           int pattern_len, bool case_sensitive);

/* Search for pattern in text using Boyer-Moore algorithm */
int bm_search(struct boyer_moore_context *ctx, const unsigned char *text,
             int text_len, int start_pos);

/* Search backwards for pattern */
int bm_search_reverse(struct boyer_moore_context *ctx, const unsigned char *text,
                     int text_len, int start_pos);

/* Free Boyer-Moore context resources */
static inline void bm_free(struct boyer_moore_context *ctx) { (void)ctx; }

#endif /* BOYER_MOORE_H */
