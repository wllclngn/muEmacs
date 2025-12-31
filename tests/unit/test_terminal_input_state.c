// test_terminal_input_state.c - Unit tests for terminal input state machine
// Tests src/terminal/input_state.c escape sequence parsing

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "terminal/input_state.h"
#include <string.h>

// ============================================================================
// Parser Lifecycle Tests
// ============================================================================

static int test_input_parser_init(void) {
    int ok = 1;
    PHASE_START("INPUT: INIT", "Input parser initialization");

    input_parser_t parser;
    input_parser_init(&parser);

    // Verify initial state
    if (parser.state != INPUT_GROUND) {
        LOG_ERROR("[FAIL] Initial state not INPUT_GROUND");
        ok = 0;
    }

    if (parser.csi_len != 0) {
        LOG_ERROR("[FAIL] Initial csi_len not zero");
        ok = 0;
    }

    // Verify statistics are zero
    uint64_t seqs, chars, errs, bytes;
    input_parser_get_stats(&parser, &seqs, &chars, &errs, &bytes);

    if (seqs != 0 || chars != 0 || errs != 0 || bytes != 0) {
        LOG_ERROR("[FAIL] Initial stats not zero");
        ok = 0;
    }

    input_parser_destroy(&parser);
    PHASE_END("INPUT: INIT", ok);
    return ok;
}

static int test_input_parser_reset(void) {
    int ok = 1;
    PHASE_START("INPUT: RESET", "Input parser reset");

    input_parser_t parser;
    input_parser_init(&parser);

    // Pollute the parser state
    parser.state = INPUT_CSI_PARAM;
    parser.csi_len = 10;
    parser.utf8_expected = 2;

    // Reset
    input_parser_reset(&parser);

    // Verify state is reset
    if (parser.state != INPUT_GROUND) {
        LOG_ERROR("[FAIL] State not reset to INPUT_GROUND");
        ok = 0;
    }

    if (parser.csi_len != 0) {
        LOG_ERROR("[FAIL] csi_len not reset");
        ok = 0;
    }

    if (parser.utf8_expected != 0) {
        LOG_ERROR("[FAIL] utf8_expected not reset");
        ok = 0;
    }

    input_parser_destroy(&parser);
    PHASE_END("INPUT: RESET", ok);
    return ok;
}

// ============================================================================
// Basic Character Input Tests
// ============================================================================

static int test_input_ascii_chars(void) {
    int ok = 1;
    PHASE_START("INPUT: ASCII", "ASCII character parsing");

    input_parser_t parser;
    input_parser_init(&parser);

    input_key_event_t event;

    // Feed ASCII 'A'
    int result = input_parser_feed(&parser, 'A', &event);

    if (result != 1) {
        LOG_ERROR("[FAIL] ASCII char did not produce event");
        ok = 0;
    }

    if (event.type != KEY_CHAR) {
        LOG_ERROR("[FAIL] Event type not KEY_CHAR");
        ok = 0;
    }

    if (event.code != 'A') {
        LOG_ERROR("[FAIL] Event code not 'A'");
        ok = 0;
    }

    input_parser_destroy(&parser);
    PHASE_END("INPUT: ASCII", ok);
    return ok;
}

static int test_input_control_chars(void) {
    int ok = 1;
    PHASE_START("INPUT: CTRL", "Control character parsing");

    input_parser_t parser;
    input_parser_init(&parser);

    input_key_event_t event;

    // Feed Ctrl+A (0x01)
    int result = input_parser_feed(&parser, 0x01, &event);

    if (result != 1) {
        LOG_ERROR("[FAIL] Ctrl char did not produce event");
        ok = 0;
    }

    if (event.type != KEY_CHAR) {
        LOG_ERROR("[FAIL] Event type not KEY_CHAR");
        ok = 0;
    }

    if (event.modifiers != MOD_CTRL) {
        LOG_ERROR("[FAIL] Modifiers not MOD_CTRL");
        ok = 0;
    }

    input_parser_destroy(&parser);
    PHASE_END("INPUT: CTRL", ok);
    return ok;
}

