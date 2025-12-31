/*	basic.c
 *
 * The routines in this file move the cursor around on the screen. They
 * compute a new value for the cursor, then adjust ".". The display code
 * always updates the cursor location, so only moves between lines, or
 * functions that adjust the top line in the window and invalidate the
 * framing, are hard.
 *
 *	modified by Petri Kutvonen
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "utf8.h"
#include "util/logger.h"

/*
 * This routine, given a pointer to a struct line, and the current cursor goal
 * column, return the best choice for the offset. The offset is returned.
 * Used by "C-N" and "C-P".
 */
static int getgoal(struct line *dlp)
{
	int col;
	int newcol;
	int dbo;
	int len = llength(dlp);

	col = 0;
	dbo = 0;
	while (dbo != len) {
		unicode_t c = lgetc(dlp, dbo);
		newcol = col;

		/* Take tabs, ^X and \xx hex characters into account */
		if (c == '\t')
			newcol |= tabmask;
		else if (c < 0x20 || c == 0x7F)
			++newcol;
		else if (c >= 0x80 && c <= 0xa0)
			newcol += 2;

		++newcol;
		if (newcol > curgoal)
			break;
		col = newcol;
		++dbo;
	}
	return dbo;
}

/*
 * Move the cursor to the beginning of the current line.
 */
int goto_line_start(int f, int n)
{
	(void)f; (void)n;
	curwp->w_doto = 0;
	curwp->w_flag |= WFMOVE | WFMODE;
	return true;
}

/*
 * Move the cursor backwards by "n" characters. If "n" is less than zero call
 * "move_char_forward" to actually do the move. Otherwise compute the new cursor
 * location. Error if you try and move out of the buffer. Set the flag if the
 * line pointer for dot changes.
 */
int move_char_backward(int f, int n)
{
	struct line *lp;

	if (n < 0)
		return move_char_forward(f, -n);
	while (n--) {
		if (curwp->w_doto == 0) {
			if ((lp = lback(curwp->w_dotp)) == curbp->b_linep)
				return false;
			curwp->w_dotp = lp;
			curwp->w_doto = llength(lp);
			curwp->w_flag |= WFMOVE | WFMODE;
		} else {
			do {
				unsigned char c;
				curwp->w_doto--;
				c = lgetc(curwp->w_dotp, curwp->w_doto);
				if (is_beginning_utf8(c))
					break;
			} while (curwp->w_doto);
			curwp->w_flag |= WFMODE;
		}
		invalidate_line_cache(curwp);
	}
	return true;
}

/*
 * Move the cursor to the end of the current line. Trivial. No errors.
 */
int goto_line_end(int f, int n)
{
	(void)f; (void)n;
	curwp->w_doto = llength(curwp->w_dotp);
	curwp->w_flag |= WFMOVE | WFMODE;
	return true;
}

/*
 * Move the cursor forwards by "n" characters. If "n" is less than zero call
 * "move_char_backward" to actually do the move. Otherwise compute the new cursor
 * location, and move ".". Error if you try and move off the end of the
 * buffer. Set the flag if the line pointer for dot changes.
 */
int move_char_forward(int f, int n)
{
	if (n < 0)
		return move_char_backward(f, -n);
	while (n--) {
		int len = llength(curwp->w_dotp);
		if (curwp->w_doto == len) {
			if (curwp->w_dotp == curbp->b_linep)
				return false;
			curwp->w_dotp = lforw(curwp->w_dotp);
			curwp->w_doto = 0;
			curwp->w_flag |= WFMOVE | WFMODE;
		} else {
			do {
				unsigned char c;
				curwp->w_doto++;
				c = lgetc(curwp->w_dotp, curwp->w_doto);
				if (is_beginning_utf8(c))
					break;
			} while (curwp->w_doto < len);
			curwp->w_flag |= WFMODE;
		}
		invalidate_line_cache(curwp);
	}
	return true;
}

/*
 * Move to a particular line.
 *
 * @n: The specified line position at the current buffer.
 */
int gotoline(int f, int n)
{
	int status;
	char arg[NSTRING]; /* Buffer to hold argument. */

	/* Get an argument if one doesnt exist. */
	if (f == false) {
		if ((status =
		     minibuf_read("LINE TO GOTO: ", arg, NSTRING)) != true) {
			mlwrite("[ABORTED]");
			return status;
		}
		char *endptr;
		errno = 0;
		long val = strtol(arg, &endptr, 10);
		if (errno != 0 || endptr == arg || *endptr != '\0') {
			mlwrite("[INVALID LINE NUMBER]");
			return false;
		}
		n = (int)val;
	}
        /* Handle the case where the user may be passed something like this:
         * em filename +
         * In this case we just go to the end of the buffer.
         */
	if (n == 0)
		return goto_buffer_end(f, n);

	/* If a bogus argument was passed, then returns false. */
	if (n < 0)
		return false;

	/* First, we go to the begin of the buffer. */
	goto_buffer_start(f, n);
	return cursor_down(f, n - 1);
}

