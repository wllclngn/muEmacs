// test_util_string.c - Unit tests for safe string functions
// Tests src/util/string.c

#include "../test_utils.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>

// Forward declarations - from src/util/string.c
extern size_t safe_strcpy(char *restrict dest, const char *restrict src, size_t dest_size);
extern size_t safe_strcat(char *restrict dest, const char *restrict src, size_t dest_size);
extern size_t safe_sprintf(char *restrict dest, size_t dest_size, const char *restrict format, ...);

// ============================================================================
// SAFE_STRCPY Tests
// ============================================================================

static int test_safe_strcpy_normal(void) {
    int ok = 1;
    PHASE_START("STRING: STRCPY-NORMAL", "Normal string copy operation");

    char dest[64] = {0};
    const char *src = "Hello";
    size_t result = safe_strcpy(dest, src, sizeof(dest));

    if (result != 5) {
        LOG_ERRORF("[FAIL] returned length wrong: got %zu, expected 5", result);
        ok = 0;
    }

    if (strcmp(dest, "Hello") != 0) {
        LOG_ERRORF("[FAIL] dest mismatch: got '%s'", dest);
        ok = 0;
    }

    PHASE_END("STRING: STRCPY-NORMAL", ok);
    return ok;
}

static int test_safe_strcpy_exact_fit(void) {
    int ok = 1;
    PHASE_START("STRING: STRCPY-EXACT", "String copy exactly fitting buffer");

    // Buffer size = string length + 1 (for null term)
    char dest[6] = {0};
    const char *src = "Hello";
    size_t result = safe_strcpy(dest, src, sizeof(dest));

    if (result != 5) {
        LOG_ERRORF("[FAIL] returned length wrong: got %zu, expected 5", result);
        ok = 0;
    }

    if (strcmp(dest, "Hello") != 0) {
        LOG_ERRORF("[FAIL] dest mismatch: got '%s'", dest);
        ok = 0;
    }

    if (dest[5] != '\0') {
        LOG_ERROR("[FAIL] not null terminated");
        ok = 0;
    }

    PHASE_END("STRING: STRCPY-EXACT", ok);
    return ok;
}

static int test_safe_strcpy_truncation(void) {
    int ok = 1;
    PHASE_START("STRING: STRCPY-TRUNCATE", "String copy with truncation");

    // Buffer too small - should truncate
    char dest[4] = {0};
    const char *src = "Hello";
    size_t result = safe_strcpy(dest, src, sizeof(dest));

    // Should return 3 (what fit)
    if (result != 3) {
        LOG_ERRORF("[FAIL] returned length wrong: got %zu, expected 3", result);
        ok = 0;
    }

    // Should be null-terminated and contain "Hel"
    if (strcmp(dest, "Hel") != 0) {
        LOG_ERRORF("[FAIL] dest mismatch: got '%s', expected 'Hel'", dest);
        ok = 0;
    }

    if (dest[3] != '\0') {
        LOG_ERROR("[FAIL] not null terminated");
        ok = 0;
    }

    PHASE_END("STRING: STRCPY-TRUNCATE", ok);
    return ok;
}

static int test_safe_strcpy_empty_src(void) {
    int ok = 1;
    PHASE_START("STRING: STRCPY-EMPTY-SRC", "Copy empty source string");

    char dest[64] = "JUNK";
    const char *src = "";
    size_t result = safe_strcpy(dest, src, sizeof(dest));

    if (result != 0) {
        LOG_ERRORF("[FAIL] returned length wrong: got %zu, expected 0", result);
        ok = 0;
    }

    if (strcmp(dest, "") != 0) {
        LOG_ERRORF("[FAIL] dest not empty: got '%s'", dest);
        ok = 0;
    }

    PHASE_END("STRING: STRCPY-EMPTY-SRC", ok);
    return ok;
}

static int test_safe_strcpy_null_pointer_src(void) {
    int ok = 1;
    PHASE_START("STRING: STRCPY-nullptr-SRC", "Copy with nullptr source");

    char dest[64] = "ORIGINAL";
    size_t result = safe_strcpy(dest, nullptr, sizeof(dest));

    // Should return 0 and not modify dest
    if (result != 0) {
        LOG_ERRORF("[FAIL] should return 0 for nullptr src, got %zu", result);
        ok = 0;
    }

    // Dest should be unchanged
    if (strcmp(dest, "ORIGINAL") != 0) {
        LOG_ERRORF("[FAIL] dest was modified: got '%s'", dest);
        ok = 0;
    }

    PHASE_END("STRING: STRCPY-nullptr-SRC", ok);
    return ok;
}

