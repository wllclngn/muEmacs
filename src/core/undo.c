/*
 * undo.c - Per-buffer, circular-array-based C23 undo system.
 * Inspired by VSCode/GNU Emacs, designed for performance and correctness.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <time.h> // Required for struct timespec and clock_gettime

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "evar.h"
#include "line.h"
#include "memory.h"
#include "error.h" // Required for REPORT_ERROR and ERR_MEMORY
#include "undo.h"
/* Helper: mark all windows showing bp to refresh their modelines */
static inline void refresh_modelines_for_buffer(struct buffer *bp) {
    struct window *wp = wheadp;
    while (wp) { if (wp->w_bufp == bp) wp->w_flag |= WFMODE; wp = wp->w_wndp; }
}

// Simple ASCII word classification for grouping decisions
static inline bool undo_is_word_byte(int ch) {
    return ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r';
}

// Per-buffer atomic undo stack
#define UNDO_INITIAL_CAPACITY 100
#define UNDO_MAX_CAPACITY 10000 // A reasonable upper limit for dynamic resizing

// An undoable operation
struct undo_operation {
    enum edit_type type;
    long dot_l;             // line number of change
    int dot_o;              // offset on line
    char *text_data;
    int text_length;
    uint64_t version_id;
    struct timespec timestamp;
    uint64_t group_id;      // grouping id for coalesced undo steps
};

// A circular array of undo operations for a buffer
struct atomic_undo_stack {
    struct undo_operation *operations;
    _Atomic int capacity;
    _Atomic int head;           // Index of the next slot to be written to
    _Atomic int tail;           // Index of the oldest operation
    _Atomic int undo_ptr;       // Index of the last operation that can be undone
    _Atomic int count;          // Number of valid operations in the stack
    _Atomic uint64_t version;
    _Atomic bool in_operation;   // Prevents recursive undo/redo
    _Atomic bool group_forced;   // Within an explicit group
    _Atomic bool resize_failed;  // Prevents infinite resize attempts
    _Atomic uint64_t current_group_id; // Current group id
};

// --- Private Functions ---

// Frees the data associated with a single undo operation
static void free_undo_operation(struct undo_operation *op) {
    if (op && op->text_data) {
        SAFE_FREE(op->text_data);
    }
}

// --- Private Functions ---

// Resizes the undo stack if it's full, doubling its capacity up to UNDO_MAX_CAPACITY
static bool undo_stack_resize_if_needed(struct atomic_undo_stack *stack) {
    if (atomic_load(&stack->count) < atomic_load(&stack->capacity)) {
        return true; // No resize needed
    }

    // If resize has already failed, don't keep trying
    if (atomic_load(&stack->resize_failed)) {
        return false;
    }

    if (atomic_load(&stack->capacity) >= UNDO_MAX_CAPACITY) {
        atomic_store(&stack->resize_failed, true);
        return false; // Cannot resize further
    }

    int old_capacity = atomic_load(&stack->capacity);
    int new_capacity = old_capacity * 2;
    if (new_capacity > UNDO_MAX_CAPACITY) {
        new_capacity = UNDO_MAX_CAPACITY;
    }

    struct undo_operation *new_operations = safe_alloc(new_capacity * sizeof(struct undo_operation), "resized undo operations array", __FILE__, __LINE__);
    if (!new_operations) {
        atomic_store(&stack->resize_failed, true);
        return false; // Allocation failed
    }

    // Copy existing operations, handling circular buffer wrap-around
    int current_index = atomic_load(&stack->tail);
    for (int i = 0; i < atomic_load(&stack->count); ++i) {
        memcpy(&new_operations[i], &stack->operations[current_index], sizeof(struct undo_operation));
        current_index = (current_index + 1) % old_capacity;
    }

    // Free old operations array
    SAFE_FREE(stack->operations);

    // Update stack pointers atomically
    stack->operations = new_operations;
    atomic_store(&stack->capacity, new_capacity);
    atomic_store(&stack->head, atomic_load(&stack->count)); // Head is now at the end of copied elements
    atomic_store(&stack->tail, 0); // Tail is at the beginning of the new array
    atomic_store(&stack->undo_ptr, atomic_load(&stack->count) - 1); // undo_ptr points to the last copied element

    return true;
}

