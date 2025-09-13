#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "μemacs/keymap.h"
#include "internal/efunc.h"

static uint64_t now_ns(void) {
    struct timespec ts;
    (void)ts;
#if defined(TIME_UTC)
    if (timespec_get(&ts, TIME_UTC) == TIME_UTC) {
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }
#endif
    return 0ULL;
}

int main(void) {
    keymap_init_from_legacy();
    struct keymap *g = atomic_load_explicit(&global_keymap, memory_order_acquire);
    if (!g) return 1;
    const int iters = 1000000;
    uint64_t start = now_ns();
    int sum = 0;
    for (int i=0;i<iters;i++) {
        uint32_t k = (uint32_t)('A' + (i % 26));
        struct keymap_entry *e = keymap_lookup(g, k);
        sum += (e!=NULL);
    }
    uint64_t end = now_ns();
    double ms = (end - start)/1e6;
    printf("[bench] keymap_lookup: %d lookups in %.2f ms (sum=%d)\n", iters, ms, sum);
    return 0;
}
