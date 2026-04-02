/*
 * sandbox.h - Extension process sandboxing for μEmacs
 *
 * Provides seccomp-bpf syscall filtering and landlock filesystem
 * restrictions for out-of-process extensions.
 *
 * C23 compliant
 */

#ifndef UEMACS_SANDBOX_H
#define UEMACS_SANDBOX_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SANDBOX_PROFILE_EXTENSION_C,      /* ~30 syscalls: minimal C extension */
    SANDBOX_PROFILE_EXTENSION_GO,     /* ~50 syscalls: goroutines need clone, epoll */
    SANDBOX_PROFILE_EXTENSION_RUST,   /* ~35 syscalls */
    SANDBOX_PROFILE_EXTENSION_HASKELL,/* ~55 syscalls: GHC RTS */
    SANDBOX_PROFILE_BUILD,            /* ~40 syscalls: needs exec, fork */
    SANDBOX_PROFILE_NONE,             /* No sandboxing */
} sandbox_profile_t;

/* Landlock path permission entry */
typedef struct {
    const char *path;
    uint64_t access;  /* LANDLOCK_ACCESS_FS_* flags */
} sandbox_path_t;

/*
 * Apply seccomp-bpf syscall filter to current process.
 * Must be called after fork/exec in the extension runner.
 * Returns 0 on success, -1 on error (caller continues unsandboxed).
 */
int sandbox_apply_seccomp(sandbox_profile_t profile);

/*
 * Apply landlock filesystem restrictions.
 * Returns 0 on success, -1 on error (e.g., kernel too old).
 */
int sandbox_apply_landlock(const sandbox_path_t *paths, int count);

/*
 * Convenience: apply full sandbox (seccomp + landlock) for extension.
 * ext_dir: extension's directory (read access)
 * config_dir: user config dir (read access)
 */
int sandbox_apply_full(sandbox_profile_t profile,
                       const char *ext_dir, const char *config_dir);

/*
 * Check if sandboxing is available on this kernel.
 */
bool sandbox_seccomp_available(void);
bool sandbox_landlock_available(void);

#endif /* UEMACS_SANDBOX_H */
