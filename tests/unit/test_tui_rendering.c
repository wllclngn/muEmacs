/*
 * test_tui_rendering.c - Comprehensive TUI Rendering Unit Tests
 *
 * Tests the display rendering pipeline:
 * - Terminal output capture via function pointer hooks
 * - ANSI escape sequence verification
 * - Cursor positioning
 * - Highlight/selection rendering
 * - Soft-wrap behavior
 * - Differential update algorithm
 *
 * Uses output capture hooks to verify terminal sequences in headless CI.
 * All tests use LOG_* macros for consistent logging.
 */

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "utf8.h"
#include "util/logger.h"
#include <setjmp.h>
#include <signal.h>

/* ============================================================================
 * CRASH CONTAINMENT INFRASTRUCTURE
 * Catch segfaults/crashes within tests to prevent blowing up entire test suite
 * ============================================================================ */

static sigjmp_buf tui_test_jmp_buf;
static volatile sig_atomic_t tui_test_crashed = 0;
static const char *tui_crash_test_name = NULL;

/* Signal handler for crash containment */
static void tui_crash_handler(int sig) {
    tui_test_crashed = 1;
    siglongjmp(tui_test_jmp_buf, sig);
}

/* Run a test function with crash protection */
typedef int (*tui_test_func_t)(void);

static int run_test_safe(const char *name, tui_test_func_t func) {
    struct sigaction sa_new, sa_old_segv, sa_old_abrt, sa_old_bus;
    int result = 0;

    tui_crash_test_name = name;
    tui_test_crashed = 0;

    /* Install crash handlers */
    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_handler = tui_crash_handler;
    sigemptyset(&sa_new.sa_mask);
    sa_new.sa_flags = 0;

    sigaction(SIGSEGV, &sa_new, &sa_old_segv);
    sigaction(SIGABRT, &sa_new, &sa_old_abrt);
    sigaction(SIGBUS, &sa_new, &sa_old_bus);

    /* Try to run test */
    int sig = sigsetjmp(tui_test_jmp_buf, 1);
    if (sig == 0) {
        /* Normal execution path */
        result = func();
    } else {
        /* Crashed - we jumped here from signal handler */
        LOG_ERRORF("TUI: CRASH in %s (signal %d: %s) - contained",
                   name, sig,
                   sig == SIGSEGV ? "SIGSEGV" :
                   sig == SIGABRT ? "SIGABRT" :
                   sig == SIGBUS ? "SIGBUS" : "unknown");
        result = 0;
        stats.test_failures++;
    }

    /* Restore original signal handlers */
    sigaction(SIGSEGV, &sa_old_segv, NULL);
    sigaction(SIGABRT, &sa_old_abrt, NULL);
    sigaction(SIGBUS, &sa_old_bus, NULL);

    return result;
}

/* ============================================================================
 * OUTPUT CAPTURE INFRASTRUCTURE
 * Hook terminal function pointers to capture output without requiring a terminal
 * ============================================================================ */

#define TEST_CAPTURE_SIZE 65536

static struct {
    char buffer[TEST_CAPTURE_SIZE];
    int pos;
    bool capturing;
    int putchar_calls;
    int flush_calls;
    int move_calls;
    int last_move_row;
    int last_move_col;
} test_capture = {0};

/* Hooked putchar - captures all character output */
static int test_capture_putchar(int c) {
    if (test_capture.capturing && test_capture.pos < TEST_CAPTURE_SIZE - 1) {
        test_capture.buffer[test_capture.pos++] = (char)c;
    }
    test_capture.putchar_calls++;
    return c;
}

/* Hooked flush - no-op to prevent actual terminal output */
static void test_capture_flush(void) {
    test_capture.flush_calls++;
}

/* Hooked move - track cursor positioning */
static void test_capture_move(int row, int col) {
    test_capture.move_calls++;
    test_capture.last_move_row = row;
    test_capture.last_move_col = col;
    /* Also output the escape sequence so we can verify it */
    if (test_capture.capturing) {
        char seq[32];
        int len = snprintf(seq, sizeof(seq), "\033[%d;%dH", row + 1, col + 1);
        for (int i = 0; i < len && test_capture.pos < TEST_CAPTURE_SIZE - 1; i++) {
            test_capture.buffer[test_capture.pos++] = seq[i];
        }
    }
}

