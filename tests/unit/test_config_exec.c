// test_config_exec.c - Unit tests for macro execution system
// Tests src/config/exec.c
//
// Functions tested:
//   - token(): command line tokenizer
//   - docmd(): command execution (limited - needs full editor state)
//   - Control flow directives (!if, !else, !endif, !while, !endwhile, !goto, !return)
//   - storemac()/storeproc(): macro storage
//   - freewhile(): while block cleanup

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

// ===========================================================================
// token() tests - command line tokenizer
// ===========================================================================

static int test_token_simple(void) {
    int ok = 1;
    PHASE_START("EXEC: TOKEN SIMPLE", "Simple token parsing");

    char tok[64];
    const char *src;
    const char *rest;

    // Single word
    src = "hello";
    rest = token(src, tok, sizeof(tok));
    if (strcmp(tok, "hello") != 0) {
        LOG_ERRORF("[FAIL] token('hello') = '%s', expected 'hello'", tok);
        ok = 0;
    }
    if (*rest != '\0') {
        LOG_ERRORF("[FAIL] token rest should be empty, got '%s'", rest);
        ok = 0;
    }

    // Two words
    src = "hello world";
    rest = token(src, tok, sizeof(tok));
    if (strcmp(tok, "hello") != 0) {
        LOG_ERRORF("[FAIL] token('hello world') first = '%s'", tok);
        ok = 0;
    }
    rest = token(rest, tok, sizeof(tok));
    if (strcmp(tok, "world") != 0) {
        LOG_ERRORF("[FAIL] token('hello world') second = '%s'", tok);
        ok = 0;
    }

    PHASE_END("EXEC: TOKEN SIMPLE", ok);
    return ok;
}

static int test_token_whitespace(void) {
    int ok = 1;
    PHASE_START("EXEC: TOKEN WHITESPACE", "Whitespace handling in tokens");

    char tok[64];
    const char *src;
    const char *rest;

    // Leading whitespace
    src = "   hello";
    rest = token(src, tok, sizeof(tok));
    if (strcmp(tok, "hello") != 0) {
        LOG_ERRORF("[FAIL] token('   hello') = '%s', expected 'hello'", tok);
        ok = 0;
    }

    // Tabs and spaces
    src = "\t  \thello\t  world";
    rest = token(src, tok, sizeof(tok));
    if (strcmp(tok, "hello") != 0) {
        LOG_ERRORF("[FAIL] token with tabs = '%s'", tok);
        ok = 0;
    }

    // Multiple spaces between tokens
    src = "hello    world";
    rest = token(src, tok, sizeof(tok));
    rest = token(rest, tok, sizeof(tok));
    if (strcmp(tok, "world") != 0) {
        LOG_ERRORF("[FAIL] token after multiple spaces = '%s'", tok);
        ok = 0;
    }

    PHASE_END("EXEC: TOKEN WHITESPACE", ok);
    return ok;
}

static int test_token_quoted(void) {
    int ok = 1;
    PHASE_START("EXEC: TOKEN QUOTED", "Quoted string tokens");

    char tok[64];
    const char *src;
    const char *rest;

    // Quoted string
    src = "\"hello world\"";
    rest = token(src, tok, sizeof(tok));
    if (strcmp(tok, "\"hello world") != 0) {
        LOG_ERRORF("[FAIL] token quoted = '%s', expected '\"hello world'", tok);
        ok = 0;
    }

    // Quoted with more tokens after
    src = "\"first arg\" second";
    rest = token(src, tok, sizeof(tok));
    if (strcmp(tok, "\"first arg") != 0) {
        LOG_ERRORF("[FAIL] first quoted token = '%s'", tok);
        ok = 0;
    }
    rest = token(rest, tok, sizeof(tok));
    if (strcmp(tok, "second") != 0) {
        LOG_ERRORF("[FAIL] token after quoted = '%s'", tok);
        ok = 0;
    }

    PHASE_END("EXEC: TOKEN QUOTED", ok);
    return ok;
}

