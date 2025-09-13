/*
 * writing.c - Simple writing mode helpers for μEmacs
 * Keyboard-only: sets a comfortable wrap column and enables/disables wrap.
 */

#include "estruct.h"
#include "edef.h"
#include "efunc.h"

static int saved_fillcol = -1;
static int saved_wrap_flag = -1; /* 0 or 1 when saved, -1 when not saved */

/* Enable writing mode: set fill column (default 80) and enable wrap for current buffer. */
int writing_mode_enable(int f, int n)
{
    int target = f ? n : 80;
    if (target < 20) target = 20;
    if (target > 200) target = 200;

    if (saved_fillcol == -1)
        saved_fillcol = fillcol;
    if (saved_wrap_flag == -1)
        saved_wrap_flag = (curbp->b_mode & MDWRAP) ? 1 : 0;

    fillcol = target;
    curbp->b_mode |= MDWRAP;
    upmode();
    mlwrite("Writing mode enabled: wrap at %d", fillcol);
    return TRUE;
}

/* Disable writing mode: restore previous fill column and wrap flag if saved. */
int writing_mode_disable(int f, int n)
{
    (void)f; (void)n;
    if (saved_fillcol != -1) {
        fillcol = saved_fillcol;
        saved_fillcol = -1;
    }
    if (saved_wrap_flag != -1) {
        if (saved_wrap_flag)
            curbp->b_mode |= MDWRAP;
        else
            curbp->b_mode &= ~MDWRAP;
        saved_wrap_flag = -1;
    }
    upmode();
    mlwrite("Writing mode disabled");
    return TRUE;
}

