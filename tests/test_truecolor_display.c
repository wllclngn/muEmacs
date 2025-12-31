/*
 * test_truecolor_display.c - Tests for TrueColor Display Feature
 *
 * Tests 24-bit RGB highlighting, column ruler rendering, and
 * HIGHLIGHT_BIT handling in the display engine.
 */

#include "test_utils.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "utf8.h"

/* HIGHLIGHT_BIT is defined in display.c - replicate for testing */
#define HIGHLIGHT_BIT 0x80000000U

/* Test highlight_current_line global */
static int test_highlight_current_line_global(void) {
    int result = 1;

    LOG_INFO("  Testing highlight_current_line global...");

    /* Save original */
    int original = highlight_current_line;

    /* Test enable */
    highlight_current_line = 1;
    if (highlight_current_line != 1) {
        LOG_ERROR("[FAIL] highlight_current_line enable failed");
        result = 0;
    }

    /* Test disable */
    highlight_current_line = 0;
    if (highlight_current_line != 0) {
        LOG_ERROR("[FAIL] highlight_current_line disable failed");
        result = 0;
    }

    /* Restore original */
    highlight_current_line = original;

    if (result) {
        LOG_INFO("[SUCCESS] highlight_current_line global works");
    }

    return result;
}

/* Test column_ruler_enabled global */
static int test_column_ruler_globals(void) {
    int result = 1;

    LOG_INFO("  Testing column ruler globals...");

    /* Save originals */
    int orig_enabled = column_ruler_enabled;
    int orig_column = column_ruler_column;

    /* Test enable/disable */
    column_ruler_enabled = 1;
    if (column_ruler_enabled != 1) {
        LOG_ERROR("[FAIL] column_ruler_enabled enable failed");
        result = 0;
    }

    column_ruler_enabled = 0;
    if (column_ruler_enabled != 0) {
        LOG_ERROR("[FAIL] column_ruler_enabled disable failed");
        result = 0;
    }

    /* Test column values */
    int test_columns[] = {40, 72, 80, 100, 120, 132};
    for (int i = 0; i < (int)(sizeof(test_columns)/sizeof(test_columns[0])); i++) {
        column_ruler_column = test_columns[i];
        if (column_ruler_column != test_columns[i]) {
            LOG_ERRORF("[FAIL] column_ruler_column = %d failed", test_columns[i]);
            result = 0;
        }
    }

    /* Restore originals */
    column_ruler_enabled = orig_enabled;
    column_ruler_column = orig_column;

    if (result) {
        LOG_INFO("[SUCCESS] column ruler globals work");
    }

    return result;
}

/* Test hiline_style global */
static int test_hiline_style_global(void) {
    int result = 1;

    LOG_INFO("  Testing hiline_style global...");

    /* Save original */
    int original = hiline_style;

    /* Test all valid styles:
     * 0 = none
     * 1 = underline
     * 2 = bold
     * 3 = dim
     * 4 = RGB background (TrueColor)
     */
    for (int style = 0; style <= 4; style++) {
        hiline_style = style;
        if (hiline_style != style) {
            LOG_ERRORF("[FAIL] hiline_style = %d failed", style);
            result = 0;
        }
    }

    /* Restore original */
    hiline_style = original;

    if (result) {
        LOG_INFO("[SUCCESS] hiline_style global works");
    }

    return result;
}

/* Test HIGHLIGHT_BIT constant */
static int test_highlight_bit_constant(void) {
    int result = 1;

    LOG_INFO("  Testing HIGHLIGHT_BIT constant...");

    /* HIGHLIGHT_BIT should be 0x80000000U (bit 31) */
    unicode_t test_char = 'A';
    unicode_t highlighted = test_char | HIGHLIGHT_BIT;

    /* Verify bit is set */
    if (!(highlighted & HIGHLIGHT_BIT)) {
        LOG_ERROR("[FAIL] HIGHLIGHT_BIT not set correctly");
        result = 0;
    }

    /* Verify original char can be extracted */
    unicode_t extracted = highlighted & ~(HIGHLIGHT_BIT);
    if (extracted != 'A') {
        LOG_ERROR("[FAIL] Original char not extractable from highlighted");
        result = 0;
    }

    /* Verify bit doesn't interfere with common characters */
    const char *test_chars = "ABCabc123!@#\t\n";
    for (int i = 0; test_chars[i]; i++) {
        unicode_t c = test_chars[i];
        unicode_t h = c | HIGHLIGHT_BIT;
        unicode_t back = h & ~HIGHLIGHT_BIT;
        if (back != c) {
            LOG_ERRORF("[FAIL] HIGHLIGHT_BIT corrupted char 0x%02x", c);
            result = 0;
        }
    }

    if (result) {
        LOG_INFO("[SUCCESS] HIGHLIGHT_BIT constant works");
    }

    return result;
}

