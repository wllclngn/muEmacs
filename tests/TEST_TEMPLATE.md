# μEmacs Test Template Guide

## Directory Structure

```
tests/
├── test_registry.h              # Add your entry point here
├── test_runner.h                # Fork-isolated runner (TEST_RUN macros)
├── test_utils.h/c               # Shared harness (timeouts, stats, colors)
├── integration/
│   └── main_test_suite.c        # Add a TEST_RUN line here
├── unit/
│   └── test_<module>_<feature>.c
├── data/                        # Test fixtures (poe, utf8-demo, etc.)
└── *.exp                        # Expect scripts (interactive tests)
```

## Template 1: Basic Unit Test

```c
// tests/unit/test_mymodule_feature.c
#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

// Individual test (static)
static int test_myfeature_basic(void) {
    int ok = 1;
    PHASE_START("MYMODULE: BASIC", "Basic feature functionality");

    // Your test logic here
    if (condition_fails) {
        printf("[%sFAIL%s] Expected X but got Y\n", RED, RESET);
        ok = 0;
    }

    PHASE_END("MYMODULE: BASIC", ok);
    return ok;
}

// Entry point (public)
int test_mymodule_feature(void) {
    int result = 1;
    result &= test_myfeature_basic();
    result &= test_myfeature_advanced();
    return result;
}
```

## Template 2: Buffer-Based Test

```c
#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"

static struct buffer *setup_test_buffer(const char *name) {
    struct buffer *bp = bfind((char *)name, true, 0);
    if (!bp) return nullptr;

    bclear(bp);                     // Clear existing content
    curbp = bp;                     // Set as current buffer
    curwp->w_bufp = bp;             // Set window to point to buffer
    curwp->w_dotp = bp->b_linep;    // Position at header line
    curwp->w_doto = 0;

    lnewline();                     // Create first line
    curwp->w_dotp = lforw(bp->b_linep);  // Move to first real line

    return bp;
}

static void cleanup_test_buffer(struct buffer *bp, struct buffer *oldbp) {
    if (bp) {
        bp->b_flag &= ~BFCHG;       // Clear "modified" flag
        zotbuf(bp);                 // Free buffer
    }
    if (oldbp) {
        curbp = oldbp;              // Restore previous buffer
        curwp->w_bufp = oldbp;
    }
}

static int test_buffer_operations(void) {
    int ok = 1;
    PHASE_START("BUFFER: OPS", "Buffer operation test");

    struct buffer *oldbp = curbp;
    struct buffer *bp = setup_test_buffer("test-buffer");

    if (!bp) {
        printf("[%sFAIL%s] Failed to create test buffer\n", RED, RESET);
        PHASE_END("BUFFER: OPS", 0);
        return 0;
    }

    // Insert text line by line
    const char *text = "Hello\nWorld\n";
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            lnewline();
        } else {
            linsert(1, *p);
        }
    }

    // Verify buffer state
    int line_count = 0;
    struct line *lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        line_count++;
        lp = lforw(lp);
    }

    if (line_count != 2) {
        printf("[%sFAIL%s] Expected 2 lines, got %d\n", RED, RESET, line_count);
        ok = 0;
    }

    cleanup_test_buffer(bp, oldbp);
    PHASE_END("BUFFER: OPS", ok);
    return ok;
}

int test_mymodule_buffer(void) {
    int result = 1;
    result &= test_buffer_operations();
    return result;
}
```

## Template 3: Assertion Helpers

