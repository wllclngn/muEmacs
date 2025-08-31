#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/efunc.h"
#include "internal/line.h"

static void init_editor_minimal(const char* name) {
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    edinit((char*)(name ? name : "bench"));
    varinit();
}

static double now_sec(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

int main(void) {
    const int repeats = 2000; // modest count to keep runtime low
    const int lines = 2000;   // generate a few thousand lines

    init_editor_minimal("bench-search");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Create a dataset of repeated lines with embedded patterns
    const char* base = "The quick brown fox jumps over the lazy dog 12345 abcde xyz\n";
    curwp->w_dotp = curbp->b_linep; curwp->w_doto = 0;
    for (int i = 0; i < lines; ++i) {
        for (const char* p = base; *p; ++p) linsert(1, *p);
        lnewline();
    }

    struct line* first = lforw(curbp->b_linep);
    curwp->w_dotp = first; curwp->w_doto = 0;

    // Patterns for benchmarks
    const char* pat4 = "abcd";   // short path
    const char* pat5 = "abcde";  // BMH path

    // Warmup
    scanner(pat4, FORWARD, PTBEG);
    scanner(pat5, FORWARD, PTBEG);

    // Time short path
    double t0 = now_sec();
    for (int i = 0; i < repeats; ++i) {
        curwp->w_dotp = first; curwp->w_doto = 0;
        (void)scanner(pat4, FORWARD, PTBEG);
    }
    double t1 = now_sec();

    // Time BMH path
    double t2 = now_sec();
    for (int i = 0; i < repeats; ++i) {
        curwp->w_dotp = first; curwp->w_doto = 0;
        (void)scanner(pat5, FORWARD, PTBEG);
    }
    double t3 = now_sec();

    printf("BMH_MIN_LEN=%d\n", BMH_MIN_LEN);
    printf("Short literal (len=4): %.3f ms total\n", (t1 - t0) * 1000.0);
    printf("BMH literal   (len=5): %.3f ms total\n", (t3 - t2) * 1000.0);
    printf("Done.\n");
    return 0;
}