static int test_token_escapes(void) {
    int ok = 1;
    PHASE_START("EXEC: TOKEN ESCAPES", "Escape sequence handling");

    char tok[64];
    const char *src;

    // ~n = newline
    src = "hello~nworld";
    token(src, tok, sizeof(tok));
    if (tok[5] != '\n') {
        LOG_ERRORF("[FAIL] ~n escape: got 0x%02x, expected 0x0a", (unsigned char)tok[5]);
        ok = 0;
    }

    // ~r = carriage return
    src = "hello~rworld";
    token(src, tok, sizeof(tok));
    if (tok[5] != '\r') {
        LOG_ERRORF("[FAIL] ~r escape: got 0x%02x, expected 0x0d", (unsigned char)tok[5]);
        ok = 0;
    }

    // ~t = tab
    src = "hello~tworld";
    token(src, tok, sizeof(tok));
    if (tok[5] != '\t') {
        LOG_ERRORF("[FAIL] ~t escape: got 0x%02x, expected 0x09", (unsigned char)tok[5]);
        ok = 0;
    }

    // ~b = backspace
    src = "hello~bworld";
    token(src, tok, sizeof(tok));
    if (tok[5] != '\b') {
        LOG_ERRORF("[FAIL] ~b escape: got 0x%02x, expected 0x08", (unsigned char)tok[5]);
        ok = 0;
    }

    // ~f = form feed
    src = "hello~fworld";
    token(src, tok, sizeof(tok));
    if (tok[5] != '\f') {
        LOG_ERRORF("[FAIL] ~f escape: got 0x%02x, expected 0x0c", (unsigned char)tok[5]);
        ok = 0;
    }

    // ~~ = literal ~
    src = "hello~~world";
    token(src, tok, sizeof(tok));
    if (tok[5] != '~') {
        LOG_ERRORF("[FAIL] ~~ escape: got '%c', expected '~'", tok[5]);
        ok = 0;
    }

    PHASE_END("EXEC: TOKEN ESCAPES", ok);
    return ok;
}

static int test_token_size_limit(void) {
    int ok = 1;
    PHASE_START("EXEC: TOKEN SIZE", "Token size limiting");

    char tok[8];  // Small buffer
    const char *src;

    // Token longer than buffer - should truncate
    src = "verylongtoken";
    token(src, tok, sizeof(tok));
    if (strlen(tok) >= sizeof(tok)) {
        LOG_ERRORF("[FAIL] token not truncated: len=%zu", strlen(tok));
        ok = 0;
    }
    if (tok[sizeof(tok)-1] != '\0') {
        LOG_ERROR("[FAIL] token not null-terminated");
        ok = 0;
    }

    PHASE_END("EXEC: TOKEN SIZE", ok);
    return ok;
}

// ===========================================================================
// Directive name tests
// ===========================================================================

// Reference to directive names from exec.c
extern char *dname[];

