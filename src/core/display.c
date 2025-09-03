/*	display.c
 *
 *      The functions in this file handle redisplay. There are two halves, the
 *      ones that update the virtual display screen, and the ones that make the
 *      physical display screen the same as the virtual display screen. These
 *      functions use hints that are left in the windows by the commands.
 *
 *	Modified by Petri Kutvonen
 */

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <locale.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "profiler.h"
#include "line.h"
#include "version.h"
#include "wrapper.h"
#include "utf8.h"
#include "../util/display_width.h"
#include "string_utils.h"
#include "memory.h"
#include "error.h"
#include "display_ops.h"

#include "../util/git_status.h"

struct video {
	int v_flag;		/* Flags */
	int v_fcolor;		/* current forground color */
	int v_bcolor;		/* current background color */
	int v_rfcolor;		/* requested forground color */
	int v_rbcolor;		/* requested background color */
	_Atomic uint32_t v_checksum;  /* Fast change detection checksum */
	unicode_t v_text[1];	/* Screen data. */
};

#define VFCHG   0x0001		/* Changed flag                 */
#define	VFEXT	0x0002		/* extended (beyond column 80)  */
#define	VFREV	0x0004		/* reverse video status         */
#define	VFREQ	0x0008		/* reverse video request        */
#define	VFCOL	0x0010		/* color change requested       */

static struct video **vscreen;		/* Virtual screen. */
static struct video **pscreen;		/* Physical screen. */

static int displaying = TRUE;

/* Fast checksum calculation for change detection optimization */
static uint32_t video_checksum(unicode_t *text, int len)
{
	uint32_t hash = 2166136261U; // FNV-1a hash
	for (int i = 0; i < len; i++) {
		hash ^= text[i];
		hash *= 16777619U;
	}
	return hash;
}

/* Update video line checksum atomically */
static void video_update_checksum(struct video *vp)
{
	uint32_t checksum = video_checksum(vp->v_text, term.t_ncol);
	atomic_store(&vp->v_checksum, checksum);
}

/* Fast comparison using checksums to avoid character-by-character scan */
static bool video_lines_differ(struct video *vp1, struct video *vp2)
{
	uint32_t sum1 = atomic_load(&vp1->v_checksum);
	uint32_t sum2 = atomic_load(&vp2->v_checksum);
	
	// If checksums differ, lines definitely differ
	if (sum1 != sum2) return true;
	
	// Checksums match - do byte comparison to confirm (handles hash collisions)
	return memcmp(vp1->v_text, vp2->v_text, term.t_ncol * sizeof(unicode_t)) != 0;
}
#if UNIX
#include <signal.h>
#endif
#ifdef SIGWINCH
#include <sys/ioctl.h>
/* for window size changes */
int chg_width, chg_height;
#endif

static int reframe(struct window *wp);
static void updone(struct window *wp);
static void updall(struct window *wp);
static int scrolls(int inserts);
static void scrscroll(int from, int to, int count);
static int texttest(int vrow, int prow);
static int endofline(unicode_t *s, int n);
static void updext(void);
static int updateline(int row, struct video *vp1, struct video *vp2);
static void modeline(struct window *wp);
#if MODERN
static void clean_statusline(struct window *wp);
static int getlinecount_modern(struct buffer *bp);
#endif
static void mlputi(int i, int r);
static void mlputli(long l, int r);
static void mlputf(int s);
static int newscreensize(int h, int w);


/*
 * Initialize the data structures used by the display code. The edge vectors
 * used to access the screens are set up. The operating system's terminal I/O
 * channel is set up. All the other things get initialized at compile time.
 * The original window has "WFCHG" set, so that it will get completely
 * redrawn on the first call to "update".
 */
void vtinit(void)
{
	int i;
	struct video *vp;

    TTopen();		/* open the screen */
    TTkopen();		/* open the keyboard */
    TTrev(FALSE);
    /* Initialize terminal optimizations/capabilities (truecolor, paste) after TTopen */
    display_init_optimization();
	vscreen = (struct video**)safe_alloc(term.t_mrow * sizeof(struct video *), "vscreen", __FILE__, __LINE__);
	if (!vscreen) return;

	pscreen = (struct video**)safe_alloc(term.t_mrow * sizeof(struct video *), "pscreen", __FILE__, __LINE__);
	if (!pscreen) return;
	for (i = 0; i < term.t_mrow; ++i) {
		vp = (struct video*)safe_alloc(sizeof(struct video) + term.t_mcol*4, "video row", __FILE__, __LINE__);
		if (!vp) return;
		vp->v_flag = 0;
		vp->v_rfcolor = 7;
		vp->v_rbcolor = 0;
		atomic_store(&vp->v_checksum, 0); // Initialize checksum
		vscreen[i] = vp;
		vp = (struct video*)safe_alloc(sizeof(struct video) + term.t_mcol*4, "physical video row", __FILE__, __LINE__);
		vp->v_flag = 0;
		atomic_store(&vp->v_checksum, 0); // Initialize checksum
		pscreen[i] = vp;
	}
}


/*
 * Clean up the virtual terminal system, in anticipation for a return to the
 * operating system. Move down to the last line and clear it out (the next
 * system prompt will be written in the line). Shut down the channel to the
 * terminal.
 */
void vttidy(void)
{
	/*
	 * Restore the primary screen buffer and other terminal modes first,
	 * so any final clears/cursor moves happen on the real shell screen.
	 */
	display_cleanup_optimization();
	TTflush();

	/* Close terminal I/O – do not write or move the cursor after
	 * restoring the primary screen to avoid altering shell history. */
	TTclose();
	TTkclose();

}

/*
 * Set the virtual cursor to the specified row and column on the virtual
 * screen. There is no checking for nonsense values; this might be a good
 * idea during the early stages.
 */
void vtmove(int row, int col)
{
	vtrow = row;
	vtcol = col;
}

/*
 * Write a character to the virtual screen. The virtual row and
 * column are updated. If we are not yet on left edge, don't print
 * it yet. If the line is too long put a "$" in the last column.
 *
 * This routine only puts printing characters into the virtual
 * terminal buffers. Only column overflow is checked.
 */
static void vtputc(int c)
{
	struct video *vp;	/* ptr to line being updated */


	/* In case somebody passes us a signed char.. */
	if (c < 0) {
		c += 256;
		if (c < 0)
			return;
	}

	vp = vscreen[vtrow];

	if (vtcol >= term.t_ncol) {
		++vtcol;
		vp->v_text[term.t_ncol - 1] = '$';
		return;
	}

	if (c == '\t') {
		do {
			vtputc(' ');
		} while (((vtcol + taboff) & tabmask) != 0);
		return;
	}

	if (c < 0x20) {
		vtputc('^');
		vtputc(c ^ 0x40);
		return;
	}

	if (c == 0x7f) {
		vtputc('^');
		vtputc('?');
		return;
	}

	if (c >= 0x80 && c <= 0xA0) {
		static const char hex[] = "0123456789abcdef";
		vtputc('\\');
		vtputc(hex[c >> 4]);
		vtputc(hex[c & 15]);
		return;
	}
	
	if (vtcol >= 0) {
		vp->v_text[vtcol] = c;
		// Mark line as changed - checksum will be updated later
		vp->v_flag |= VFCHG;
	}
	++vtcol;
}

