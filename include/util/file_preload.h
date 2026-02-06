/*
 * file_preload.h - Async file preloading for μEmacs
 *
 * Spawns a background thread to read file content while editor initializes.
 * Pattern matches git_status.c - fire-and-forget with cached result.
 */

#ifndef FILE_PRELOAD_H
#define FILE_PRELOAD_H

#include <stddef.h>

/*
 * Start preloading a file in background thread.
 * Non-blocking - returns immediately after spawning thread.
 * Only one file can be preloaded at a time.
 */
void file_preload_start(const char *fname);

/*
 * Check if preload is complete (non-blocking).
 * Returns: 1 if ready, 0 if still loading, -1 if error/not started
 */
int file_preload_ready(const char *fname);

/*
 * Get preloaded file content.
 * Blocks until preload completes if still in progress.
 *
 * On success: returns 0, sets *data and *size
 *   Caller takes ownership of *data and must free() it
 * On error: returns -1, *data is nullptr
 */
int file_preload_get(const char *fname, char **data, size_t *size);

/*
 * Cancel any in-progress preload and free resources.
 * Safe to call even if no preload active.
 */
void file_preload_cleanup(void);

#endif /* FILE_PRELOAD_H */
