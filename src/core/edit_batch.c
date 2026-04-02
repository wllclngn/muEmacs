#include <stdatomic.h>
#include <stdbool.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "edit_batch.h"

void edit_begin(void) {
    atomic_fetch_add(&edit_transaction_depth, 1);
}

void edit_commit(void) {
    int prev = atomic_fetch_sub(&edit_transaction_depth, 1);
    if (prev <= 1) {
        // publish pending changes atomically
        sgarbf = true;
        update(true);
        atomic_store(&edit_transaction_depth, 0);
    }
}

void edit_abort(void) {
    // best-effort rollback just forces redraw; content rollback is caller's responsibility
    atomic_store(&edit_transaction_depth, 0);
    sgarbf = true;
    update(true);
}

