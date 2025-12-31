// test_config_eval.c - Unit tests for evaluation/variable system
// Tests src/config/eval.c
//
// Functions tested:
//   - int_to_string(): integer to string conversion
//   - string_to_bool(): string to logical (boolean)
//   - bool_to_string(): logical to string
//   - string_to_upper()/string_to_lower(): case conversion
//   - string_find_index(): substring search
//   - string_translate(): character translation
//   - token_get_type(): token type identification
//   - abs()/editor_random(): math utilities

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

// ===========================================================================
// int_to_string() tests - integer to ascii string
// ===========================================================================

static int test_int_to_string_positive(void) {
    int ok = 1;
    PHASE_START("EVAL: ITOA POSITIVE", "Positive integer conversion");

    // Basic positive numbers
    char *result = int_to_string(0);
    if (strcmp(result, "0") != 0) {
        LOG_ERRORF("[FAIL] int_to_string(0) = '%s', expected '0'", result);
        ok = 0;
    }

    result = int_to_string(1);
    if (strcmp(result, "1") != 0) {
        LOG_ERRORF("[FAIL] int_to_string(1) = '%s', expected '1'", result);
        ok = 0;
    }

    result = int_to_string(42);
    if (strcmp(result, "42") != 0) {
        LOG_ERRORF("[FAIL] int_to_string(42) = '%s', expected '42'", result);
        ok = 0;
    }

    result = int_to_string(12345);
    if (strcmp(result, "12345") != 0) {
        LOG_ERRORF("[FAIL] int_to_string(12345) = '%s', expected '12345'", result);
        ok = 0;
    }

    // Large positive number
    result = int_to_string(2147483647);
    if (strcmp(result, "2147483647") != 0) {
        LOG_ERRORF("[FAIL] int_to_string(INT_MAX) = '%s', expected '2147483647'", result);
        ok = 0;
    }

    PHASE_END("EVAL: ITOA POSITIVE", ok);
    return ok;
}

static int test_int_to_string_negative(void) {
    int ok = 1;
    PHASE_START("EVAL: ITOA NEGATIVE", "Negative integer conversion");

    char *result = int_to_string(-1);
    if (strcmp(result, "-1") != 0) {
        LOG_ERRORF("[FAIL] int_to_string(-1) = '%s', expected '-1'", result);
        ok = 0;
    }

    result = int_to_string(-42);
    if (strcmp(result, "-42") != 0) {
        LOG_ERRORF("[FAIL] int_to_string(-42) = '%s', expected '-42'", result);
        ok = 0;
    }

    result = int_to_string(-12345);
    if (strcmp(result, "-12345") != 0) {
        LOG_ERRORF("[FAIL] int_to_string(-12345) = '%s', expected '-12345'", result);
        ok = 0;
    }

    PHASE_END("EVAL: ITOA NEGATIVE", ok);
    return ok;
}

// ===========================================================================
// string_to_bool() tests - string to logical
// ===========================================================================

static int test_stol_boolean(void) {
    int ok = 1;
    PHASE_START("EVAL: STOL BOOLEAN", "String to logical boolean values");

    // TRUE/FALSE literals
    if (string_to_bool("TRUE") != true) {
        LOG_ERROR("[FAIL] string_to_bool('TRUE') should be true");
        ok = 0;
    }

    if (string_to_bool("FALSE") != false) {
        LOG_ERROR("[FAIL] string_to_bool('FALSE') should be false");
        ok = 0;
    }

    // First char check
    if (string_to_bool("Txxx") != true) {
        LOG_ERROR("[FAIL] string_to_bool('Txxx') should be true (T prefix)");
        ok = 0;
    }

    if (string_to_bool("Fxxx") != false) {
        LOG_ERROR("[FAIL] string_to_bool('Fxxx') should be false (F prefix)");
        ok = 0;
    }

    PHASE_END("EVAL: STOL BOOLEAN", ok);
    return ok;
}

static int test_stol_numeric(void) {
    int ok = 1;
    PHASE_START("EVAL: STOL NUMERIC", "String to logical numeric values");

    // Zero is false
    if (string_to_bool("0") != false) {
        LOG_ERROR("[FAIL] string_to_bool('0') should be false");
        ok = 0;
    }

    // Non-zero is true
    if (string_to_bool("1") != true) {
        LOG_ERROR("[FAIL] string_to_bool('1') should be true");
        ok = 0;
    }

    if (string_to_bool("42") != true) {
        LOG_ERROR("[FAIL] string_to_bool('42') should be true");
        ok = 0;
    }

    if (string_to_bool("-1") != true) {
        LOG_ERROR("[FAIL] string_to_bool('-1') should be true");
        ok = 0;
    }

    PHASE_END("EVAL: STOL NUMERIC", ok);
    return ok;
}