// --- Public API ---

// Creates and initializes a new undo stack for a buffer.
struct atomic_undo_stack *undo_stack_create(void) {
    struct atomic_undo_stack *stack = safe_alloc(sizeof(struct atomic_undo_stack), "undo stack", __FILE__, __LINE__);
    if (!stack) return NULL;

    stack->operations = safe_alloc(UNDO_INITIAL_CAPACITY * sizeof(struct undo_operation), "undo operations array", __FILE__, __LINE__);
    if (!stack->operations) {
        SAFE_FREE(stack);
        return NULL;
    }

    atomic_store(&stack->capacity, UNDO_INITIAL_CAPACITY);
    atomic_store(&stack->head, 0);
    atomic_store(&stack->tail, 0);
    atomic_store(&stack->undo_ptr, -1);
    atomic_store(&stack->count, 0);
    atomic_store(&stack->version, 1);
    atomic_store(&stack->in_operation, false);
    atomic_store(&stack->group_forced, false);
    atomic_store(&stack->resize_failed, false);
    atomic_store(&stack->current_group_id, 1);

    return stack;
}

// Destroys an undo stack, freeing all associated memory.
void undo_stack_destroy(struct atomic_undo_stack *stack) {
    if (!stack) return;

    for (int i = 0; i < atomic_load(&stack->capacity); ++i) {
        free_undo_operation(&stack->operations[i]);
    }
    SAFE_FREE(stack->operations);
    SAFE_FREE(stack);
}

// Records a text insertion for undo.
void undo_record_insert(struct buffer *bp, long l, int o, const char *text, int len) {
    if (!bp || !bp->b_undo_stack || !text) return;
    struct atomic_undo_stack *stack = bp->b_undo_stack;

    if (atomic_load(&stack->in_operation)) return;

    // Resize if needed before adding new operation
    if (!undo_stack_resize_if_needed(stack)) {
        // Silently disable undo to prevent infinite error loop
        atomic_store(&stack->resize_failed, true);
        return;
    }

    // Invalidate any "redo" operations
    int undo_ptr = atomic_load(&stack->undo_ptr);
    if (undo_ptr != (atomic_load(&stack->head) - 1 + atomic_load(&stack->capacity)) % atomic_load(&stack->capacity)) {
        int current = (undo_ptr + 1) % atomic_load(&stack->capacity);
        while (current != atomic_load(&stack->head)) {
            free_undo_operation(&stack->operations[current]);
            atomic_fetch_sub(&stack->count, 1);
            current = (current + 1) % atomic_load(&stack->capacity);
        }
        atomic_store(&stack->head, (undo_ptr + 1) % atomic_load(&stack->capacity));
    }

    // Get the next slot
    int head = atomic_load(&stack->head);
    struct undo_operation *op = &stack->operations[head];
    free_undo_operation(op); // Free any old data at this slot

    op->type = EDIT_INSERT;
    op->dot_l = l;
    op->dot_o = o;
    op->text_length = len;
    op->version_id = atomic_fetch_add(&stack->version, 1);
    clock_gettime(CLOCK_MONOTONIC, &op->timestamp);
    op->text_data = safe_alloc(len + 1, "undo text", __FILE__, __LINE__);
    if (op->text_data) {
        memcpy(op->text_data, text, len);
        op->text_data[len] = '\0';
    }

    // Assign grouping id (auto or forced)
    uint64_t group_id = atomic_load(&stack->current_group_id);
    bool forced = atomic_load(&stack->group_forced);
    if (!forced) {
        // Auto-group with previous op if insertion at adjacent location and recent
        int prev_idx = (head - 1 + atomic_load(&stack->capacity)) % atomic_load(&stack->capacity);
        if (atomic_load(&stack->count) > 0) {
            struct undo_operation *prev = &stack->operations[prev_idx];
            if (prev->type == EDIT_INSERT && prev->dot_l == l && (prev->dot_o + prev->text_length == o)) {
                long dt_ns = (op->timestamp.tv_sec - prev->timestamp.tv_sec) * 1000000000LL + (op->timestamp.tv_nsec - prev->timestamp.tv_nsec);
                bool time_ok = (dt_ns >= 0 && dt_ns < 400000000); // 400ms
                bool single_char = (len == 1 && prev->text_length == 1);
                bool keep_group = time_ok;
                if (single_char && prev->text_data && op->text_data) {
                    int a = (unsigned char)op->text_data[0];
                    int b = (unsigned char)prev->text_data[0];
                    // Keep grouping for word characters; break when transitioning into a new word
                    if (undo_is_word_byte(a) && undo_is_word_byte(b)) {
                        keep_group = keep_group && true;
                    } else if (!undo_is_word_byte(a)) {
                        // Whitespace: safe to keep grouping
                        keep_group = keep_group && true;
                    } else {
                        keep_group = false;
                    }
                }
                group_id = keep_group ? prev->group_id : (atomic_fetch_add(&stack->current_group_id, 1) + 1);
            } else {
                group_id = atomic_fetch_add(&stack->current_group_id, 1) + 1;
            }
        } else {
            group_id = atomic_fetch_add(&stack->current_group_id, 1) + 1;
        }
    }
    op->group_id = group_id;

    // Advance head and undo_ptr
    atomic_store(&stack->head, (head + 1) % atomic_load(&stack->capacity));
    atomic_store(&stack->undo_ptr, head);

    // Manage count and tail for circular buffer
    if (atomic_load(&stack->count) < atomic_load(&stack->capacity)) {
        atomic_fetch_add(&stack->count, 1);
    } else {
        atomic_store(&stack->tail, (atomic_load(&stack->tail) + 1) % atomic_load(&stack->capacity));
    }
}

