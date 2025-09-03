#include <stdio.h>

/* Undo history persistence API (stub) */
bool undo_stack_save_to_file(struct buffer *bp, const char *filename);
bool undo_stack_load_from_file(struct buffer *bp, const char *filename);
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

/**
 * @brief Creates and initializes a new undo stack.
 * 
 * @return A pointer to the newly created atomic_undo_stack, or NULL on failure.
 */
struct atomic_undo_stack *undo_stack_create(void);

/**
 * @brief Destroys an undo stack and frees all associated memory.
 * 
 * @param stack Pointer to the undo stack to be destroyed.
 */
void undo_stack_destroy(struct atomic_undo_stack *stack);

/**
 * @brief Records a text insertion operation for undo.
 * 
 * @param bp Pointer to the buffer where the insertion occurred.
 * @param l The line number of the insertion.
 * @param o The offset on the line where the insertion started.
 * @param text The text that was inserted.
 * @param len The length of the inserted text.
 */
void undo_record_insert(struct buffer *bp, long l, int o, const char *text, int len);

/**
 * @brief Records a text deletion operation for undo.
 * 
 * @param bp Pointer to the buffer where the deletion occurred.
 * @param l The line number of the deletion.
 * @param o The offset on the line where the deletion started.
 * @param text The text that was deleted.
 * @param len The length of the deleted text.
 */
void undo_record_delete(struct buffer *bp, long l, int o, const char *text, int len);

/**
 * @brief The user-facing command to perform an undo operation.
 * 
 * @param f Flag, typically unused.
 * @param n Numeric argument, for repeating the command.
 * @return int Status of the operation.
 */
int undo_cmd(int f, int n);

/**
 * @brief The user-facing command to perform a redo operation.
 * 
 * @param f Flag, typically unused.
 * @param n Numeric argument, for repeating the command.
 * @return int Status of the operation.
 */
int redo_cmd(int f, int n);

/* Optional grouping API to coalesce multiple edits into a single undo step */
void undo_group_begin(struct buffer *bp);
void undo_group_end(struct buffer *bp);

/**
 * @brief Mark the current buffer state as saved/clean.
 * Call this after a successful file save or initial read.
 */
void undo_mark_saved(struct buffer *bp);

#endif // UNDO_H_