// ============================================================================
// UTF-8 Sequence Tests
// ============================================================================

static int test_input_utf8_2byte(void) {
    int ok = 1;
    PHASE_START("INPUT: UTF8-2", "2-byte UTF-8 sequence");

    input_parser_t parser;
    input_parser_init(&parser);

    input_key_event_t event;

    // Euro sign € = 0xE2 0x82 0xAC (3 bytes)
    // Let's use simpler 2-byte: á = 0xC3 0xA1
    int result = input_parser_feed(&parser, 0xC3, &event);
    if (result != 0) {
        LOG_ERROR("[FAIL] First byte produced event");
        ok = 0;
    }

    result = input_parser_feed(&parser, 0xA1, &event);
    if (result != 1) {
        LOG_ERROR("[FAIL] Second byte did not produce event");
        ok = 0;
    }

    if (event.type != KEY_CHAR) {
        LOG_ERROR("[FAIL] Event type not KEY_CHAR");
        ok = 0;
    }

    // á = U+00E1
    if (event.code != 0x00E1) {
        LOG_ERRORF("[FAIL] Wrong codepoint: got 0x%X expected 0x00E1", event.code);
        ok = 0;
    }

    input_parser_destroy(&parser);
    PHASE_END("INPUT: UTF8-2", ok);
    return ok;
}

// ============================================================================
// Escape Sequence Tests
// ============================================================================

static int test_input_escape_basic(void) {
    int ok = 1;
    PHASE_START("INPUT: ESC", "Basic escape handling");

    input_parser_t parser;
    input_parser_init(&parser);

    input_key_event_t event;

    // Feed ESC
    int result = input_parser_feed(&parser, 0x1B, &event);

    if (result != 0) {
        LOG_ERROR("[FAIL] ESC immediately produced event");
        ok = 0;
    }

    if (parser.state != INPUT_ESCAPE) {
        LOG_ERROR("[FAIL] State not INPUT_ESCAPE after ESC");
        ok = 0;
    }

    // Check if ESC is pending
    if (!input_parser_has_pending_esc(&parser)) {
        LOG_ERROR("[FAIL] ESC not marked as pending");
        ok = 0;
    }

    // Flush ESC
    result = input_parser_flush_esc(&parser, &event);

    if (result != 1) {
        LOG_ERROR("[FAIL] Flush ESC did not produce event");
        ok = 0;
    }

    if (event.code != 0x1B) {
        LOG_ERROR("[FAIL] Flushed event not ESC");
        ok = 0;
    }

    input_parser_destroy(&parser);
    PHASE_END("INPUT: ESC", ok);
    return ok;
}

// ============================================================================
// CSI Sequence Tests
// ============================================================================

static int test_input_csi_arrow_up(void) {
    int ok = 1;
    PHASE_START("INPUT: CSI-UP", "CSI arrow up sequence");

    input_parser_t parser;
    input_parser_init(&parser);

    input_key_event_t event;

    // Arrow up: ESC [ A
    const uint8_t seq[] = {0x1B, '[', 'A'};

    for (size_t i = 0; i < sizeof(seq); i++) {
        int result = input_parser_feed(&parser, seq[i], &event);

        // Last byte should produce event
        if (i == sizeof(seq) - 1) {
            if (result != 1) {
                LOG_ERROR("[FAIL] Final byte did not produce event");
                ok = 0;
            }

            if (event.type != KEY_SPECIAL) {
                LOG_ERROR("[FAIL] Event type not KEY_SPECIAL");
                ok = 0;
            }

            if (event.code != SPECIAL_UP) {
                LOG_ERROR("[FAIL] Event code not SPECIAL_UP");
                ok = 0;
            }
        } else {
            if (result != 0) {
                LOG_ERROR("[FAIL] Intermediate byte produced event");
                ok = 0;
            }
        }
    }

    input_parser_destroy(&parser);
    PHASE_END("INPUT: CSI-UP", ok);
    return ok;
}

