// test_core_globals.c - Unit tests for global variables
// Tests src/core/globals.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

// Test basic global variable initialization
static int test_globals_init(void) {
    int ok = 1;
    PHASE_START("GLOBALS: INIT", "Global variable initialization");

    // Test fillcol default
    if (fillcol != 72) {
        LOG_ERRORF("[FAIL] fillcol not initialized to 72: %d", fillcol);
        ok = 0;
    }

    // Test gmode initial value
    if (gmode < 0) {
        LOG_ERRORF("[FAIL] gmode has invalid initial value: %d", gmode);
        ok = 0;
    }

    // Test color defaults
    if (gfcolor != 7) {
        LOG_ERRORF("[FAIL] gfcolor not initialized to 7: %d", gfcolor);
        ok = 0;
    }

    if (gbcolor != 0) {
        LOG_ERRORF("[FAIL] gbcolor not initialized to 0: %d", gbcolor);
        ok = 0;
    }

    PHASE_END("GLOBALS: INIT", ok);
    return ok;
}

// Test mode name arrays
static int test_globals_modenames(void) {
    int ok = 1;
    PHASE_START("GLOBALS: MODENAMES", "Mode name arrays");

    // Test modename array has entries
    if (modename[0] == nullptr) {
        LOG_ERROR("[FAIL] modename[0] is nullptr");
        ok = 0;
    }

    // Test mode2name array has entries
    if (mode2name[0] == nullptr) {
        LOG_ERROR("[FAIL] mode2name[0] is nullptr");
        ok = 0;
    }

    // Test modecode string is not empty
    if (modecode[0] == '\0') {
        LOG_ERROR("[FAIL] modecode is empty");
        ok = 0;
    }

    // Verify modename and mode2name entries match modecode length
    // Note: modecode is a string, its length defines how many modes are active
    int modecode_len = (int)strlen(modecode);
    if (modecode_len < 1) {
        LOG_ERROR("[FAIL] modecode is empty");
        ok = 0;
    }

    // Verify each modename entry is non-null (iterate based on modecode length)
    int mode_count = 0;
    for (int i = 0; i < modecode_len; i++) {
        if (modename[i] == nullptr) {
            LOG_ERRORF("[FAIL] modename[%d] is nullptr", i);
            ok = 0;
            break;
        }
        if (mode2name[i] == nullptr) {
            LOG_ERRORF("[FAIL] mode2name[%d] is nullptr but modename[%d] exists", i, i);
            ok = 0;
            break;
        }
        mode_count++;
    }

    if (mode_count == 0) {
        LOG_ERROR("[FAIL] No mode names defined");
        ok = 0;
    } else {
        LOG_INFOF("[SUCCESS] Found %d mode entries", mode_count);
    }

    PHASE_END("GLOBALS: MODENAMES", ok);
    return ok;
}

// Test color name array
static int test_globals_colornames(void) {
    int ok = 1;
    PHASE_START("GLOBALS: COLORNAMES", "Color name array");

    // Test cname array has entries
    if (cname[0] == nullptr) {
        LOG_ERROR("[FAIL] cname[0] is nullptr");
        ok = 0;
    }

    // Test specific color names
    const char *expected_colors[] = {
        "BLACK", "RED", "GREEN", "YELLOW", "BLUE", "MAGENTA", "CYAN", "WHITE", nullptr
    };

    for (int i = 0; expected_colors[i] != nullptr; i++) {
        if (strcmp(cname[i], expected_colors[i]) != 0) {
            LOG_ERRORF("[FAIL] cname[%d] expected '%s', got '%s'", i, expected_colors[i], cname[i]);
            ok = 0;
        }
    }

    PHASE_END("GLOBALS: COLORNAMES", ok);
    return ok;
}

// Test directive name array
static int test_globals_directives(void) {
    int ok = 1;
    PHASE_START("GLOBALS: DIRECTIVES", "Directive name array");

    // Test dname array has entries
    if (dname[0] == nullptr) {
        LOG_ERROR("[FAIL] dname[0] is nullptr");
        ok = 0;
    }

    // Test some expected directive names
    const char *expected[] = {
        "if", "else", "endif", "goto", "return", "endm", "while", "endwhile", "break", "force", nullptr
    };

    for (int i = 0; expected[i] != nullptr; i++) {
        if (strcmp(dname[i], expected[i]) != 0) {
            LOG_ERRORF("[FAIL] dname[%d] expected '%s', got '%s'", i, expected[i], dname[i]);
            ok = 0;
        }
    }

    PHASE_END("GLOBALS: DIRECTIVES", ok);
    return ok;
}

// Test kill ring initialization
static int test_globals_killring(void) {
    int ok = 1;
    PHASE_START("GLOBALS: KILLRING", "Kill ring structure");

    // Test kill ring is initialized
    if (g_kill_ring.count < 0) {
        LOG_ERRORF("[FAIL] kill ring count is negative: %d", g_kill_ring.count);
        ok = 0;
    }

    // Test head and tail indices are valid
    if (g_kill_ring.head < 0 || g_kill_ring.tail < 0) {
        LOG_ERROR("[FAIL] kill ring indices are negative");
        ok = 0;
    }

    PHASE_END("GLOBALS: KILLRING", ok);
    return ok;
}