/*
 * Goto the beginning of the buffer. Massive adjustment of dot. This is
 * considered to be hard motion; it really isn't if the original value of dot
 * is the same as the new value of dot. Normally bound to "M-<".
 */
int goto_buffer_start(int f, int n)
{
	curwp->w_dotp = lforw(curbp->b_linep);
	curwp->w_doto = 0;
	curwp->w_flag |= WFHARD;
	return true;
}

/*
 * Move to the end of the buffer. Dot is always put at the end of the file
 * (ZJ). The standard screen code does most of the hard parts of update.
 * Bound to "M->".
 */
int goto_buffer_end(int f, int n)
{
	curwp->w_dotp = curbp->b_linep;
	curwp->w_doto = 0;
	curwp->w_flag |= WFHARD;
	return true;
}

/*
 * Move forward by full lines. If the number of lines to move is less than
 * zero, call the backward line function to actually do it. The last command
 * controls how the goal column is set. Bound to "C-N". No errors are
 * possible.
 */
int cursor_down(int f, int n)
{
	struct line *dlp;

	if (n < 0)
		return cursor_up(f, -n);

	/* if we are on the last line as we start....fail the command */
	if (curwp->w_dotp == curbp->b_linep)
		return false;

	/* if the last command was not note a line move,
	   reset the goal column */
	if ((lastflag & CFCPCN) == 0)
		curgoal = getccol(false);

	/* flag this command as a line move */
	thisflag |= CFCPCN;

	/* and move the point down */
	dlp = curwp->w_dotp;
	while (n-- && dlp != curbp->b_linep)
		dlp = lforw(dlp);

	/* Never land cursor on sentinel - back up to last real line.
	 * This prevents display corruption when scrolling past EOF. */
	if (dlp == curbp->b_linep) {
		dlp = lback(dlp);
		if (dlp == curbp->b_linep)
			return false;  /* Empty buffer edge case */
	}

	/* reseting the current position */
	curwp->w_dotp = dlp;
	curwp->w_doto = getgoal(dlp);
	curwp->w_flag |= WFMOVE | WFMODE;
	invalidate_line_cache(curwp);
	return true;
}

/*
 * This function is like "cursor_down", but goes backwards. The scheme is exactly
 * the same. Check for arguments that are less than zero and call your
 * alternate. Figure out the new line and call "movedot" to perform the
 * motion. No errors are possible. Bound to "C-P".
 */
int cursor_up(int f, int n)
{
	struct line *dlp;

	if (n < 0)
		return cursor_down(f, -n);

	/* if we are on the last line as we start....fail the command */
	if (lback(curwp->w_dotp) == curbp->b_linep)
		return false;

	/* if the last command was not note a line move,
	   reset the goal column */
	if ((lastflag & CFCPCN) == 0)
		curgoal = getccol(false);

	/* flag this command as a line move */
	thisflag |= CFCPCN;

	/* and move the point up */
	dlp = curwp->w_dotp;
	while (n-- && lback(dlp) != curbp->b_linep)
		dlp = lback(dlp);

	/* reseting the current position */
	curwp->w_dotp = dlp;
	curwp->w_doto = getgoal(dlp);
	curwp->w_flag |= WFMOVE | WFMODE;
	invalidate_line_cache(curwp);
	return true;
}

static int is_new_para(void)
{
	int i, len;

	len = llength(curwp->w_dotp);

	        for (i = 0; i < len; i++) {
	                int c = lgetc(curwp->w_dotp, i);
	                if (c == ' ' || c == TAB) {
	                        continue;
	                }
	                if (!is_letter(c))
	                        return 1;
	                return 0;
	        }	return 1;
}

/*
 * go back to the beginning of the current paragraph
 * here we look for a <NL><NL> or <NL><TAB> or <NL><SPACE>
 * combination to delimit the beginning of a paragraph
 *
 * int f, n;		default Flag & Numeric argument
 */
int goto_para_start(int f, int n)
{
	int suc;  /* success of last move_char_backward */

	if (n < 0) /* the other way... */
		return goto_para_end(f, -n);

	while (n-- > 0) {  /* for each one asked for */

		/* first scan back until we are in a word */
		suc = move_char_backward(false, 1);
		while (!inword() && suc)
			suc = move_char_backward(false, 1);
		curwp->w_doto = 0;	/* and go to the B-O-Line */

		/* and scan back until we hit a <NL><NL> or <NL><TAB>
		   or a <NL><SPACE>                                     */
		while (lback(curwp->w_dotp) != curbp->b_linep) {
			if (is_new_para())
				break;
			curwp->w_dotp = lback(curwp->w_dotp);
		}

		/* and then forward until we are in a word */
		suc = move_char_forward(false, 1);
		while (suc && !inword())
			suc = move_char_forward(false, 1);
	}
	curwp->w_flag |= WFMOVE | WFMODE;
	invalidate_line_cache(curwp);	/* force screen update */
	return true;
}