/* Put character with highlighting (reverse video) */
#define HIGHLIGHT_BIT 0x80000000U  /* High bit indicates highlighting */

static void vtputc_highlighted(int c)
{
	struct video *vp;

	/* In case somebody passes us a signed char.. */
	if (c < 0) {
		c += 256;
		if (c < 0)
			return;
	}

	vp = vscreen[vtrow];

	if (vtcol >= term.t_ncol) {
		++vtcol;
		vp->v_flag |= VFEXT;
		return;
	}

	/* Handle tabs */
	if (c == '\t') {
		do {
			vtputc_highlighted(' ');
		} while (((vtcol + taboff) & tabmask) != 0);
		return;
	}

	/* Handle control characters */
	if (c < 0x20) {
		vtputc_highlighted('^');
		vtputc_highlighted(c ^ 0x40);
		return;
	}

	if (c == 0x7f) {
		vtputc_highlighted('^');
		vtputc_highlighted('?');
		return;
	}

	if (c >= 0x80 && c <= 0xA0) {
		static const char hex[] = "0123456789abcdef";
		vtputc_highlighted('\\');
		vtputc_highlighted(hex[c >> 4]);
		vtputc_highlighted(hex[c & 15]);
		return;
	}
	
	/* Store character with highlight bit set */
	if (vtcol >= 0) {
		vp->v_text[vtcol] = c | HIGHLIGHT_BIT;
		// Mark line as changed - checksum will be updated later
		vp->v_flag |= VFCHG;
	}
	++vtcol;
}

/*
 * Erase from the end of the software cursor to the end of the line on which
 * the software cursor is located.
 */
static void vteeol(void)
{
	unicode_t *vcp = vscreen[vtrow]->v_text;

	while (vtcol < term.t_ncol)
		vcp[vtcol++] = ' ';
}

/*
 * upscreen:
 *	user routine to force a screen update
 *	always finishes complete update
 */
int upscreen(int f, int n)
{
	update(TRUE);
	return TRUE;
}

static int scrflags;

/*
 * Make sure that the display is right. This is a three part process. First,
 * scan through all of the windows looking for dirty ones. Check the framing,
 * and refresh the screen. Second, make sure that "currow" and "curcol" are
 * correct for the current window. Third, make the virtual and physical
 * screens the same.
 *
 * int force;		force update past type ahead?
 */
int update(int force)
{
	// --- PROFILING HOOK ---
	perf_start_timing("update");

	struct window *wp;
	// Defer updates during edit transactions unless explicitly forced
	if (!force && atomic_load(&edit_transaction_depth) > 0) {
		perf_end_timing("update");
		return TRUE;
	}

	// Minimize signal masking to reduce latency: block only SIGWINCH
	sigset_t oldmask, mask;
	sigemptyset(&mask);
#ifdef SIGWINCH
	sigaddset(&mask, SIGWINCH);
#endif
	pthread_sigmask(SIG_BLOCK, &mask, &oldmask);

#if	VISMAC == 0
	if (force == FALSE && kbdmode == PLAY) {
		pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
		perf_end_timing("update");
		return TRUE;
	}
#endif

	displaying = TRUE;

	/* first, propagate mode line changes to all instances of
	   a buffer displayed in more than one window */
	wp = wheadp;
	while (wp != NULL) {
		if (wp->w_flag & WFMODE) {
			if (wp->w_bufp->b_nwnd > 1) {
				/* make sure all previous windows have this */
				struct window *owp;
				owp = wheadp;
				while (owp != NULL) {
					if (owp->w_bufp == wp->w_bufp)
						owp->w_flag |= WFMODE;
					owp = owp->w_wndp;
				}
			}
		}
		wp = wp->w_wndp;
	}

	/* update any windows that need refreshing */
	wp = wheadp;
	while (wp != NULL) {
		if (wp->w_flag) {
			/* if the window has changed, service it */
			reframe(wp);	/* check the framing */
			if (wp->w_flag & (WFKILLS | WFINS)) {
				scrflags |=
				    (wp->w_flag & (WFINS | WFKILLS));
				wp->w_flag &= ~(WFKILLS | WFINS);
			}
			if ((wp->w_flag & ~WFMODE) == WFEDIT)
				updone(wp);	/* update EDITed line */
			else if (wp->w_flag & ~WFMOVE)
				updall(wp);	/* update all lines */
			if (scrflags || (wp->w_flag & WFMODE))
#if MODERN
				clean_statusline(wp);  /* clean lightline-style status line */
#else
				modeline(wp);	/* update modeline */
#endif
			wp->w_flag = 0;
			wp->w_force = 0;
		}
		/* on to the next window */
		wp = wp->w_wndp;
	}

	/* recalc the current hardware cursor location */
	updpos();


	/* check for lines to de-extend */
	upddex();

	/* if screen is garbage, re-plot it */
	if (sgarbf != FALSE)
		updgar();

	/* update the virtual screen to the physical screen */
	updupd(force);

	/* update the cursor and flush the buffers */
	movecursor(currow, curcol - lbound);
	TTflush();
	displaying = FALSE;
#if SIGWINCH
	while (chg_width || chg_height)
		newscreensize(chg_height, chg_width);
#endif
	
	// Restore signal handling after display update
	pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
	perf_end_timing("update");
	return TRUE;
}

/*
 * reframe:
 *	check to see if the cursor is on in the window
 *	and re-frame it if needed or wanted
 */
static int reframe(struct window *wp)
{
	struct line *lp, *lp0;
	int i = 0;

	/* if not a requested reframe, check for a needed one */
	if ((wp->w_flag & WFFORCE) == 0) {
		/* loop from one line above the window to one line after */
		lp = wp->w_linep;
		lp0 = lback(lp);
		if (lp0 == wp->w_bufp->b_linep)
			i = 0;
		else {
			i = -1;
			lp = lp0;
		}
		for (; i <= (int) (wp->w_ntrows); i++)
		{
			/* if the line is in the window, no reframe */
			if (lp == wp->w_dotp) {
				/* if not _quite_ in, we'll reframe gently */
				if (i < 0 || i == wp->w_ntrows) {
					/* if the terminal can't help, then
					   we're simply outside */
					if (term.t_scroll == NULL)
						i = wp->w_force;
					break;
				}
				return TRUE;
			}

			/* if we are at the end of the file, reframe */
			if (lp == wp->w_bufp->b_linep)
				break;

			/* on to the next line */
			lp = lforw(lp);
		}
	}
	if (i == -1) {		/* we're just above the window */
		i = scrollcount;	/* put dot at first line */
		scrflags |= WFINS;
	} else if (i == wp->w_ntrows) {	/* we're just below the window */
		i = -scrollcount;	/* put dot at last line */
		scrflags |= WFKILLS;
	} else			/* put dot where requested */
		i = wp->w_force;	/* (is 0, unless reposition() was called) */

	wp->w_flag |= WFMODE;

	/* how far back to reframe? */
	if (i > 0) {		/* only one screen worth of lines max */
		if (--i >= wp->w_ntrows)
			i = wp->w_ntrows - 1;
	} else if (i < 0) {	/* negative update???? */
		i += wp->w_ntrows;
		if (i < 0)
			i = 0;
	} else
		i = wp->w_ntrows / 2;

	/* backup to new line at top of window */
	lp = wp->w_dotp;
	while (i != 0 && lback(lp) != wp->w_bufp->b_linep) {
		--i;
		lp = lback(lp);
	}

	/* and reset the current line at top of window */
	wp->w_linep = lp;
	wp->w_flag |= WFHARD;
	wp->w_flag &= ~WFFORCE;
	return TRUE;
}

