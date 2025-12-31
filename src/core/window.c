/*	window.c
 *
 *      Window management. Some of the functions are internal, and some are
 *      attached to keys that the user actually types.
 *
 */

#include <stdio.h>
#include <stdbool.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "wrapper.h"
#include "memory.h"
#include "util/logger.h"

/*
 * Reposition dot in the current window to line "n". If the argument is
 * positive, it is that line. If it is negative it is that line from the
 * bottom. If it is 0 the window is centered (this is what the standard
 * redisplay code does). With no argument it defaults to 0. Bound to M-!.
 */
int reposition(int f, int n)
{
	if (f == false)		/* default to 0 to center screen */
		n = 0;
	curwp->w_force = n;
	curwp->w_flag |= WFFORCE;
	return true;
}

/*
 * Refresh the screen. With no argument, it just does the refresh. With an
 * argument it recenters "." in the current window. Bound to "C-L".
 */
int redraw(int f, int n)
{
	if (f == false)
		sgarbf = true;
	else {
		curwp->w_force = 0;	/* Center dot. */
		curwp->w_flag |= WFFORCE;
	}

	return true;
}

/*
 * The command make the next window (next => down the screen) the current
 * window. There are no real errors, although the command does nothing if
 * there is only 1 window on the screen. Bound to "C-X C-N".
 *
 * with an argument this command finds the <n>th window from the top
 *
 * int f, n;		default flag and numeric argument
 *
 */
int window_next(int f, int n)
{
	struct window *wp;
	int nwindows;	/* total number of windows */

	if (f) {

		/* first count the # of windows */
		wp = wheadp;
		nwindows = 1;
		while (wp->w_wndp != nullptr) {
			nwindows++;
			wp = wp->w_wndp;
		}

		/* if the argument is negative, it is the nth window
		   from the bottom of the screen                        */
		if (n < 0)
			n = nwindows + n + 1;

		/* if an argument, give them that window from the top */
		if (n > 0 && n <= nwindows) {
			wp = wheadp;
			while (--n)
				wp = wp->w_wndp;
		} else {
			mlwrite("WINDOW NUMBER OUT OF RANGE");
			return false;
		}
	} else if ((wp = curwp->w_wndp) == nullptr)
		wp = wheadp;
	curwp = wp;
	curbp = wp->w_bufp;
	upmode();
	return true;
}

/*
 * This command makes the previous window (previous => up the screen) the
 * current window. There arn't any errors, although the command does not do a
 * lot if there is 1 window.
 */
int window_prev(int f, int n)
{
	struct window *wp1;
	struct window *wp2;

	/* if we have an argument, we mean the nth window from the bottom */
	if (f)
		return window_next(f, -n);

	wp1 = wheadp;
	wp2 = curwp;

	if (wp1 == wp2)
		wp2 = nullptr;

	while (wp1->w_wndp != wp2)
		wp1 = wp1->w_wndp;

	curwp = wp1;
	curbp = wp1->w_bufp;
	upmode();
	return true;
}

/*
 * This command moves the current window down by "arg" lines. Recompute the
 * top line in the window. The move up and move down code is almost completely
 * the same; most of the work has to do with reframing the window, and picking
 * a new dot. We share the code by having "move down" just be an interface to
 * "move up". Magic. Bound to "C-X C-N".
 */
int window_move_down(int f, int n)
{
	return window_move_up(f, -n);
}

/*
 * Move the current window up by "arg" lines. Recompute the new top line of
 * the window. Look to see if "." is still on the screen. If it is, you win.
 * If it isn't, then move "." to center it in the new framing of the window
 * (this command does not really move "."; it moves the frame). Bound to
 * "C-X C-P".
 */
int window_move_up(int f, int n)
{
	struct line *lp;
	int i;

	lp = curwp->w_linep;

	if (n < 0) {
		while (n++ && lp != curbp->b_linep)
			lp = lforw(lp);
	} else {
		while (n-- && lback(lp) != curbp->b_linep)
			lp = lback(lp);
	}

	curwp->w_linep = lp;
	curwp->w_flag |= WFHARD;	/* Mode line is OK. */

	for (i = 0; i < curwp->w_ntrows; ++i) {
		if (lp == curwp->w_dotp)
			return true;
		if (lp == curbp->b_linep)
			break;
		lp = lforw(lp);
	}

	lp = curwp->w_linep;
	i = curwp->w_ntrows / 2;

	while (i-- && lp != curbp->b_linep)
		lp = lforw(lp);

	curwp->w_dotp = lp;
	curwp->w_doto = 0;
	return true;
}

