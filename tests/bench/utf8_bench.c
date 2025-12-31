#include "../test_utils.h"
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include "utf8.h"

static uint64_t now_ns(void) {
    struct timespec ts;
    (void)ts;
#if defined(TIME_UTC)
    if (timespec_get(&ts, TIME_UTC) == TIME_UTC) {
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }
#endif
    /* Portable fallback if timespec_get is unavailable */
    return 0ULL;
}

int main(void) {
    const int iters = 500000;
    uint8_t buf[4];
    uint64_t start = now_ns();
    unicode_t code = 0;
    int consumed = 0;
    int okcount = 0;
    for (int i=0;i<iters;i++) {
        // Generate a simple 2-byte sequence in Latin-1 range extended
        uint32_t cp = 0x00A0 + (i % 0x40);
        int len = unicode_to_utf8(cp, (char*)buf);
        consumed = (int)utf8_to_unicode((const char*)buf, 0u, (unsigned)len, &code);
        okcount += (consumed > 0 && code == cp);
    }
    uint64_t end = now_ns();
    double ms = (end - start)/1e6;
    LOG_INFOF("[bench] utf8 encode/decode: %d ops in %.2f ms (ok=%d)", iters, ms, okcount);
    return 0;
}