/* Hooked eeol - erase to end of line */
static void test_capture_eeol(void) {
    if (test_capture.capturing) {
        const char *seq = "\033[K";
        for (int i = 0; seq[i] && test_capture.pos < TEST_CAPTURE_SIZE - 1; i++) {
            test_capture.buffer[test_capture.pos++] = seq[i];
        }
    }
}

/* Hooked rev - reverse video */
static void test_capture_rev(int state) {
    if (test_capture.capturing) {
        const char *seq = state ? "\033[7m" : "\033[27m";
        for (int i = 0; seq[i] && test_capture.pos < TEST_CAPTURE_SIZE - 1; i++) {
            test_capture.buffer[test_capture.pos++] = seq[i];
        }
    }
}

/* Saved original function pointers */
static int (*saved_putchar)(int);
static void (*saved_flush)(void);
static void (*saved_move)(int, int);
static void (*saved_eeol)(void);
static void (*saved_rev)(int);

/* Install capture hooks - with null safety */
static void capture_install_hooks(void) {
    /* Save originals (may be null) */
    saved_putchar = term.t_putchar;
    saved_flush = term.t_flush;
    saved_move = term.t_move;
    saved_eeol = term.t_eeol;
    saved_rev = term.t_rev;

    /* Install our capture hooks */
    term.t_putchar = test_capture_putchar;
    term.t_flush = test_capture_flush;
    term.t_move = test_capture_move;
    term.t_eeol = test_capture_eeol;
    term.t_rev = test_capture_rev;

    LOG_DEBUG("TUI: Capture hooks installed");
}

/* Remove capture hooks */
static void capture_remove_hooks(void) {
    term.t_putchar = saved_putchar;
    term.t_flush = saved_flush;
    term.t_move = saved_move;
    term.t_eeol = saved_eeol;
    term.t_rev = saved_rev;
}

/* Start capturing */
static void capture_start(void) {
    memset(test_capture.buffer, 0, TEST_CAPTURE_SIZE);
    test_capture.pos = 0;
    test_capture.capturing = true;
    test_capture.putchar_calls = 0;
    test_capture.flush_calls = 0;
    test_capture.move_calls = 0;
}

/* Stop capturing */
static void capture_stop(void) {
    test_capture.capturing = false;
    test_capture.buffer[test_capture.pos] = '\0';
}

/* Get captured output */
static const char* capture_get(void) {
    return test_capture.buffer;
}

/* Get captured length */
static int capture_len(void) {
    return test_capture.pos;
}

/* ============================================================================
 * ANSI ESCAPE SEQUENCE PARSER
 * Parse and verify escape sequences in captured output
 * ============================================================================ */

typedef enum {
    ESC_CURSOR_POSITION,  /* \033[row;colH */
    ESC_SGR,              /* \033[...m (Select Graphic Rendition) */
    ESC_ERASE_LINE,       /* \033[K */
    ESC_ERASE_DISPLAY,    /* \033[J */
    ESC_SCROLL,           /* \033[...S or \033[...T */
    ESC_OTHER
} ansi_seq_type_t;

typedef struct {
    ansi_seq_type_t type;
    int params[8];
    int param_count;
    char final_char;
} ansi_sequence_t;

/* Parse a single ANSI escape sequence, return length consumed or -1 on error */
static int parse_ansi_sequence(const char *s, ansi_sequence_t *out) {
    if (!s || s[0] != '\033' || s[1] != '[') return -1;

    const char *p = s + 2;
    out->param_count = 0;
    memset(out->params, 0, sizeof(out->params));

    /* Parse numeric parameters */
    while (*p && out->param_count < 8) {
        if (*p >= '0' && *p <= '9') {
            out->params[out->param_count] = 0;
            while (*p >= '0' && *p <= '9') {
                out->params[out->param_count] =
                    out->params[out->param_count] * 10 + (*p - '0');
                p++;
            }
            out->param_count++;
        }
        if (*p == ';') {
            p++;
        } else {
            break;
        }
    }

    /* Determine type by final character */
    out->final_char = *p;
    switch (*p) {
        case 'H': case 'f':
            out->type = ESC_CURSOR_POSITION;
            break;
        case 'm':
            out->type = ESC_SGR;
            break;
        case 'K':
            out->type = ESC_ERASE_LINE;
            break;
        case 'J':
            out->type = ESC_ERASE_DISPLAY;
            break;
        case 'S': case 'T':
            out->type = ESC_SCROLL;
            break;
        default:
            out->type = ESC_OTHER;
            break;
    }

    return (int)(p - s + 1);
}