static int test_input_csi_function_key(void) {
    int ok = 1;
    PHASE_START("INPUT: CSI-F1", "CSI function key F1");

    input_parser_t parser;
    input_parser_init(&parser);

    input_key_event_t event;

    // F1: ESC [ 1 1 ~
    const uint8_t seq[] = {0x1B, '[', '1', '1', '~'};

    int result = 0;
    for (size_t i = 0; i < sizeof(seq); i++) {
        result = input_parser_feed(&parser, seq[i], &event);
    }

    if (result != 1) {
        LOG_ERROR("[FAIL] F1 sequence did not produce event");
        ok = 0;
    }

    if (event.type != KEY_SPECIAL) {
        LOG_ERROR("[FAIL] Event type not KEY_SPECIAL");
        ok = 0;
    }

    if (event.code != SPECIAL_F1) {
        LOG_ERROR("[FAIL] Event code not SPECIAL_F1");
        ok = 0;
    }

    input_parser_destroy(&parser);
    PHASE_END("INPUT: CSI-F1", ok);
    return ok;
}

// ============================================================================
// SS3 Sequence Tests
// ============================================================================

static int test_input_ss3_arrow(void) {
    int ok = 1;
    PHASE_START("INPUT: SS3", "SS3 arrow sequence");

    input_parser_t parser;
    input_parser_init(&parser);

    input_key_event_t event;

    // Arrow up (application mode): ESC O A
    const uint8_t seq[] = {0x1B, 'O', 'A'};

    int result = 0;
    for (size_t i = 0; i < sizeof(seq); i++) {
        result = input_parser_feed(&parser, seq[i], &event);
    }

    if (result != 1) {
        LOG_ERROR("[FAIL] SS3 sequence did not produce event");
        ok = 0;
    }

    if (event.type != KEY_SPECIAL) {
        LOG_ERROR("[FAIL] Event type not KEY_SPECIAL");
        ok = 0;
    }

    if (event.code != SPECIAL_UP) {
        LOG_ERROR("[FAIL] Event code not SPECIAL_UP");
        ok = 0;
    }

    input_parser_destroy(&parser);
    PHASE_END("INPUT: SS3", ok);
    return ok;
}

// ============================================================================
// Bracketed Paste Tests
// ============================================================================

static int test_input_bracketed_paste(void) {
    int ok = 1;
    PHASE_START("INPUT: PASTE", "Bracketed paste sequences");

    input_parser_t parser;
    input_parser_init(&parser);

    input_key_event_t event;

    // Paste start: ESC [ 2 0 0 ~
    const uint8_t start_seq[] = {0x1B, '[', '2', '0', '0', '~'};

    int result = 0;
    for (size_t i = 0; i < sizeof(start_seq); i++) {
        result = input_parser_feed(&parser, start_seq[i], &event);
    }

    if (result != 1) {
        LOG_ERROR("[FAIL] Paste start did not produce event");
        ok = 0;
    }

    if (event.type != KEY_PASTE_START) {
        LOG_ERROR("[FAIL] Event type not KEY_PASTE_START");
        ok = 0;
    }

    // Should now be in paste mode
    if (parser.state != INPUT_PASTE_CONTENT) {
        LOG_ERROR("[FAIL] Not in INPUT_PASTE_CONTENT state");
        ok = 0;
    }

    input_parser_destroy(&parser);
    PHASE_END("INPUT: PASTE", ok);
    return ok;
}

// ============================================================================
// OSC Sequence Tests
// ============================================================================