// ===========================================================================
// bool_to_string() tests - logical to string
// ===========================================================================

static int test_bool_to_string(void) {
    int ok = 1;
    PHASE_START("EVAL: LTOS", "Logical to string conversion");

    char *result = bool_to_string(true);
    if (strcmp(result, "true") != 0) {
        LOG_ERRORF("[FAIL] bool_to_string(true) = '%s', expected 'true'", result);
        ok = 0;
    }

    result = bool_to_string(false);
    if (strcmp(result, "false") != 0) {
        LOG_ERRORF("[FAIL] bool_to_string(false) = '%s', expected 'false'", result);
        ok = 0;
    }

    // Non-zero treated as true
    result = bool_to_string(42);
    if (strcmp(result, "true") != 0) {
        LOG_ERRORF("[FAIL] bool_to_string(42) = '%s', expected 'true'", result);
        ok = 0;
    }

    result = bool_to_string(0);
    if (strcmp(result, "false") != 0) {
        LOG_ERRORF("[FAIL] bool_to_string(0) = '%s', expected 'false'", result);
        ok = 0;
    }

    PHASE_END("EVAL: LTOS", ok);
    return ok;
}

// ===========================================================================
// string_to_upper()/string_to_lower() tests - case conversion
// ===========================================================================

static int test_string_to_upper(void) {
    int ok = 1;
    PHASE_START("EVAL: MKUPPER", "String uppercase conversion");

    char buf[64];

    strcpy(buf, "hello");
    string_to_upper(buf);
    if (strcmp(buf, "HELLO") != 0) {
        LOG_ERRORF("[FAIL] string_to_upper('hello') = '%s', expected 'HELLO'", buf);
        ok = 0;
    }

    strcpy(buf, "HeLLo WoRLd");
    string_to_upper(buf);
    if (strcmp(buf, "HELLO WORLD") != 0) {
        LOG_ERRORF("[FAIL] string_to_upper('HeLLo WoRLd') = '%s'", buf);
        ok = 0;
    }

    strcpy(buf, "123abc456");
    string_to_upper(buf);
    if (strcmp(buf, "123ABC456") != 0) {
        LOG_ERRORF("[FAIL] string_to_upper('123abc456') = '%s'", buf);
        ok = 0;
    }

    // Empty string
    buf[0] = '\0';
    string_to_upper(buf);
    if (buf[0] != '\0') {
        LOG_ERROR("[FAIL] string_to_upper('') should be empty");
        ok = 0;
    }

    PHASE_END("EVAL: MKUPPER", ok);
    return ok;
}

static int test_string_to_lower(void) {
    int ok = 1;
    PHASE_START("EVAL: MKLOWER", "String lowercase conversion");

    char buf[64];

    strcpy(buf, "HELLO");
    string_to_lower(buf);
    if (strcmp(buf, "hello") != 0) {
        LOG_ERRORF("[FAIL] string_to_lower('HELLO') = '%s', expected 'hello'", buf);
        ok = 0;
    }

    strcpy(buf, "HeLLo WoRLd");
    string_to_lower(buf);
    if (strcmp(buf, "hello world") != 0) {
        LOG_ERRORF("[FAIL] string_to_lower('HeLLo WoRLd') = '%s'", buf);
        ok = 0;
    }

    strcpy(buf, "123ABC456");
    string_to_lower(buf);
    if (strcmp(buf, "123abc456") != 0) {
        LOG_ERRORF("[FAIL] string_to_lower('123ABC456') = '%s'", buf);
        ok = 0;
    }

    PHASE_END("EVAL: MKLOWER", ok);
    return ok;
}

// ===========================================================================
// string_find_index() tests - substring search
// ===========================================================================

static int test_sindex_found(void) {
    int ok = 1;
    PHASE_START("EVAL: SINDEX FOUND", "Substring search - found cases");

    // Basic substring at start
    int pos = string_find_index("hello world", "hello");
    if (pos != 1) {
        LOG_ERRORF("[FAIL] string_find_index('hello world', 'hello') = %d, expected 1", pos);
        ok = 0;
    }

    // Substring in middle
    pos = string_find_index("hello world", "lo wo");
    if (pos != 4) {
        LOG_ERRORF("[FAIL] string_find_index('hello world', 'lo wo') = %d, expected 4", pos);
        ok = 0;
    }

    // Substring at end
    pos = string_find_index("hello world", "world");
    if (pos != 7) {
        LOG_ERRORF("[FAIL] string_find_index('hello world', 'world') = %d, expected 7", pos);
        ok = 0;
    }

    // Single character
    pos = string_find_index("hello", "e");
    if (pos != 2) {
        LOG_ERRORF("[FAIL] string_find_index('hello', 'e') = %d, expected 2", pos);
        ok = 0;
    }

    // Empty pattern (should match at start)
    pos = string_find_index("hello", "");
    if (pos != 1) {
        LOG_ERRORF("[FAIL] string_find_index('hello', '') = %d, expected 1", pos);
        ok = 0;
    }

    PHASE_END("EVAL: SINDEX FOUND", ok);
    return ok;
}