```c
// Common assertion macros
#define ASSERT_EQ(actual, expected, msg) \
    do { \
        if ((actual) != (expected)) { \
            printf("[%sFAIL%s] %s: got %d expected %d\n", \
                   RED, RESET, msg, (int)(actual), (int)(expected)); \
            ok = 0; \
        } \
    } while (0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("[%sFAIL%s] %s\n", RED, RESET, msg); \
            ok = 0; \
        } \
    } while (0)

#define ASSERT_NULL(ptr, msg) \
    do { \
        if ((ptr) != nullptr) { \
            printf("[%sFAIL%s] %s: expected NULL, got %p\n", \
                   RED, RESET, msg, (void*)(ptr)); \
            ok = 0; \
        } \
    } while (0)

#define ASSERT_NOTNULL(ptr, msg) \
    do { \
        if ((ptr) == nullptr) { \
            printf("[%sFAIL%s] %s: unexpected NULL\n", RED, RESET, msg); \
            ok = 0; \
        } \
    } while (0)

#define ASSERT_STREQ(actual, expected, msg) \
    do { \
        if (strcmp((actual), (expected)) != 0) { \
            printf("[%sFAIL%s] %s: got '%s' expected '%s'\n", \
                   RED, RESET, msg, (actual), (expected)); \
            ok = 0; \
        } \
    } while (0)

// Usage example
static int test_with_assertions(void) {
    int ok = 1;
    PHASE_START("EXAMPLE: ASSERT", "Using assertion macros");

    struct buffer *bp = bfind("test", true, 0);
    ASSERT_NOTNULL(bp, "Buffer creation failed");

    int count = count_windows();
    ASSERT_EQ(count, 1, "Window count mismatch");

    const char *name = bp->b_bname;
    ASSERT_STREQ(name, "test", "Buffer name mismatch");

    cleanup_test_buffer(bp, nullptr);
    PHASE_END("EXAMPLE: ASSERT", ok);
    return ok;
}
```

## Template 4: Window Management Test

```c
static int count_windows(void) {
    int count = 0;
    struct window *wp = wheadp;
    while (wp) {
        count++;
        wp = wp->w_wndp;
    }
    return count;
}

static int test_window_split(void) {
    int ok = 1;
    PHASE_START("WINDOW: SPLIT", "Window splitting");

    int initial_count = count_windows();

    if (splitwind(false, 1) != true) {
        printf("[%sFAIL%s] splitwind failed\n", RED, RESET);
        ok = 0;
        PHASE_END("WINDOW: SPLIT", ok);
        return ok;
    }

    int new_count = count_windows();
    if (new_count != initial_count + 1) {
        printf("[%sFAIL%s] Expected %d windows, got %d\n",
               RED, RESET, initial_count + 1, new_count);
        ok = 0;
    }

    // Cleanup
    delwind(false, 1);

    PHASE_END("WINDOW: SPLIT", ok);
    return ok;
}
```

## Template 5: Performance/Benchmark Test

```c
#include <time.h>

static int test_performance_benchmark(void) {
    int ok = 1;
    PHASE_START("PERF: BENCH", "Performance benchmark");

    const int operations = 100000;

    clock_t start = clock();
    for (int i = 0; i < operations; i++) {
        // Operation to benchmark
        linsert(1, 'x');
    }
    clock_t end = clock();

    double time_per_op = ((double)(end - start)) / CLOCKS_PER_SEC / operations * 1000000;

    printf("[INFO] %.2f μs per operation\n", time_per_op);

    if (time_per_op > 10.0) {  // 10μs threshold
        printf("[%sFAIL%s] Too slow: %.2f μs (threshold: 10.0 μs)\n",
               RED, RESET, time_per_op);
        ok = 0;
    }

    stats.operations_completed += operations;

    PHASE_END("PERF: BENCH", ok);
    return ok;
}
```

## Registration Checklist

### 1. Add declaration to `tests/test_registry.h`

```c
// In test_registry.h
int test_mymodule_feature(void);
```

### 2. Add a TEST_RUN to `tests/integration/main_test_suite.c`

Every test runs through the fork-isolated runner (`test_runner.h`).
Inside the appropriate `TEST_SUITE_BEGIN`/`TEST_SUITE_END` block in
`main()`:

```c
TEST_RUN(test_mymodule_feature, "One-line description", 30);
```

The third argument is the per-test timeout in seconds (0 uses the 60s
default). Exit code 77 from a test marks it SKIP.

### 3. Build and run