static int test_safe_strcpy_null_pointer_dest(void) {
    int ok = 1;
    PHASE_START("STRING: STRCPY-nullptr-DEST", "Copy with nullptr destination");

    const char *src = "Hello";
    size_t result = safe_strcpy(nullptr, src, 64);

    if (result != 0) {
        LOG_ERRORF("[FAIL] should return 0 for nullptr dest, got %zu", result);
        ok = 0;
    }

    PHASE_END("STRING: STRCPY-nullptr-DEST", ok);
    return ok;
}

static int test_safe_strcpy_zero_size(void) {
    int ok = 1;
    PHASE_START("STRING: STRCPY-ZERO-SIZE", "Copy with zero buffer size");

    char dest[64] = "ORIGINAL";
    const char *src = "Hello";
    size_t result = safe_strcpy(dest, src, 0);

    if (result != 0) {
        LOG_ERRORF("[FAIL] should return 0 for size 0, got %zu", result);
        ok = 0;
    }

    // Dest should be unchanged
    if (strcmp(dest, "ORIGINAL") != 0) {
        LOG_ERRORF("[FAIL] dest was modified: got '%s'", dest);
        ok = 0;
    }

    PHASE_END("STRING: STRCPY-ZERO-SIZE", ok);
    return ok;
}

static int test_safe_strcpy_long_string(void) {
    int ok = 1;
    PHASE_START("STRING: STRCPY-LONG", "Copy long string with truncation");

    char dest[32] = {0};
    const char *src = "The quick brown fox jumps over the lazy dog";
    size_t result = safe_strcpy(dest, src, sizeof(dest));

    // Should return 31 (dest_size - 1)
    if (result != 31) {
        LOG_ERRORF("[FAIL] returned length wrong: got %zu, expected 31", result);
        ok = 0;
    }

    // Should be exactly 31 chars + null term
    if (strlen(dest) != 31) {
        LOG_ERRORF("[FAIL] dest length wrong: got %zu", strlen(dest));
        ok = 0;
    }

    // Should be null-terminated
    if (dest[31] != '\0') {
        LOG_ERROR("[FAIL] not null terminated");
        ok = 0;
    }

    PHASE_END("STRING: STRCPY-LONG", ok);
    return ok;
}

// ============================================================================
// SAFE_STRCAT Tests
// ============================================================================

static int test_safe_strcat_normal(void) {
    int ok = 1;
    PHASE_START("STRING: STRCAT-NORMAL", "Normal string concatenation");

    char dest[64] = "Hello";
    const char *src = " World";
    size_t result = safe_strcat(dest, src, sizeof(dest));

    if (result != 11) {
        LOG_ERRORF("[FAIL] returned length wrong: got %zu, expected 11", result);
        ok = 0;
    }

    if (strcmp(dest, "Hello World") != 0) {
        LOG_ERRORF("[FAIL] dest mismatch: got '%s'", dest);
        ok = 0;
    }

    PHASE_END("STRING: STRCAT-NORMAL", ok);
    return ok;
}

static int test_safe_strcat_empty_src(void) {
    int ok = 1;
    PHASE_START("STRING: STRCAT-EMPTY-SRC", "Concatenate empty source");

    char dest[64] = "Hello";
    const char *src = "";
    size_t result = safe_strcat(dest, src, sizeof(dest));

    if (result != 5) {
        LOG_ERRORF("[FAIL] returned length wrong: got %zu, expected 5", result);
        ok = 0;
    }

    if (strcmp(dest, "Hello") != 0) {
        LOG_ERRORF("[FAIL] dest mismatch: got '%s'", dest);
        ok = 0;
    }

    PHASE_END("STRING: STRCAT-EMPTY-SRC", ok);
    return ok;
}

static int test_safe_strcat_empty_dest(void) {
    int ok = 1;
    PHASE_START("STRING: STRCAT-EMPTY-DEST", "Concatenate to empty destination");

    char dest[64] = "";
    const char *src = "Hello";
    size_t result = safe_strcat(dest, src, sizeof(dest));

    if (result != 5) {
        LOG_ERRORF("[FAIL] returned length wrong: got %zu, expected 5", result);
        ok = 0;
    }

    if (strcmp(dest, "Hello") != 0) {
        LOG_ERRORF("[FAIL] dest mismatch: got '%s'", dest);
        ok = 0;
    }

    PHASE_END("STRING: STRCAT-EMPTY-DEST", ok);
    return ok;
}

