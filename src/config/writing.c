/*
 * writing.c - Simple writing mode helpers for μEmacs
 * Keyboard-only: sets a comfortable wrap column and enables/disables wrap.
 */

#include "estruct.h"
#include "edef.h"
#include "efunc.h"

static int saved_fillcol = -1;
static int saved_wrap_flag = -1; /* 0 or 1 when saved, -1 when not saved */

/* Reset writing mode state (for testing/re-initialization). */
void writing_mode_reset(void)
{
    saved_fillcol = -1;
    saved_wrap_flag = -1;
}

/* Enable writing mode: set fill column (default 80) and enable wrap for current buffer. */
int writing_mode_enable(int f, int n)
{
    int target = f ? n : 80;
    if (target < 20) target = 20;
    if (target > 200) target = 200;

    if (saved_fillcol == -1)
        saved_fillcol = fillcol;
    if (saved_wrap_flag == -1 && curbp != NULL)
        saved_wrap_flag = (curbp->b_mode & MDWRAP) ? 1 : 0;

    fillcol = target;
    if (curbp != NULL) {
        curbp->b_mode |= MDWRAP;
        upmode();
    }
    /* Silent - no need to announce */
    return true;
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
    mlwrite("COLUMN WIDTH DISABLED");
    return true;
}