/*
 * This command makes the current window the only window on the screen. Bound
 * to "C-X 1". Try to set the framing so that "." does not have to move on the
 * display. Some care has to be taken to keep the values of dot and mark in
 * the buffer structures right if the distruction of a window makes a buffer
 * become undisplayed.
 */
int window_only(int f, int n)
{
	struct window *wp;
	struct line *lp;
	int i;

	while (wheadp != curwp) {
		wp = wheadp;
		wheadp = wp->w_wndp;
		if (--wp->w_bufp->b_nwnd == 0) {
			wp->w_bufp->b_dotp = wp->w_dotp;
			wp->w_bufp->b_doto = wp->w_doto;
			wp->w_bufp->b_markp = wp->w_markp;
			wp->w_bufp->b_marko = wp->w_marko;
		}
		SAFE_FREE(wp);
	}
	while (curwp->w_wndp != nullptr) {
		wp = curwp->w_wndp;
		curwp->w_wndp = wp->w_wndp;
		if (--wp->w_bufp->b_nwnd == 0) {
			wp->w_bufp->b_dotp = wp->w_dotp;
			wp->w_bufp->b_doto = wp->w_doto;
			wp->w_bufp->b_markp = wp->w_markp;
			wp->w_bufp->b_marko = wp->w_marko;
		}
		SAFE_FREE(wp);
	}
	lp = curwp->w_linep;
	i = curwp->w_toprow;
	while (i != 0 && lback(lp) != curbp->b_linep) {
		--i;
		lp = lback(lp);
	}
	curwp->w_toprow = 0;
	/* Use effective_nrow for overlay support - shrinks when overlay active */
	curwp->w_ntrows = effective_nrow > 0 ? effective_nrow : term.t_nrow - 1;
	curwp->w_linep = lp;
	curwp->w_flag |= WFMODE | WFHARD;
	return true;
}

/*
 * Delete the current window, placing its space in the window above,
 * or, if it is the top window, the window below. Bound to C-X 0.
 *
 * int f, n;	arguments are ignored for this command
 */
int window_delete(int f, int n)
{
	struct window *wp;	/* window to recieve deleted space */
	struct window *lwp;	/* ptr window before curwp */
	int target;	/* target line to search for */

	/* if there is only one window, don't delete it */
	if (wheadp->w_wndp == nullptr) {
		mlwrite("CANNOT DELETE THIS WINDOW");
		return false;
	}

	/* find window before curwp in linked list */
	wp = wheadp;
	lwp = nullptr;
	while (wp != nullptr) {
		if (wp == curwp)
			break;
		lwp = wp;
		wp = wp->w_wndp;
	}

	/* find recieving window and give up our space */
	wp = wheadp;
	if (curwp->w_toprow == 0) {
		/* find the next window down */
		target = curwp->w_ntrows + 1;
		while (wp != nullptr) {
			if (wp->w_toprow == target)
				break;
			wp = wp->w_wndp;
		}
		if (wp == nullptr)
			return false;
		wp->w_toprow = 0;
		wp->w_ntrows += target;
	} else {
		/* find the next window up */
		target = curwp->w_toprow - 1;
		while (wp != nullptr) {
			if ((wp->w_toprow + wp->w_ntrows) == target)
				break;
			wp = wp->w_wndp;
		}
		if (wp == nullptr)
			return false;
		wp->w_ntrows += 1 + curwp->w_ntrows;
	}

	/* get rid of the current window */
	if (--curwp->w_bufp->b_nwnd == 0) {
		curwp->w_bufp->b_dotp = curwp->w_dotp;
		curwp->w_bufp->b_doto = curwp->w_doto;
		curwp->w_bufp->b_markp = curwp->w_markp;
		curwp->w_bufp->b_marko = curwp->w_marko;
	}
	if (lwp == nullptr)
		wheadp = curwp->w_wndp;
	else
		lwp->w_wndp = curwp->w_wndp;
	SAFE_FREE(curwp);
	curwp = wp;
	wp->w_flag |= WFHARD;
	curbp = wp->w_bufp;
	upmode();
	return true;
}