static int test_safe_strcat_truncation(void) {
    int ok = 1;
    PHASE_START("STRING: STRCAT-TRUNCATE", "Concatenation with truncation");

    char dest[8] = "Hi";
    const char *src = "LongStr";
    size_t result = safe_strcat(dest, src, sizeof(dest));

    // dest has 2 chars, can fit 5 more (7 total + null)
    // Should truncate src to "LongS" -> "HiLongS"
    if (result != 7) {
        LOG_ERRORF("[FAIL] returned length wrong: got %zu, expected 7", result);
        ok = 0;
    }

    if (strcmp(dest, "HiLongS") != 0) {
        LOG_ERRORF("[FAIL] dest mismatch: got '%s', expected 'HiLongS'", dest);
        ok = 0;
    }

    PHASE_END("STRING: STRCAT-TRUNCATE", ok);
    return ok;
}

static int test_safe_strcat_null_src(void) {
    int ok = 1;
    PHASE_START("STRING: STRCAT-nullptr-SRC", "Concatenate nullptr source");

    char dest[64] = "Hello";
    size_t result = safe_strcat(dest, nullptr, sizeof(dest));

    if (result != 0) {
        LOG_ERRORF("[FAIL] should return 0 for nullptr src, got %zu", result);
        ok = 0;
    }

    if (strcmp(dest, "Hello") != 0) {
        LOG_ERRORF("[FAIL] dest was modified: got '%s'", dest);
        ok = 0;
    }

    PHASE_END("STRING: STRCAT-nullptr-SRC", ok);
    return ok;
}

static int test_safe_strcat_null_dest(void) {
    int ok = 1;
    PHASE_START("STRING: STRCAT-nullptr-DEST", "Concatenate nullptr destination");

    const char *src = "Hello";
    size_t result = safe_strcat(nullptr, src, 64);

    if (result != 0) {
        LOG_ERRORF("[FAIL] should return 0 for nullptr dest, got %zu", result);
        ok = 0;
    }

    PHASE_END("STRING: STRCAT-nullptr-DEST", ok);
    return ok;
}

static int test_safe_strcat_zero_size(void) {
    int ok = 1;
    PHASE_START("STRING: STRCAT-ZERO-SIZE", "Concatenate with zero buffer size");

    char dest[64] = "Hello";
    const char *src = "World";
    size_t result = safe_strcat(dest, src, 0);

    if (result != 0) {
        LOG_ERRORF("[FAIL] should return 0 for size 0, got %zu", result);
        ok = 0;
    }

    if (strcmp(dest, "Hello") != 0) {
        LOG_ERRORF("[FAIL] dest was modified: got '%s'", dest);
        ok = 0;
    }

    PHASE_END("STRING: STRCAT-ZERO-SIZE", ok);
    return ok;
}

static int test_safe_strcat_not_null_terminated(void) {
    int ok = 1;
    PHASE_START("STRING: STRCAT-NOT-nullptr-TERM", "Concatenate into non-null-terminated buffer");

    char dest[8];
    memset(dest, 'X', sizeof(dest));
    // First ensure it's not null-terminated
    const char *src = "Hi";

    // This is an edge case - destination is not null-terminated
    // safe_strcat should detect this and return early
    size_t result = safe_strcat(dest, src, sizeof(dest));

    // Should return dest_size (8) indicating error
    if (result != sizeof(dest)) {
        LOG_ERRORF("[FAIL] should return %zu for non-null-term dest, got %zu", sizeof(dest), result);
        ok = 0;
    }

    PHASE_END("STRING: STRCAT-NOT-nullptr-TERM", ok);
    return ok;
}

static int test_safe_strcat_exactly_full(void) {
    int ok = 1;
    PHASE_START("STRING: STRCAT-EXACTLY-FULL", "Concatenate exactly filling buffer");

    // dest_size = 6, "Hi" (2 chars) + "World" (5 chars) = 7 needed, but we only have 6
    char dest[6] = "Hi";
    const char *src = "World";
    size_t result = safe_strcat(dest, src, sizeof(dest));

    // Can only fit 3 more chars (5 - 2 = 3 remaining)
    // So "HiWor" + null
    if (result != 5) {
        LOG_ERRORF("[FAIL] returned length wrong: got %zu, expected 5", result);
        ok = 0;
    }

    if (strcmp(dest, "HiWor") != 0) {
        LOG_ERRORF("[FAIL] dest mismatch: got '%s', expected 'HiWor'", dest);
        ok = 0;
    }

    PHASE_END("STRING: STRCAT-EXACTLY-FULL", ok);
    return ok;
}

// ============================================================================
// SAFE_SPRINTF Tests (bonus)
// ============================================================================