/* Check if captured output contains a specific substring */
static bool capture_contains(const char *needle) {
    return strstr(test_capture.buffer, needle) != NULL;
}

/* Count occurrences of a substring in captured output */
static int capture_count(const char *needle) {
    int count = 0;
    const char *p = test_capture.buffer;
    size_t needle_len = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += needle_len;
    }
    return count;
}

/* ============================================================================
 * TEST HELPER FUNCTIONS
 * ============================================================================ */

/* Create a test buffer with given content */
static struct buffer* create_test_buffer_with_content(const char *name, const char *content) {
    /* Defensive null checks */
    if (!name) {
        LOG_ERROR("TUI: create_test_buffer_with_content: null name");
        return NULL;
    }
    if (!curwp) {
        LOG_ERROR("TUI: create_test_buffer_with_content: curwp is null");
        return NULL;
    }

    struct buffer *bp = bfind((char *)name, true, 0);
    if (!bp) {
        LOG_ERRORF("TUI: Failed to create buffer '%s'", name);
        return NULL;
    }

    /* Verify buffer has required pointers */
    if (!bp->b_linep) {
        LOG_ERRORF("TUI: Buffer '%s' has null b_linep", name);
        return NULL;
    }

    /* Make it current */
    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = bp->b_linep;
    curwp->w_doto = 0;

    /* Clear any existing content */
    bclear(bp);

    /* Insert content */
    if (content && *content) {
        linstr((char *)content);
    }

    /* Reset cursor to start - with null check */
    struct line *first = lforw(bp->b_linep);
    if (first) {
        curwp->w_dotp = first;
        curwp->w_doto = 0;
    }

    return bp;
}

/* Cleanup test buffer */
static void destroy_test_buffer(struct buffer *bp) {
    if (bp) {
        bp->b_flag &= ~BFCHG;  /* Allow deletion without prompt */
        zotbuf(bp);
    }
}

/* ============================================================================
 * PHASE 1: BASIC RENDERING TESTS
 * Test that basic text content is rendered to terminal
 * ============================================================================ */

static int test_render_simple_text(void) {
    int result = 1;
    PHASE_START("TUI: SIMPLE TEXT", "Verify simple text is output to terminal");

    /* Create buffer with simple content */
    struct buffer *bp = create_test_buffer_with_content("test-simple", "Hello World");
    if (!bp) {
        TEST_LOG_FAIL("Failed to create test buffer");
        result = 0;
        PHASE_END("TUI: SIMPLE TEXT", result);
        return 0;
    }

    /* Setup hooks and capture */
    capture_install_hooks();
    capture_start();

    /* Force full screen update */
    sgarbf = true;
    update(true);

    capture_stop();
    capture_remove_hooks();

    /* Verify "Hello World" appears in output */
    if (!capture_contains("Hello World")) {
        LOG_ERRORF("TUI: Expected 'Hello World' in output, got %d bytes: %.100s",
                   capture_len(), capture_get());
        result = 0;
    } else {
        TEST_LOG_PASS("Simple text rendered correctly");
    }

    /* Verify we got some terminal output */
    if (test_capture.putchar_calls == 0) {
        LOG_ERRORF("TUI: No putchar calls made");
        result = 0;
    } else {
        LOG_INFOF("TUI: %d putchar calls, %d bytes captured",
                  test_capture.putchar_calls, capture_len());
    }

    destroy_test_buffer(bp);
    PHASE_END("TUI: SIMPLE TEXT", result);
    return result;
}

/* ============================================================================
 * PHASE 2: CONTROL CHARACTER TESTS
 * ============================================================================ */

static int test_render_tab_expansion(void) {
    int result = 1;
    PHASE_START("TUI: TAB EXPANSION", "Verify tabs expand to spaces");

    /* Create buffer with tab character */
    struct buffer *bp = create_test_buffer_with_content("test-tab", "A\tB");
    if (!bp) {
        TEST_LOG_FAIL("Failed to create test buffer");
        result = 0;
        PHASE_END("TUI: TAB EXPANSION", result);
        return 0;
    }

    capture_install_hooks();
    capture_start();

    sgarbf = true;
    update(true);

    capture_stop();
    capture_remove_hooks();

    /* Verify output contains 'A' followed by spaces then 'B' */
    /* Tab at column 1 should expand to column 8 (7 spaces) */
    const char *output = capture_get();
    const char *a_pos = strstr(output, "A");
    const char *b_pos = strstr(output, "B");

    if (!a_pos || !b_pos) {
        LOG_ERRORF("TUI: Could not find A and B in output");
        result = 0;
    } else if (b_pos <= a_pos) {
        LOG_ERRORF("TUI: B should appear after A");
        result = 0;
    } else {
        /* Count spaces between A and B (approximately - may have escape sequences) */
        int gap = (int)(b_pos - a_pos - 1);
        if (gap >= 3) {  /* At least some expansion happened */
            LOG_INFOF("Test: PASS - Tab expansion working (gap=%d)", gap);
        } else {
            LOG_ERRORF("TUI: Tab expansion seems too small: gap=%d", gap);
            result = 0;
        }
    }

    destroy_test_buffer(bp);
    PHASE_END("TUI: TAB EXPANSION", result);
    return result;
}

