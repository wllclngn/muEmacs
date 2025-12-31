// test_io_input.c - Unit tests for input handling
// Tests src/io/input.c - Unified parser version
// Key decode tests removed - unified parser (input_state.c) handles decoding

#include "../test_utils.h"
#include "estruct.h"
#include "edef.h"
#include "internal/efunc.h"
#include "μemacs/keymap.h"

// Forward declarations
extern void input_reset_parser_state(void);
extern int (*typahead_override)(void);
extern int input_read_byte(void);

// ============================================================================
// PASTE TESTS - Stream simulation infrastructure
// ============================================================================

static unsigned char stream_buf[1024];
static int stream_len = 0;
static int stream_pos = 0;

static void set_stream(const unsigned char* data, int len) {
    if (len > (int)sizeof(stream_buf)) len = (int)sizeof(stream_buf);
    memcpy(stream_buf, data, (size_t)len);
    stream_len = len;
    stream_pos = 0;
}

static int test_getchar(void) {
    if (stream_pos < stream_len) return (int)stream_buf[stream_pos++];
    return -1;
}

static int test_typahead(void) {
    // Return number of remaining characters in test stream
    return stream_len - stream_pos;
}

static int collect_n(unsigned char* out, int need) {
    int i = 0;
    while (i < need) {
        int c = input_read_byte();
        if (c == -1) break;
        out[i++] = (unsigned char)c;
    }
    return i;
}

// ============================================================================
// PASTE TESTS
// ============================================================================

static int test_paste_bracketed(void) {
    int ok = 1;
    PHASE_START("INPUT: PASTE-BRACKET", "Bracketed paste mode");

    int (*orig_getchar)(void) = term.t_getchar;
    int (*orig_typahead)(void) = typahead_override;
    term.t_getchar = test_getchar;
    typahead_override = test_typahead;
    kbdmode = 0;

    input_reset_parser_state();
    unsigned char seq[] = {0x1B,'[','2','0','0','~', 'H','e','l','l','o', 0x1B,'[','2','0','1','~'};
    set_stream(seq, sizeof seq);
    unsigned char out[16];
    int n = collect_n(out, 5);

    if (n != 5 || memcmp(out, "Hello", 5) != 0) {
        LOG_ERRORF("[FAIL] bracketed paste: expected 'Hello', got %d bytes", n);
        ok = 0;
    }

    term.t_getchar = orig_getchar;
    typahead_override = orig_typahead;

    PHASE_END("INPUT: PASTE-BRACKET", ok);
    return ok;
}

static int test_paste_partial(void) {
    int ok = 1;
    PHASE_START("INPUT: PASTE-PARTIAL", "Partial and interleaved sequences");

    int (*orig_getchar)(void) = term.t_getchar;
    int (*orig_typahead)(void) = typahead_override;
    term.t_getchar = test_getchar;
    typahead_override = test_typahead;
    kbdmode = 0;

    // Test incomplete start sequence
    input_reset_parser_state();
    unsigned char incomplete[] = {0x1B, '[', '2', '0', 'X'}; // Not '0' at end
    set_stream(incomplete, sizeof incomplete);
    unsigned char out[16];
    int n = collect_n(out, sizeof incomplete);

    // Should get literal characters since it's not a valid paste start
    if (n < 1) {
        LOG_ERROR("[FAIL] incomplete sequence: expected some output");
        ok = 0;
    }

    term.t_getchar = orig_getchar;
    typahead_override = orig_typahead;

    PHASE_END("INPUT: PASTE-PARTIAL", ok);
    return ok;
}

static int test_paste_stress(void) {
    int ok = 1;
    PHASE_START("INPUT: PASTE-STRESS", "Stress test with long paste");

    int (*orig_getchar)(void) = term.t_getchar;
    int (*orig_typahead)(void) = typahead_override;
    term.t_getchar = test_getchar;
    typahead_override = test_typahead;
    kbdmode = 0;

    input_reset_parser_state();

    // Build a long paste: start + 200 A's + end
    unsigned char s1[256];
    int idx = 0;
    s1[idx++] = 0x1B; s1[idx++] = '['; s1[idx++] = '2'; s1[idx++] = '0'; s1[idx++] = '0'; s1[idx++] = '~';
    for (int i = 0; i < 200; ++i) s1[idx++] = 'A';
    s1[idx++] = 0x1B; s1[idx++] = '['; s1[idx++] = '2'; s1[idx++] = '0'; s1[idx++] = '1'; s1[idx++] = '~';
    set_stream(s1, idx);

    unsigned char out[256];
    int n = collect_n(out, 200);

    int pass = 1;
    if (n != 200) pass = 0;
    for (int i = 0; i < n && pass; ++i) {
        if (out[i] != 'A') pass = 0;
    }

    if (!pass) {
        LOG_ERRORF("[FAIL] stress test: expected 200 A's, got %d bytes", n);
        ok = 0;
    }

    term.t_getchar = orig_getchar;
    typahead_override = orig_typahead;

    PHASE_END("INPUT: PASTE-STRESS", ok);
    return ok;
}

// ============================================================================
// ENTRY POINT
// ============================================================================

int test_io_input(void) {
    int all_passed = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("io-input");

    // Key decode tests removed - unified parser handles decoding
    // Paste tests use input_read_byte() which now uses the unified parser
    all_passed &= test_paste_bracketed();
    all_passed &= test_paste_partial();
    all_passed &= test_paste_stress();

    return all_passed;
}