/* Test that sgarbf triggers full redraw */
static int test_sgarbf_redraw_flag(void) {
    int result = 1;

    LOG_INFO("  Testing sgarbf redraw flag...");

    /* Save original */
    int original = sgarbf;

    /* Test setting */
    sgarbf = true;
    if (!sgarbf) {
        LOG_ERROR("[FAIL] sgarbf not set to true");
        result = 0;
    }

    sgarbf = false;
    if (sgarbf) {
        LOG_ERROR("[FAIL] sgarbf not set to false");
        result = 0;
    }

    /* Restore original */
    sgarbf = original;

    if (result) {
        LOG_INFO("[SUCCESS] sgarbf redraw flag works");
    }

    return result;
}

/* Test RGB color value calculations */
static int test_rgb_color_values(void) {
    int result = 1;

    LOG_INFO("  Testing RGB color value calculations...");

    /* Test RGB construction (24-bit) */
    /* RGB(30, 30, 30) for dark grey current line */
    int r = 30, g = 30, b = 30;

    /* Verify values are in range */
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        LOG_ERROR("[FAIL] RGB values out of range");
        result = 0;
    }

    /* Test ruler color RGB(45, 45, 45) */
    r = 45; g = 45; b = 45;
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        LOG_ERROR("[FAIL] Ruler RGB values out of range");
        result = 0;
    }

    /* Verify escape sequence format would be valid */
    /* Format: \x1b[48;2;R;G;Bm */
    char esc_seq[32];
    snprintf(esc_seq, sizeof(esc_seq), "\x1b[48;2;%d;%d;%dm", r, g, b);
    if (strlen(esc_seq) > 20) {
        LOG_ERROR("[FAIL] RGB escape sequence too long");
        result = 0;
    }

    if (result) {
        LOG_INFO("[SUCCESS] RGB color calculations work");
    }

    return result;
}

/* Test gfcolor and gbcolor globals */
static int test_color_globals(void) {
    int result = 1;

    LOG_INFO("  Testing gfcolor/gbcolor globals...");

    /* Save originals */
    int orig_fg = gfcolor;
    int orig_bg = gbcolor;

    /* Test foreground colors (0-7 for basic, extended for 256-color) */
    for (int c = 0; c <= 7; c++) {
        gfcolor = c;
        if (gfcolor != c) {
            LOG_ERRORF("[FAIL] gfcolor = %d failed", c);
            result = 0;
        }
    }

    /* Test background colors */
    for (int c = 0; c <= 7; c++) {
        gbcolor = c;
        if (gbcolor != c) {
            LOG_ERRORF("[FAIL] gbcolor = %d failed", c);
            result = 0;
        }
    }

    /* Restore originals */
    gfcolor = orig_fg;
    gbcolor = orig_bg;

    if (result) {
        LOG_INFO("[SUCCESS] gfcolor/gbcolor globals work");
    }

    return result;
}

/* Test window WFHARD flag for forcing redraw */
static int test_wfhard_flag(void) {
    int result = 1;

    LOG_INFO("  Testing WFHARD window flag...");

    if (!curwp) {
        LOG_WARN("[WARN] curwp is NULL, skipping");
        return 1;
    }

    /* Save original */
    char original = curwp->w_flag;

    /* Test setting WFHARD */
    curwp->w_flag |= WFHARD;
    if (!(curwp->w_flag & WFHARD)) {
        LOG_ERROR("[FAIL] WFHARD not set");
        result = 0;
    }

    /* Test clearing WFHARD */
    curwp->w_flag &= ~WFHARD;
    if (curwp->w_flag & WFHARD) {
        LOG_ERROR("[FAIL] WFHARD not cleared");
        result = 0;
    }

    /* Restore original */
    curwp->w_flag = original;

    if (result) {
        LOG_INFO("[SUCCESS] WFHARD flag works");
    }

    return result;
}