/*
 * Split the current window.  A window smaller than 3 lines cannot be
 * split.  An argument of 1 forces the cursor into the upper window, an
 * argument of two forces the cursor to the lower window.  The only
 * other error that is possible is a "malloc" failure allocating the
 * structure for the new window.  Bound to "C-X 2". 
 *
 * int f, n;	default flag and numeric argument
 */
int window_split(int f, int n)
{
	struct window *wp;
	struct line *lp;
	int ntru;
	int ntrl;
	int ntrd;
	struct window *wp1;
	struct window *wp2;

	LOG_INFOF("Window: window_split() called f=%d n=%d curwp->w_ntrows=%d",
	          f, n, curwp->w_ntrows);

	if (!curwp->w_linep) {
		mlwrite("WINDOW NOT INITIALIZED");
		return false;
	}
	if (curwp->w_ntrows < 3) {
		mlwrite("CANNOT SPLIT A %d LINE WINDOW", curwp->w_ntrows);
		return false;
	}
	wp = (struct window *)safe_alloc(sizeof(struct window), "split window", __FILE__, __LINE__);
	if (!wp) {
		return false;
	}
	++curbp->b_nwnd;	/* Displayed twice.     */
	wp->w_bufp = curbp;
	wp->w_dotp = curwp->w_dotp;
	wp->w_doto = curwp->w_doto;
	wp->w_markp = curwp->w_markp;
	wp->w_marko = curwp->w_marko;
	wp->w_flag = 0;
	wp->w_force = 0;
	wp->w_wrap_col = curwp->w_wrap_col;  /* Inherit wrap setting */
	/* set the colors of the new window */
	wp->w_fcolor = gfcolor;
	wp->w_bcolor = gbcolor;
	ntru = (curwp->w_ntrows - 1) / 2;	/* Upper size           */
	ntrl = (curwp->w_ntrows - 1) - ntru;	/* Lower size           */
	lp = curwp->w_linep;
	ntrd = 0;
	/* Safety limit to prevent infinite loop if w_dotp not in line chain */
	int safety = 100000;
	while (lp != curwp->w_dotp && --safety > 0) {
		++ntrd;
		lp = lforw(lp);
		/* Detect self-referential line (circular list of one) */
		if (lp == curwp->w_linep && lp != curwp->w_dotp) {
			/* w_dotp not reachable from w_linep - corrupt state */
			mlwrite("Window state corrupt: w_dotp unreachable");
			safe_free((void **)&wp);
			return false;
		}
	}
	if (safety <= 0) {
		mlwrite("Window state corrupt: too many lines");
		safe_free((void **)&wp);
		return false;
	}
	lp = curwp->w_linep;
	if (!lp) {
		mlwrite("Window line pointer NULL - cannot split");
		safe_free((void **)&wp);
		return false;
	}
	if (((f == false) && (ntrd <= ntru)) || ((f == true) && (n == 1))) {
		/* Old is upper window. */
		if (ntrd == ntru) {	/* Hit mode line.       */
			struct line *next = lforw(lp);
			if (next && next != curbp->b_linep)
				lp = next;
		}
		curwp->w_ntrows = ntru;
		wp->w_wndp = curwp->w_wndp;
		curwp->w_wndp = wp;
		wp->w_toprow = curwp->w_toprow + ntru + 1;
		wp->w_ntrows = ntrl;
		/* Both windows start at same line */
		curwp->w_linep = lp;
		wp->w_linep = lp;
	} else {		/* Old is lower window  */
		wp1 = nullptr;
		wp2 = wheadp;
		while (wp2 != curwp) {
			wp1 = wp2;
			wp2 = wp2->w_wndp;
		}
		if (wp1 == nullptr)
			wheadp = wp;
		else
			wp1->w_wndp = wp;
		wp->w_wndp = curwp;
		wp->w_toprow = curwp->w_toprow;
		wp->w_ntrows = ntru;
		/* New upper window starts at original position */
		wp->w_linep = lp;
		++ntru;		/* Mode line.           */
		curwp->w_toprow += ntru;
		curwp->w_ntrows = ntrl;
		/* Advance to find where lower window starts */
		while (ntru--) {
			if (!lp || lp == curbp->b_linep) {
				/* Not enough lines in buffer - use what we have */
				break;
			}
			lp = lforw(lp);
		}
		/* Ensure we have a valid line pointer */
		if (!lp || lp == curbp->b_linep) {
			lp = lforw(curbp->b_linep);
		}
		curwp->w_linep = lp;
	}
	curwp->w_flag |= WFMODE | WFHARD;
	wp->w_flag |= WFMODE | WFHARD;
	return true;
}