static int test_safe_sprintf_simple(void) {
    int ok = 1;
    PHASE_START("STRING: SPRINTF-SIMPLE", "Simple formatted string");

    char dest[64] = {0};
    size_t result = safe_sprintf(dest, sizeof(dest), "Hello %s", "World");

    if (result != 11) {
        LOG_ERRORF("[FAIL] returned length wrong: got %zu, expected 11", result);
        ok = 0;
    }

    if (strcmp(dest, "Hello World") != 0) {
        LOG_ERRORF("[FAIL] dest mismatch: got '%s'", dest);
        ok = 0;
    }

    PHASE_END("STRING: SPRINTF-SIMPLE", ok);
    return ok;
}

static int test_safe_sprintf_integer(void) {
    int ok = 1;
    PHASE_START("STRING: SPRINTF-INT", "Integer formatting");

    char dest[64] = {0};
    size_t result = safe_sprintf(dest, sizeof(dest), "Number: %d", 42);

    if (strcmp(dest, "Number: 42") != 0) {
        LOG_ERRORF("[FAIL] dest mismatch: got '%s'", dest);
        ok = 0;
    }

    PHASE_END("STRING: SPRINTF-INT", ok);
    return ok;
}

static int test_safe_sprintf_truncation(void) {
    int ok = 1;
    PHASE_START("STRING: SPRINTF-TRUNCATE", "Formatted string truncation");

    char dest[10] = {0};
    size_t result = safe_sprintf(dest, sizeof(dest), "Hello %s", "World");

    // "Hello World" is 11 chars, buffer is 10
    // vsnprintf will write up to 9 chars + null terminator
    // So we get "Hello Wor" (9 chars + null)
    if (result != 9) {
        LOG_ERRORF("[FAIL] returned length wrong: got %zu, expected 9", result);
        ok = 0;
    }

    if (strcmp(dest, "Hello Wor") != 0) {
        LOG_ERRORF("[FAIL] dest mismatch: got '%s', expected 'Hello Wor'", dest);
        ok = 0;
    }

    PHASE_END("STRING: SPRINTF-TRUNCATE", ok);
    return ok;
}

static int test_safe_sprintf_null_format(void) {
    int ok = 1;
    PHASE_START("STRING: SPRINTF-nullptr-FMT", "nullptr format string");

    char dest[64] = "ORIGINAL";
    size_t result = safe_sprintf(dest, sizeof(dest), nullptr);

    if (result != 0) {
        LOG_ERRORF("[FAIL] should return 0 for nullptr format, got %zu", result);
        ok = 0;
    }

    if (strcmp(dest, "ORIGINAL") != 0) {
        LOG_ERRORF("[FAIL] dest was modified: got '%s'", dest);
        ok = 0;
    }

    PHASE_END("STRING: SPRINTF-nullptr-FMT", ok);
    return ok;
}

// ============================================================================
// PUBLIC ENTRY POINT
// ============================================================================

int test_util_string(void) {
    LOG_INFOF("\n%s### String Utility Unit Tests ###%s\n", BLUE, RESET);

    int results = 0;

    // safe_strcpy tests
    results += test_safe_strcpy_normal();
    results += test_safe_strcpy_exact_fit();
    results += test_safe_strcpy_truncation();
    results += test_safe_strcpy_empty_src();
    results += test_safe_strcpy_null_pointer_src();
    results += test_safe_strcpy_null_pointer_dest();
    results += test_safe_strcpy_zero_size();
    results += test_safe_strcpy_long_string();

    // safe_strcat tests
    results += test_safe_strcat_normal();
    results += test_safe_strcat_empty_src();
    results += test_safe_strcat_empty_dest();
    results += test_safe_strcat_truncation();
    results += test_safe_strcat_null_src();
    results += test_safe_strcat_null_dest();
    results += test_safe_strcat_zero_size();
    results += test_safe_strcat_not_null_terminated();
    results += test_safe_strcat_exactly_full();

    // safe_sprintf tests
    results += test_safe_sprintf_simple();
    results += test_safe_sprintf_integer();
    results += test_safe_sprintf_truncation();
    results += test_safe_sprintf_null_format();

    int total = 21;

    LOG_INFOF("\n%s========================================%s", BLUE, RESET);
    LOG_INFOF("%sTest Summary%s", BLUE, RESET);
    LOG_INFOF("%s========================================%s", BLUE, RESET);
    LOG_INFOF("Passed: %d", results);
    LOG_INFOF("Failed: %d", total - results);
    LOG_INFOF("%s========================================%s\n", BLUE, RESET);

    return results == total;  // 1 on success, 0 on failure
}
