/*
 * μEmacs - C23 unified buffer dispatch system  
 * Replaces 40 identical cbuf functions with single implementation
 */

#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

// Simple unified dispatch - no statistics for Phase 1
static int cbuf_dispatch(int f, int n, int buffer_num)
{
    // Bounds check
    if (buffer_num < 1 || buffer_num > 40) {
        return FALSE;
    }
    
    // Call existing cbuf implementation  
    return cbuf(f, n, buffer_num);
}

// Generate all 40 wrapper functions using C23 function generation
#define CBUF_WRAPPER(num) \
    int cbuf##num(int f, int n) { \
        return cbuf_dispatch(f, n, num); \
    }

// C23 macro expansion for all 40 functions
CBUF_WRAPPER(1)  CBUF_WRAPPER(2)  CBUF_WRAPPER(3)  CBUF_WRAPPER(4)  CBUF_WRAPPER(5)
CBUF_WRAPPER(6)  CBUF_WRAPPER(7)  CBUF_WRAPPER(8)  CBUF_WRAPPER(9)  CBUF_WRAPPER(10)
CBUF_WRAPPER(11) CBUF_WRAPPER(12) CBUF_WRAPPER(13) CBUF_WRAPPER(14) CBUF_WRAPPER(15)
CBUF_WRAPPER(16) CBUF_WRAPPER(17) CBUF_WRAPPER(18) CBUF_WRAPPER(19) CBUF_WRAPPER(20)
CBUF_WRAPPER(21) CBUF_WRAPPER(22) CBUF_WRAPPER(23) CBUF_WRAPPER(24) CBUF_WRAPPER(25)
CBUF_WRAPPER(26) CBUF_WRAPPER(27) CBUF_WRAPPER(28) CBUF_WRAPPER(29) CBUF_WRAPPER(30)
CBUF_WRAPPER(31) CBUF_WRAPPER(32) CBUF_WRAPPER(33) CBUF_WRAPPER(34) CBUF_WRAPPER(35)
CBUF_WRAPPER(36) CBUF_WRAPPER(37) CBUF_WRAPPER(38) CBUF_WRAPPER(39) CBUF_WRAPPER(40)

#undef CBUF_WRAPPER