/*
 * Enlarge the current window. Find the window that loses space. Make sure it
 * is big enough. If so, hack the window descriptions, and ask redisplay to do
 * all the hard work. You don't just set "force reframe" because dot would
 * move. Bound to "C-X Z".
 */
int window_enlarge(int f, int n)
{
	struct window *adjwp;
	struct line *lp;
	int i;

	if (n < 0)
		return window_shrink(f, -n);
	if (wheadp->w_wndp == nullptr) {
		mlwrite("ONLY ONE WINDOW");
		return false;
	}
	if ((adjwp = curwp->w_wndp) == nullptr) {
		adjwp = wheadp;
		while (adjwp && adjwp->w_wndp != curwp)
			adjwp = adjwp->w_wndp;
		if (!adjwp) {
			mlwrite("WINDOW LIST CORRUPT");
			return false;
		}
	}
	if (adjwp->w_ntrows <= n) {
		mlwrite("IMPOSSIBLE CHANGE");
		return false;
	}
	if (curwp->w_wndp == adjwp) {	/* Shrink below.        */
		lp = adjwp->w_linep;
		if (!lp) {
			mlwrite("WINDOW LINE POINTER NULL");
			return false;
		}
		for (i = 0; i < n && lp != adjwp->w_bufp->b_linep; ++i) {
			if (!lp) {
				mlwrite("LINE LIST CORRUPT (NULL)");
				return false;
			}
			lp = lforw(lp);
		}
		adjwp->w_linep = lp;
		adjwp->w_toprow += n;
	} else {		/* Shrink above.        */
		lp = curwp->w_linep;
		if (!lp) {
			mlwrite("WINDOW LINE POINTER NULL");
			return false;
		}
		for (i = 0; i < n && lback(lp) != curbp->b_linep; ++i) {
			if (!lback(lp)) {
				mlwrite("LINE LIST CORRUPT (NULL BACK)");
				return false;
			}
			lp = lback(lp);
		}
		curwp->w_linep = lp;
		curwp->w_toprow -= n;
	}
	curwp->w_ntrows += n;
	adjwp->w_ntrows -= n;
	curwp->w_flag |= WFMODE | WFHARD;
	adjwp->w_flag |= WFMODE | WFHARD;
	return true;
}

/*
 * Shrink the current window. Find the window that gains space. Hack at the
 * window descriptions. Ask the redisplay to do all the hard work. Bound to
 * "C-X C-Z".
 */
int window_shrink(int f, int n)
{
	struct window *adjwp;
	struct line *lp;
	int i;

	if (n < 0)
		return window_enlarge(f, -n);
	if (wheadp->w_wndp == nullptr) {
		mlwrite("ONLY ONE WINDOW");
		return false;
	}
	if ((adjwp = curwp->w_wndp) == nullptr) {
		adjwp = wheadp;
		while (adjwp && adjwp->w_wndp != curwp)
			adjwp = adjwp->w_wndp;
		if (!adjwp) {
			mlwrite("WINDOW LIST CORRUPT");
			return false;
		}
	}
	if (curwp->w_ntrows <= n) {
		mlwrite("IMPOSSIBLE CHANGE");
		return false;
	}
	if (curwp->w_wndp == adjwp) {	/* Grow below.          */
		lp = adjwp->w_linep;
		if (!lp) {
			mlwrite("WINDOW LINE POINTER NULL");
			return false;
		}
		for (i = 0; i < n && lback(lp) != adjwp->w_bufp->b_linep;
		     ++i) {
			if (!lback(lp)) {
				mlwrite("LINE LIST CORRUPT (NULL BACK)");
				return false;
			}
			lp = lback(lp);
		}
		adjwp->w_linep = lp;
		adjwp->w_toprow -= n;
	} else {		/* Grow above.          */
		lp = curwp->w_linep;
		if (!lp) {
			mlwrite("WINDOW LINE POINTER NULL");
			return false;
		}
		for (i = 0; i < n && lp != curbp->b_linep; ++i) {
			if (!lp) {
				mlwrite("LINE LIST CORRUPT (NULL)");
				return false;
			}
			lp = lforw(lp);
		}
		curwp->w_linep = lp;
		curwp->w_toprow += n;
	}
	curwp->w_ntrows -= n;
	adjwp->w_ntrows += n;
	curwp->w_flag |= WFMODE | WFHARD;
	adjwp->w_flag |= WFMODE | WFHARD;
	return true;
}