static int test_sindex_not_found(void) {
    int ok = 1;
    PHASE_START("EVAL: SINDEX NOT FOUND", "Substring search - not found cases");

    int pos = string_find_index("hello world", "xyz");
    if (pos != 0) {
        LOG_ERRORF("[FAIL] string_find_index('hello world', 'xyz') = %d, expected 0", pos);
        ok = 0;
    }

    // By default eq() is case-insensitive, so HELLO matches hello
    // This test verifies the case-INSENSITIVE behavior (pos == 1 means match found)
    pos = string_find_index("hello world", "HELLO");
    if (pos != 1) {
        LOG_ERRORF("[FAIL] string_find_index('hello world', 'HELLO') = %d, expected 1 (case insensitive)", pos);
        ok = 0;
    }

    // Pattern longer than source
    pos = string_find_index("hi", "hello");
    if (pos != 0) {
        LOG_ERRORF("[FAIL] string_find_index('hi', 'hello') = %d, expected 0", pos);
        ok = 0;
    }

    PHASE_END("EVAL: SINDEX NOT FOUND", ok);
    return ok;
}

// ===========================================================================
// string_translate() tests - character translation
// ===========================================================================

static int test_xlat_basic(void) {
    int ok = 1;
    PHASE_START("EVAL: XLAT BASIC", "Character translation");

    // Simple translation: h->1, e->2, l->3, o->4
    // "hello" = h(1) e(2) l(3) l(3) o(4) = "12334"
    char *result = string_translate("hello", "helo", "1234");
    if (strcmp(result, "12334") != 0) {
        LOG_ERRORF("[FAIL] string_translate('hello', 'helo', '1234') = '%s', expected '12334'", result);
        ok = 0;
    }

    // ROT13-style translation
    result = string_translate("abc", "abc", "xyz");
    if (strcmp(result, "xyz") != 0) {
        LOG_ERRORF("[FAIL] string_translate('abc', 'abc', 'xyz') = '%s', expected 'xyz'", result);
        ok = 0;
    }

    // No matches - pass through unchanged
    result = string_translate("xyz", "abc", "123");
    if (strcmp(result, "xyz") != 0) {
        LOG_ERRORF("[FAIL] string_translate('xyz', 'abc', '123') = '%s', expected 'xyz'", result);
        ok = 0;
    }

    // Empty source
    result = string_translate("", "abc", "123");
    if (strcmp(result, "") != 0) {
        LOG_ERRORF("[FAIL] string_translate('', 'abc', '123') = '%s', expected ''", result);
        ok = 0;
    }

    PHASE_END("EVAL: XLAT BASIC", ok);
    return ok;
}

// ===========================================================================
// token_get_type() tests - token type identification
// ===========================================================================

