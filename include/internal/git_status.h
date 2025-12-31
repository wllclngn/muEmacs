// git_status.h - Asynchronous, throttled Git status for status line

#ifndef GIT_STATUS_H
#define GIT_STATUS_H

#include <stddef.h>

// Initialize Git status helper (legacy - now uses TOML config).
void git_status_init(void);

// Enable/disable git status (called after TOML settings load).
void git_status_set_enabled(int enabled);

// Request asynchronous update if stale; returns immediately.
// If cwd is nullptr, uses current working directory.
void git_status_request_async(const char* cwd);

// Copy cached status into out buffer.
// Returns length copied; 0 if disabled or not in a git repo.
int git_status_get_cached(char* out, size_t out_sz);

#endif // GIT_STATUS_H