/*
 * Resize the current window to the requested size
 *
 * int f, n;		default flag and numeric argument
 */
int resize(int f, int n)
{
	int clines;		/* current # of lines in window */

	/* must have a non-default argument, else ignore call */
	if (f == false)
		return true;

	/* find out what to do */
	clines = curwp->w_ntrows;

	/* already the right size? */
	if (clines == n)
		return true;

	return window_enlarge(true, n - clines);
}

/*
 * Pick a window for a pop-up. Split the screen if there is only one window.
 * Pick the uppermost window that isn't the current window. An LRU algorithm
 * might be better. Return a pointer, or nullptr on error.
 */
struct window *wpopup(void)
{
	struct window *wp;

	if (wheadp->w_wndp == nullptr	/* Only 1 window        */
	    && window_split(false, 0) == false)	/* and it won't split   */
		return nullptr;
	wp = wheadp;		/* Find window to use   */
	while (wp != nullptr && wp == curwp)
		wp = wp->w_wndp;
	return wp;
}

int scroll_other_up(int f, int n)
{				/* scroll the next window up (back) a page */
	window_next(false, 1);
	move_page_up(f, n);
	window_prev(false, 1);
	return true;
}

int scroll_other_down(int f, int n)
{				/* scroll the next window down (forward) a page */
	window_next(false, 1);
	move_page_down(f, n);
	window_prev(false, 1);
	return true;
}

int window_save(int f, int n)
{				/* save ptr to current window */
	swindow = curwp;
	return true;
}

int window_restore(int f, int n)
{				/* restore the saved screen */
	struct window *wp;

	/* find the window */
	wp = wheadp;
	while (wp != nullptr) {
		if (wp == swindow) {
			curwp = wp;
			curbp = wp->w_bufp;
			upmode();
			return true;
		}
		wp = wp->w_wndp;
	}

	mlwrite("[NO SUCH WINDOW EXISTS]");
	return false;
}

/*
 * resize the screen, re-writing the screen
 *
 * int f;	default flag
 * int n;	numeric argument
 */
int newsize(int f, int n)
{
	struct window *wp;		/* current window being examined */
	struct window *nextwp;	/* next window to scan */
	struct window *lastwp;	/* last window scanned */
	int lastline;		/* screen line of last line of current window */

	/* if the command defaults, assume the largest */
	if (f == false)
		n = term.t_mrow + 1;

	/* make sure it's in range */
	if (n < 3 || n > term.t_mrow + 1) {
		mlwrite("SCREEN SIZE OUT OF RANGE");
		return false;
	}

	if (term.t_nrow == n - 1)
		return true;
	else if (term.t_nrow < n - 1) {

		/* go to the last window */
		wp = wheadp;
		while (wp->w_wndp != nullptr)
			wp = wp->w_wndp;

		/* and enlarge it as needed */
		wp->w_ntrows = n - wp->w_toprow - 2;
		wp->w_flag |= WFHARD | WFMODE;

	} else {

		/* rebuild the window structure */
		nextwp = wheadp;
		wp = nullptr;
		lastwp = nullptr;
		while (nextwp != nullptr) {
			wp = nextwp;
			nextwp = wp->w_wndp;

			/* get rid of it if it is too low */
			if (wp->w_toprow > n - 2) {

				/* save the point/mark if needed */
				if (--wp->w_bufp->b_nwnd == 0) {
					wp->w_bufp->b_dotp = wp->w_dotp;
					wp->w_bufp->b_doto = wp->w_doto;
					wp->w_bufp->b_markp = wp->w_markp;
					wp->w_bufp->b_marko = wp->w_marko;
				}

				/* update curwp and lastwp if needed */
				if (wp == curwp)
					curwp = wheadp;
				curbp = curwp->w_bufp;
				if (lastwp != nullptr)
					lastwp->w_wndp = nullptr;

				/* free the structure */
				SAFE_FREE(wp);
				wp = nullptr;

			} else {
				/* need to change this window size? */
				lastline = wp->w_toprow + wp->w_ntrows - 1;
				if (lastline >= n - 2) {
					wp->w_ntrows =
					    n - wp->w_toprow - 2;
					wp->w_flag |= WFHARD | WFMODE;
				}
			}

			lastwp = wp;
		}
	}

	/* screen is garbage */
	term.t_nrow = n - 1;
	sgarbf = true;
	return true;
}