// Records a text deletion for undo.
void undo_record_delete(struct buffer *bp, long l, int o, const char *text, int len) {
    if (!bp || !bp->b_undo_stack || !text) return;
    struct atomic_undo_stack *stack = bp->b_undo_stack;

    if (atomic_load(&stack->in_operation)) return;

    // Resize if needed before adding new operation
    if (!undo_stack_resize_if_needed(stack)) {
        // Silently disable undo to prevent infinite error loop
        atomic_store(&stack->resize_failed, true);
        return;
    }

    // Invalidate any "redo" operations (same logic as insert)
    int undo_ptr = atomic_load(&stack->undo_ptr);
    if (undo_ptr != (atomic_load(&stack->head) - 1 + atomic_load(&stack->capacity)) % atomic_load(&stack->capacity)) {
        int current = (undo_ptr + 1) % atomic_load(&stack->capacity);
        while (current != atomic_load(&stack->head)) {
            free_undo_operation(&stack->operations[current]);
            atomic_fetch_sub(&stack->count, 1);
            current = (current + 1) % atomic_load(&stack->capacity);
        }
        atomic_store(&stack->head, (undo_ptr + 1) % atomic_load(&stack->capacity));
    }

    // Get the next slot
    int head = atomic_load(&stack->head);
    struct undo_operation *op = &stack->operations[head];
    free_undo_operation(op);

    op->type = EDIT_DELETE;
    op->dot_l = l;
    op->dot_o = o;
    op->text_length = len;
    op->version_id = atomic_fetch_add(&stack->version, 1);
    clock_gettime(CLOCK_MONOTONIC, &op->timestamp);
    op->text_data = safe_alloc(len + 1, "undo text", __FILE__, __LINE__);
    if (op->text_data) {
        memcpy(op->text_data, text, len);
        op->text_data[len] = '\0';
    }

    // Assign grouping id (auto or forced)
    uint64_t group_id = atomic_load(&stack->current_group_id);
    bool forced = atomic_load(&stack->group_forced);
    if (!forced) {
        int prev_idx = (head - 1 + atomic_load(&stack->capacity)) % atomic_load(&stack->capacity);
        if (atomic_load(&stack->count) > 0) {
            struct undo_operation *prev = &stack->operations[prev_idx];
            long dt_ns = (op->timestamp.tv_sec - prev->timestamp.tv_sec) * 1000000000LL + (op->timestamp.tv_nsec - prev->timestamp.tv_nsec);
            bool time_ok = (dt_ns >= 0 && dt_ns < 400000000);
            bool same_line = (prev->dot_l == l);
            bool same_offset = (prev->dot_o == o); // forward delete
            bool backspace_adjacent = (prev->dot_o == o + op->text_length);
            bool single_char = (len == 1 && prev->text_length == 1);
            bool keep_group = false;
            if (prev->type == EDIT_DELETE && same_line && time_ok && (same_offset || backspace_adjacent)) {
                keep_group = true;
                if (single_char && prev->text_data && op->text_data) {
                    int a = (unsigned char)op->text_data[0];
                    int b = (unsigned char)prev->text_data[0];
                    if (undo_is_word_byte(a) && undo_is_word_byte(b)) {
                        keep_group = keep_group && true;
                    } else if (!undo_is_word_byte(a)) {
                        keep_group = keep_group && true;
                    } else {
                        keep_group = false;
                    }
                }
            }
            group_id = keep_group ? prev->group_id : (atomic_fetch_add(&stack->current_group_id, 1) + 1);
        } else {
            group_id = atomic_fetch_add(&stack->current_group_id, 1) + 1;
        }
    }
    op->group_id = group_id;

    // Advance head and undo_ptr
    atomic_store(&stack->head, (head + 1) % atomic_load(&stack->capacity));
    atomic_store(&stack->undo_ptr, head);

    // Manage count and tail for circular buffer
    if (atomic_load(&stack->count) < atomic_load(&stack->capacity)) {
        atomic_fetch_add(&stack->count, 1);
    } else {
        atomic_store(&stack->tail, (atomic_load(&stack->tail) + 1) % atomic_load(&stack->capacity));
    }
}