/* Check if a character position is within the marked region */
static int in_region(struct line *lp, int pos)
{
	struct window *wp = curwp;
	struct line *markp = wp->w_markp;
	int marko = wp->w_marko;
	struct line *dotp = wp->w_dotp;
	int doto = wp->w_doto;
	
	/* No mark set or same position - no selection */
	if (markp == NULL || (markp == dotp && marko == doto))
		return FALSE;
	
	/* For single-line selections, handle directly */
	if (markp == dotp) {
		/* Same line - only highlight between mark and cursor */
		if (lp != markp) return FALSE; /* Wrong line entirely */
		
		int start_pos = (marko < doto) ? marko : doto;
		int end_pos = (marko < doto) ? doto : marko;
		return (pos >= start_pos && pos < end_pos);
	}
	
	/* Multi-line selection - determine line order */
	struct line *start_line, *end_line;
	int start_pos, end_pos;
	int mark_before_cursor = FALSE;
	
	/* Find which line comes first in buffer order */
	struct line *scan = wp->w_bufp->b_linep;
	while ((scan = lforw(scan)) != wp->w_bufp->b_linep) {
		if (scan == markp) {
			mark_before_cursor = TRUE;
			break;
		} else if (scan == dotp) {
			mark_before_cursor = FALSE;
			break;
		}
	}
	
	if (mark_before_cursor) {
		start_line = markp; start_pos = marko;
		end_line = dotp; end_pos = doto;
	} else {
		start_line = dotp; start_pos = doto;
		end_line = markp; end_pos = marko;
	}
	
	/* Check position within multi-line selection */
	if (lp == start_line) {
		return (pos >= start_pos);
	} else if (lp == end_line) {
		return (pos < end_pos);
	} else {
		/* Check if line is between start and end */
		struct line *between_scan = start_line;
		while ((between_scan = lforw(between_scan)) != wp->w_bufp->b_linep && 
		       between_scan != end_line) {
			if (between_scan == lp)
				return TRUE;
		}
		return FALSE;
	}
}

static void show_line(struct line *lp)
{
	int i = 0, len = llength(lp);
	int in_selection = FALSE;
	
	/* Only apply selection highlighting to content lines in the current window */
	/* Also ensure this line belongs to the current window's buffer */
	int apply_highlighting = (curwp != NULL && curwp->w_markp != NULL && 
	                         lp != NULL && lp != curwp->w_bufp->b_linep);

	while (i < len) {
		unicode_t c;
		int bytes = utf8_to_unicode(lp->l_text, i, len, &c);
		int char_in_selection = apply_highlighting ? in_region(lp, i) : FALSE;
		
		/* Apply highlighting by modifying character representation */
		if (char_in_selection != in_selection) {
			in_selection = char_in_selection;
		}
		
		// Filter control characters that corrupt terminal display
		if (c == '\r') {
			// Skip carriage returns - they show as ^M and corrupt display
			i += bytes;
			continue;
		}
		if (c < 32 && c != '\t') {
			// Display other control chars as printable to avoid corruption
			if (in_selection) {
				vtputc_highlighted('^');
				vtputc_highlighted('@' + c);
			} else {
				vtputc('^');
				vtputc('@' + c);
			}
		} else {
			if (in_selection) {
				vtputc_highlighted(c);
			} else {
				vtputc(c);
			}
		}
		i += bytes;
	}
}

/*
 * updone:
 *	update the current line	to the virtual screen
 *
 * struct window *wp;		window to update current line in
 */
static void updone(struct window *wp)
{
	struct line *lp;	/* line to update */
	int sline;	/* physical screen line to update */

	/* search down the line we want */
	lp = wp->w_linep;
	sline = wp->w_toprow;
	while (lp != wp->w_dotp) {
		++sline;
		lp = lforw(lp);
	}

	/* and update the virtual line */
	vscreen[sline]->v_flag |= VFCHG;
	vscreen[sline]->v_flag &= ~VFREQ;
	vtmove(sline, 0);
	show_line(lp);
	vscreen[sline]->v_rfcolor = wp->w_fcolor;
	vscreen[sline]->v_rbcolor = wp->w_bcolor;
	vteeol();
}

/*
 * updall:
 *	update all the lines in a window on the virtual screen
 *
 * struct window *wp;		window to update lines in
 */
static void updall(struct window *wp)
{
	struct line *lp;	/* line to update */
	int sline;	/* physical screen line to update */

	/* search down the lines, updating them */
	lp = wp->w_linep;
	sline = wp->w_toprow;
	while (sline < wp->w_toprow + wp->w_ntrows) {

		/* and update the virtual line */
		vscreen[sline]->v_flag |= VFCHG;
		vscreen[sline]->v_flag &= ~VFREQ;
		vtmove(sline, 0);
		if (lp != wp->w_bufp->b_linep) {
			/* if we are not at the end */
			show_line(lp);
			lp = lforw(lp);
		}

		/* on to the next one */
		vscreen[sline]->v_rfcolor = wp->w_fcolor;
		vscreen[sline]->v_rbcolor = wp->w_bcolor;
		vteeol();
		++sline;
	}

}

/*
 * updpos:
 *	update the position of the hardware cursor and handle extended
 *	lines. This is the only update for simple moves.
 */
void updpos(void)
{
	struct line *lp;
	int i;

	/* find the current row */
	lp = curwp->w_linep;
	currow = curwp->w_toprow;
	while (lp != curwp->w_dotp) {
		++currow;
		lp = lforw(lp);
	}

	/* find the current column using cached UTF-8 display width */
	curcol = calculate_display_column_cached(curwp->w_dotp, curwp->w_doto, 8);

	/* if extended, flag so and update the virtual line image */
	if (curcol >= term.t_ncol - 1) {
		vscreen[currow]->v_flag |= (VFEXT | VFCHG);
		updext();
	} else
		lbound = 0;
}

/*
 * Atomic line number cache for instant status line updates
 */
