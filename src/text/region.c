/*	region.c
 *
 *      The routines in this file deal with the region, that magic space
 *      between "." and mark. Some functions are commands. Some functions are
 *      just for internal use.
 *
 *	Modified by Petri Kutvonen
 */

#include <stdio.h>
#include <stdbool.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "memory.h"
#include "clipboard.h"

/* Clear the visual selection mark */
static void clear_selection(void)
{
	if (curwp != nullptr) {
		curwp->w_markp = nullptr;
		curwp->w_marko = 0;
		/* Force screen update to clear highlighting */
		curwp->w_flag |= WFHARD;
	}
}

/*
 * Kill the region. Ask "getregion"
 * to figure out the bounds of the region.
 * Move "." to the start, and kill the characters.
 * Bound to "C-W".
 */
int region_kill(int f, int n)
{
	int s;
	struct region region;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if ((s = getregion(&region)) != true)
		return s;
	if ((lastflag & CFKILL) == 0)	/* This is a kill type  */
		kdelete();	/* command, so do magic */
	thisflag |= CFKILL;	/* kill buffer stuff.   */
	curwp->w_dotp = region.r_linep;
	curwp->w_doto = region.r_offset;
	clear_selection();	/* Clear visual selection after cut */
	return ldelete(region.r_size, true);
}

/*
 * Copy all of the characters in the
 * region to the kill buffer. Don't move dot
 * at all. This is a bit like a kill region followed
 * by a yank. Bound to "M-W".
 */
int region_copy(int f, int n)
{
	struct line *linep;
	int loffs;
	int s;
	struct region region;
	/* For system clipboard aggregation */
	char *cb = nullptr;
	long cb_len = 0;
	long cb_cap = 0;

	if ((s = getregion(&region)) != true)
		return s;
	if ((lastflag & CFKILL) == 0)	/* Kill type command.   */
		kdelete();
	thisflag |= CFKILL;
	linep = region.r_linep;	/* Current line.        */
	loffs = region.r_offset;	/* Current offset.      */
	/* Pre-size clipboard buffer (exact size) */
	if (region.r_size > 0) {
		cb_cap = region.r_size + 1; /* include NUL */
		cb = safe_alloc((size_t)cb_cap, "region_copy clipboard", __FILE__, __LINE__);
		if (!cb) return false;
	}
	while (region.r_size--) {
		if (loffs == llength(linep)) {	/* End of line.         */
			if ((s = kinsert('\n')) != true)
				goto copy_fail;
			if (cb && cb_len < cb_cap - 1) cb[cb_len++] = '\n';
			linep = lforw(linep);
			loffs = 0;
		} else {	/* Middle of line.      */
			int ch = lgetc(linep, loffs);
			if ((s = kinsert(ch)) != true)
				goto copy_fail;
			if (cb && cb_len < cb_cap - 1) cb[cb_len++] = (char)ch;
			++loffs;
		}
	}
	/* Push to system clipboard (copy does not consume temp kill buffer) */
	if (cb) {
		cb[cb_len] = '\0';
		clipboard_set(cb, (size_t)cb_len);
		SAFE_FREE(cb);
	}
	mlwrite("[REGION COPIED]");
	clear_selection();	/* Clear visual selection after copy */
	return true;

copy_fail:
	if (cb) SAFE_FREE(cb);
	return s;
}

/*
 * Lower case region. Zap all of the upper
 * case characters in the region to lower case. Use
 * the region code to set the limits. Scan the buffer,
 * doing the changes. Call "lchange" to ensure that
 * redisplay is done in all buffers. Bound to
 * "C-X C-L".
 */
int lowerregion(int f, int n)
{
	struct line *linep;
	int loffs;
	int c;
	int s;
	struct region region;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if ((s = getregion(&region)) != true)
		return s;
	lchange(WFHARD);
	linep = region.r_linep;
	loffs = region.r_offset;
	while (region.r_size--) {
		if (loffs == llength(linep)) {
			linep = lforw(linep);
			loffs = 0;
		} else {
			c = lgetc(linep, loffs);
			if (c >= 'A' && c <= 'Z')
				lputc(linep, loffs, c + 'a' - 'A');
			++loffs;
		}
	}
	return true;
}

