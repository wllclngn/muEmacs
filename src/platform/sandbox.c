/*
 * sandbox.c - seccomp-bpf + landlock extension sandboxing for μEmacs
 *
 * Restricts extension runner processes to a whitelist of syscalls
 * and a whitelist of filesystem paths. No libseccomp dependency;
 * uses raw BPF filter programs and direct syscall numbers.
 *
 * C23 compliant
 */

#include "platform/sandbox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <stddef.h>

#ifdef __has_include
#if __has_include(<linux/seccomp.h>)
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#define HAVE_SECCOMP 1
#endif
#if __has_include(<linux/landlock.h>)
#include <linux/landlock.h>
#define HAVE_LANDLOCK 1
#endif
#endif

// BPF macros for seccomp filter construction
#ifdef HAVE_SECCOMP

#define ALLOW_SYSCALL(nr) \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_##nr, 0, 1), \
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)

// Common syscalls needed by all extensions
static struct sock_filter base_filter[] = {
    // Load syscall number
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),

    // Memory management
    ALLOW_SYSCALL(read),
    ALLOW_SYSCALL(write),
    ALLOW_SYSCALL(close),
    ALLOW_SYSCALL(fstat),
    ALLOW_SYSCALL(lseek),
    ALLOW_SYSCALL(mmap),
    ALLOW_SYSCALL(mprotect),
    ALLOW_SYSCALL(munmap),
    ALLOW_SYSCALL(brk),
    ALLOW_SYSCALL(mremap),

    // Process basics
    ALLOW_SYSCALL(exit_group),
    ALLOW_SYSCALL(exit),
    ALLOW_SYSCALL(rt_sigreturn),
    ALLOW_SYSCALL(rt_sigaction),
    ALLOW_SYSCALL(rt_sigprocmask),
    ALLOW_SYSCALL(getpid),
    ALLOW_SYSCALL(gettid),

    // Time
    ALLOW_SYSCALL(clock_gettime),
    ALLOW_SYSCALL(clock_nanosleep),
    ALLOW_SYSCALL(nanosleep),

    // IPC (for memfd shared memory with editor)
    ALLOW_SYSCALL(memfd_create),
    ALLOW_SYSCALL(ftruncate),
    ALLOW_SYSCALL(poll),

    // Sync
    ALLOW_SYSCALL(futex),
    ALLOW_SYSCALL(sched_yield),

    // Misc
    ALLOW_SYSCALL(getrandom),
    ALLOW_SYSCALL(newfstatat),
    ALLOW_SYSCALL(openat),
    ALLOW_SYSCALL(ioctl),
    ALLOW_SYSCALL(fcntl),

    // Default: kill
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
};

// Additional syscalls for Go runtime (goroutines, GC, netpoller)
static struct sock_filter go_extra_filter[] = {
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),

    ALLOW_SYSCALL(clone),
    ALLOW_SYSCALL(clone3),
    ALLOW_SYSCALL(sigaltstack),
    ALLOW_SYSCALL(tgkill),
    ALLOW_SYSCALL(madvise),
    ALLOW_SYSCALL(epoll_create1),
    ALLOW_SYSCALL(epoll_ctl),
    ALLOW_SYSCALL(epoll_wait),
    ALLOW_SYSCALL(pipe2),
    ALLOW_SYSCALL(eventfd2),
    ALLOW_SYSCALL(sched_getaffinity),
    ALLOW_SYSCALL(set_robust_list),
    ALLOW_SYSCALL(rseq),
    ALLOW_SYSCALL(prlimit64),
    ALLOW_SYSCALL(getrlimit),
    ALLOW_SYSCALL(mincore),

    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
};

static int install_seccomp_filter(struct sock_filter *filter, size_t count) {
    struct sock_fprog prog = {
        .len = (unsigned short)count,
        .filter = filter,
    };

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0)
        return -1;

    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0)
        return -1;

    return 0;
}

#endif /* HAVE_SECCOMP */

