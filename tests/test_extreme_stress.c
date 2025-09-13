#include "test_utils.h"
#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/efunc.h"
#include "internal/line.h"
#include "μemacs/buffer_utils.h"

#define CYAN    "\x1b[36m"
#define MAGENTA "\x1b[35m"

static void init_editor_minimal(const char* name) {
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    edinit((char*)(name ? name : "extreme"));
    varinit();
}

static int stress_factor(void) {
    const char* s = getenv("UEMACS_STRESS");
    if (!s) return 1; // default intensity
    int f = atoi(s);
    if (f < 1) f = 1;
    if (f > 100) f = 100; // cap to avoid pathological CI
    return f;
}

// EXTREME stress test - 10x beyond current levels (scalable)
int test_extreme_text_operations(void) {
    printf("\n%s=== EXTREME TEXT OPERATIONS STRESS TEST ===%s\n", CYAN, RESET);
    int ok = 1;
    
    init_editor_minimal("extreme-stress");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;
    
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    int F = stress_factor();
    long insert_target = 1000000L * F;
    // PHASE 1: EXTREME TEXT INSERTION
    printf("Testing EXTREME text insertion (%ld characters; factor=%d)...\n", insert_target, F);
    curwp->w_dotp = curbp->b_linep;
    curwp->w_doto = 0;
    lnewline();
    curwp->w_dotp = lforw(curbp->b_linep);
    
    for (long i = 0; i < insert_target; i++) {
        char c = 'A' + (i % 26);
        if (!linsert(1, c)) {
            printf("[%sFAIL%s] Text insertion failed at %d\n", RED, RESET, i);
            ok = 0;
            break;
        }
        if (i % (100000 * F) == 0) {
            printf("Progress: %ld/%ld characters inserted\n", i, insert_target);
        }
    }
    
    // PHASE 2: EXTREME LINE OPERATIONS
    long lines_target = 100000L * F;
    printf("Testing EXTREME line operations (%ld new lines)...\n", lines_target);
    for (long i = 0; i < lines_target; i++) {
        if (!lnewline()) {
            printf("[%sFAIL%s] Line creation failed at %d\n", RED, RESET, i);
            ok = 0;
            break;
        }
        if (i % (10000 * F) == 0) {
            printf("Progress: %ld/%ld lines created\n", i, lines_target);
        }
    }
    
    // PHASE 3: EXTREME DELETION STRESS
    long del_target = 500000L * F;
    printf("Testing EXTREME deletion stress (%ld deletions)...\n", del_target);
    for (long i = 0; i < del_target; i++) {
        if (!ldelete(1, FALSE)) {
            break; // Hit end of buffer
        }
        if (i % (50000 * F) == 0) {
            printf("Progress: %ld/%ld deletions completed\n", i, del_target);
        }
    }
    
    gettimeofday(&end, NULL);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    
    printf("[%s%s%s] EXTREME text operations completed in %.2f seconds\n", 
           ok ? GREEN : RED, ok ? "SUCCESS" : "FAIL", RESET, elapsed);
    
    return ok;
}

// Memory pressure stress test
int test_extreme_memory_stress(void) {
    printf("\n%s=== EXTREME MEMORY STRESS TEST ===%s\n", CYAN, RESET);
    int ok = 1;
    
    struct rusage usage_start, usage_end;
    getrusage(RUSAGE_SELF, &usage_start);
    
    // Create multiple large buffers simultaneously
    struct buffer *stress_buffers[50];
    for (int i = 0; i < 50; i++) {
        char bufname[32];
        snprintf(bufname, sizeof(bufname), "stress-buffer-%d", i);
        stress_buffers[i] = bfind(bufname, TRUE, 0);
        
        if (!stress_buffers[i]) {
            printf("[%sFAIL%s] Failed to create buffer %d\n", RED, RESET, i);
            ok = 0;
            break;
        }
        
        // Switch to buffer and fill with data
        curbp = stress_buffers[i];
        bclear(curbp);
        curbp->b_mode &= ~MDVIEW;
        
        curwp->w_dotp = curbp->b_linep;
        curwp->w_doto = 0;
        lnewline();
        curwp->w_dotp = lforw(curbp->b_linep);
        
        // Fill with 10,000 characters per buffer
        for (int j = 0; j < 10000; j++) {
            char c = 'a' + (j % 26);
            if (!linsert(1, c)) {
                printf("[%sFAIL%s] Buffer %d fill failed at char %d\n", RED, RESET, i, j);
                ok = 0;
                break;
            }
        }
        
        if (i % 10 == 0) {
            printf("Created and filled %d/50 stress buffers\n", i + 1);
        }
    }
    
    getrusage(RUSAGE_SELF, &usage_end);
    long memory_used = usage_end.ru_maxrss - usage_start.ru_maxrss;
    
    printf("Memory usage increase: %ld KB\n", memory_used);
    printf("[%s%s%s] EXTREME memory stress test completed\n", 
           ok ? GREEN : RED, ok ? "SUCCESS" : "FAIL", RESET);
    
    return ok;
}

