#include "test_utils.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "parallel_search.h"

static double now_sec(void) {
    struct timeval tv; gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static void init_editor_minimal(const char* name) {
    // Initialize terminal struct manually to avoid ncurses interaction
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    
    edinit((char*)(name ? name : "bench"));
    varinit();
}

int main(int argc, char **argv) {
    LOG_INFO("Benchmarking Parallel Search...");
    
    // Initialize editor
    init_editor_minimal("bench-parallel");
    
    // Clear buffer
    (void)bclear(curbp);
    curbp->b_mode &= ~MDVIEW;
    
    // Generate large buffer (10,000 lines) to trigger parallel search
    LOG_INFO("Generating 10,000 lines...");
    for (int i = 0; i < 10000; i++) {
        if (i % 5000 == 0) {
            // Place match at line 0, 5000, 10000
            linstr("MatchMe");
        } else {
            linstr("The quick brown fox jumps over the lazy dog");
        }
        lnewline();
    }
    
    // Reset cursor to top
    curwp->w_dotp = lforw(curbp->b_linep);
    curwp->w_doto = 0;
    
    LOG_INFO("Starting Parallel Search for 'MatchMe'...");
    
    double start = now_sec();
    int result = parallel_search("MatchMe", DIR_FORWARD);
    double end = now_sec();
    
    double time_taken = end - start;
    
    if (result) {
        LOG_INFOF("[SUCCESS] Found 'MatchMe' in %f seconds.", time_taken);
        // Check match position
        long lnum = getlinenum(curbp, matchline);
        LOG_INFOF("Match Line: %ld", lnum);
        // First match should be at line 1 (index 0) since we inserted at i=0.
        // Wait, we insert, then newline.
        // i=0: "MatchMe" -> newline. Line 1 is "MatchMe".
        // i=50000: Line 50001 is "MatchMe".
        // Parallel search should find the *first* one.
        // If threads chunk:
        // Thread 0: 0..6250. Should find line 1.
        // So we expect line 1.
    } else {
        LOG_INFO("[FAIL] Did not find 'MatchMe'");
    }
    
    return result ? 0 : 1;
}