/*
 * Upper case region. Zap all of the lower
 * case characters in the region to upper case. Use
 * the region code to set the limits. Scan the buffer,
 * doing the changes. Call "lchange" to ensure that
 * redisplay is done in all buffers. Bound to
 * "C-X C-L".
 */
int upperregion(int f, int n)
{
	struct line *linep;
	int loffs;
	int c;
	int s;
	struct region region;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if ((s = getregion(&region)) != true)
		return s;
	lchange(WFHARD);
	linep = region.r_linep;
	loffs = region.r_offset;
	while (region.r_size--) {
		if (loffs == llength(linep)) {
			linep = lforw(linep);
			loffs = 0;
		} else {
			c = lgetc(linep, loffs);
			if (c >= 'a' && c <= 'z')
				lputc(linep, loffs, c - 'a' + 'A');
			++loffs;
		}
	}
	return true;
}

/*
 * This routine figures out the
 * bounds of the region in the current window, and
 * fills in the fields of the "struct region" structure pointed
 * to by "rp". Because the dot and mark are usually very
 * close together, we scan outward from dot looking for
 * mark. This should save time. Return a standard code.
 * Callers of this routine should be prepared to get
 * an "ABORT" status; we might make this have the
 * conform thing later.
 */
int getregion(struct region *rp)
{
	struct line *flp;
	struct line *blp;
	long fsize;
	long bsize;

	if (curwp->w_markp == nullptr) {
		mlwrite("NO MARK SET IN THIS WINDOW");
		return false;
	}
	if (curwp->w_dotp == curwp->w_markp) {
		rp->r_linep = curwp->w_dotp;
		if (curwp->w_doto < curwp->w_marko) {
			rp->r_offset = curwp->w_doto;
			rp->r_size =
			    (long) (curwp->w_marko - curwp->w_doto);
		} else {
			rp->r_offset = curwp->w_marko;
			rp->r_size =
			    (long) (curwp->w_doto - curwp->w_marko);
		}
		return true;
	}
	blp = curwp->w_dotp;
	bsize = (long) curwp->w_doto;
	flp = curwp->w_dotp;
	fsize = (long) (llength(flp) - curwp->w_doto + 1);
	while (flp != curbp->b_linep || lback(blp) != curbp->b_linep) {
		if (flp != curbp->b_linep) {
			flp = lforw(flp);
			if (flp == curwp->w_markp) {
				rp->r_linep = curwp->w_dotp;
				rp->r_offset = curwp->w_doto;
				rp->r_size = fsize + curwp->w_marko;
				return true;
			}
			fsize += llength(flp) + 1;
		}
		if (lback(blp) != curbp->b_linep) {
			blp = lback(blp);
			bsize += llength(blp) + 1;
			if (blp == curwp->w_markp) {
				rp->r_linep = blp;
				rp->r_offset = curwp->w_marko;
				rp->r_size = bsize - curwp->w_marko;
				return true;
			}
		}
	}
	mlwrite("BUG: LOST MARK");
	return false;
}

/*
 * Copy region directly to system clipboard (same as region_copy but
 * explicit about clipboard destination). Bound to nothing by default.
 */
int copy_to_clipboard(int f, int n)
{
	return region_copy(f, n);
}

/*
 * Display the current clipboard provider.
 * Useful for debugging clipboard configuration.
 */
int clipboard_provider_cmd(int f, int n)
{
	(void)f; (void)n;
	const char *provider = clipboard_provider_name();
	bool available = clipboard_available();

	if (available) {
		mlwrite("[CLIPBOARD PROVIDER: %s%s]",
			provider,
			g_clipboard_config.sync_kills ? " (sync enabled)" : "");
	} else {
		mlwrite("[CLIPBOARD: NOT AVAILABLE]");
	}
	return true;
}