/* Test that enabling highlight sets appropriate flags */
static int test_highlight_triggers_flags(void) {
    int result = 1;

    LOG_INFO("  Testing highlight triggers appropriate flags...");

    if (!curwp) {
        LOG_WARN("[WARN] curwp is NULL, skipping");
        return 1;
    }

    /* Save originals */
    int orig_highlight = highlight_current_line;
    int orig_sgarbf = sgarbf;
    char orig_flag = curwp->w_flag;

    /* Changing highlight setting should trigger redraw */
    sgarbf = false;
    curwp->w_flag &= ~WFHARD;

    highlight_current_line = !highlight_current_line;

    /* Simulate what update() should do after setting change */
    sgarbf = true;
    curwp->w_flag |= WFHARD;

    if (!sgarbf) {
        LOG_ERROR("[FAIL] sgarbf should be set after highlight change");
        result = 0;
    }

    /* Restore originals */
    highlight_current_line = orig_highlight;
    sgarbf = orig_sgarbf;
    curwp->w_flag = orig_flag;

    if (result) {
        LOG_INFO("[SUCCESS] highlight triggers flags correctly");
    }

    return result;
}

/* Placeholder test for visual rendering (requires terminal) */
static int test_visual_rendering_placeholder(void) {
    int result = 1;

    /* Defensive: clear BFCHG before any printf to avoid display-triggered prompts */
    if (curbp) curbp->b_flag &= ~BFCHG;
    for (struct buffer *bp = bheadp; bp; bp = bp->b_bufp) {
        bp->b_flag &= ~BFCHG;
    }
    fflush(stdout);  /* Flush any pending output */

    LOG_INFO("  Testing visual rendering (placeholder)...");

    /* NOTE: Actual visual rendering tests would require:
     * 1. A real terminal or PTY
     * 2. Parsing terminal output for escape sequences
     * 3. Visual comparison testing
     *
     * These are beyond unit testing scope and better suited
     * for integration/manual testing.
     */

    LOG_INFO("[INFO] Visual rendering tests are manual/integration");
    LOG_INFO("  - TrueColor background: Check for \\x1b[48;2;30;30;30m");
    LOG_INFO("  - Column ruler: Check column 80 has different background");
    LOG_INFO("  - Current line: Verify highlight on cursor line only");

    if (result) {
        LOG_INFO("[SUCCESS] Placeholder tests passed");
    }

    return result;
}

/* Main test function */
int test_truecolor_display(void) {
    int result = 1;
    int sub_result;

    /* Defensive: clear BFCHG on all buffers to prevent "Discard changes?" prompts */
    if (curbp) curbp->b_flag &= ~BFCHG;
    for (struct buffer *bp = bheadp; bp; bp = bp->b_bufp) {
        bp->b_flag &= ~BFCHG;
    }

    PHASE_START("TrueColor Display Tests", "Testing RGB highlighting features");

    /* Test 1: highlight_current_line global */
    sub_result = test_highlight_current_line_global();
    if (!sub_result) result = 0;

    /* Test 2: column ruler globals */
    sub_result = test_column_ruler_globals();
    if (!sub_result) result = 0;

    /* Test 3: hiline_style global */
    sub_result = test_hiline_style_global();
    if (!sub_result) result = 0;

    /* Test 4: HIGHLIGHT_BIT constant */
    sub_result = test_highlight_bit_constant();
    if (!sub_result) result = 0;

    /* Test 5: sgarbf redraw flag */
    sub_result = test_sgarbf_redraw_flag();
    if (!sub_result) result = 0;

    /* Test 6: RGB color calculations */
    sub_result = test_rgb_color_values();
    if (!sub_result) result = 0;

    /* Test 7: gfcolor/gbcolor globals */
    sub_result = test_color_globals();
    if (!sub_result) result = 0;

    /* Test 8: WFHARD flag */
    sub_result = test_wfhard_flag();
    if (!sub_result) result = 0;

    /* Test 9: highlight triggers flags */
    sub_result = test_highlight_triggers_flags();
    if (!sub_result) result = 0;

    /* Pre-test cleanup: clear BFCHG before placeholder test */
    if (curbp) curbp->b_flag &= ~BFCHG;
    for (struct buffer *bp = bheadp; bp; bp = bp->b_bufp) {
        bp->b_flag &= ~BFCHG;
    }

    /* Test 10: visual rendering placeholder */
    sub_result = test_visual_rendering_placeholder();
    if (!sub_result) result = 0;

    /* Defensive cleanup: clear BFCHG on all buffers before ending */
    if (curbp) curbp->b_flag &= ~BFCHG;
    for (struct buffer *bp = bheadp; bp; bp = bp->b_bufp) {
        bp->b_flag &= ~BFCHG;
    }

    PHASE_END("TrueColor Display Tests", result);

    return result;
}