int get_line_number_cached(struct window *wp)
{
	if (!wp || !wp->w_dotp) return 1;
	
	// Check if cache is clean and current
	bool cache_dirty = atomic_load(&wp->w_line_cache_dirty);
	if (!cache_dirty) {
		int cached_line = atomic_load(&wp->w_line_cache);
		if (cached_line > 0) return cached_line;  // Ensure never return 0
	}
	
	// Cache miss - calculate from scratch
	int current_line = 1;
	struct line *lp = wp->w_bufp->b_linep->l_fp;
	while (lp != wp->w_dotp && lp != wp->w_bufp->b_linep) {
		current_line++;
		lp = lp->l_fp;
	}
	
	// Ensure we never cache or return 0
	if (current_line <= 0) current_line = 1;
	
	// Update cache atomically
	atomic_store(&wp->w_line_cache, current_line);
	atomic_store(&wp->w_line_cache_dirty, false);
	
	return current_line;
}

void invalidate_line_cache(struct window *wp)
{
	if (wp) {
		atomic_store(&wp->w_line_cache_dirty, true);
	}
}

/*
 * upddex:
 *	de-extend any line that derserves it
 */
void upddex(void)
{
	struct window *wp;
	struct line *lp;
	int i;

	wp = wheadp;

	while (wp != NULL) {
		lp = wp->w_linep;
		i = wp->w_toprow;

		while (i < wp->w_toprow + wp->w_ntrows) {
			if (vscreen[i]->v_flag & VFEXT) {
				if ((wp != curwp) || (lp != wp->w_dotp) ||
				    (curcol < term.t_ncol - 1)) {
					vtmove(i, 0);
					show_line(lp);
					vteeol();

					/* this line no longer is extended */
					vscreen[i]->v_flag &= ~VFEXT;
					vscreen[i]->v_flag |= VFCHG;
				}
			}
			lp = lforw(lp);
			++i;
		}
		/* and onward to the next window */
		wp = wp->w_wndp;
	}
}

/*
 * updgar:
 *	if the screen is garbage, clear the physical screen and
 *	the virtual screen and force a full update
 */
void updgar(void)
{
	unicode_t *txt;
	int i, j;

	for (i = 0; i < term.t_nrow; ++i) {
		vscreen[i]->v_flag |= VFCHG;
#if	REVSTA
		vscreen[i]->v_flag &= ~VFREV;
#endif
		vscreen[i]->v_fcolor = gfcolor;
		vscreen[i]->v_bcolor = gbcolor;
#if	MEMMAP == 0 || SCROLLCODE
		txt = pscreen[i]->v_text;
		for (j = 0; j < term.t_ncol; ++j)
			txt[j] = ' ';
#endif
	}

	movecursor(0, 0);	/* Erase the screen. */
	(*term.t_eeop) ();
	sgarbf = FALSE;		/* Erase-page clears */
	mpresf = FALSE;		/* the message area. */
	mlerase();		/* needs to be cleared if colored */
}

/*
 * updupd:
 *	update the physical screen from the virtual screen
 *
 * int force;		forced update flag
 */
int updupd(int force)
{
	struct video *vp1;
	int i;

	if (scrflags & WFKILLS)
		scrolls(FALSE);
	if (scrflags & WFINS)
		scrolls(TRUE);
	scrflags = 0;

	for (i = 0; i < term.t_nrow; ++i) {
		vp1 = vscreen[i];

		/* for each line that needs to be updated */
		if ((vp1->v_flag & VFCHG) != 0) {
			// Update checksum after content changes
			video_update_checksum(vp1);
			
			// Use checksum-optimized comparison before expensive update
			if (!force && !video_lines_differ(vp1, pscreen[i])) {
				vp1->v_flag &= ~VFCHG; // Clear change flag - lines are identical
			} else {
				updateline(i, vp1, pscreen[i]);
			}
		}
	}
	return TRUE;
}

#if SCROLLCODE

/*
 * optimize out scrolls (line breaks, and newlines)
 * arg. chooses between looking for inserts or deletes
 */
static int scrolls(int inserts)
{				/* returns true if it does something */
	struct video *vpv;	/* virtual screen image */
	struct video *vpp;	/* physical screen image */
	int i, j, k;
	int rows, cols;
	int first, match, count, target, end;
	int longmatch, longcount;
	int from, to;

	if (!term.t_scroll)	/* no way to scroll */
		return FALSE;

	rows = term.t_nrow;
	cols = term.t_ncol;

	first = -1;
	for (i = 0; i < rows; i++) {	/* find first wrong line */
		if (!texttest(i, i)) {
			first = i;
			break;
		}
	}

	if (first < 0)
		return FALSE;	/* no text changes */

	vpv = vscreen[first];
	vpp = pscreen[first];

	if (inserts) {
		/* determine types of potential scrolls */
		end = endofline(vpv->v_text, cols);
		if (end == 0)
			target = first;	/* newlines */
		else if (memcmp(vpp->v_text, vpv->v_text, 4*end) == 0)
			target = first + 1;	/* broken line newlines */
		else
			target = first;
	} else {
		target = first + 1;
	}

	/* find the matching shifted area */
	match = -1;
	longmatch = -1;
	longcount = 0;
	from = target;
	for (i = from + 1; i < rows - longcount /* P.K. */ ; i++) {
		if (inserts ? texttest(i, from) : texttest(from, i)) {
			match = i;
			count = 1;
			for (j = match + 1, k = from + 1;
			     j < rows && k < rows; j++, k++) {
				if (inserts ? texttest(j, k) :
				    texttest(k, j))
					count++;
				else
					break;
			}
			if (longcount < count) {
				longcount = count;
				longmatch = match;
			}
		}
	}
	match = longmatch;
	count = longcount;

	if (!inserts) {
		/* full kill case? */
		if (match > 0 && texttest(first, match - 1)) {
			target--;
			match--;
			count++;
		}
	}

	/* do the scroll */
	if (match > 0 && count > 2) {	/* got a scroll */
		/* move the count lines starting at target to match */
		if (inserts) {
			from = target;
			to = match;
		} else {
			from = match;
			to = target;
		}
		if (2 * count < abs(from - to))
			return FALSE;
		scrscroll(from, to, count);
		for (i = 0; i < count; i++) {
			vpp = pscreen[to + i];
			vpv = vscreen[to + i];
			memcpy(vpp->v_text, vpv->v_text, 4*cols);
			vpp->v_flag = vpv->v_flag;	/* XXX */
			if (vpp->v_flag & VFREV) {
				vpp->v_flag &= ~VFREV;
				vpp->v_flag |= ~VFREQ;
			}
#if	MEMMAP
			vscreen[to + i]->v_flag &= ~VFCHG;
#endif
		}
		if (inserts) {
			from = target;
			to = match;
		} else {
			from = target + count;
			to = match + count;
		}
#if	MEMMAP == 0
		for (i = from; i < to; i++) {
			unicode_t *txt;
			txt = pscreen[i]->v_text;
			for (j = 0; j < term.t_ncol; ++j)
				txt[j] = ' ';
			vscreen[i]->v_flag |= VFCHG;
		}
#endif
		return TRUE;
	}
	return FALSE;
}

