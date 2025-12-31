/*
 * test_runner.h - Production Test Runner with Fork-Based Isolation
 *
 * STANDARD for μEmacs test suite. Every test runs in a forked subprocess.
 *
 * Catches:
 * - CRASH  (SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL)
 * - TIMEOUT (configurable per-test, default 60s)
 * - ERROR  (non-zero exit, setup failures)
 * - FAIL   (test returned 0/false)
 * - PASS   (test returned 1/true)
 * - SKIP   (exit code 77)
 *
 * Usage:
 *   TEST_SUITE_BEGIN("Suite Name")
 *   TEST_RUN(test_function, "Description", timeout_seconds)
 *   TEST_SUITE_END()
 */

#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <fcntl.h>

/* ============================================================================
 * Result Types
 * ============================================================================ */

typedef enum {
    TEST_PASS    = 0,
    TEST_FAIL    = 1,
    TEST_ERROR   = 2,
    TEST_CRASH   = 3,
    TEST_TIMEOUT = 4,
    TEST_SKIP    = 5
} test_result_t;

/* ============================================================================
 * ANSI Colors
 * ============================================================================ */

#define C_RED     "\x1b[31m"
#define C_GREEN   "\x1b[32m"
#define C_YELLOW  "\x1b[33m"
#define C_BLUE    "\x1b[34m"
#define C_MAGENTA "\x1b[35m"
#define C_CYAN    "\x1b[36m"
#define C_RESET   "\x1b[0m"
#define C_BOLD    "\x1b[1m"

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define DEFAULT_TEST_TIMEOUT_SEC  60
#define MAX_STDERR_CAPTURE        8192
#define POLL_INTERVAL_USEC        10000  /* 10ms */

/* ============================================================================
 * Global Statistics (for suite tracking)
 * ============================================================================ */

typedef struct {
    int total;
    int passed;
    int failed;
    int errors;
    int crashes;
    int timeouts;
    int skipped;
    double total_time_sec;
    long peak_memory_kb;
} test_suite_stats_t;

static test_suite_stats_t g_suite_stats = {0};
static struct timeval g_suite_start_time;

/* ============================================================================
 * Signal Name Helper
 * ============================================================================ */

static inline const char *signal_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (Segmentation fault)";
        case SIGBUS:  return "SIGBUS (Bus error)";
        case SIGABRT: return "SIGABRT (Abort)";
        case SIGFPE:  return "SIGFPE (Floating point exception)";
        case SIGILL:  return "SIGILL (Illegal instruction)";
        case SIGKILL: return "SIGKILL (Killed)";
        case SIGTERM: return "SIGTERM (Terminated)";
        case SIGPIPE: return "SIGPIPE (Broken pipe)";
        case SIGSYS:  return "SIGSYS (Bad system call)";
        default:      return "Unknown signal";
    }
}

/* ============================================================================
 * Core Test Runner - Fork-Based Isolation
 * ============================================================================ */

typedef int (*test_fn_t)(void);