/*
 * resize the screen, re-writing the screen
 *
 * int f;		default flag
 * int n;		numeric argument
 */
int newwidth(int f, int n)
{
	struct window *wp;

	/* if the command defaults, assume the largest */
	if (f == false)
		n = term.t_mcol;

	/* make sure it's in range */
	if (n < 10 || n > term.t_mcol) {
		mlwrite("SCREEN WIDTH OUT OF RANGE");
		return false;
	}

	/* otherwise, just re-width it (no big deal) */
	term.t_ncol = n;
	term.t_margin = n / 10;
	term.t_scrsiz = n - (term.t_margin * 2);

	/* florce all windows to redraw */
	wp = wheadp;
	while (wp) {
		wp->w_flag |= WFHARD | WFMOVE | WFMODE;
		wp = wp->w_wndp;
	}
	sgarbf = true;

	return true;
}

int window_get_position(void)
{				/* get screen offset of current line in current window */
	int sline;	/* screen line from top of window */
	struct line *lp;	/* scannile line pointer */

	/* search down the line we want */
	lp = curwp->w_linep;
	sline = 1;
	while (lp != curwp->w_dotp) {
		++sline;
		lp = lforw(lp);
	}

	/* and return the value */
	return sline;
}

/* ==========================================================================
 * Terminal Overlay Support
 * ========================================================================== */

/*
 * adjust_windows_for_overlay - Shrink windows to make room for terminal overlay
 *
 * Called when terminal overlay is opened. Reduces effective_nrow and shrinks
 * the bottom window(s) accordingly.
 *
 * @param overlay_height  Number of rows needed for overlay
 */
void adjust_windows_for_overlay(int overlay_height) {
	struct window *wp;
	int new_effective;
	int total_shrink;

	LOG_DEBUGF("adjust_windows_for_overlay: overlay_height=%d", overlay_height);

	/* Calculate new effective rows (leave room for at least 1 status line) */
	new_effective = term.t_nrow - 1 - overlay_height;
	if (new_effective < 3) {
		new_effective = 3;  /* Minimum usable window height */
	}

	LOG_DEBUGF("adjust_windows_for_overlay: effective_nrow %d -> %d",
	           effective_nrow, new_effective);

	/* Initialize effective_nrow if needed */
	if (effective_nrow <= 0) {
		effective_nrow = term.t_nrow - 1;
	}

	/* Calculate how much to shrink */
	total_shrink = effective_nrow - new_effective;
	if (total_shrink <= 0) {
		LOG_DEBUG("adjust_windows_for_overlay: No shrink needed");
		return;
	}

	/* Find the bottom-most window */
	wp = wheadp;
	while (wp && wp->w_wndp != nullptr) {
		wp = wp->w_wndp;
	}

	if (!wp) {
		LOG_ERROR("adjust_windows_for_overlay: No windows found");
		return;
	}

	/* Shrink bottom window */
	LOG_DEBUGF("adjust_windows_for_overlay: Shrinking bottom window '%s' by %d rows",
	           wp->w_bufp ? wp->w_bufp->b_bname : "(null)", total_shrink);

	if (wp->w_ntrows > total_shrink) {
		wp->w_ntrows -= total_shrink;
	} else {
		/* Window too small - set to minimum */
		wp->w_ntrows = 1;
	}

	wp->w_flag |= WFMODE | WFHARD;

	/* Update effective_nrow */
	effective_nrow = new_effective;

	LOG_DEBUGF("adjust_windows_for_overlay: Done, effective_nrow=%d", effective_nrow);
}