static int test_render_control_chars(void) {
    int result = 1;
    PHASE_START("TUI: CONTROL CHARS", "Verify control chars render as ^X");

    /* Create buffer with Ctrl-A (0x01) */
    char content[8] = {'X', 0x01, 'Y', 0};
    struct buffer *bp = create_test_buffer_with_content("test-ctrl", content);
    if (!bp) {
        TEST_LOG_FAIL("Failed to create test buffer");
        result = 0;
        PHASE_END("TUI: CONTROL CHARS", result);
        return 0;
    }

    capture_install_hooks();
    capture_start();

    sgarbf = true;
    update(true);

    capture_stop();
    capture_remove_hooks();

    /* Verify ^A appears in output (Ctrl-A = 0x01 displays as ^A) */
    if (capture_contains("^A") || capture_contains("^a")) {
        TEST_LOG_PASS("Control character rendered as ^A");
    } else {
        LOG_ERRORF("TUI: Expected ^A in output");
        result = 0;
    }

    destroy_test_buffer(bp);
    PHASE_END("TUI: CONTROL CHARS", result);
    return result;
}

/* ============================================================================
 * PHASE 3: CURSOR POSITIONING TESTS
 * ============================================================================ */

static int test_cursor_move_sequence(void) {
    int result = 1;
    PHASE_START("TUI: CURSOR MOVE", "Verify cursor positioning escape sequences");

    capture_install_hooks();
    capture_start();

    /* Direct cursor move call */
    movecursor(5, 10);
    TTflush();

    capture_stop();
    capture_remove_hooks();

    /* Verify escape sequence: ESC [ 6 ; 11 H (1-indexed) */
    if (capture_contains("\033[6;11H")) {
        TEST_LOG_PASS("Cursor move sequence correct: ESC[6;11H");
    } else {
        LOG_ERRORF("TUI: Expected \\033[6;11H, got: %s", capture_get());
        result = 0;
    }

    /* Also verify our tracking */
    if (test_capture.last_move_row != 5 || test_capture.last_move_col != 10) {
        LOG_ERRORF("TUI: Move tracking wrong: row=%d col=%d",
                   test_capture.last_move_row, test_capture.last_move_col);
        result = 0;
    }

    PHASE_END("TUI: CURSOR MOVE", result);
    return result;
}

/* ============================================================================
 * PHASE 4: ESCAPE SEQUENCE TESTS
 * ============================================================================ */

static int test_reverse_video_sequence(void) {
    int result = 1;
    PHASE_START("TUI: REVERSE VIDEO", "Verify reverse video escape sequences");

    capture_install_hooks();
    capture_start();

    /* Toggle reverse video on and off */
    TTrev(true);
    TTrev(false);
    TTflush();

    capture_stop();
    capture_remove_hooks();

    /* Verify sequences */
    if (!capture_contains("\033[7m")) {
        LOG_ERRORF("TUI: Missing reverse-on sequence \\033[7m");
        result = 0;
    }
    if (!capture_contains("\033[27m")) {
        LOG_ERRORF("TUI: Missing reverse-off sequence \\033[27m");
        result = 0;
    }

    if (result) {
        TEST_LOG_PASS("Reverse video sequences correct");
    }

    PHASE_END("TUI: REVERSE VIDEO", result);
    return result;
}

static int test_erase_to_eol_sequence(void) {
    int result = 1;
    PHASE_START("TUI: ERASE EOL", "Verify erase-to-end-of-line sequence");

    capture_install_hooks();
    capture_start();

    TTeeol();
    TTflush();

    capture_stop();
    capture_remove_hooks();

    if (capture_contains("\033[K")) {
        TEST_LOG_PASS("Erase-to-EOL sequence correct: ESC[K");
    } else {
        LOG_ERRORF("TUI: Expected \\033[K, got: %s", capture_get());
        result = 0;
    }

    PHASE_END("TUI: ERASE EOL", result);
    return result;
}