// Performs an undo operation on the given buffer.
int undo_operation(struct buffer *bp) {
    if (!bp || !bp->b_undo_stack) return FALSE;
    struct atomic_undo_stack *stack = bp->b_undo_stack;

    if (atomic_load(&stack->in_operation)) return FALSE;
    
    int undo_ptr = atomic_load(&stack->undo_ptr);
    if (undo_ptr == -1 || undo_ptr == (atomic_load(&stack->tail) - 1 + atomic_load(&stack->capacity)) % atomic_load(&stack->capacity)) {
        return FALSE; // Nothing to undo
    }

    struct undo_operation *op = &stack->operations[undo_ptr];
    atomic_store(&stack->in_operation, true);

    gotoline(TRUE, op->dot_l);
    curwp->w_doto = op->dot_o;

    bool success = false;
    if (op->type == EDIT_INSERT) {
        success = ldelete(op->text_length, FALSE);
    } else if (op->type == EDIT_DELETE) {
        success = linsert_str(op->text_data);
    }

    if (success) {
        // Undo all operations in the same group
        uint64_t gid = op->group_id;
        int cap = atomic_load(&stack->capacity);
        int cursor = undo_ptr;
        while (true) {
            int prev = (cursor - 1 + cap) % cap;
            if (prev == ((atomic_load(&stack->tail) - 1 + cap) % cap)) break;
            struct undo_operation *pop = &stack->operations[prev];
            if (pop->group_id != gid) break;
            // apply previous op in group
            gotoline(TRUE, pop->dot_l);
            curwp->w_doto = pop->dot_o;
            if (pop->type == EDIT_INSERT) {
                if (!ldelete(pop->text_length, FALSE)) break;
            } else if (pop->type == EDIT_DELETE) {
                if (!linsert_str(pop->text_data)) break;
            } else {
                break;
            }
            cursor = prev;
        }
        atomic_store(&stack->undo_ptr, (cursor - 1 + cap) % cap);

        // Clean/dirty decision: if current version equals saved_version, we are clean
        uint64_t cur_version = (atomic_load(&stack->undo_ptr) == -1)
            ? 1
            : stack->operations[atomic_load(&stack->undo_ptr)].version_id;
        if (cur_version == atomic_load(&bp->b_saved_version_id)) {
            bp->b_flag &= ~BFCHG;
        } else {
            bp->b_flag |= BFCHG;
        }
        refresh_modelines_for_buffer(bp);
    }
    
    curwp->w_flag |= WFHARD;
    atomic_store(&stack->in_operation, false);
    return success ? TRUE : FALSE;
}

