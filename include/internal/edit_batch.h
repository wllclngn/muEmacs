/* edit_batch.h - Display update batching for grouped edits
 *
 * Batches display updates when multiple edits should appear as one visual
 * change. NOT a transaction system -- no content rollback. Undo is in undo.c.
 */
#ifndef EDIT_BATCH_H_
#define EDIT_BATCH_H_

void edit_begin(void);
void edit_commit(void);
void edit_abort(void);

#endif /* EDIT_BATCH_H_ */