/* move the "count" lines starting at "from" to "to" */
static void scrscroll(int from, int to, int count)
{
	ttrow = ttcol = -1;
	(*term.t_scroll) (from, to, count);
}

/*
 * return TRUE on text match
 *
 * int vrow, prow;		virtual, physical rows
 */
static int texttest(int vrow, int prow)
{
	struct video *vpv = vscreen[vrow];	/* virtual screen image */
	struct video *vpp = pscreen[prow];	/* physical screen image */

	return !memcmp(vpv->v_text, vpp->v_text, 4*term.t_ncol);
}

/*
 * return the index of the first blank of trailing whitespace
 */
static int endofline(unicode_t *s, int n)
{
	int i;
	for (i = n - 1; i >= 0; i--)
		if (s[i] != ' ')
			return i + 1;
	return 0;
}

#endif				/* SCROLLCODE */

/*
 * updext:
 *	update the extended line which the cursor is currently
 *	on at a column greater than the terminal width. The line
 *	will be scrolled right or left to let the user see where
 *	the cursor is
 */
static void updext(void)
{
	int rcursor;	/* real cursor location */
	struct line *lp;	/* pointer to current line */

	/* calculate what column the real cursor will end up in */
	rcursor = ((curcol - term.t_ncol) % term.t_scrsiz) + term.t_margin;
	taboff = lbound = curcol - rcursor + 1;

	/* scan through the line outputing characters to the virtual screen */
	/* once we reach the left edge                                  */
	vtmove(currow, -lbound);	/* start scanning offscreen */
	lp = curwp->w_dotp;	/* line to output */
	show_line(lp);

	/* truncate the virtual line, restore tab offset */
	vteeol();
	taboff = 0;

	/* and put a '$' in column 1 */
	vscreen[currow]->v_text[0] = '$';
}

/*
 * Update a single line. This does not know how to use insert or delete
 * character sequences; we are using VT52 functionality. Update the physical
 * row and column variables. It does try an exploit erase to end of line. The
 * RAINBOW version of this routine uses fast video.
 */
#if	MEMMAP
/*	UPDATELINE specific code for the IBM-PC and other compatables */

static int updateline(int row, struct video *vp1, struct video *vp2)
{
#if	SCROLLCODE
	unicode_t *cp1;
	unicode_t *cp2;
	int nch;

	cp1 = &vp1->v_text[0];
	cp2 = &vp2->v_text[0];
	nch = term.t_ncol;
	do {
		*cp2 = *cp1;
		++cp2;
		++cp1;
	}
	while (--nch);
#endif
#if	COLOR
	scwrite(row, vp1->v_text, vp1->v_rfcolor, vp1->v_rbcolor);
	vp1->v_fcolor = vp1->v_rfcolor;
	vp1->v_bcolor = vp1->v_rbcolor;
#else
	if (vp1->v_flag & VFREQ)
		scwrite(row, vp1->v_text, 0, 7);
	else
		scwrite(row, vp1->v_text, 7, 0);
#endif
	vp1->v_flag &= ~(VFCHG | VFCOL);	/* flag this line as changed */

}

#else

/*
 * updateline()
 *
 * int row;		row of screen to update
 * struct video *vp1;	virtual screen image
 * struct video *vp2;	physical screen image
 */
static int updateline(int row, struct video *vp1, struct video *vp2)
{
#if RAINBOW
/*	UPDATELINE specific code for the DEC rainbow 100 micro	*/

	unicode_t *cp1;
	unicode_t *cp2;
	int nch;

	/* since we don't know how to make the rainbow do this, turn it off */
	flags &= (~VFREV & ~VFREQ);

	cp1 = &vp1->v_text[0];	/* Use fast video. */
	cp2 = &vp2->v_text[0];
	putline(row + 1, 1, cp1);
	nch = term.t_ncol;

	do {
		*cp2 = *cp1;
		++cp2;
		++cp1;
	}
	while (--nch);
	*flags &= ~VFCHG;
#else
/*	UPDATELINE code for all other versions		*/

	unicode_t *cp1;
	unicode_t *cp2;
	unicode_t *cp3;
	unicode_t *cp4;
	unicode_t *cp5;
	int nbflag;	/* non-blanks to the right flag? */
	int rev;		/* reverse video flag */
	int req;		/* reverse video request flag */


	/* set up pointers to virtual and physical lines */
	cp1 = &vp1->v_text[0];
	cp2 = &vp2->v_text[0];

	TTforg(vp1->v_rfcolor);
	TTbacg(vp1->v_rbcolor);

#if	REVSTA
	/* if we need to change the reverse video status of the
	   current line, we need to re-write the entire line     */
	rev = (vp1->v_flag & VFREV) == VFREV;
	req = (vp1->v_flag & VFREQ) == VFREQ;
	if ((rev != req)
	    || (vp1->v_fcolor != vp1->v_rfcolor)
	    || (vp1->v_bcolor != vp1->v_rbcolor)
	    ) {
		movecursor(row, 0);	/* Go to start of line. */
		/* set rev video if needed */
		if (rev != req)
			(*term.t_rev) (req);

		/* scan through the line and dump it to the screen and
		   the virtual screen array                             */
		cp3 = &vp1->v_text[term.t_ncol];
		int current_reverse = req;
		while (cp1 < cp3) {
			int highlighted = (*cp1 & HIGHLIGHT_BIT) != 0;
			unicode_t ch = *cp1 & ~HIGHLIGHT_BIT;
			
			/* Toggle reverse video if highlight state changes */
			if (highlighted != current_reverse) {
				current_reverse = highlighted;
				(*term.t_rev)(current_reverse);
			}
			
			TTputc(ch);
			++ttcol;
			*cp2++ = *cp1++;
		}
		
		/* Ensure reverse video is properly reset at end of line */
		if (current_reverse) {
			(*term.t_rev)(FALSE);
		}
		/* turn rev video off */
		if (rev != req)
			(*term.t_rev) (FALSE);

		/* update the needed flags */
		vp1->v_flag &= ~VFCHG;
		if (req)
			vp1->v_flag |= VFREV;
		else
			vp1->v_flag &= ~VFREV;
		vp1->v_fcolor = vp1->v_rfcolor;
		vp1->v_bcolor = vp1->v_rbcolor;
		return TRUE;
	}
#endif

	/* advance past any common chars at the left */
	while (cp1 != &vp1->v_text[term.t_ncol] && cp1[0] == cp2[0]) {
		++cp1;
		++cp2;
	}

/* This can still happen, even though we only call this routine on changed
 * lines. A hard update is always done when a line splits, a massive
 * change is done, or a buffer is displayed twice. This optimizes out most
 * of the excess updating. A lot of computes are used, but these tend to
 * be hard operations that do a lot of update, so I don't really care.
 */
	/* if both lines are the same, no update needs to be done */
	if (cp1 == &vp1->v_text[term.t_ncol]) {
		vp1->v_flag &= ~VFCHG;	/* flag this line is changed */
		return TRUE;
	}

	/* find out if there is a match on the right */
	nbflag = FALSE;
	cp3 = &vp1->v_text[term.t_ncol];
	cp4 = &vp2->v_text[term.t_ncol];

	while (cp3[-1] == cp4[-1]) {
		--cp3;
		--cp4;
		if (cp3[0] != ' ')	/* Note if any nonblank */
			nbflag = TRUE;	/* in right match. */
	}

	cp5 = cp3;

	/* Erase to EOL ? */
	if (nbflag == FALSE && eolexist == TRUE && (req != TRUE)) {
		while (cp5 != cp1 && cp5[-1] == ' ')
			--cp5;

		if (cp3 - cp5 <= 3)	/* Use only if erase is */
			cp5 = cp3;	/* fewer characters. */
	}

	movecursor(row, cp1 - &vp1->v_text[0]);	/* Go to start of line. */
#if	REVSTA
	TTrev(rev);
#endif

	{
		int current_reverse = FALSE;
		while (cp1 != cp5) {	/* Ordinary. */
			int highlighted = (*cp1 & HIGHLIGHT_BIT) != 0;
			unicode_t ch = *cp1 & ~HIGHLIGHT_BIT;
			
			/* Toggle reverse video only when state changes */
			if (highlighted != current_reverse) {
				current_reverse = highlighted;
				TTrev(current_reverse);
			}
			
			TTputc(ch);
			++ttcol;
			*cp2++ = *cp1++;
		}
		
		/* Ensure reverse video is off at end */
		if (current_reverse) {
			TTrev(FALSE);
		}
	}

	if (cp5 != cp3) {	/* Erase. */
		TTeeol();
		while (cp1 != cp3)
			*cp2++ = *cp1++;
	}
#if	REVSTA
	TTrev(FALSE);
#endif
	vp1->v_flag &= ~VFCHG;	/* flag this line as updated */
	
	// Update physical screen checksum after copying data
	video_update_checksum(vp2);
	
	return TRUE;
#endif
}
#endif