static int test_input_osc_sequence(void) {
    int ok = 1;
    PHASE_START("INPUT: OSC", "OSC sequence handling");

    input_parser_t parser;
    input_parser_init(&parser);

    input_key_event_t event;

    // OSC sequence: ESC ] 0 ; title BEL
    const uint8_t seq[] = {0x1B, ']', '0', ';', 't', 'e', 's', 't', 0x07};

    int result = 0;
    for (size_t i = 0; i < sizeof(seq); i++) {
        result = input_parser_feed(&parser, seq[i], &event);
    }

    if (result != 1) {
        LOG_ERROR("[FAIL] OSC sequence did not produce event");
        ok = 0;
    }

    if (event.type != KEY_OSC_RECEIVED) {
        LOG_ERROR("[FAIL] Event type not KEY_OSC_RECEIVED");
        ok = 0;
    }

    input_parser_destroy(&parser);
    PHASE_END("INPUT: OSC", ok);
    return ok;
}

// ============================================================================
// Pending Buffer Tests
// ============================================================================

static int test_input_pushback(void) {
    int ok = 1;
    PHASE_START("INPUT: PUSHBACK", "Pending buffer pushback");

    input_parser_t parser;
    input_parser_init(&parser);

    // Pushback some bytes
    const uint8_t data[] = {'a', 'b', 'c'};
    input_parser_pushback(&parser, data, sizeof(data));

    // Verify we have pending bytes
    if (!input_parser_has_pending(&parser)) {
        LOG_ERROR("[FAIL] No pending bytes after pushback");
        ok = 0;
    }

    // Get them back
    for (size_t i = 0; i < sizeof(data); i++) {
        int c = input_parser_get_pending(&parser);
        if (c != data[i]) {
            LOG_ERRORF("[FAIL] Wrong byte from pending: got %d expected %d", c, data[i]);
            ok = 0;
        }
    }

    // Should be empty now
    if (input_parser_has_pending(&parser)) {
        LOG_ERROR("[FAIL] Still has pending after get_all");
        ok = 0;
    }

    input_parser_destroy(&parser);
    PHASE_END("INPUT: PUSHBACK", ok);
    return ok;
}

// ============================================================================
// Statistics Tests
// ============================================================================

static int test_input_statistics(void) {
    int ok = 1;
    PHASE_START("INPUT: STATS", "Parser statistics");

    input_parser_t parser;
    input_parser_init(&parser);

    input_key_event_t event;

    // Feed some input
    input_parser_feed(&parser, 'A', &event);  // 1 char
    input_parser_feed(&parser, 'B', &event);  // 1 char

    // Feed CSI sequence
    const uint8_t seq[] = {0x1B, '[', 'A'};
    for (size_t i = 0; i < sizeof(seq); i++) {
        input_parser_feed(&parser, seq[i], &event);
    }

    // Get stats
    uint64_t seqs, chars, errs, bytes;
    input_parser_get_stats(&parser, &seqs, &chars, &errs, &bytes);

    if (chars != 3) {  // A, B, arrow
        LOG_ERRORF("[FAIL] chars_emitted = %lu, expected 3", chars);
        ok = 0;
    }

    if (seqs != 1) {  // 1 CSI sequence
        LOG_ERRORF("[FAIL] sequences_parsed = %lu, expected 1", seqs);
        ok = 0;
    }

    if (bytes != 5) {  // A + B + ESC + [ + A
        LOG_ERRORF("[FAIL] bytes_processed = %lu, expected 5", bytes);
        ok = 0;
    }

    input_parser_destroy(&parser);
    PHASE_END("INPUT: STATS", ok);
    return ok;
}

// ============================================================================
// Entry Point
// ============================================================================

int test_terminal_input_state(void) {
    int result = 1;

    result &= test_input_parser_init();
    result &= test_input_parser_reset();
    result &= test_input_ascii_chars();
    result &= test_input_control_chars();
    result &= test_input_utf8_2byte();
    result &= test_input_escape_basic();
    result &= test_input_csi_arrow_up();
    result &= test_input_csi_function_key();
    result &= test_input_ss3_arrow();
    result &= test_input_bracketed_paste();
    result &= test_input_osc_sequence();
    result &= test_input_pushback();
    result &= test_input_statistics();

    return result;
}