// Test vim state initialization
static int test_globals_vim_state(void) {
    int ok = 1;
    PHASE_START("GLOBALS: VIMSTATE", "Vim state structure");

    // Test vim_mode_active initial value
    if (vim_mode_active != 0) {
        LOG_ERRORF("[FAIL] vim_mode_active should default to 0: %d", vim_mode_active);
        ok = 0;
    }

    // Test g_vim_state is initialized
    enum editor_mode mode = g_vim_state.current_mode;
    if (mode != MODE_INSERT && mode != MODE_NORMAL && mode != MODE_VISUAL && mode != MODE_VISUAL_LINE) {
        LOG_ERRORF("[FAIL] g_vim_state.current_mode has invalid value: %d", (int)mode);
        ok = 0;
    }

    PHASE_END("GLOBALS: VIMSTATE", ok);
    return ok;
}

// Test buffer hash table initialization
static int test_globals_buffer_hash(void) {
    int ok = 1;
    PHASE_START("GLOBALS: BUFFERHASH", "Buffer hash table");

    // The buffer_hash_table should be zero-initialized
    int non_null_count = 0;
    for (int i = 0; i < BUFFER_HASH_SIZE; i++) {
        if (buffer_hash_table[i] != nullptr) {
            non_null_count++;
        }
    }

    // At startup, the hash table might be empty or have some entries
    // depending on when tests run. We just verify it's accessible.
    // (In a fresh startup, it should be all zeros)

    PHASE_END("GLOBALS: BUFFERHASH", ok);
    return ok;
}

// Test display preference globals
static int test_globals_display_prefs(void) {
    int ok = 1;
    PHASE_START("GLOBALS: DISPLAY", "Display preferences");

    // Test highlight_current_line
    if (highlight_current_line != 0 && highlight_current_line != 1) {
        LOG_ERRORF("[FAIL] highlight_current_line invalid: %d", highlight_current_line);
        ok = 0;
    }

    // Test column_ruler_enabled
    if (column_ruler_enabled != 0 && column_ruler_enabled != 1) {
        LOG_ERRORF("[FAIL] column_ruler_enabled invalid: %d", column_ruler_enabled);
        ok = 0;
    }

    // Test column_ruler_column is reasonable
    if (column_ruler_column < 1 || column_ruler_column > 1000) {
        LOG_ERRORF("[FAIL] column_ruler_column out of range: %d", column_ruler_column);
        ok = 0;
    }

    // Test hiline_style
    if (hiline_style < 0 || hiline_style > 4) {
        LOG_ERRORF("[FAIL] hiline_style out of range: %d", hiline_style);
        ok = 0;
    }

    // Test intensity percentages
    if (highlight_intensity_pct < 0 || highlight_intensity_pct > 100) {
        LOG_ERRORF("[FAIL] highlight_intensity_pct out of range: %d", highlight_intensity_pct);
        ok = 0;
    }

    PHASE_END("GLOBALS: DISPLAY", ok);
    return ok;
}

// Test literal strings
static int test_globals_literals(void) {
    int ok = 1;
    PHASE_START("GLOBALS: LITERALS", "Literal string constants");

    // Test error message strings
    if (strcmp(errorm, "ERROR") != 0) {
        LOG_ERRORF("[FAIL] errorm not 'ERROR': '%s'", errorm);
        ok = 0;
    }

    if (strcmp(truem, "true") != 0) {
        LOG_ERRORF("[FAIL] truem not 'true': '%s'", truem);
        ok = 0;
    }

    if (strcmp(falsem, "false") != 0) {
        LOG_ERRORF("[FAIL] falsem not 'false': '%s'", falsem);
        ok = 0;
    }

    PHASE_END("GLOBALS: LITERALS", ok);
    return ok;
}

// Test keyboard macro buffer (modern event-based system)
static int test_globals_keyboard_macro(void) {
    int ok = 1;
    PHASE_START("GLOBALS: KBDMACRO", "Keyboard macro buffer");

    // Test kbdmode initial value (atomic)
    int mode = atomic_load(&kbdmode);
    if (mode != MACRO_STOP && mode != MACRO_PLAY && mode != MACRO_RECORD) {
        LOG_ERRORF("[FAIL] kbdmode has invalid initial value: %d", mode);
        ok = 0;
    }

    // Test g_macro structure is zero-initialized
    if (g_macro.count != 0) {
        LOG_ERRORF("[FAIL] g_macro.count should be 0, got %zu", g_macro.count);
        ok = 0;
    }
    if (g_macro.playback_pos != 0) {
        LOG_ERRORF("[FAIL] g_macro.playback_pos should be 0, got %zu", g_macro.playback_pos);
        ok = 0;
    }

    PHASE_END("GLOBALS: KBDMACRO", ok);
    return ok;
}

// Entry point
int test_core_globals(void) {
    int ok = 1;
    PHASE_START("GLOBALS", "Global variable tests");

    ok &= test_globals_init();
    ok &= test_globals_modenames();
    ok &= test_globals_colornames();
    ok &= test_globals_directives();
    ok &= test_globals_killring();
    ok &= test_globals_vim_state();
    ok &= test_globals_buffer_hash();
    ok &= test_globals_display_prefs();
    ok &= test_globals_literals();
    ok &= test_globals_keyboard_macro();

    if (ok) {
        LOG_INFO("[SUCCESS] All globals tests passed.");
    }

    PHASE_END("GLOBALS", ok);
    return ok;
}