/*
 * Redisplay the mode line for the window pointed to by the "wp". This is the
 * only routine that has any idea of how the modeline is formatted. You can
 * change the modeline format by hacking at this routine. Called by "update"
 * any time there is a dirty window.
 */
static void modeline(struct window *wp)
{
	char *cp;
	int c;
	int n;		/* cursor position count */
	struct buffer *bp;
	int i;		/* loop index */
	int lchar;	/* character to draw line in buffer with */
	int firstm;	/* is this the first mode? */
	char tline[NLINE];	/* buffer for part of mode line */

	n = wp->w_toprow + wp->w_ntrows;	/* Location. */
	// Bounds check for vscreen array access
	if (n >= term.t_mrow) {
		return;  // Safety: don't access beyond vscreen bounds
	}
	vscreen[n]->v_flag |= VFCHG | VFREQ | VFCOL;	/* Redraw next time. */
	// Don't force statusline colors - use terminal defaults
	vtmove(n, 0);		/* Seek to right line. */
	if (wp == curwp)	/* mark the current buffer */
		lchar = '-';
	else
#if	REVSTA
	if (revexist)
		lchar = ' ';
	else
#endif
		lchar = '-';

	bp = wp->w_bufp;
		vtputc(lchar);

	if ((bp->b_flag & BFCHG) != 0)	/* "*" if changed. */
		vtputc('*');
	else
		vtputc(lchar);

	n = 2;

	safe_strcpy(tline, " ", sizeof(tline));
	safe_strcat(tline, PROGRAM_NAME_LONG, sizeof(tline));
	safe_strcat(tline, " ", sizeof(tline));
	safe_strcat(tline, VERSION, sizeof(tline));
	safe_strcat(tline, ": ", sizeof(tline));
	cp = &tline[0];
	while ((c = *cp++) != 0) {
		vtputc(c);
		++n;
	}

	cp = &bp->b_bname[0];
	while ((c = *cp++) != 0) {
		vtputc(c);
		++n;
	}

	safe_strcpy(tline, " (", sizeof(tline));

	/* display the modes */

	firstm = TRUE;
	if ((bp->b_flag & BFTRUNC) != 0) {
		firstm = FALSE;
        safe_strcat(tline, "Truncated", sizeof(tline));
	}
	for (i = 0; i < NUMMODES; i++)	/* add in the mode flags */
		if (wp->w_bufp->b_mode & (1 << i)) {
			if (firstm != TRUE)
                safe_strcat(tline, " ", sizeof(tline));
			firstm = FALSE;
                safe_strcat(tline, mode2name[i], sizeof(tline));
		}
    safe_strcat(tline, ") ", sizeof(tline));

	cp = &tline[0];
	while ((c = *cp++) != 0) {
		vtputc(c);
		++n;
	}

	if (bp->b_fname[0] != 0 && strcmp(bp->b_bname, bp->b_fname) != 0)
	{

		cp = &bp->b_fname[0];

		while ((c = *cp++) != 0) {
			vtputc(c);
			++n;
		}

		vtputc(' ');
		++n;
	}

	// Instant status line statistics using cached values
	char info_buffer[128];
	int current_line = 1;
	int total_lines;
	long file_bytes;
	int word_count;
	
	// Fast line number calculation - only count to current position
	struct line *lp = bp->b_linep;
	while ((lp = lforw(lp)) != wp->w_dotp && lp != bp->b_linep)
		current_line++;
	
	// Get cached file statistics instantly (O(1) operation)
	buffer_get_stats_fast(bp, &total_lines, &file_bytes, &word_count);
	
	// Fast UTF-8 aware column calculation using atomic cache
	int current_col = calculate_display_column_cached(wp->w_dotp, wp->w_doto, 8) + 1;
	
	// Format complete status info using cached statistics
	if (file_bytes >= 1024*1024) {
		safe_snprintf(info_buffer, sizeof(info_buffer), " C%d L%d/%d %.1fMB %dW ",
			current_col, current_line, total_lines, file_bytes / (1024.0*1024.0), word_count);
	} else if (file_bytes >= 1024) {
		safe_snprintf(info_buffer, sizeof(info_buffer), " C%d L%d/%d %.1fKB %dW ",
			current_col, current_line, total_lines, file_bytes / 1024.0, word_count);
	} else {
		safe_snprintf(info_buffer, sizeof(info_buffer), " C%d L%d/%d %ldB %dW ",
			current_col, current_line, total_lines, file_bytes, word_count);
	}
	
	int info_len = strlen(info_buffer);
	int padding = term.t_ncol - n - info_len;
	
	// Pad with line characters, leaving space for info
	for (int i = 0; i < padding && n < term.t_ncol; i++) {
		vtputc(lchar);
		++n;
	}
	
	// Add the dynamic info
	cp = info_buffer;
	while ((c = *cp++) != 0 && n < term.t_ncol) {
		vtputc(c);
		++n;
	}
	
	while (n < term.t_ncol) {	/* Final padding if needed */
		vtputc(lchar);
		++n;
	}

	{			/* determine if top line, bottom line, or both are visible */
		struct line *lp = wp->w_linep;
		int rows = wp->w_ntrows;
		char *msg = NULL;

		vtcol = n - 7;	/* strlen(" top ") plus a couple */
		while (rows--) {
			lp = lforw(lp);
			if (lp == wp->w_bufp->b_linep) {
				msg = " Bot ";
				break;
			}
		}
		if (lback(wp->w_linep) == wp->w_bufp->b_linep) {
			if (msg) {
				if (wp->w_linep == wp->w_bufp->b_linep)
					msg = " Emp ";
				else
					msg = " All ";
			} else {
				msg = " Top ";
			}
		}
		if (!msg) {
			struct line *lp;
			int numlines, predlines, ratio;

			lp = lforw(bp->b_linep);
			numlines = 0;
			predlines = 0;
			while (lp != bp->b_linep) {
				if (lp == wp->w_linep) {
					predlines = numlines;
				}
				++numlines;
				lp = lforw(lp);
			}
			if (wp->w_dotp == bp->b_linep) {
				msg = " Bot ";
			} else {
				ratio = 0;
				if (numlines != 0)
					ratio =
					    (100L * predlines) / numlines;
				if (ratio > 99)
					ratio = 99;
				safe_snprintf(tline, sizeof(tline), " %2d%% ", ratio);
				msg = tline;
			}
		}

		cp = msg;
		while ((c = *cp++) != 0) {
			vtputc(c);
			++n;
		}
	}
}