```bash
cmake --build build
./build/bin/full_integration_test

# Any command-line args act as substring filters on test names:
./build/bin/full_integration_test mymodule
```

## Common Patterns

### Initialize Editor

```c
static void init_editor_minimal(const char* name) {
    term.t_nrow = 24 - 1;  // rows minus modeline
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;

    edinit((char*)(name ? name : "test"));
    varinit();
}
```

### Get Buffer Contents

```c
static char *get_buffer_content(void) {
    static char buf[4096];
    int pos = 0;

    struct line *lp = lforw(curbp->b_linep);
    while (lp != curbp->b_linep && pos < 4095) {
        int len = llength(lp);
        for (int i = 0; i < len && pos < 4095; i++) {
            buf[pos++] = lgetc(lp, i);
        }
        lp = lforw(lp);
        if (lp != curbp->b_linep && pos < 4095) {
            buf[pos++] = '\n';
        }
    }
    buf[pos] = '\0';
    return buf;
}
```

### Line Traversal

```c
// Forward iteration
struct line *lp = lforw(curbp->b_linep);
while (lp != curbp->b_linep) {
    int len = llength(lp);
    for (int i = 0; i < len; i++) {
        char c = lgetc(lp, i);
        // Process character
    }
    lp = lforw(lp);
}

// Backward iteration
struct line *lp = lback(curbp->b_linep);
while (lp != curbp->b_linep) {
    // Process line
    lp = lback(lp);
}
```

## Environment Variables

- `UEMACS_INTERACTIVE=1` - Enable interactive/expect tests
- `UEMACS_PHASE_TIMEOUT=<seconds>` - Override 180s timeout
- `ASAN_OPTIONS=detect_leaks=0` - Disable ASAN leak detection
- `LSAN_OPTIONS=detect_leaks=0` - Disable LSAN

## Available Test Data

Located in `tests/data/`:
- `poe-collected-works.txt` (308KB) - Text wrapping edge cases
- `poe-collected-fictions.txt` (2.9MB) - Larger wrapping/load cases
- `utf8-demo.txt` (14KB) - UTF-8 encoding validation

Large-file tests generate their fixtures into /tmp at run time (see
`tests/tui/test_piece_table.py`); no multi-megabyte fixture is checked
in.

## Color Output

Use consistent ANSI codes from `test_utils.h`:

```c
printf("[%sFAIL%s] Error message\n", RED, RESET);
printf("[%sSUCCESS%s] Test passed\n", GREEN, RESET);
printf("[%sWARNING%s] Skipped test\n", YELLOW, RESET);
printf("[%sINFO%s] Informational message\n", BLUE, RESET);
```

## Return Values

- Return `1` for **pass**
- Return `0` for **fail**
- Use `result &= test_function()` to chain tests

## Best Practices

1. **State isolation**: Always save/restore `curbp` and `curwp`
2. **Buffer cleanup**: Clear `BFCHG` flag before `zotbuf()`
3. **Unique names**: Use prefixed buffer names ("test-mymodule-X")
4. **Early exit**: Return immediately on critical failures
5. **Descriptive phases**: Use clear `PHASE_START` descriptions
6. **Memory tracking**: Stats are automatically aggregated
7. **Timeout safety**: Tests auto-timeout after 180s (configurable)

## Quick Start Example

```c
// tests/unit/test_example.c
#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"

static int test_simple_example(void) {
    int ok = 1;
    PHASE_START("EXAMPLE: SIMPLE", "Simple example test");

    // Test something
    if (1 + 1 != 2) {
        printf("[%sFAIL%s] Math is broken\n", RED, RESET);
        ok = 0;
    }

    PHASE_END("EXAMPLE: SIMPLE", ok);
    return ok;
}

int test_example(void) {
    return test_simple_example();
}
```

Then add to `test_registry.h`:
```c
int test_example(void);
```

And to `main_test_suite.c`:
```c
TEST_RUN(test_example, "Simple example test", 30);
```