// Performs a redo operation on the given buffer.
int redo_operation(struct buffer *bp) {
    if (!bp || !bp->b_undo_stack) return FALSE;
    struct atomic_undo_stack *stack = bp->b_undo_stack;

    if (atomic_load(&stack->in_operation)) return FALSE;

    int undo_ptr = atomic_load(&stack->undo_ptr);
    int capacity = atomic_load(&stack->capacity);
    int head = atomic_load(&stack->head);
    
    // If undo_ptr is -1, we're at the beginning, so redo from tail
    int redo_ptr;
    if (undo_ptr == -1) {
        redo_ptr = atomic_load(&stack->tail);
    } else {
        redo_ptr = (undo_ptr + 1) % capacity;
    }

    if (redo_ptr == head) {
        return FALSE; // Nothing to redo
    }

    struct undo_operation *op = &stack->operations[redo_ptr];
    atomic_store(&stack->in_operation, true);

    gotoline(TRUE, op->dot_l);
    curwp->w_doto = op->dot_o;

    bool success = false;
    if (op->type == EDIT_INSERT) {
        success = linsert_str(op->text_data);
    } else if (op->type == EDIT_DELETE) {
        success = ldelete(op->text_length, FALSE);
    }

    if (success) {
        // Redo all operations in the same group
        uint64_t gid = op->group_id;
        int cap = atomic_load(&stack->capacity);
        int cursor = redo_ptr;
        while (true) {
            int next = (cursor + 1) % cap;
            if (next == atomic_load(&stack->head)) break;
            struct undo_operation *nop = &stack->operations[next];
            if (nop->group_id != gid) break;
            gotoline(TRUE, nop->dot_l);
            curwp->w_doto = nop->dot_o;
            if (nop->type == EDIT_INSERT) {
                if (!linsert_str(nop->text_data)) break;
            } else if (nop->type == EDIT_DELETE) {
                if (!ldelete(nop->text_length, FALSE)) break;
            } else {
                break;
            }
            cursor = next;
        }
        atomic_store(&stack->undo_ptr, cursor);

        // Clean/dirty decision vs saved baseline
        uint64_t cur_version = stack->operations[cursor].version_id;
        if (cur_version == atomic_load(&bp->b_saved_version_id)) {
            bp->b_flag &= ~BFCHG;
        } else {
            bp->b_flag |= BFCHG;
        }
        refresh_modelines_for_buffer(bp);
    }

    curwp->w_flag |= WFHARD;
    atomic_store(&stack->in_operation, false);
    return success ? TRUE : FALSE;
}

// Command entry points for key bindings
int undo_cmd(int f, int n) {
    if (!curbp) return FALSE;
    return undo_operation(curbp);
}

int redo_cmd(int f, int n) {
    if (!curbp) return FALSE;
    return redo_operation(curbp);
}

/* Mark current state as saved baseline for clean/dirty checks */
void undo_mark_saved(struct buffer *bp) {
    if (!bp || !bp->b_undo_stack) return;
    struct atomic_undo_stack *stack = bp->b_undo_stack;
    int up = atomic_load(&stack->undo_ptr);
    uint64_t version = (up == -1) ? 1 : stack->operations[up].version_id;
    atomic_store(&bp->b_saved_version_id, version);
    bp->b_flag &= ~BFCHG;
    refresh_modelines_for_buffer(bp);
}

void undo_group_begin(struct buffer *bp) {
    if (!bp || !bp->b_undo_stack) return;
    atomic_store(&bp->b_undo_stack->group_forced, true);
}

void undo_group_end(struct buffer *bp) {
    if (!bp || !bp->b_undo_stack) return;
    // Advance to a fresh group id for subsequent operations
    atomic_fetch_add(&bp->b_undo_stack->current_group_id, 1);
    atomic_store(&bp->b_undo_stack->group_forced, false);
}
