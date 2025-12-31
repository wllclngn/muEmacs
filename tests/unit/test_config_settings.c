// test_config_settings.c - Unit tests for settings system
// Tests src/config/settings.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

// Test setting integer values
static int test_settings_int(void) {
    int ok = 1;
    PHASE_START("SETTINGS: INT", "Integer settings");

    // Test $fillcol (fill column)
    int orig_fillcol = fillcol;

    // Set via variable system
    fillcol = 72;
    if (fillcol != 72) {
        LOG_ERRORF("[FAIL] fillcol not set: %d", fillcol);
        ok = 0;
    }

    // Test $tabsize
    int orig_tabsize = tabsize;
    tabsize = 4;
    if (tabsize != 4) {
        LOG_ERRORF("[FAIL] tabsize not set: %d", tabsize);
        ok = 0;
    }

    // Restore
    fillcol = orig_fillcol;
    tabsize = orig_tabsize;

    PHASE_END("SETTINGS: INT", ok);
    return ok;
}

// Test boolean mode settings
static int test_settings_mode(void) {
    int ok = 1;
    PHASE_START("SETTINGS: MODE", "Mode settings");

    // Ensure we have a buffer for testing modes
    if (!curbp) {
        struct buffer *bp = bfind("test-settings", true, 0);
        if (bp) {
            curbp = bp;
            if (curwp) curwp->w_bufp = bp;
        }
    }

    if (!curbp) {
        LOG_ERROR("[FAIL] no current buffer");
        ok = 0;
        PHASE_END("SETTINGS: MODE", ok);
        return ok;
    }

    int orig_mode = curbp->b_mode;

    // Test MDWRAP mode
    curbp->b_mode |= MDWRAP;
    if (!(curbp->b_mode & MDWRAP)) {
        LOG_ERROR("[FAIL] MDWRAP not set");
        ok = 0;
    }

    curbp->b_mode &= ~MDWRAP;
    if (curbp->b_mode & MDWRAP) {
        LOG_ERROR("[FAIL] MDWRAP not cleared");
        ok = 0;
    }

    // Test MDEXACT mode
    curbp->b_mode |= MDEXACT;
    if (!(curbp->b_mode & MDEXACT)) {
        LOG_ERROR("[FAIL] MDEXACT not set");
        ok = 0;
    }

    // Restore
    curbp->b_mode = orig_mode;

    PHASE_END("SETTINGS: MODE", ok);
    return ok;
}

// Test global flags
static int test_settings_flags(void) {
    int ok = 1;
    PHASE_START("SETTINGS: FLAGS", "Global flags");

    // Test discmd (display command flag)
    int orig_discmd = discmd;
    discmd = true;
    if (!discmd) {
        LOG_ERROR("[FAIL] discmd not set");
        ok = 0;
    }

    // Test disinp (display input flag)
    int orig_disinp = disinp;
    disinp = true;
    if (!disinp) {
        LOG_ERROR("[FAIL] disinp not set");
        ok = 0;
    }

    // Restore
    discmd = orig_discmd;
    disinp = orig_disinp;

    PHASE_END("SETTINGS: FLAGS", ok);
    return ok;
}

// Test color settings
static int test_settings_color(void) {
    int ok = 1;
    PHASE_START("SETTINGS: COLOR", "Color settings");

    int orig_gfcolor = gfcolor;
    int orig_gbcolor = gbcolor;

    // Set foreground
    gfcolor = 7;  // White
    if (gfcolor != 7) {
        LOG_ERROR("[FAIL] gfcolor not set");
        ok = 0;
    }

    // Set background
    gbcolor = 0;  // Black
    if (gbcolor != 0) {
        LOG_ERROR("[FAIL] gbcolor not set");
        ok = 0;
    }

    // Restore
    gfcolor = orig_gfcolor;
    gbcolor = orig_gbcolor;

    PHASE_END("SETTINGS: COLOR", ok);
    return ok;
}

// Test search settings
static int test_settings_search(void) {
    int ok = 1;
    PHASE_START("SETTINGS: SEARCH", "Search settings");

    // Test search pattern storage
    char orig_pat[NPAT];
    strncpy(orig_pat, pat, NPAT);

    strncpy(pat, "test-pattern", NPAT);
    if (strcmp(pat, "test-pattern") != 0) {
        LOG_ERROR("[FAIL] search pattern not set");
        ok = 0;
    }

    // Restore
    strncpy(pat, orig_pat, NPAT);

    PHASE_END("SETTINGS: SEARCH", ok);
    return ok;
}

// Test terminal size settings
static int test_settings_terminal(void) {
    int ok = 1;
    PHASE_START("SETTINGS: TERMINAL", "Terminal settings");

    // Terminal dimensions should be positive
    if (term.t_nrow <= 0) {
        LOG_ERRORF("[FAIL] t_nrow not positive: %d", term.t_nrow);
        ok = 0;
    }

    if (term.t_ncol <= 0) {
        LOG_ERRORF("[FAIL] t_ncol not positive: %d", term.t_ncol);
        ok = 0;
    }

    // Max should be >= current
    if (term.t_mrow < term.t_nrow) {
        LOG_ERROR("[FAIL] t_mrow < t_nrow");
        ok = 0;
    }

    if (term.t_mcol < term.t_ncol) {
        LOG_ERROR("[FAIL] t_mcol < t_ncol");
        ok = 0;
    }

    PHASE_END("SETTINGS: TERMINAL", ok);
    return ok;
}

// Entry point
int test_config_settings(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("settings");

    PHASE_START("SETTINGS", "Settings system tests");

    ok &= test_settings_int();
    ok &= test_settings_mode();
    ok &= test_settings_flags();
    ok &= test_settings_color();
    ok &= test_settings_search();
    ok &= test_settings_terminal();

    if (ok) {
        LOG_INFO("[SUCCESS] All settings tests passed.");
    }

    PHASE_END("SETTINGS", ok);
    return ok;
}