/*
 * restore_windows_from_overlay - Restore windows after terminal overlay closes
 *
 * Called when terminal overlay is closed. Restores effective_nrow and expands
 * the bottom window to reclaim space.
 */
void restore_windows_from_overlay(void) {
	struct window *wp;
	int old_effective;
	int expand_amount;

	LOG_DEBUG("restore_windows_from_overlay: Restoring windows");

	/* Calculate how much space to reclaim */
	old_effective = effective_nrow;
	effective_nrow = term.t_nrow - 1;  /* Full screen minus message line */

	expand_amount = effective_nrow - old_effective;
	if (expand_amount <= 0) {
		LOG_DEBUG("restore_windows_from_overlay: No expansion needed");
		return;
	}

	LOG_DEBUGF("restore_windows_from_overlay: Expanding by %d rows", expand_amount);

	/* Find the bottom-most window */
	wp = wheadp;
	while (wp && wp->w_wndp != nullptr) {
		wp = wp->w_wndp;
	}

	if (!wp) {
		LOG_ERROR("restore_windows_from_overlay: No windows found");
		return;
	}

	/* Expand bottom window */
	LOG_DEBUGF("restore_windows_from_overlay: Expanding bottom window '%s' by %d rows",
	           wp->w_bufp ? wp->w_bufp->b_bname : "(null)", expand_amount);

	wp->w_ntrows += expand_amount;
	wp->w_flag |= WFMODE | WFHARD;

	LOG_DEBUGF("restore_windows_from_overlay: Done, effective_nrow=%d", effective_nrow);
}

/*
 * terminal_split_bottom - Create a terminal window at the bottom of the screen
 *
 * height_rows: Number of rows for the terminal window
 *
 * Returns: Pointer to the new window, or NULL on failure
 *
 * This function finds the bottom-most window, shrinks it, and creates
 * a new window below it for the terminal.
 */
struct window *terminal_split_bottom(int height_rows) {
	struct window *bottom_wp;
	struct window *term_wp;

	LOG_DEBUGF("terminal_split_bottom: Creating terminal window with %d rows", height_rows);

	/* Find bottom-most window */
	bottom_wp = wheadp;
	while (bottom_wp && bottom_wp->w_wndp != nullptr) {
		bottom_wp = bottom_wp->w_wndp;
	}

	if (!bottom_wp) {
		LOG_ERROR("terminal_split_bottom: No windows found");
		return nullptr;
	}

	/* Check minimum size - need room for both windows */
	if (bottom_wp->w_ntrows < height_rows + 3) {
		LOG_ERRORF("terminal_split_bottom: Bottom window too small (%d rows, need %d)",
		           bottom_wp->w_ntrows, height_rows + 3);
		mlwrite("Window too small for terminal");
		return nullptr;
	}

	/* Allocate new window structure */
	term_wp = (struct window *)safe_alloc(sizeof(struct window), "terminal window", __FILE__, __LINE__);
	if (!term_wp) {
		LOG_ERROR("terminal_split_bottom: Cannot allocate window");
		return nullptr;
	}

	/* Initialize terminal window */
	memset(term_wp, 0, sizeof(struct window));
	term_wp->w_wndp = nullptr;  /* Terminal is at the end of the list */
	term_wp->w_bufp = nullptr;  /* Will be set by caller */
	term_wp->w_toprow = bottom_wp->w_toprow + bottom_wp->w_ntrows - height_rows;
	term_wp->w_ntrows = height_rows;
	term_wp->w_flag = WFMODE | WFHARD;
	term_wp->w_force = 0;
	term_wp->w_fcolor = gfcolor;
	term_wp->w_bcolor = gbcolor;
	term_wp->w_wrap_col = 0;

	/* Shrink bottom window to make room (minus 1 for mode line) */
	bottom_wp->w_ntrows -= (height_rows + 1);
	bottom_wp->w_wndp = term_wp;
	bottom_wp->w_flag |= WFMODE | WFHARD;

	LOG_DEBUGF("terminal_split_bottom: Created terminal window at row %d, %d rows",
	           term_wp->w_toprow, term_wp->w_ntrows);
	LOG_DEBUGF("terminal_split_bottom: Bottom window now has %d rows",
	           bottom_wp->w_ntrows);

	return term_wp;
}
