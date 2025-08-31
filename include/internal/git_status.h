// git_status.h - Asynchronous, throttled Git status for status line

#ifndef GIT_STATUS_H
#define GIT_STATUS_H

#include <stddef.h>

// Initialize Git status helper. Enabled only if UEMACS_GIT_STATUS=1.
void git_status_init(void);

// Request asynchronous update if stale; returns immediately.
// If cwd is NULL, uses current working directory.
void git_status_request_async(const char* cwd);

// Copy cached status into out buffer.
// Returns length copied; 0 if disabled or not in a git repo.
int git_status_get_cached(char* out, size_t out_sz);

#endif // GIT_STATUS_H