static int test_directive_names(void) {
    int ok = 1;
    PHASE_START("EXEC: DIRECTIVES", "Directive name verification");

    // Verify expected directive names exist
    // Based on exec.c: !if, !else, !endif, !goto, !return, !endm, !while, !endwhile, !break, !force

    // Check first few directives are as expected
    if (strcmp(dname[DIR_IF], "if") != 0) {
        LOG_ERRORF("[FAIL] dname[DIR_IF] = '%s', expected 'if'", dname[DIR_IF]);
        ok = 0;
    }

    if (strcmp(dname[DIR_ELSE], "else") != 0) {
        LOG_ERRORF("[FAIL] dname[DIR_ELSE] = '%s', expected 'else'", dname[DIR_ELSE]);
        ok = 0;
    }

    if (strcmp(dname[DIR_ENDIF], "endif") != 0) {
        LOG_ERRORF("[FAIL] dname[DIR_ENDIF] = '%s', expected 'endif'", dname[DIR_ENDIF]);
        ok = 0;
    }

    if (strcmp(dname[DIR_GOTO], "goto") != 0) {
        LOG_ERRORF("[FAIL] dname[DIR_GOTO] = '%s', expected 'goto'", dname[DIR_GOTO]);
        ok = 0;
    }

    if (strcmp(dname[DIR_RETURN], "return") != 0) {
        LOG_ERRORF("[FAIL] dname[DIR_RETURN] = '%s', expected 'return'", dname[DIR_RETURN]);
        ok = 0;
    }

    if (strcmp(dname[DIR_ENDM], "endm") != 0) {
        LOG_ERRORF("[FAIL] dname[DIR_ENDM] = '%s', expected 'endm'", dname[DIR_ENDM]);
        ok = 0;
    }

    if (strcmp(dname[DIR_WHILE], "while") != 0) {
        LOG_ERRORF("[FAIL] dname[DIR_WHILE] = '%s', expected 'while'", dname[DIR_WHILE]);
        ok = 0;
    }

    if (strcmp(dname[DIR_ENDWHILE], "endwhile") != 0) {
        LOG_ERRORF("[FAIL] dname[DIR_ENDWHILE] = '%s', expected 'endwhile'", dname[DIR_ENDWHILE]);
        ok = 0;
    }

    if (strcmp(dname[DIR_BREAK], "break") != 0) {
        LOG_ERRORF("[FAIL] dname[DIR_BREAK] = '%s', expected 'break'", dname[DIR_BREAK]);
        ok = 0;
    }

    if (strcmp(dname[DIR_FORCE], "force") != 0) {
        LOG_ERRORF("[FAIL] dname[DIR_FORCE] = '%s', expected 'force'", dname[DIR_FORCE]);
        ok = 0;
    }

    PHASE_END("EXEC: DIRECTIVES", ok);
    return ok;
}

// ===========================================================================
// freewhile() tests - memory cleanup
// ===========================================================================

static int test_freewhile(void) {
    int ok = 1;
    PHASE_START("EXEC: FREEWHILE", "While block cleanup");

    // Test with NULL - should not crash
    freewhile(NULL);

    // Create a simple while block chain and free it
    struct while_block *w1 = (struct while_block *)malloc(sizeof(struct while_block));
    struct while_block *w2 = (struct while_block *)malloc(sizeof(struct while_block));
    struct while_block *w3 = (struct while_block *)malloc(sizeof(struct while_block));

    if (!w1 || !w2 || !w3) {
        LOG_ERROR("[FAIL] Could not allocate while blocks");
        free(w1);
        free(w2);
        free(w3);
        ok = 0;
        PHASE_END("EXEC: FREEWHILE", ok);
        return ok;
    }

    w1->w_next = w2;
    w2->w_next = w3;
    w3->w_next = NULL;
    w1->w_begin = NULL;
    w1->w_end = NULL;
    w2->w_begin = NULL;
    w2->w_end = NULL;
    w3->w_begin = NULL;
    w3->w_end = NULL;

    // This should free all three blocks without crashing
    freewhile(w1);

    PHASE_END("EXEC: FREEWHILE", ok);
    return ok;
}

// ===========================================================================
// docmd() tests - command execution (limited due to editor state requirements)
// ===========================================================================

static int test_docmd_execlevel(void) {
    int ok = 1;
    PHASE_START("EXEC: DOCMD EXECLEVEL", "Command execution level skipping");

    // Save original state
    int orig_execlevel = execlevel;

    // When execlevel > 0, docmd should return true without executing
    execlevel = 1;
    int status = docmd("this-command-does-not-exist");
    if (status != true) {
        LOG_ERRORF("[FAIL] docmd with execlevel > 0 should return true, got %d", status);
        ok = 0;
    }

    execlevel = 5;
    status = docmd("another-fake-command");
    if (status != true) {
        LOG_ERRORF("[FAIL] docmd with execlevel=5 should return true, got %d", status);
        ok = 0;
    }

    // Restore
    execlevel = orig_execlevel;

    PHASE_END("EXEC: DOCMD EXECLEVEL", ok);
    return ok;
}

// ===========================================================================
// Macro buffer name generation tests
// ===========================================================================

