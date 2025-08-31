#include <stdio.h>
#include <sys/time.h>
#include <string.h>

#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/line.h"
#include "internal/efunc.h"

// Profiler API
void perf_init(void);
void perf_shutdown(void);
void perf_report(void);

static void init_editor_minimal(const char* name) {
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    edinit((char*)(name ? name : "bench-editor"));
    varinit();
}

static double now_sec(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

int main(void) {
    perf_init();
    init_editor_minimal("bench-editor");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    const int lines = 2000;
    const char* payload = "The quick brown fox jumps over the lazy dog.";

    // Build buffer content
    curwp->w_dotp = curbp->b_linep; curwp->w_doto = 0; lnewline();
    curwp->w_dotp = lforw(curbp->b_linep); curwp->w_doto = 0;
    for (int i = 0; i < lines; ++i) {
        for (const char* p = payload; *p; ++p) linsert(1, *p);
        lnewline();
    }

    // Measure redraw/update cost for N iterations
    const int iters = 200;
    double t0 = now_sec();
    for (int i = 0; i < iters; ++i) {
        update(TRUE);
    }
    double t1 = now_sec();

    // Measure insert throughput on a single line
    curwp->w_dotp = lforw(curbp->b_linep); curwp->w_doto = 0;
    const int insert_chars = 10000;
    double t2 = now_sec();
    for (int i = 0; i < insert_chars; ++i) linsert(1, 'a' + (i % 26));
    double t3 = now_sec();

    printf("Redraw iterations: %d time: %.3f ms\n", iters, (t1 - t0) * 1000.0);
    printf("Insert chars: %d time: %.3f ms\n", insert_chars, (t3 - t2) * 1000.0);

    // Print profiler results (timings for insert, update, scroll, etc.)
    perf_report();

    // Print display matrix stats (redraw/dirty region telemetry)
#ifdef DEBUG
    extern void display_matrix_dump_stats(void);
    display_matrix_dump_stats();
#endif

    perf_shutdown();
    return 0;
}

