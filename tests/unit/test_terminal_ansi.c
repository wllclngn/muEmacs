// test_terminal_ansi.c - Unit tests for ANSI/VT100 parser
// Tests src/terminal/terminal.c ANSI parsing functions
//
// Note: The ANSI parser (parse_ansi/handle_csi) are static functions within
// terminal.c and require full terminal state initialization. These tests
// verify the terminal subsystem can be initialized and called without crashing,
// and document the expected behavior.

#include "../test_utils.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "gapbuffer.h"
#include "terminal/terminal.h"

// ============================================================================
// Terminal API Tests
// ============================================================================

static int test_terminal_init(void) {
    int ok = 1;
    PHASE_START("TERMINAL: INIT", "Terminal subsystem initialization");

    // Should not crash
    terminal_init();
    ok = 1;

    PHASE_END("TERMINAL: INIT", ok);
    return ok;
}

static int test_terminal_process_output_null_buffer(void) {
    int ok = 1;
    PHASE_START("TERMINAL: NULL-BUFFER", "Process output with NULL buffer");

    // Should handle gracefully without crashing
    terminal_process_output(NULL, "test", 4);
    ok = 1;

    PHASE_END("TERMINAL: NULL-BUFFER", ok);
    return ok;
}

static int test_terminal_process_output_null_data(void) {
    int ok = 1;
    PHASE_START("TERMINAL: NULL-DATA", "Process output with NULL data");

    // Create minimal buffer
    struct buffer *bp = malloc(sizeof(struct buffer));
    if (bp) {
        memset(bp, 0, sizeof(struct buffer));
        bp->b_linep = lalloc(0);

        // Should handle NULL data gracefully
        terminal_process_output(bp, NULL, 0);
        terminal_process_output(bp, "test", 0);  // Zero length

        // Cleanup
        lfree(bp->b_linep);
        free(bp);
        ok = 1;
    }

    PHASE_END("TERMINAL: NULL-DATA", ok);
    return ok;
}

static int test_terminal_process_output_empty_data(void) {
    int ok = 1;
    PHASE_START("TERMINAL: EMPTY-DATA", "Process empty output");

    // Create minimal buffer
    struct buffer *bp = malloc(sizeof(struct buffer));
    if (bp) {
        memset(bp, 0, sizeof(struct buffer));
        bp->b_linep = lalloc(0);

        // Should handle empty data gracefully
        terminal_process_output(bp, "", 0);
        terminal_process_output(bp, "data", 0);

        // Cleanup
        lfree(bp->b_linep);
        free(bp);
        ok = 1;
    }

    PHASE_END("TERMINAL: EMPTY-DATA", ok);
    return ok;
}

static int test_terminal_process_output_without_term_data(void) {
    int ok = 1;
    PHASE_START("TERMINAL: NO-TERM-DATA", "Process output without b_term_data");

    // Create minimal buffer without terminal state
    struct buffer *bp = malloc(sizeof(struct buffer));
    if (bp) {
        memset(bp, 0, sizeof(struct buffer));
        bp->b_linep = lalloc(0);
        // Leave b_term_data as NULL

        // Should return early without crashing (per design)
        terminal_process_output(bp, "Hello", 5);

        // Cleanup
        lfree(bp->b_linep);
        free(bp);
        ok = 1;
    }

    PHASE_END("TERMINAL: NO-TERM-DATA", ok);
    return ok;
}

// ============================================================================
// Edge Case Tests
// ============================================================================

static int test_ansi_edge_large_input(void) {
    int ok = 1;
    PHASE_START("ANSI: LARGE-INPUT", "Large data input handling");

    struct buffer *bp = malloc(sizeof(struct buffer));
    if (bp) {
        memset(bp, 0, sizeof(struct buffer));
        bp->b_linep = lalloc(0);

        // Create large input buffer
        char large_data[4096];
        memset(large_data, 'A', sizeof(large_data));

        // Should not crash with large input
        terminal_process_output(bp, large_data, sizeof(large_data));

        // Cleanup
        lfree(bp->b_linep);
        free(bp);
        ok = 1;
    }

    PHASE_END("ANSI: LARGE-INPUT", ok);
    return ok;
}

static int test_ansi_edge_special_chars(void) {
    int ok = 1;
    PHASE_START("ANSI: SPECIAL-CHARS", "Special character handling");

    struct buffer *bp = malloc(sizeof(struct buffer));
    if (bp) {
        memset(bp, 0, sizeof(struct buffer));
        bp->b_linep = lalloc(0);

        // Test with various special characters
        const char *test_chars = "\x00\x01\x1b\x7f\xff";
        terminal_process_output(bp, test_chars, 5);

        // Cleanup
        lfree(bp->b_linep);
        free(bp);
        ok = 1;
    }

    PHASE_END("ANSI: SPECIAL-CHARS", ok);
    return ok;
}

static int test_ansi_edge_escape_sequences(void) {
    int ok = 1;
    PHASE_START("ANSI: ESCAPE-SEQUENCES", "ANSI escape sequence input");

    struct buffer *bp = malloc(sizeof(struct buffer));
    if (bp) {
        memset(bp, 0, sizeof(struct buffer));
        bp->b_linep = lalloc(0);

        // Test with various ANSI sequences (without full terminal state)
        const char *sequences = "\x1b[0m\x1b[31m\x1b[H\x1b[2J";
        terminal_process_output(bp, sequences, strlen(sequences));

        // Cleanup
        lfree(bp->b_linep);
        free(bp);
        ok = 1;
    }

    PHASE_END("ANSI: ESCAPE-SEQUENCES", ok);
    return ok;
}

static int test_ansi_edge_utf8_sequences(void) {
    int ok = 1;
    PHASE_START("ANSI: UTF8-SEQUENCES", "UTF-8 byte sequences");

    struct buffer *bp = malloc(sizeof(struct buffer));
    if (bp) {
        memset(bp, 0, sizeof(struct buffer));
        bp->b_linep = lalloc(0);

        // UTF-8 test: é (U+00E9) = 0xC3 0xA9
        const unsigned char utf8_data[] = {0xC3, 0xA9, 0xE2, 0x9C, 0x93};  // é + checkmark
        terminal_process_output(bp, (const char *)utf8_data, sizeof(utf8_data));

        // Cleanup
        lfree(bp->b_linep);
        free(bp);
        ok = 1;
    }

    PHASE_END("ANSI: UTF8-SEQUENCES", ok);
    return ok;
}

// ============================================================================
// PUBLIC ENTRY POINT
// ============================================================================

int test_terminal_ansi(void) {
    LOG_INFOF("\n%s### Terminal ANSI Parser Unit Tests ###%s\n", BLUE, RESET);

    // Initialize minimal terminal subsystem
    terminal_init();

    int results = 0;
    results += test_terminal_init();
    results += test_terminal_process_output_null_buffer();
    results += test_terminal_process_output_null_data();
    results += test_terminal_process_output_empty_data();
    results += test_terminal_process_output_without_term_data();
    results += test_ansi_edge_large_input();
    results += test_ansi_edge_special_chars();
    results += test_ansi_edge_escape_sequences();
    results += test_ansi_edge_utf8_sequences();

    int total = 9;

    LOG_INFOF("\n%s========================================%s", BLUE, RESET);
    LOG_INFOF("%sTest Summary%s", BLUE, RESET);
    LOG_INFOF("%s========================================%s", BLUE, RESET);
    LOG_INFOF("Passed: %d", results);
    LOG_INFOF("Failed: %d", total - results);
    LOG_INFOF("%s========================================%s\n", BLUE, RESET);

    return results == total;  // 1 on success, 0 on failure
}