// Concurrent operations stress test
int test_extreme_concurrent_stress(void) {
    printf("\n%s=== EXTREME CONCURRENT OPERATIONS STRESS TEST ===%s\n", CYAN, RESET);
    int ok = 1;
    
    init_editor_minimal("concurrent-stress");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;
    
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    // Simulate rapid concurrent-like operations
    for (int cycle = 0; cycle < 1000; cycle++) {
        // Rapid insert/delete/move cycle
        for (int op = 0; op < 100; op++) {
            // Insert text
            linsert(1, 'X');
            linsert(1, 'Y');
            linsert(1, 'Z');
            
            // Move cursor
            if (curwp->w_doto > 0) curwp->w_doto--;
            
            // Delete
            ldelete(1, FALSE);
            
            // Create new line occasionally
            if (op % 20 == 0) {
                lnewline();
            }
        }
        
        if (cycle % 100 == 0) {
            printf("Completed %d/1000 concurrent operation cycles\n", cycle);
        }
    }
    
    gettimeofday(&end, NULL);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    
    printf("[%sSUCCESS%s] EXTREME concurrent stress completed in %.2f seconds\n", 
           GREEN, RESET, elapsed);
    
    return ok;
}

// Ultra-large file simulation
int test_extreme_file_size_stress(void) {
    printf("\n%s=== EXTREME FILE SIZE STRESS TEST ===%s\n", CYAN, RESET);
    int ok = 1;
    int F = stress_factor();
    
    init_editor_minimal("giant-file");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;
    
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    curwp->w_dotp = curbp->b_linep;
    curwp->w_doto = 0;
    lnewline();
    curwp->w_dotp = lforw(curbp->b_linep);
    
    // Simulate a large file with long lines (scalable)
    long lines = 50000L * F;
    int chars_per_line = 1000 * (F > 1 ? 2 : 1); // modestly scale line width
    const char* env_lines = getenv("UEMACS_GIANT_LINES");
    const char* env_width = getenv("UEMACS_GIANT_WIDTH");
    if (env_lines) {
        long v = atol(env_lines);
        if (v > 0 && v < 1000000000L) lines = v; // sanity bounds
    }
    if (env_width) {
        long v = atol(env_width);
        if (v > 0 && v < 10000000L) chars_per_line = (int)v; // sanity bounds
    }
    
    printf("Simulating giant file: %ld lines × %d chars (factor=%d)\n", 
           lines, chars_per_line, F);
    
    for (long line = 0; line < lines; line++) {
        // Insert line content
        for (int c = 0; c < chars_per_line; c++) {
            char ch = 'a' + (c % 26);
            if (!linsert(1, ch)) {
                printf("[%sFAIL%s] Giant file simulation failed at line %d, char %d\n", 
                       RED, RESET, line, c);
                ok = 0;
                goto cleanup;
            }
        }
        
        // Add newline (except last line)
        if (line < lines - 1) {
            if (!lnewline()) {
                printf("[%sFAIL%s] Newline failed at line %d\n", RED, RESET, line);
                ok = 0;
                break;
            }
        }
        
        if (line % (5000 * F) == 0) {
            printf("Progress: %ld/%ld lines (%.1f%% complete)\n", 
                   line, lines, (line * 100.0) / lines);
        }
    }
    
cleanup:
    gettimeofday(&end, NULL);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    
    printf("[%s%s%s] EXTREME file size stress completed in %.2f seconds\n", 
           ok ? GREEN : RED, ok ? "SUCCESS" : "FAIL", RESET, elapsed);
    
    return ok;
}

// Main extreme stress test runner
int test_extreme_stress_suite(void) {
    printf("\n%s========================================%s\n", MAGENTA, RESET);
    printf("%s   EXTREME STRESS TEST SUITE (10X)     %s\n", MAGENTA, RESET);
    printf("%s========================================%s\n", MAGENTA, RESET);
    
    int total_passed = 0;
    
    if (!getenv("UEMACS_SKIP_TEXT_OPS"))
        total_passed += test_extreme_text_operations();
    else
        printf("[INFO] Skipping EXTREME text operations (UEMACS_SKIP_TEXT_OPS=1)\n");

    if (!getenv("UEMACS_SKIP_MEM"))
        total_passed += test_extreme_memory_stress();
    else
        printf("[INFO] Skipping EXTREME memory stress (UEMACS_SKIP_MEM=1)\n");

    if (!getenv("UEMACS_SKIP_CONCURRENT"))
        total_passed += test_extreme_concurrent_stress();
    else
        printf("[INFO] Skipping EXTREME concurrent stress (UEMACS_SKIP_CONCURRENT=1)\n");

    if (!getenv("UEMACS_SKIP_GIANT_FILE"))
        total_passed += test_extreme_file_size_stress();
    else
        printf("[INFO] Skipping EXTREME giant file stress (UEMACS_SKIP_GIANT_FILE=1)\n");
    
    printf("\n%s========================================%s\n", MAGENTA, RESET);
    int planned = 4
        - (getenv("UEMACS_SKIP_TEXT_OPS") ? 1 : 0)
        - (getenv("UEMACS_SKIP_MEM") ? 1 : 0)
        - (getenv("UEMACS_SKIP_CONCURRENT") ? 1 : 0)
        - (getenv("UEMACS_SKIP_GIANT_FILE") ? 1 : 0);
    if (planned <= 0) planned = 0;
    printf("EXTREME STRESS RESULTS: %d/%d tests passed\n", total_passed, planned);
    printf("%s========================================%s\n", MAGENTA, RESET);
    
    return total_passed == planned;
}
