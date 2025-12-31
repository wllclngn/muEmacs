// tests/unit/test_util_utf8.c - Unit tests for src/util/utf8.c
#include "../test_utils.h"
#include "../test_registry.h"

#include "internal/utf8.h"
#include <stdlib.h>
#include <time.h>

int test_utf8_invalid_sequences(void) {
    int ok = 1;
    PHASE_START("UTF8: INVALID", "Decode invalid UTF-8 sequences safely");

    // Continuation byte as first byte (invalid)
    const char invalid1[] = { (char)0x80, 0 };
    unicode_t u = 0; unsigned len = utf8_to_unicode(invalid1, 0, 1, &u);
    if (!(len == 1 && u == 0x80)) {
        LOG_ERROR("[FAIL] invalid1 not handled as raw byte");
        ok = 0;
    }

    // Overlong encoding (0xC0 0xAF) - returned as raw byte
    const char invalid2[] = { (char)0xC0, (char)0xAF, 0 };
    len = utf8_to_unicode(invalid2, 0, 2, &u);
    if (len != 1) {
        LOG_ERROR("[FAIL] overlong not rejected as 1-byte");
        ok = 0;
    }

    // Truncated multi-byte sequence
    const char invalid3[] = { (char)0xE2, 0 };
    len = utf8_to_unicode(invalid3, 0, 1, &u);
    if (len != 1) {
        LOG_ERROR("[FAIL] truncated 3-byte not handled as 1-byte");
        ok = 0;
    }

    PHASE_END("UTF8: INVALID", ok);
    return ok;
}

int test_utf8_randomized_sanity(void) {
    int ok = 1;
    PHASE_START("UTF8: RAND", "Randomized decode sanity");

    // Seed RNG deterministically for CI
    srand(12345);
    char buf[8];
    unicode_t u;
    const char* stress = getenv("STRESS");
    int iters = 1000;
    if (stress && strcmp(stress, "1") == 0) {
        iters = 100000; // heavier randomized coverage under stress mode
        LOG_INFOF("[INFO] STRESS=1: UTF-8 randomized iterations=%d", iters);
    }

    for (int i = 0; i < iters; ++i) {
        // Fill with random bytes (0..255), length 1..4
        int len = 1 + (rand() % 4);
        for (int j = 0; j < len; ++j) buf[j] = (char)(rand() % 256);
        unsigned consumed = utf8_to_unicode(buf, 0, len, &u);
        // Sanity: must always consume at least 1 and no more than len
        if (consumed < 1 || consumed > (unsigned)len) {
            LOG_ERRORF("[FAIL] utf8_to_unicode consumed=%u len=%d", consumed, len);
            ok = 0; break;
        }
    }

    PHASE_END("UTF8: RAND", ok);
    return ok;
}