int sandbox_apply_seccomp(sandbox_profile_t profile) {
    if (profile == SANDBOX_PROFILE_NONE)
        return 0;

#ifdef HAVE_SECCOMP
    struct sock_filter *filter;
    size_t count;

    switch (profile) {
    case SANDBOX_PROFILE_EXTENSION_GO:
    case SANDBOX_PROFILE_EXTENSION_HASKELL:
        filter = go_extra_filter;
        count = sizeof(go_extra_filter) / sizeof(go_extra_filter[0]);
        break;
    case SANDBOX_PROFILE_EXTENSION_C:
    case SANDBOX_PROFILE_EXTENSION_RUST:
    case SANDBOX_PROFILE_BUILD:
    default:
        filter = base_filter;
        count = sizeof(base_filter) / sizeof(base_filter[0]);
        break;
    }

    return install_seccomp_filter(filter, count);
#else
    (void)profile;
    return -1;
#endif
}

int sandbox_apply_landlock(const sandbox_path_t *paths, int count) {
    if (!paths || count <= 0)
        return -1;

#ifdef HAVE_LANDLOCK
    struct landlock_ruleset_attr attr = {
        .handled_access_fs =
            LANDLOCK_ACCESS_FS_READ_FILE |
            LANDLOCK_ACCESS_FS_READ_DIR |
            LANDLOCK_ACCESS_FS_WRITE_FILE |
            LANDLOCK_ACCESS_FS_MAKE_REG |
            LANDLOCK_ACCESS_FS_EXECUTE,
    };

    int ruleset_fd = (int)syscall(__NR_landlock_create_ruleset, &attr, sizeof(attr), 0);
    if (ruleset_fd < 0)
        return -1;

    for (int i = 0; i < count; i++) {
        int path_fd = open(paths[i].path, O_PATH | O_CLOEXEC);
        if (path_fd < 0) continue;

        struct landlock_path_beneath_attr rule = {
            .allowed_access = paths[i].access,
            .parent_fd = path_fd,
        };
        syscall(__NR_landlock_add_rule, ruleset_fd,
                LANDLOCK_RULE_PATH_BENEATH, &rule, 0);
        close(path_fd);
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        close(ruleset_fd);
        return -1;
    }

    int ret = (int)syscall(__NR_landlock_restrict_self, ruleset_fd, 0);
    close(ruleset_fd);
    return ret;
#else
    (void)paths; (void)count;
    return -1;
#endif
}

int sandbox_apply_full(sandbox_profile_t profile,
                       const char *ext_dir, const char *config_dir) {
    int rc = 0;

    // Apply landlock first (before seccomp restricts syscalls)
#ifdef HAVE_LANDLOCK
    sandbox_path_t allowed[] = {
        { ext_dir,    LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR },
        { config_dir, LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR },
        { "/tmp",     LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_WRITE_FILE |
                      LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_READ_DIR },
        { "/usr/lib", LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR },
        { "/lib",     LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR },
    };
    int npath = sizeof(allowed) / sizeof(allowed[0]);

    // Skip nullptr paths
    int valid = 0;
    for (int i = 0; i < npath; i++) {
        if (allowed[i].path) valid++;
    }
    if (valid > 0) {
        if (sandbox_apply_landlock(allowed, npath) < 0)
            rc = -1;  // Non-fatal, continue with seccomp
    }
#else
    (void)ext_dir; (void)config_dir;
#endif

    // Apply seccomp
    if (sandbox_apply_seccomp(profile) < 0)
        rc = -1;

    return rc;
}

bool sandbox_seccomp_available(void) {
#ifdef HAVE_SECCOMP
    return prctl(PR_GET_SECCOMP) >= 0;
#else
    return false;
#endif
}

bool sandbox_landlock_available(void) {
#ifdef HAVE_LANDLOCK
    struct landlock_ruleset_attr attr = { .handled_access_fs = 0 };
    int fd = (int)syscall(__NR_landlock_create_ruleset, &attr, sizeof(attr),
                          LANDLOCK_CREATE_RULESET_VERSION);
    if (fd >= 0) {
        close(fd);
        return true;
    }
    return false;
#else
    return false;
#endif
}