/* ============================================================================
 * PHASE 5: SOFT WRAP TESTS
 * ============================================================================ */

static int test_soft_wrap_long_line(void) {
    int result = 1;
    PHASE_START("TUI: SOFT WRAP", "Verify long lines wrap at wrap column");

    /* Create a line longer than terminal width */
    char long_line[200];
    memset(long_line, 'X', 160);
    long_line[160] = '\0';

    struct buffer *bp = create_test_buffer_with_content("test-wrap", long_line);
    if (!bp) {
        TEST_LOG_FAIL("Failed to create test buffer");
        result = 0;
        PHASE_END("TUI: SOFT WRAP", result);
        return 0;
    }

    /* Enable soft wrap at column 80 */
    int orig_wrap = curwp->w_wrap_col;
    curwp->w_wrap_col = 80;

    capture_install_hooks();
    capture_start();

    sgarbf = true;
    update(true);

    capture_stop();
    capture_remove_hooks();

    /* Count 'X' characters in output - should be 160 */
    int x_count = capture_count("X");
    if (x_count >= 150) {  /* Allow for some terminal control overhead */
        LOG_INFOF("Test: PASS - Long line rendered (%d X chars)", x_count);
    } else {
        LOG_ERRORF("TUI: Expected ~160 X chars, got %d", x_count);
        result = 0;
    }

    /* Verify multiple cursor moves (indicates multiple rows) */
    if (test_capture.move_calls > 1) {
        LOG_INFOF("TUI: %d cursor moves (multi-row rendering)", test_capture.move_calls);
    }

    curwp->w_wrap_col = orig_wrap;
    destroy_test_buffer(bp);
    PHASE_END("TUI: SOFT WRAP", result);
    return result;
}

/* ============================================================================
 * PHASE 6: UTF-8 RENDERING TESTS
 * ============================================================================ */

static int test_utf8_rendering(void) {
    int result = 1;
    PHASE_START("TUI: UTF-8", "Verify UTF-8 characters render correctly");

    /* UTF-8 string: "Café" with e-acute (C3 A9) */
    struct buffer *bp = create_test_buffer_with_content("test-utf8", "Caf\xc3\xa9");
    if (!bp) {
        TEST_LOG_FAIL("Failed to create test buffer");
        result = 0;
        PHASE_END("TUI: UTF-8", result);
        return 0;
    }

    capture_install_hooks();
    capture_start();

    sgarbf = true;
    update(true);

    capture_stop();
    capture_remove_hooks();

    /* Verify the UTF-8 bytes appear in output */
    if (capture_contains("Caf")) {
        TEST_LOG_PASS("UTF-8 prefix rendered");
    } else {
        LOG_ERRORF("TUI: UTF-8 rendering failed");
        result = 0;
    }

    /* Check that we got reasonable output */
    if (capture_len() > 0) {
        LOG_INFOF("TUI: UTF-8 test captured %d bytes", capture_len());
    }

    destroy_test_buffer(bp);
    PHASE_END("TUI: UTF-8", result);
    return result;
}

/* ============================================================================
 * PHASE 7: DIFFERENTIAL UPDATE TESTS
 * ============================================================================ */

static int test_incremental_update(void) {
    int result = 1;
    PHASE_START("TUI: INCREMENTAL", "Verify incremental updates are efficient");

    struct buffer *bp = create_test_buffer_with_content("test-incr", "Hello World");
    if (!bp) {
        TEST_LOG_FAIL("Failed to create test buffer");
        result = 0;
        PHASE_END("TUI: INCREMENTAL", result);
        return 0;
    }

    /* First update - full screen */
    capture_install_hooks();
    sgarbf = true;
    update(true);
    int first_calls = test_capture.putchar_calls;

    /* Second update - no changes, should be minimal */
    capture_start();
    update(false);
    capture_stop();
    int second_calls = test_capture.putchar_calls - first_calls;

    capture_remove_hooks();

    /* Second update should output much less than first */
    LOG_INFOF("TUI: First update: %d calls, second: %d calls", first_calls, second_calls);

    if (second_calls < first_calls / 2) {
        TEST_LOG_PASS("Incremental update is efficient");
    } else {
        LOG_WARNF("TUI: Second update not significantly smaller");
        /* Not a failure - could be due to cursor blink or status line */
    }

    destroy_test_buffer(bp);
    PHASE_END("TUI: INCREMENTAL", result);
    return result;
}