static int test_token_get_type(void) {
    int ok = 1;
    PHASE_START("EVAL: GETTYP", "Token type identification");

    char tok[32];

    // Empty token
    strcpy(tok, "");
    if (token_get_type(tok) != TKNUL) {
        LOG_ERRORF("[FAIL] token_get_type('') = %d, expected TKNUL(%d)", token_get_type(tok), TKNUL);
        ok = 0;
    }

    // Numeric literal
    strcpy(tok, "42");
    if (token_get_type(tok) != TKLIT) {
        LOG_ERRORF("[FAIL] token_get_type('42') = %d, expected TKLIT(%d)", token_get_type(tok), TKLIT);
        ok = 0;
    }

    strcpy(tok, "0");
    if (token_get_type(tok) != TKLIT) {
        LOG_ERRORF("[FAIL] token_get_type('0') = %d, expected TKLIT(%d)", token_get_type(tok), TKLIT);
        ok = 0;
    }

    // String literal
    strcpy(tok, "\"hello\"");
    if (token_get_type(tok) != TKSTR) {
        LOG_ERRORF("[FAIL] token_get_type('\"hello\"') = %d, expected TKSTR(%d)", token_get_type(tok), TKSTR);
        ok = 0;
    }

    // Directive
    strcpy(tok, "!if");
    if (token_get_type(tok) != TKDIR) {
        LOG_ERRORF("[FAIL] token_get_type('!if') = %d, expected TKDIR(%d)", token_get_type(tok), TKDIR);
        ok = 0;
    }

    // Argument
    strcpy(tok, "@prompt");
    if (token_get_type(tok) != TKARG) {
        LOG_ERRORF("[FAIL] token_get_type('@prompt') = %d, expected TKARG(%d)", token_get_type(tok), TKARG);
        ok = 0;
    }

    // Buffer reference
    strcpy(tok, "#buffer");
    if (token_get_type(tok) != TKBUF) {
        LOG_ERRORF("[FAIL] token_get_type('#buffer') = %d, expected TKBUF(%d)", token_get_type(tok), TKBUF);
        ok = 0;
    }

    // Environment variable
    strcpy(tok, "$fillcol");
    if (token_get_type(tok) != TKENV) {
        LOG_ERRORF("[FAIL] token_get_type('$fillcol') = %d, expected TKENV(%d)", token_get_type(tok), TKENV);
        ok = 0;
    }

    // User variable
    strcpy(tok, "%myvar");
    if (token_get_type(tok) != TKVAR) {
        LOG_ERRORF("[FAIL] token_get_type('%%myvar') = %d, expected TKVAR(%d)", token_get_type(tok), TKVAR);
        ok = 0;
    }

    // Function
    strcpy(tok, "&add");
    if (token_get_type(tok) != TKFUN) {
        LOG_ERRORF("[FAIL] token_get_type('&add') = %d, expected TKFUN(%d)", token_get_type(tok), TKFUN);
        ok = 0;
    }

    // Label
    strcpy(tok, "*label");
    if (token_get_type(tok) != TKLBL) {
        LOG_ERRORF("[FAIL] token_get_type('*label') = %d, expected TKLBL(%d)", token_get_type(tok), TKLBL);
        ok = 0;
    }

    // Command
    strcpy(tok, "forward-char");
    if (token_get_type(tok) != TKCMD) {
        LOG_ERRORF("[FAIL] token_get_type('forward-char') = %d, expected TKCMD(%d)", token_get_type(tok), TKCMD);
        ok = 0;
    }

    PHASE_END("EVAL: GETTYP", ok);
    return ok;
}

// ===========================================================================
// abs() tests - absolute value
// ===========================================================================

static int test_abs_value(void) {
    int ok = 1;
    PHASE_START("EVAL: ABS", "Absolute value");

    if (abs(0) != 0) {
        LOG_ERRORF("[FAIL] abs(0) = %d, expected 0", abs(0));
        ok = 0;
    }

    if (abs(42) != 42) {
        LOG_ERRORF("[FAIL] abs(42) = %d, expected 42", abs(42));
        ok = 0;
    }

    if (abs(-42) != 42) {
        LOG_ERRORF("[FAIL] abs(-42) = %d, expected 42", abs(-42));
        ok = 0;
    }

    if (abs(-1) != 1) {
        LOG_ERRORF("[FAIL] abs(-1) = %d, expected 1", abs(-1));
        ok = 0;
    }

    PHASE_END("EVAL: ABS", ok);
    return ok;
}

// ===========================================================================
// editor_random() tests - random number generation
// ===========================================================================

static int test_editor_random(void) {
    int ok = 1;
    PHASE_START("EVAL: ERND", "Random number generation");

    // Generate some random numbers and verify they change
    int r1 = editor_random();
    int r2 = editor_random();
    int r3 = editor_random();

    // They should be different from each other
    if (r1 == r2 && r2 == r3) {
        LOG_ERROR("[FAIL] editor_random() returning same value repeatedly");
        ok = 0;
    }

    // All values should be positive (abs is applied)
    if (r1 < 0 || r2 < 0 || r3 < 0) {
        LOG_ERROR("[FAIL] editor_random() returning negative values");
        ok = 0;
    }

    PHASE_END("EVAL: ERND", ok);
    return ok;
}

// ===========================================================================
// Entry point
// ===========================================================================

int test_config_eval(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("eval");

    PHASE_START("CONFIG EVAL", "Evaluation system tests");

    // int_to_string tests
    ok &= test_int_to_string_positive();
    ok &= test_int_to_string_negative();

    // stol/ltos tests
    ok &= test_stol_boolean();
    ok &= test_stol_numeric();
    ok &= test_bool_to_string();

    // String conversion tests
    ok &= test_string_to_upper();
    ok &= test_string_to_lower();

    // sindex tests
    ok &= test_sindex_found();
    ok &= test_sindex_not_found();

    // xlat tests
    ok &= test_xlat_basic();

    // gettyp tests
    ok &= test_token_get_type();

    // Math utility tests
    ok &= test_abs_value();
    ok &= test_editor_random();

    if (ok) {
        LOG_INFO("[SUCCESS] All eval tests passed.");
    }

    PHASE_END("CONFIG EVAL", ok);
    return ok;
}
