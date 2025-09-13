#include "test_utils.h"
#include "internal/efunc.h"
#include "μemacs/keymap.h"
#include "μemacs/core.h"  /* CONTROL/META flags */

static int expect_legacy(const unsigned char* seq, size_t len, uint32_t expected)
{
    struct key_event evt = {0};
    size_t consumed = 0;
    int ok = decode_key_from_bytes(seq, len, &evt, &consumed);
    if (!ok) {
        printf("[%sFAIL%s] decode_key_from_bytes failed\n", RED, RESET);
        return 0;
    }
    uint32_t legacy = key_to_legacy(evt);
    if (legacy != expected) {
        printf("[%sFAIL%s] legacy mismatch: got=0x%08X expected=0x%08X\n", RED, RESET, legacy, expected);
        return 0;
    }
    return 1;
}

int run_key_decode_edge_tests(void)
{
    int ok = 1;
    PHASE_START("KEY-DECODE: EDGES", "Meta on punctuation/digits and Kitty CSI u");

    // ESC as Meta for punctuation
    const unsigned char esc_lt[] = {0x1B, '<'};   ok &= expect_legacy(esc_lt, sizeof esc_lt, META | '<');
    const unsigned char esc_gt[] = {0x1B, '>'};   ok &= expect_legacy(esc_gt, sizeof esc_gt, META | '>');
    const unsigned char esc_sl[] = {0x1B, '/'};   ok &= expect_legacy(esc_sl, sizeof esc_sl, META | '/') ;
    const unsigned char esc_qm[] = {0x1B, '?'};   ok &= expect_legacy(esc_qm, sizeof esc_qm, META | '?');
    const unsigned char esc_dash[] = {0x1B, '-'}; ok &= expect_legacy(esc_dash, sizeof esc_dash, META | '-');

    // ESC as Meta for digits 0..9
    for (int d = '0'; d <= '9'; d++) {
        unsigned char seq[2] = {0x1B, (unsigned char)d};
        ok &= expect_legacy(seq, 2, (uint32_t)(META | d));
    }

    // Kitty CSI u Alt punctuation (mods: 4)
    ok &= expect_legacy((const unsigned char*)"\x1b[60;4u", 7, META | '<');
    ok &= expect_legacy((const unsigned char*)"\x1b[62;4u", 7, META | '>');
    ok &= expect_legacy((const unsigned char*)"\x1b[47;4u", 7, META | '/');
    ok &= expect_legacy((const unsigned char*)"\x1b[63;4u", 7, META | '?');
    ok &= expect_legacy((const unsigned char*)"\x1b[45;4u", 7, META | '-');
    // Kitty CSI u Alt digits 0..9
    for (int d = 0; d <= 9; d++) {
        char buf[16];
        int code = 48 + d;
        int n = snprintf(buf, sizeof buf, "\x1b[%d;4u", code);
        ok &= expect_legacy((const unsigned char*)buf, (size_t)n, (uint32_t)(META | (48 + d)));
    }

    PHASE_END("KEY-DECODE: EDGES", ok);
    return ok;
}