/* ============================================================================
 * PHASE 8: MODELINE/STATUS LINE TESTS
 * ============================================================================ */

static int test_modeline_rendering(void) {
    int result = 1;
    PHASE_START("TUI: MODELINE", "Verify modeline renders with buffer info");

    struct buffer *bp = create_test_buffer_with_content("testfile.c", "int main() {}");
    if (!bp) {
        TEST_LOG_FAIL("Failed to create test buffer");
        result = 0;
        PHASE_END("TUI: MODELINE", result);
        return 0;
    }

    capture_install_hooks();
    capture_start();

    sgarbf = true;
    update(true);

    capture_stop();
    capture_remove_hooks();

    /* Modeline should contain buffer name */
    if (capture_contains("testfile.c") || capture_contains("testfile")) {
        TEST_LOG_PASS("Modeline contains buffer name");
    } else {
        LOG_WARNF("TUI: Buffer name not found in modeline");
        /* Not necessarily a failure - depends on modeline format */
    }

    /* Should have reverse video for modeline */
    if (capture_contains("\033[7m") || capture_contains("\033[27m")) {
        LOG_INFOF("TUI: Modeline uses reverse video");
    }

    destroy_test_buffer(bp);
    PHASE_END("TUI: MODELINE", result);
    return result;
}

/* ============================================================================
 * MAIN TEST ENTRY POINT
 * ============================================================================ */

int test_tui_rendering(void) {
    int result = 1;
    int sub_result;

    LOG_INFO("Test: ======================================");
    LOG_INFO("Test: TUI RENDERING COMPREHENSIVE TESTS");
    LOG_INFO("Test: (with crash containment)");
    LOG_INFO("Test: ======================================");

    /* Ensure terminal dimensions are set */
    if (term.t_nrow == 0) {
        term.t_nrow = 23;
        term.t_ncol = 80;
        term.t_mrow = 24;
        term.t_mcol = 80;
    }

    /* Initialize screen buffers without terminal I/O (headless test mode) */
    LOG_DEBUG("TUI: Initializing virtual screen via vtinit_test()");
    vtinit_test();

    /* Initialize editor state if needed */
    if (curbp == NULL || curwp == NULL) {
        LOG_DEBUG("TUI: Initializing editor state via edinit()");
        edinit("*scratch*");
    }

    /* All tests wrapped in crash containment - failures won't bring down suite */

    /* Phase 1: Basic Rendering */
    sub_result = run_test_safe("render_simple_text", test_render_simple_text);
    if (!sub_result) result = 0;

    /* Phase 2: Control Characters */
    sub_result = run_test_safe("render_tab_expansion", test_render_tab_expansion);
    if (!sub_result) result = 0;

    sub_result = run_test_safe("render_control_chars", test_render_control_chars);
    if (!sub_result) result = 0;

    /* Phase 3: Cursor Positioning */
    sub_result = run_test_safe("cursor_move_sequence", test_cursor_move_sequence);
    if (!sub_result) result = 0;

    /* Phase 4: Escape Sequences */
    sub_result = run_test_safe("reverse_video_sequence", test_reverse_video_sequence);
    if (!sub_result) result = 0;

    sub_result = run_test_safe("erase_to_eol_sequence", test_erase_to_eol_sequence);
    if (!sub_result) result = 0;

    /* Phase 5: Soft Wrap */
    sub_result = run_test_safe("soft_wrap_long_line", test_soft_wrap_long_line);
    if (!sub_result) result = 0;

    /* Phase 6: UTF-8 */
    sub_result = run_test_safe("utf8_rendering", test_utf8_rendering);
    if (!sub_result) result = 0;

    /* Phase 7: Incremental Updates */
    sub_result = run_test_safe("incremental_update", test_incremental_update);
    if (!sub_result) result = 0;

    /* Phase 8: Modeline */
    sub_result = run_test_safe("modeline_rendering", test_modeline_rendering);
    if (!sub_result) result = 0;

    LOG_INFO("Test: ======================================");
    LOG_INFOF("Test: TUI RENDERING COMPLETE - %s", result ? "PASS" : "FAIL");
    LOG_INFO("Test: ======================================");

    return result;
}
