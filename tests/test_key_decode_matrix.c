#include "test_utils.h"
#include "internal/efunc.h"
#include "μemacs/keymap.h"
#include "μemacs/core.h"  /* for CONTROL/META flags */

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
        printf("[%sFAIL%s] legacy mismatch: got=%u expected=%u\n", RED, RESET, legacy, expected);
        return 0;
    }
    return 1;
}

int run_key_decode_matrix_tests(void)
{
    int ok = 1;
    // ESC a => META|'a'
    const unsigned char esc_a[] = {0x1B, 'a'};
    ok &= expect_legacy(esc_a, sizeof esc_a, META | 'a');
    // ESC CTRL-A => META|CONTROL|'A'
    const unsigned char esc_ctrl_a[] = {0x1B, 0x01};
    ok &= expect_legacy(esc_ctrl_a, sizeof esc_ctrl_a, META | CONTROL | 'A');
    // Plain CTRL-C
    const unsigned char ctrl_c[] = {0x03};
    ok &= expect_legacy(ctrl_c, sizeof ctrl_c, CONTROL | 'C');
    // CSI u Alt-a
    const unsigned char csiu_alt_a[] = {0x1B,'[','9','7',';','4','u'};
    ok &= expect_legacy(csiu_alt_a, sizeof csiu_alt_a, META | 'a');
    // CSI u Ctrl-A
    const unsigned char csiu_ctrl_A[] = {0x1B,'[','6','5',';','8','u'};
    ok &= expect_legacy(csiu_ctrl_A, sizeof csiu_ctrl_A, CONTROL | 'A');
    return ok;
}