void upmode(void)
{				/* update all the mode lines */
	struct window *wp;

	wp = wheadp;
	while (wp != NULL) {
		wp->w_flag |= WFMODE;
		wp = wp->w_wndp;
	}
}

/*
 * Send a command to the terminal to move the hardware cursor to row "row"
 * and column "col". The row and column arguments are origin 0. Optimize out
 * random calls. Update "ttrow" and "ttcol".
 */
void movecursor(int row, int col)
{
	// CRITICAL FIX: Signal masking during cursor coordinate updates
	// Prevents SIGWINCH and other interrupts from corrupting cursor position
	sigset_t oldmask, mask;
	sigfillset(&mask);
	pthread_sigmask(SIG_BLOCK, &mask, &oldmask);
	
	if (row != ttrow || col != ttcol) {
		ttrow = row;
		ttcol = col;
		TTmove(row, col);
	}
	
	pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
}

/*
 * Erase the message line. This is a special routine because the message line
 * is not considered to be part of the virtual screen. It always works
 * immediately; the terminal buffer is flushed via a call to the flusher.
 */
void mlerase(void)
{
	int i;

	movecursor(term.t_nrow, 0);
	if (discmd == FALSE)
		return;

	TTforg(7);
	TTbacg(0);
	if (eolexist == TRUE)
		TTeeol();
	else {
		for (i = 0; i < term.t_ncol - 1; i++)
			TTputc(' ');
		movecursor(term.t_nrow, 1);	/* force the move! */
		movecursor(term.t_nrow, 0);
	}
	TTflush();
	mpresf = FALSE;
}

/*
 * Write a message into the message line. Keep track of the physical cursor
 * position. A small class of printf like format items is handled. Assumes the
 * stack grows down; this assumption is made by the "++" in the argument scan
 * loop. Set the "message line" flag TRUE.
 *
 * char *fmt;		format string for output
 * char *arg;		pointer to first argument to print
 */
void mlwrite(const char *fmt, ...)
{
	int c;		/* current char in format string */
	va_list ap;

	/* if we are not currently echoing on the command line, abort this */
	if (discmd == FALSE) {
		movecursor(term.t_nrow, 0);
		return;
	}
	/* set up the proper colors for the command line */
	TTforg(7);
	TTbacg(0);

	/* if we can not erase to end-of-line, do it manually */
	if (eolexist == FALSE) {
		mlerase();
		TTflush();
	}

	movecursor(term.t_nrow, 0);
	va_start(ap, fmt);
	while ((c = *fmt++) != 0) {
		if (c != '%') {
			TTputc(c);
			++ttcol;
		} else {
			c = *fmt++;
			switch (c) {
			case 'd':
				mlputi(va_arg(ap, int), 10);
				break;

			case 'o':
				mlputi(va_arg(ap, int), 8);
				break;

			case 'x':
				mlputi(va_arg(ap, int), 16);
				break;

			case 'D':
				mlputli(va_arg(ap, long), 10);
				break;

			case 's':
				mlputs(va_arg(ap, char *));
				break;

			case 'f':
				mlputf(va_arg(ap, int));
				break;

			default:
				TTputc(c);
				++ttcol;
			}
		}
	}
	va_end(ap);

	/* if we can, erase to the end of screen */
	if (eolexist == TRUE)
		TTeeol();
	TTflush();
	mpresf = TRUE;
}

/*
 * Force a string out to the message line regardless of the
 * current $discmd setting. This is needed when $debug is TRUE
 * and for the write-message and clear-message-line commands
 *
 * char *s;		string to force out
 */
void mlforce(const char *s)
{
	int oldcmd;	/* original command display flag */

	oldcmd = discmd;	/* save the discmd value */
	discmd = TRUE;		/* and turn display on */
	mlwrite(s);		/* write the string out */
	discmd = oldcmd;	/* and restore the original setting */
}

/*
 * Write out a string. Update the physical cursor position. This assumes that
 * the characters in the string all have width "1"; if this is not the case
 * things will get screwed up a little.
 */
void mlputs(const char *s)
{
	int c;

	while ((c = *s++) != 0) {
		TTputc(c);
		++ttcol;
	}
}

/*
 * Write out an integer, in the specified radix. Update the physical cursor
 * position.
 */
static void mlputi(int i, int r)
{
	int q;
	static char hexdigits[] = "0123456789ABCDEF";

	if (i < 0) {
		i = -i;
		TTputc('-');
	}

	q = i / r;

	if (q != 0)
		mlputi(q, r);

	TTputc(hexdigits[i % r]);
	++ttcol;
}

/*
 * do the same except as a long integer.
 */
static void mlputli(long l, int r)
{
	long q;

	if (l < 0) {
		l = -l;
		TTputc('-');
	}

	q = l / r;

	if (q != 0)
		mlputli(q, r);

	TTputc((int) (l % r) + '0');
	++ttcol;
}

/*
 * write out a scaled integer with two decimal places
 *
 * int s;		scaled integer to output
 */
static void mlputf(int s)
{
	int i;			/* integer portion of number */
	int f;			/* fractional portion of number */

	/* break it up */
	i = s / 100;
	f = s % 100;

	/* send out the integer portion */
	mlputi(i, 10);
	TTputc('.');
	TTputc((f / 10) + '0');
	TTputc((f % 10) + '0');
	ttcol += 3;
}

#if RAINBOW