static inline test_result_t run_test_isolated(
    const char *name,
    const char *description,
    test_fn_t func,
    int timeout_sec
) {
    if (timeout_sec <= 0) timeout_sec = DEFAULT_TEST_TIMEOUT_SEC;

    /* Flush before fork */
    fflush(stdout);
    fflush(stderr);

    /* Create pipe for stderr capture */
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        printf("[%sERROR%s] %s - pipe() failed: %s\n",
               C_RED, C_RESET, name, strerror(errno));
        return TEST_ERROR;
    }

    /* Set pipe to non-blocking for parent reads */
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);

    struct timeval test_start;
    gettimeofday(&test_start, NULL);

    pid_t pid = fork();
    if (pid == -1) {
        printf("[%sERROR%s] %s - fork() failed: %s\n",
               C_RED, C_RESET, name, strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return TEST_ERROR;
    }

    if (pid == 0) {
        /* ============================================================
         * CHILD PROCESS - Run the test
         * ============================================================ */
        close(pipefd[0]);  /* Close read end */

        /* Redirect stderr to pipe */
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        /* Reset signal handlers to default */
        signal(SIGSEGV, SIG_DFL);
        signal(SIGBUS, SIG_DFL);
        signal(SIGABRT, SIG_DFL);
        signal(SIGFPE, SIG_DFL);
        signal(SIGILL, SIG_DFL);

        /* Run the test */
        int result = func();

        /* Flush and exit */
        fflush(stderr);
        _exit(result ? 0 : 1);
    }

    /* ================================================================
     * PARENT PROCESS - Monitor child with timeout
     * ================================================================ */
    close(pipefd[1]);  /* Close write end */

    char stderr_buf[MAX_STDERR_CAPTURE] = {0};
    size_t stderr_len = 0;
    int status;
    pid_t wpid;

    while (1) {
        /* Non-blocking check for child exit */
        wpid = waitpid(pid, &status, WNOHANG);

        if (wpid == pid) {
            /* Child exited */
            break;
        }

        if (wpid == -1 && errno != EINTR) {
            printf("[%sERROR%s] %s - waitpid() failed: %s\n",
                   C_RED, C_RESET, name, strerror(errno));
            close(pipefd[0]);
            return TEST_ERROR;
        }

        /* Read any available stderr (non-blocking) */
        if (stderr_len < MAX_STDERR_CAPTURE - 1) {
            ssize_t n = read(pipefd[0], stderr_buf + stderr_len,
                            MAX_STDERR_CAPTURE - 1 - stderr_len);
            if (n > 0) stderr_len += n;
        }

        /* Check timeout */
        struct timeval now;
        gettimeofday(&now, NULL);
        int elapsed = (int)(now.tv_sec - test_start.tv_sec);

        if (elapsed >= timeout_sec) {
            /* Timeout - kill the child */
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            close(pipefd[0]);

            printf("[%sTIMEOUT%s] %s - exceeded %d seconds\n",
                   C_YELLOW, C_RESET, name, timeout_sec);
            if (description && description[0]) {
                printf("         %s\n", description);
            }
            if (stderr_len > 0) {
                stderr_buf[stderr_len] = '\0';
                printf("         Last stderr: %.200s%s\n",
                       stderr_buf, stderr_len > 200 ? "..." : "");
            }
            return TEST_TIMEOUT;
        }

        usleep(POLL_INTERVAL_USEC);
    }

    /* Read remaining stderr */
    while (stderr_len < MAX_STDERR_CAPTURE - 1) {
        ssize_t n = read(pipefd[0], stderr_buf + stderr_len,
                        MAX_STDERR_CAPTURE - 1 - stderr_len);
        if (n <= 0) break;
        stderr_len += n;
    }
    close(pipefd[0]);
    stderr_buf[stderr_len] = '\0';

    /* Calculate elapsed time */
    struct timeval test_end;
    gettimeofday(&test_end, NULL);
    double elapsed_ms = (test_end.tv_sec - test_start.tv_sec) * 1000.0 +
                        (test_end.tv_usec - test_start.tv_usec) / 1000.0;

    /* Analyze exit status */
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("[%sCRASH%s]   %s - %s (signal %d) [%.0fms]\n",
               C_MAGENTA, C_RESET, name, signal_name(sig), sig, elapsed_ms);
        if (description && description[0]) {
            printf("          %s\n", description);
        }
        if (stderr_len > 0) {
            printf("          Captured stderr:\n%s\n", stderr_buf);
        }
        return TEST_CRASH;
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);

        if (code == 0) {
            printf("[%sPASS%s]    %s [%.0fms]\n",
                   C_GREEN, C_RESET, name, elapsed_ms);
            return TEST_PASS;
        }

        if (code == 77) {
            printf("[%sSKIP%s]    %s\n", C_CYAN, C_RESET, name);
            if (stderr_len > 0) {
                printf("          Reason: %s\n", stderr_buf);
            }
            return TEST_SKIP;
        }

        /* Test failed */
        printf("[%sFAIL%s]    %s (exit %d) [%.0fms]\n",
               C_RED, C_RESET, name, code, elapsed_ms);
        if (description && description[0]) {
            printf("          %s\n", description);
        }
        if (stderr_len > 0) {
            /* Print stderr, indented */
            printf("          Details:\n");
            char *line = stderr_buf;
            char *next;
            while ((next = strchr(line, '\n')) != NULL) {
                *next = '\0';
                printf("            %s\n", line);
                line = next + 1;
            }
            if (*line) {
                printf("            %s\n", line);
            }
        }
        return TEST_FAIL;
    }

    printf("[%sERROR%s]   %s - unknown exit status\n", C_RED, C_RESET, name);
    return TEST_ERROR;
}

/* ============================================================================
 * Suite Macros - Declarative Test Definition
 * ============================================================================ */

