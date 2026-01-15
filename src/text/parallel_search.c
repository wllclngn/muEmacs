/*
 * parallel_search.c - Parallel search implementation using pthreads
 *
 * Utilizes the O(1) line index and refactored scan_region_forward
 * to distribute search workload across multiple threads.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "memory.h"
#include "boyer_moore.h"
#include "gapbuffer.h"

/* Use configurable values from [performance] TOML section */
#define MIN_LINES_PER_THREAD (perf_parallel_threshold > 0 ? perf_parallel_threshold : 1000)
#define MAX_THREADS (perf_max_threads > 0 ? perf_max_threads : 8)

/*
 * Search result structure
 */
struct search_result {
    struct line *match_line;
    int match_off;
    int found;
};

/*
 * Thread context
 */
struct search_thread_ctx {
    pthread_t thread_id;
    int thread_idx;
    struct buffer *bp;
    int start_line_idx;
    int end_line_idx;
    const char *pattern;
    struct search_result result;
};

/*
 * Thread worker function
 */
static void *search_worker_safe(void *arg) {
    struct search_thread_ctx *ctx = (struct search_thread_ctx *)arg;
    struct line *start_lp;
    struct line *end_lp;
    
    // Get start and end lines using O(1) index
    // Note: index access is read-only and thread-safe as long as no one is deleting lines
    // parallel search should only run when buffer is stable (no concurrent edits).
    
    start_lp = buffer_index_get(ctx->bp, ctx->start_line_idx);
    int total_lines = atomic_load(&ctx->bp->b_line_count);
    
    if (ctx->end_line_idx >= total_lines) {
        end_lp = ctx->bp->b_linep; // Header is end sentinel
    } else {
        end_lp = buffer_index_get(ctx->bp, ctx->end_line_idx);
    }
    
    if (!start_lp || !end_lp) {
        ctx->result.found = 0;
        return nullptr;
    }
    
    // Perform search in this region
    // scan_region_forward is now thread-safe (uses stack buffer)
    // It returns TRUE if found and writes to its own matchline/matchoff?
    // Wait, scan_region_forward in search.c writes to GLOBAL matchline/matchoff.
    // That is NOT thread safe.
    // I removed `scan_region_forward` from `search.c` header previously because it wasn't there.
    // But `parallel_search.c` needs a thread-safe scanner.
    // I will re-implement a local thread-safe scanner here to avoid global state issues entirely.
    
    // ... (Implementation of parallel_scan_region below) ...
    return nullptr;
}

/*
 * Thread-safe region scanner (Duplicates logic from search.c to avoid global state)
 */
static int parallel_scan_region(struct line *start_lp, struct line *end_lp, const char *pattern, struct search_result *res) {
    struct line *curline = start_lp;
    int patlen = strlen(pattern);
    char line_buf[NSTRING];
    
    if (patlen <= 0) return 0;
    
    // Check if pattern has newline (not supported in simple BMH)
    if (strchr(pattern, '\n') || strchr(pattern, '\r')) {
        return 0; 
    }
    
    struct boyer_moore_context bm = {0};
    bool case_sensitive = ((curwp->w_bufp->b_mode & MDEXACT) != 0);
    
    if (bm_init(&bm, (const unsigned char*)pattern, patlen, case_sensitive) != 0) {
        return 0;
    }
    
    while (curline != end_lp) {
        int n = llength(curline);
        if (n > 0) {
            if (n >= NSTRING) n = NSTRING - 1;
            lget_text(curline, 0, (size_t)n, line_buf, NSTRING);
            line_buf[n] = '\0';
            
            int idx = bm_search(&bm, (const unsigned char*)line_buf, n, 0);
            if (idx >= 0) {
                res->match_line = curline;
                res->match_off = idx;
                res->found = 1;
                bm_free(&bm);
                return 1;
            }
        }
        curline = lforw(curline);
    }
    
    bm_free(&bm);
    return 0;
}

/*
 * Worker wrapper
 */
static void *run_worker(void *arg) {
    struct search_thread_ctx *ctx = (struct search_thread_ctx *)arg;
    
    struct line *start_lp = buffer_index_get(ctx->bp, ctx->start_line_idx);
    struct line *end_lp;
    
    int total_lines = atomic_load(&ctx->bp->b_line_count);
    if (ctx->end_line_idx >= total_lines) {
        end_lp = ctx->bp->b_linep;
    } else {
        end_lp = buffer_index_get(ctx->bp, ctx->end_line_idx);
    }
    
    if (start_lp && end_lp) {
        parallel_scan_region(start_lp, end_lp, ctx->pattern, &ctx->result);
    }
    return nullptr;
}

/*
 * Execute parallel search
 * Returns TRUE if found, FALSE otherwise.
 * Updates global matchline/matchoff if found.
 */
int parallel_search(const char *pattern, int direction) {
    // Only support forward search for now
    if (direction != DIR_FORWARD) return 0;
    
    // Get total lines
    int total_lines = atomic_load(&curbp->b_line_count);
    
    // If small buffer, fallback to serial
    if (total_lines < MIN_LINES_PER_THREAD * 2) {
        return 0; 
    }
    
    // Determine number of threads
    int num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_threads > MAX_THREADS) num_threads = MAX_THREADS;
    if (num_threads < 2) return 0; 
    
    int lines_per_thread = total_lines / num_threads;
    if (lines_per_thread < MIN_LINES_PER_THREAD) {
        num_threads = total_lines / MIN_LINES_PER_THREAD;
        lines_per_thread = MIN_LINES_PER_THREAD;
    }
    
    struct search_thread_ctx contexts[MAX_THREADS];
    
    long current_line_num = getlinenum(curbp, curwp->w_dotp); // 1-based
    int start_idx = (int)current_line_num - 1;
    int remaining_lines = total_lines - start_idx;
    
    if (remaining_lines < MIN_LINES_PER_THREAD * 2) return 0; 
    
    int active_threads = 0;
    int chunk_size = remaining_lines / num_threads;
    
    for (int i = 0; i < num_threads; i++) {
        contexts[i].bp = curbp;
        contexts[i].pattern = pattern;
        contexts[i].result.found = 0;
        
        contexts[i].start_line_idx = start_idx + (i * chunk_size);
        if (i == num_threads - 1) {
            contexts[i].end_line_idx = total_lines; // To end
        } else {
            contexts[i].end_line_idx = contexts[i].start_line_idx + chunk_size;
        }
        
        if (pthread_create(&contexts[i].thread_id, nullptr, run_worker, &contexts[i]) == 0) {
            active_threads++;
        }
    }
    
    // Wait for threads
    int found = 0;
    struct line *first_match_line = nullptr;
    int first_match_off = -1;
    int first_match_thread_idx = 9999;
    
    for (int i = 0; i < active_threads; i++) {
        pthread_join(contexts[i].thread_id, nullptr);
        
        if (contexts[i].result.found) {
            if (!found || i < first_match_thread_idx) {
                found = 1;
                first_match_thread_idx = i;
                first_match_line = contexts[i].result.match_line;
                first_match_off = contexts[i].result.match_off;
            }
        }
    }
    
    if (found) {
        matchline = first_match_line;
        matchoff = first_match_off;
        curwp->w_dotp = matchline;
        curwp->w_doto = matchoff;
        curwp->w_flag |= WFMOVE;
        return 1;
    }
    
    return 0;
}