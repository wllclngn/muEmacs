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
// INPUT PARSER TESTS
// ============================================================================
// NOTE: Bracketed paste tests removed - paste handling is done at the event
// level (input_read_event), not raw byte level (input_read_byte). Paste
// functionality is tested through TUI tests instead.

static int test_input_parser_basic(void) {
    int ok = 1;
    PHASE_START("INPUT: PARSER-BASIC", "Basic input parser functionality");

    // Test that parser state functions exist and don't crash
    input_reset_parser_state();

    // Basic validation that input system is initialized
    if (term.t_getchar == nullptr) {
        LOG_ERROR("[FAIL] term.t_getchar is nullptr");
        ok = 0;
    }

    PHASE_END("INPUT: PARSER-BASIC", ok);
    return ok;
}

// ============================================================================
// ENTRY POINT
// ============================================================================

int test_io_input(void) {
    int all_passed = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("io-input");

    // Basic parser test - paste tests moved to TUI level
    all_passed &= test_input_parser_basic();

    return all_passed;
}
