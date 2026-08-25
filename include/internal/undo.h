/*
 * undo.h - Public API for the per-buffer undo system.
 */
#ifndef UNDO_H_
#define UNDO_H_

#include <stdbool.h>

// Forward declarations
struct buffer;
struct atomic_undo_stack;

/* Edit operation types */
enum edit_type {
    EDIT_INSERT,    // Text insertion
    EDIT_DELETE,    // Text deletion
    EDIT_REPLACE    // Text replacement (not yet implemented)
};

/* Returns nullptr on allocation failure */
struct atomic_undo_stack *undo_stack_create(void);

void undo_stack_destroy(struct atomic_undo_stack *stack);

/* Called when a buffer is cleared to prevent stale undo operations */
void undo_stack_clear(struct atomic_undo_stack *stack);

/* Record an insertion of text at line l, offset o */
void undo_record_insert(struct buffer *bp, long l, int o, const char *text, int len);

/* Record a deletion of text at line l, offset o */
void undo_record_delete(struct buffer *bp, long l, int o, const char *text, int len);

int undo_cmd(int f, int n);

int redo_cmd(int f, int n);

/* Optional grouping API to coalesce multiple edits into a single undo step */
void undo_group_begin(struct buffer *bp);
void undo_group_end(struct buffer *bp);

/* Mark the buffer state clean; call after a successful save or initial read */
void undo_mark_saved(struct buffer *bp);

#endif // UNDO_H_
