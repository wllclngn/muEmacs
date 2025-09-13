#include "test_utils.h"
#include "internal/efunc.h"
#include "μemacs/core.h"  /* for CONTROL/META flags */

static int expect_seq(const char* seq, int expected)
{
    int out = 0;
    int ok = parse_kitty_csi_u(seq, &out);
    if (!ok) {
        printf("[%sFAIL%s] parse_kitty_csi_u failed for '%s'\n", RED, RESET, seq);
        return 0;
    }
    if (out != expected) {
        printf("[%sFAIL%s] CSIu decode mismatch for '%s': got=%d expected=%d\n", RED, RESET, seq, out, expected);
        return 0;
    }
    return 1;
}

int run_kitty_csi_u_tests(void)
{
    int ok = 1;
    /* Plain A */
    ok &= expect_seq("\x1b[65;0u", 'A');
    /* Alt-a -> META|'a' (legacy path keeps case; getcmd may normalize) */
    ok &= expect_seq("\x1b[97;4u", META | 'a');
    /* Ctrl-A -> CONTROL|'A' */
    ok &= expect_seq("\x1b[65;8u", CONTROL | 'A');
    /* Ctrl-a -> CONTROL|'A' */
    ok &= expect_seq("\x1b[97;8u", CONTROL | 'A');
    /* Alt+Ctrl+Z */
    ok &= expect_seq("\x1b[90;12u", META | (CONTROL | 'Z'));
    /* Non-ASCII é (233) Alt */
    ok &= expect_seq("\x1b[233;4u", META | 233);
    return ok;
}