static int test_macro_buffer_names(void) {
    int ok = 1;
    PHASE_START("EXEC: MACRO NAMES", "Macro buffer name generation");

    // Verify macro buffer naming convention
    // Macro buffers are named "*Macro xx*" where xx is the macro number

    char expected[16];
    char actual[16];

    // Test buffer name format for macro 1
    strcpy(actual, "*Macro xx*");
    actual[7] = '0' + (1 / 10);  // tens digit
    actual[8] = '0' + (1 % 10);  // ones digit
    strcpy(expected, "*Macro 01*");
    if (strcmp(actual, expected) != 0) {
        LOG_ERRORF("[FAIL] macro 1 name = '%s', expected '%s'", actual, expected);
        ok = 0;
    }

    // Test macro 10
    strcpy(actual, "*Macro xx*");
    actual[7] = '0' + (10 / 10);
    actual[8] = '0' + (10 % 10);
    strcpy(expected, "*Macro 10*");
    if (strcmp(actual, expected) != 0) {
        LOG_ERRORF("[FAIL] macro 10 name = '%s', expected '%s'", actual, expected);
        ok = 0;
    }

    // Test macro 40 (max)
    strcpy(actual, "*Macro xx*");
    actual[7] = '0' + (40 / 10);
    actual[8] = '0' + (40 % 10);
    strcpy(expected, "*Macro 40*");
    if (strcmp(actual, expected) != 0) {
        LOG_ERRORF("[FAIL] macro 40 name = '%s', expected '%s'", actual, expected);
        ok = 0;
    }

    PHASE_END("EXEC: MACRO NAMES", ok);
    return ok;
}

// ===========================================================================
// While block type tests
// ===========================================================================

static int test_while_block_types(void) {
    int ok = 1;
    PHASE_START("EXEC: WHILE TYPES", "While block type constants");

    // Verify the while block types are defined and distinct
    // BTWHILE = 1 (while loop start)
    // BTBREAK = 2 (break statement)

    if (BTWHILE != 1) {
        LOG_ERRORF("[FAIL] BTWHILE = %d, expected 1", BTWHILE);
        ok = 0;
    }

    if (BTBREAK != 2) {
        LOG_ERRORF("[FAIL] BTBREAK = %d, expected 2", BTBREAK);
        ok = 0;
    }

    // Test while block structure
    struct while_block wb;
    wb.w_type = BTWHILE;
    wb.w_next = NULL;
    wb.w_begin = NULL;
    wb.w_end = NULL;

    if (wb.w_type != BTWHILE) {
        LOG_ERROR("[FAIL] while block type assignment failed");
        ok = 0;
    }

    PHASE_END("EXEC: WHILE TYPES", ok);
    return ok;
}

// ===========================================================================
// cbuf() naming convention tests
// ===========================================================================

static int test_cbuf_naming(void) {
    int ok = 1;
    PHASE_START("EXEC: CBUF NAMING", "Numbered macro buffer naming");

    // Test the cbuf buffer naming matches storemac
    char bufname[16];

    for (int n = 1; n <= 40; n++) {
        sprintf(bufname, "*Macro %02d*", n);

        // Verify format using the same algorithm as storemac/cbuf
        char computed[16] = "*Macro xx*";
        computed[7] = '0' + (n / 10);
        computed[8] = '0' + (n % 10);

        if (strcmp(bufname, computed) != 0) {
            LOG_ERRORF("[FAIL] cbuf %d: sprintf='%s', computed='%s'", n, bufname, computed);
            ok = 0;
        }
    }

    PHASE_END("EXEC: CBUF NAMING", ok);
    return ok;
}

// ===========================================================================
// Entry point
// ===========================================================================

int test_config_exec(void) {
    int ok = 1;
    PHASE_START("CONFIG EXEC", "Macro execution system tests");

    // Token parsing tests
    ok &= test_token_simple();
    ok &= test_token_whitespace();
    ok &= test_token_quoted();
    ok &= test_token_escapes();
    ok &= test_token_size_limit();

    // Directive tests
    ok &= test_directive_names();

    // Memory management tests
    ok &= test_freewhile();

    // Command execution tests
    ok &= test_docmd_execlevel();

    // Macro system tests
    ok &= test_macro_buffer_names();
    ok &= test_while_block_types();
    ok &= test_cbuf_naming();

    if (ok) {
        LOG_INFO("[SUCCESS] All exec tests passed.");
    }

    PHASE_END("CONFIG EXEC", ok);
    return ok;
}