#define TEST_SUITE_BEGIN(suite_name) \
    do { \
        memset(&g_suite_stats, 0, sizeof(g_suite_stats)); \
        gettimeofday(&g_suite_start_time, NULL); \
        printf("\n%s%s========================================%s\n", C_BOLD, C_BLUE, C_RESET); \
        printf("%s%s  %s%s\n", C_BOLD, C_BLUE, suite_name, C_RESET); \
        printf("%s%s========================================%s\n\n", C_BOLD, C_BLUE, C_RESET); \
    } while (0)

#define TEST_RUN(func, desc, timeout) \
    do { \
        g_suite_stats.total++; \
        test_result_t _result = run_test_isolated(#func, desc, func, timeout); \
        switch (_result) { \
            case TEST_PASS:    g_suite_stats.passed++; break; \
            case TEST_FAIL:    g_suite_stats.failed++; break; \
            case TEST_ERROR:   g_suite_stats.errors++; break; \
            case TEST_CRASH:   g_suite_stats.crashes++; break; \
            case TEST_TIMEOUT: g_suite_stats.timeouts++; break; \
            case TEST_SKIP:    g_suite_stats.skipped++; break; \
        } \
    } while (0)

#define TEST_RUN_DEFAULT(func, desc) TEST_RUN(func, desc, 0)

#define TEST_SECTION(section_name) \
    printf("\n%s--- %s ---%s\n", C_BLUE, section_name, C_RESET)

#define TEST_SUITE_END() \
    do { \
        struct timeval _end; \
        gettimeofday(&_end, NULL); \
        g_suite_stats.total_time_sec = (_end.tv_sec - g_suite_start_time.tv_sec) + \
                                       (_end.tv_usec - g_suite_start_time.tv_usec) / 1000000.0; \
        struct rusage _usage; \
        if (getrusage(RUSAGE_CHILDREN, &_usage) == 0) { \
            g_suite_stats.peak_memory_kb = _usage.ru_maxrss; \
        } \
        printf("\n%s========================================%s\n", C_BLUE, C_RESET); \
        printf("%s        TEST SUITE RESULTS%s\n", C_BLUE, C_RESET); \
        printf("%s========================================%s\n", C_BLUE, C_RESET); \
        printf("Total Time: %.2f seconds\n", g_suite_stats.total_time_sec); \
        printf("Peak Memory: %ld KB\n", g_suite_stats.peak_memory_kb); \
        printf("\n"); \
        printf("Total:    %d\n", g_suite_stats.total); \
        printf("%sPassed:   %d%s\n", C_GREEN, g_suite_stats.passed, C_RESET); \
        if (g_suite_stats.failed > 0) \
            printf("%sFailed:   %d%s\n", C_RED, g_suite_stats.failed, C_RESET); \
        if (g_suite_stats.crashes > 0) \
            printf("%sCrashes:  %d%s\n", C_MAGENTA, g_suite_stats.crashes, C_RESET); \
        if (g_suite_stats.timeouts > 0) \
            printf("%sTimeouts: %d%s\n", C_YELLOW, g_suite_stats.timeouts, C_RESET); \
        if (g_suite_stats.errors > 0) \
            printf("%sErrors:   %d%s\n", C_RED, g_suite_stats.errors, C_RESET); \
        if (g_suite_stats.skipped > 0) \
            printf("%sSkipped:  %d%s\n", C_CYAN, g_suite_stats.skipped, C_RESET); \
        printf("\n"); \
        int _failures = g_suite_stats.failed + g_suite_stats.crashes + \
                        g_suite_stats.timeouts + g_suite_stats.errors; \
        if (_failures == 0) { \
            printf("%s%s[ALL TESTS PASSED]%s\n", C_BOLD, C_GREEN, C_RESET); \
        } else { \
            printf("%s%s[%d TEST(S) FAILED]%s\n", C_BOLD, C_RED, _failures, C_RESET); \
        } \
        printf("%s========================================%s\n", C_BLUE, C_RESET); \
    } while (0)

#define TEST_SUITE_RETURN() \
    (g_suite_stats.failed + g_suite_stats.crashes + \
     g_suite_stats.timeouts + g_suite_stats.errors)

/* ============================================================================
 * Utility: Get stats for external use
 * ============================================================================ */

static inline test_suite_stats_t *test_get_stats(void) {
    return &g_suite_stats;
}

#endif /* TEST_RUNNER_H */
