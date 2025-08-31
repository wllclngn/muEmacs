/*
 * μEmacs - C23 unified buffer dispatch system
 * Modern replacement for 40 identical cbuf functions
 */

#ifndef UEMACS_CBUF_DISPATCH_H
#define UEMACS_CBUF_DISPATCH_H

#include <stdint.h>
#include "core.h"

// C23 unified buffer dispatch context
struct cbuf_dispatch_context {
    int buffer_number;
    int (*base_function)(int, int, int);
    _Atomic uint64_t call_count;
    _Atomic uint32_t last_error;
    _Atomic bool initialized;
};

// Generic buffer dispatch - replaces all 40 cbuf functions
int cbuf_dispatch(int f, int n, int buffer_num);

// Initialize dispatch system (called once at startup)
void cbuf_dispatch_init(void);

// Performance statistics for monitoring
struct cbuf_stats {
    uint64_t total_calls;
    uint64_t successful_calls;
    uint64_t failed_calls;
    uint32_t most_used_buffer;
    uint64_t buffer_usage[40];
};

void cbuf_get_stats(struct cbuf_stats *stats);
void cbuf_reset_stats(void);

#endif // UEMACS_CBUF_DISPATCH_H