/*
 * Go forword to the end of the current paragraph
 * here we look for a <NL><NL> or <NL><TAB> or <NL><SPACE>
 * combination to delimit the beginning of a paragraph
 *
 * int f, n;		default Flag & Numeric argument
 */
int goto_para_end(int f, int n)
{
	int suc;  /* success of last move_char_backward */

	if (n < 0)  /* the other way... */
		return goto_para_start(f, -n);

	while (n-- > 0) {  /* for each one asked for */
		/* first scan forward until we are in a word */
		suc = move_char_forward(false, 1);
		while (!inword() && suc)
			suc = move_char_forward(false, 1);
		curwp->w_doto = 0;	/* and go to the B-O-Line */
		if (suc)	/* of next line if not at EOF */
			curwp->w_dotp = lforw(curwp->w_dotp);

		/* and scan forword until we hit a <NL><NL> or <NL><TAB>
		   or a <NL><SPACE>                                     */
		while (curwp->w_dotp != curbp->b_linep) {
			if (is_new_para())
				break;
			curwp->w_dotp = lforw(curwp->w_dotp);
		}

		/* and then backward until we are in a word */
		suc = move_char_backward(false, 1);
		while (suc && !inword()) {
			suc = move_char_backward(false, 1);
		}
		curwp->w_doto = llength(curwp->w_dotp);	/* and to the EOL */
	}
	curwp->w_flag |= WFMOVE | WFMODE;
	invalidate_line_cache(curwp);  /* force screen update */
	return true;
}

/*
 * Scroll forward by a specified number of lines, or by a full page if no
 * argument. Bound to "C-V". The "2" in the arithmetic on the window size is
 * the overlap; this value is the default overlap value in ITS EMACS. Because
 * this zaps the top line in the display window, we have to do a hard update.
 */
int move_page_down(int f, int n)
{
	struct line *lp;

	if (f == false) {
		if (term.t_scroll != nullptr)
			if (overlap == 0)
				n = curwp->w_ntrows / 3 * 2;
			else
				n = curwp->w_ntrows - overlap;
		else
			n = curwp->w_ntrows - 2;  /* Default scroll. */
		if (n <= 0)	/* Forget the overlap. */
			n = 1;	/* If tiny window. */
	} else if (n < 0)
		return move_page_up(f, -n);
	lp = curwp->w_linep;
	while (n-- && lp != curbp->b_linep)
		lp = lforw(lp);
	curwp->w_linep = lp;
	curwp->w_dotp = lp;
	curwp->w_doto = 0;
	curwp->w_flag |= WFHARD | WFFORCE | WFMODE;
	invalidate_line_cache(curwp);  /* force line number update in status line */
	return true;
}

/*
 * This command is like "move_page_down", but it goes backwards. The "2", like
 * above, is the overlap between the two windows. The value is from the ITS
 * EMACS manual. Bound to "M-V". We do a hard update for exactly the same
 * reason.
 */
int move_page_up(int f, int n)
{
	struct line *lp;

	if (f == false) {
		if (term.t_scroll != nullptr)
			if (overlap == 0)
				n = curwp->w_ntrows / 3 * 2;
			else
				n = curwp->w_ntrows - overlap;
		else
			n = curwp->w_ntrows - 2; /* Default scroll. */
		if (n <= 0)	/* Don't blow up if the. */
			n = 1;	/* Window is tiny. */
	} else if (n < 0)
		return move_page_down(f, -n);
	lp = curwp->w_linep;
	while (n-- && lback(lp) != curbp->b_linep)
		lp = lback(lp);
	curwp->w_linep = lp;
	curwp->w_dotp = lp;
	curwp->w_doto = 0;
	curwp->w_flag |= WFHARD | WFFORCE | WFMODE;
	invalidate_line_cache(curwp);  /* force line number update in status line */
	return true;
}

/*
 * Set the mark in the current window to the value of "." in the window. No
 * errors are possible. Bound to "M-.".
 */
int setmark(int f, int n)
{
	curwp->w_markp = curwp->w_dotp;
	curwp->w_marko = curwp->w_doto;
	/* Force screen update to show selection highlighting */
	curwp->w_flag |= WFHARD;
	mlwrite("[MARK SET]");
	return true;
}

/*
 * Swap the values of "." and "mark" in the current window. This is pretty
 * easy, bacause all of the hard work gets done by the standard routine
 * that moves the mark about. The only possible error is "no mark". Bound to
 * "C-X C-X".
 */
int swapmark(int f, int n)
{
	struct line *odotp;
	int odoto;

	if (curwp->w_markp == nullptr) {
		mlwrite("NO MARK IN THIS WINDOW");
		return false;
	}
	odotp = curwp->w_dotp;
	odoto = curwp->w_doto;
	curwp->w_dotp = curwp->w_markp;
	curwp->w_doto = curwp->w_marko;
	curwp->w_markp = odotp;
	curwp->w_marko = odoto;
	curwp->w_flag |= WFMOVE | WFMODE;
	invalidate_line_cache(curwp);
	return true;
}