static void putline(int row, int col, char *buf)
{
	int n;

	n = strlen(buf);
	if (col + n - 1 > term.t_ncol)
		n = term.t_ncol - col + 1;
	Put_Data(row, col, n, buf);
}
#endif

/* Get terminal size from system.
   Store number of lines into *heightp and width into *widthp.
   If zero or a negative number is stored, the value is not valid.  */

void getscreensize(int *widthp, int *heightp)
{
#ifdef TIOCGWINSZ
	struct winsize size;
	*widthp = 0;
	*heightp = 0;
	if (ioctl(0, TIOCGWINSZ, &size) < 0)
		return;
	*widthp = size.ws_col;
	*heightp = size.ws_row;
#else
	*widthp = 0;
	*heightp = 0;
#endif
}

#ifdef SIGWINCH
// Global flag for async-signal-safe resize handling
static volatile sig_atomic_t resize_flag = 0;

void sizesignal(int signr)
{
	// ASYNC-SIGNAL-SAFE: Only set a flag in signal handler
	resize_flag = 1;
	signal(SIGWINCH, sizesignal);
}

// Check and handle pending resize - called from main loop
void check_pending_resize(void)
{
    if (resize_flag) {
        // Defer resize while an edit transaction is open
        if (atomic_load(&edit_transaction_depth) > 0) {
            return;
        }
        resize_flag = 0;
        int w, h;
        getscreensize(&w, &h);
        if (h && w && (h - 1 != term.t_nrow || w != term.t_ncol))
            newscreensize(h, w);
    }
}
#endif

static int newscreensize(int h, int w)
{
	/* do the change later */
	if (displaying) {
		chg_width = w;
		chg_height = h;
		return FALSE;
	}
	chg_width = chg_height = 0;
	if (h - 1 < term.t_mrow)
		newsize(TRUE, h);
	if (w < term.t_mcol)
		newwidth(TRUE, w);

	update(TRUE);
	return TRUE;
}

#if MODERN
/*
 * Modern unified status line matching user's Kitty+Vim+Bash aesthetic
 * Format: ● MODE │ filename │ type │ encoding │ modified │ size │ position │ time
 * Uses terminal colors and Unicode symbols for ecosystem consistency
 */

/* Helper function to count total lines in buffer */
static int getlinecount_modern(struct buffer *bp)
{
	struct line *lp;
	int count = 0;
	
	lp = lforw(bp->b_linep);
	while (lp != bp->b_linep) {
		count++;
		lp = lforw(lp);
	}
	return count;
}

// Clean statusline matching user's lightline format
static void clean_statusline(struct window *wp)
{

	char left_info[192];
	char right_info[128];
	struct buffer *bp = wp->w_bufp;
	int n = wp->w_toprow + wp->w_ntrows;
	int col_pos = 0;
	char *cp;

	// Bounds check for vscreen array access  
	if (n >= term.t_mrow) {
		// Debug info for boundary violation
		fprintf(stderr, "[DEBUG] clean_statusline: n=%d, t_mrow=%d, toprow=%d, ntrows=%d\n", 
		        n, term.t_mrow, wp->w_toprow, wp->w_ntrows);
		return;  // Safety: don't access beyond vscreen bounds
	}

	// Set up virtual screen without reverse video flags to avoid color conflicts
	vscreen[n]->v_flag |= VFCHG;
	vtmove(n, 0);

	// Use direct ANSI escape sequences for consistent status line colors
	ttputc('\033');
	ttputc('[');
	ttputc('7');  // Reverse video
	ttputc('m');

	// Get cached file statistics instantly (O(1) operation)
	long file_size;
	int total_lines;
	int word_count;
	buffer_get_stats_fast(bp, &total_lines, &file_size, &word_count);

	// Get cached line number instantly (O(1) operation)
	int current_line = get_line_number_cached(wp);

	// Format left: FILENAME  TYPE  ENCODING  [MODIFIED] (with leading spacing buffer)
	// Delta symbol for modified files - modern Unicode modification indicator
	const char *mod_indicator = (bp->b_flag & BFCHG) ? "  Δ" : "";

	// --- GIT STATUS INTEGRATION ---
	char git_info[64] = "";
	git_status_request_async(NULL); // non-blocking, throttled
	if (getenv("UEMACS_GIT_STATUS")) {
		git_status_get_cached(git_info, sizeof(git_info));
	}

	// Compose left_info with git
	if (git_info[0]) {
		safe_snprintf(left_info, sizeof(left_info), "   %s  %s  Text  UTF-8%s", \
			bp->b_fname[0] ? bp->b_fname : bp->b_bname, git_info, mod_indicator);
	} else {
		safe_snprintf(left_info, sizeof(left_info), "   %s  Text  UTF-8%s", \
			bp->b_fname[0] ? bp->b_fname : bp->b_bname, mod_indicator);
	}

	// Format right: C{COL} L{LINE}/{TOTAL}  {SIZE} {WORDS}
	char size_str[32];
	if (file_size < 1024) {
		snprintf(size_str, sizeof(size_str), "%ldB", file_size);
	} else if (file_size < 1048576) {
		snprintf(size_str, sizeof(size_str), "%.2fKB", file_size / 1024.0);
	} else if (file_size < 1073741824) {
		snprintf(size_str, sizeof(size_str), "%.2fMB", file_size / 1048576.0);
	} else if (file_size < 1099511627776LL) {
		snprintf(size_str, sizeof(size_str), "%.2fGB", file_size / 1073741824.0);
	} else {
		snprintf(size_str, sizeof(size_str), "%.2fTB", file_size / 1099511627776.0);
	}

	// Fast UTF-8 aware column calculation using atomic cache
	int current_col = calculate_display_column_cached(wp->w_dotp, wp->w_doto, 8) + 1;

	safe_snprintf(right_info, sizeof(right_info), "C%d L%d/%d  %s %dW   ",
		current_col, current_line, total_lines, size_str, word_count);

	// Display left info with proper UTF-8 handling for delta symbol
	int right_len = strlen(right_info);
	int i = 0;
	int left_len = strlen(left_info);
	while (i < left_len && col_pos < term.t_ncol - right_len - 1) {
		unicode_t c;
		int bytes = utf8_to_unicode(left_info, i, left_len, &c);
		if (bytes > 0) {
			vtputc(c);
			i += bytes;
			col_pos++;
		} else {
			break;
		}
	}

	// Fill middle with spaces
	while (col_pos < term.t_ncol - right_len) {
		vtputc(' ');
		col_pos++;
	}

	// Display right info
	cp = right_info;
	while (*cp && col_pos < term.t_ncol) {
		vtputc(*cp++);
		col_pos++;
	}

	// Fill remainder
	while (col_pos < term.t_ncol) {
		vtputc(' ');
		col_pos++;
	}

	// Reset reverse video directly with ANSI escape sequence
	ttputc('\033');
	ttputc('[');
	ttputc('2');
	ttputc('7');  // Reset reverse video
	ttputc('m');
}

#endif /* MODERN */
