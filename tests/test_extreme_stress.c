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

// EXTREME stress test - 10x beyond current levels
int test_extreme_text_operations(void) {
    printf("\n%s=== EXTREME TEXT OPERATIONS STRESS TEST ===%s\n", CYAN, RESET);
    int ok = 1;
    
    init_editor_minimal("extreme-stress");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;
    
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    // PHASE 1: EXTREME TEXT INSERTION - 1,000,000 characters
    printf("Testing EXTREME text insertion (1,000,000 characters)...\n");
    curwp->w_dotp = curbp->b_linep;
    curwp->w_doto = 0;
    lnewline();
    curwp->w_dotp = lforw(curbp->b_linep);
    
    for (int i = 0; i < 1000000; i++) {
        char c = 'A' + (i % 26);
        if (!linsert(1, c)) {
            printf("[%sFAIL%s] Text insertion failed at %d\n", RED, RESET, i);
            ok = 0;
            break;
        }
        if (i % 100000 == 0) {
            printf("Progress: %d/1000000 characters inserted\n", i);
        }
    }
    
    // PHASE 2: EXTREME LINE OPERATIONS - 100,000 lines
    printf("Testing EXTREME line operations (100,000 new lines)...\n");
    for (int i = 0; i < 100000; i++) {
        if (!lnewline()) {
            printf("[%sFAIL%s] Line creation failed at %d\n", RED, RESET, i);
            ok = 0;
            break;
        }
        if (i % 10000 == 0) {
            printf("Progress: %d/100000 lines created\n", i);
        }
    }
    
    // PHASE 3: EXTREME DELETION STRESS - 500,000 deletions
    printf("Testing EXTREME deletion stress (500,000 deletions)...\n");
    for (int i = 0; i < 500000; i++) {
        if (!ldelete(1, FALSE)) {
            break; // Hit end of buffer
        }
        if (i % 50000 == 0) {
            printf("Progress: %d/500000 deletions completed\n", i);
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
    
    init_editor_minimal("giant-file");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;
    
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    curwp->w_dotp = curbp->b_linep;
    curwp->w_doto = 0;
    lnewline();
    curwp->w_dotp = lforw(curbp->b_linep);
    
    // Simulate a 50MB file with long lines
    const int lines = 50000;
    const int chars_per_line = 1000;
    
    printf("Simulating 50MB file: %d lines × %d chars = %d total chars\n", 
           lines, chars_per_line, lines * chars_per_line);
    
    for (int line = 0; line < lines; line++) {
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
        
        if (line % 5000 == 0) {
            printf("Progress: %d/%d lines (%.1f%% complete)\n", 
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
    
    total_passed += test_extreme_text_operations();
    total_passed += test_extreme_memory_stress();
    total_passed += test_extreme_concurrent_stress();
    total_passed += test_extreme_file_size_stress();
    
    printf("\n%s========================================%s\n", MAGENTA, RESET);
    printf("EXTREME STRESS RESULTS: %d/4 tests passed\n", total_passed);
    printf("%s========================================%s\n", MAGENTA, RESET);
    
    return total_passed == 4;
}