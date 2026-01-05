/*	random.c
 *
 *      This file contains the command processing functions for a number of
 *      random commands. There is no functional grouping here, for sure.
 *
 *	Modified by Petri Kutvonen
 */

#include <stdio.h>
#include <stdbool.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "string_utils.h"
#include "memory.h"
#include "gapbuffer.h"

int tabsize; /* Tab size (0: use real tabs) */

/*
 * Set fill column to n.
 */
/* set-fill-column command removed; writing-mode manages fill column directly */

/*
 * Display the current position of the cursor, in origin 1 X-Y coordinates,
 * the character that is under the cursor (in hex), and the fraction of the
 * text that is before the cursor. The displayed column is not the current
 * column, but the column that would be used on an infinite width display.
 * Normally this is bound to "C-X =".
 */
int showcpos(int f, int n)
{
	struct line *lp;	/* current line */
	long numchars;	/* # of chars in file */
	int numlines;	/* # of lines in file */
	long predchars;	/* # chars preceding point */
	int predlines;	/* # lines preceding point */
	int curchar;	/* character under cursor */
	int ratio;
	int col;
	int savepos;		/* temp save for current offset */
	int ecol;		/* column pos/end of current line */

	/* starting at the beginning of the buffer */
	lp = lforw(curbp->b_linep);

	/* start counting chars and lines */
	numchars = 0;
	numlines = 0;
	predchars = 0;
	predlines = 0;
	curchar = 0;
	while (lp != curbp->b_linep) {
		/* if we are on the current line, record it */
		if (lp == curwp->w_dotp) {
			predlines = numlines;
			predchars = numchars + curwp->w_doto;
			if ((curwp->w_doto) == llength(lp))
				curchar = '\n';
			else
				curchar = lgetc(lp, curwp->w_doto);
		}
		/* on to the next line */
		++numlines;
		numchars += llength(lp) + 1;
		lp = lforw(lp);
	}

	/* if at end of file, record it */
	if (curwp->w_dotp == curbp->b_linep) {
		predlines = numlines;
		predchars = numchars;
		curchar = 0;
	}

	/* Get real column and end-of-line column. */
	col = getccol(false);
	savepos = curwp->w_doto;
	curwp->w_doto = llength(curwp->w_dotp);
	ecol = getccol(false);
	curwp->w_doto = savepos;

	ratio = 0;		/* Ratio before dot. */
	if (numchars != 0)
		ratio = (100L * predchars) / numchars;

	/* summarize and report the info */
	mlwrite("LINE %d/%d COL %d/%d CHAR %D/%D [%d%%] CHAR = 0X%x",
		predlines + 1, numlines + 1, col, ecol,
		predchars, numchars, ratio, curchar);
	return true;
}

int getcline(void)
{				/* get the current line number */
	struct line *lp;	/* current line */
	int numlines;	/* # of lines before point */

	/* starting at the beginning of the buffer */
	lp = lforw(curbp->b_linep);

	/* start counting lines */
	numlines = 0;
	while (lp != curbp->b_linep) {
		/* if we are on the current line, record it */
		if (lp == curwp->w_dotp)
			break;
		++numlines;
		lp = lforw(lp);
	}

	/* and return the resulting count */
	return numlines + 1;
}

/*
 * Return current column.  Stop at first non-blank given true argument.
 */
int getccol(int bflg)
{
	int i, col;
	struct line *dlp = curwp->w_dotp;
	int byte_offset = curwp->w_doto;
	int len = llength(dlp);

	col = i = 0;
	while (i < byte_offset) {
		unicode_t c;

		char utf8_buf[6];
		int remaining = len - i;
		int extract_len = (remaining > 6) ? 6 : remaining;
		for (int j = 0; j < extract_len; j++) {
			utf8_buf[j] = lgetc(dlp, i + j);
		}
		i += utf8_to_unicode(utf8_buf, 0, extract_len, &c);
		if (c != ' ' && c != '\t' && bflg)
			break;
		if (c == '\t')
			col |= tabmask;
		else if (c < 0x20 || c == 0x7F)
			++col;  /* Control chars display as ^X (2 cols) */
		/* Dead code removed: c >= 0xc0 && c <= 0xa0 was always false */
		++col;
	}
	return col;
}

/*
 * Set current column.
 *
 * int pos;		position to set cursor
 */
int setccol(int pos)
{
	int c;		/* character being scanned */
	int i;		/* index into current line */
	int col;	/* current cursor column   */
	int llen;	/* length of line in bytes */

	col = 0;
	llen = llength(curwp->w_dotp);

	/* scan the line until we are at or past the target column */
	for (i = 0; i < llen; ++i) {
		/* upon reaching the target, drop out */
		if (col >= pos)
			break;

		/* advance one character */
		c = lgetc(curwp->w_dotp, i);
		if (c == '\t')
			col |= tabmask;
		else if (c < 0x20 || c == 0x7F)
			++col;
		++col;
	}

	/* set us at the new position */
	curwp->w_doto = i;

	/* and tell weather we made it */
	return col >= pos;
}

/*
 * Twiddle the two characters on either side of dot. If dot is at the end of
 * the line twiddle the two characters before it. Return with an error if dot
 * is at the beginning of line; it seems to be a bit pointless to make this
 * work. This fixes up a very common typo with a single stroke. Normally bound
 * to "C-T". This always works within a line, so "WFEDIT" is good enough.
 */
int twiddle(int f, int n)
{
	struct line *dotp;
	int doto;
	int cl;
	int cr;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	dotp = curwp->w_dotp;
	doto = curwp->w_doto;
	if (doto == llength(dotp) && --doto < 0)
		return false;
	cr = lgetc(dotp, doto);
	if (--doto < 0)
		return false;
	cl = lgetc(dotp, doto);
	lputc(dotp, doto + 0, cr);
	lputc(dotp, doto + 1, cl);
	lchange(WFEDIT);
	return true;
}

/*
 * Quote the next character, and insert it into the buffer. All the characters
 * are taken literally, with the exception of the newline, which always has
 * its line splitting meaning. The character is always read, even if it is
 * inserted 0 times, for regularity. Bound to "C-Q"
 */
int quote(int f, int n)
{
	int s;
	int c;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	c = input_read_byte();
	if (n < 0)
		return false;
	if (n == 0)
		return true;
	if (c == '\n') {
		do {
			s = lnewline();
		} while (s == true && --n);
		return s;
	}
	return linsert(n, c);
}

/*
 * Set tab size if given non-default argument (n <> 1).  Otherwise, insert a
 * tab into file.  If given argument, n, of zero, change to true tabs.
 * If n > 1, simulate tab stop every n-characters using spaces. This has to be
 * done in this slightly funny way because the tab (in ASCII) has been turned
 * into "C-I" (in 10 bit code) already. Bound to "C-I".
 */
int insert_tab(int f, int n)
{
	if (n < 0)
		return false;
	if (n == 0 || n > 1) {
		tabsize = n;
		return true;
	}
	if (!tabsize)
		return linsert(1, '\t');
	return linsert(tabsize - (getccol(false) % tabsize), ' ');
}

/*
 * change tabs to spaces
 *
 * int f, n;		default flag and numeric repeat count
 */
int detab(int f, int n)
{
	int inc;	/* increment to next line [sgn(n)] */

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */

	if (f == false)
		n = 1;

	/* loop thru detabbing n lines */
	inc = ((n > 0) ? 1 : -1);
	while (n) {
		curwp->w_doto = 0;	/* start at the beginning */

		/* detab the entire current line */
		while (curwp->w_doto < llength(curwp->w_dotp)) {
			/* if we have a tab */
			if (lgetc(curwp->w_dotp, curwp->w_doto) == '\t') {
				ldelchar(1, false);
				insspace(true,
					 (tabmask + 1) -
					 (curwp->w_doto & tabmask));
			}
			move_char_forward(false, 1);
		}

		/* advance/or back to the next line */
		cursor_down(true, inc);
		n -= inc;
	}
	curwp->w_doto = 0;	/* to the begining of the line */
	thisflag &= ~CFCPCN;	/* flag that this resets the goal column */
	lchange(WFEDIT);	/* yes, we have made at least an edit */
	return true;
}

/*
 * change spaces to tabs where posible
 *
 * int f, n;		default flag and numeric repeat count
 */
int entab(int f, int n)
{
	int inc;	/* increment to next line [sgn(n)] */
	int fspace;	/* pointer to first space if in a run */
	int ccol;	/* current cursor column */
	char cchar;	/* current character */

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */

	if (f == false)
		n = 1;

	/* loop thru entabbing n lines */
	inc = ((n > 0) ? 1 : -1);
	while (n) {
		curwp->w_doto = 0;	/* start at the beginning */

		/* entab the entire current line */
		fspace = -1;
		ccol = 0;
		while (curwp->w_doto < llength(curwp->w_dotp)) {
			/* see if it is time to compress */
			if ((fspace >= 0) && (nextab(fspace) <= ccol)) {
				if (ccol - fspace < 2)
					fspace = -1;
				else {
					/* Known limitation: mixed space/tab lines may not convert perfectly */
					move_char_backward(true, ccol - fspace);
					ldelete((long) (ccol - fspace),
						false);
					linsert(1, '\t');
					fspace = -1;
				}
			}

			/* get the current character */
			cchar = lgetc(curwp->w_dotp, curwp->w_doto);

			switch (cchar) {
			case '\t':	/* a tab...count em up */
				ccol = nextab(ccol);
				break;

			case ' ':	/* a space...compress? */
				if (fspace == -1)
					fspace = ccol;
				ccol++;
				break;

			default:	/* any other char...just count */
				ccol++;
				fspace = -1;
				break;
			}
			move_char_forward(false, 1);
		}

		/* advance/or back to the next line */
		cursor_down(true, inc);
		n -= inc;
	}
	curwp->w_doto = 0;	/* to the begining of the line */
	thisflag &= ~CFCPCN;	/* flag that this resets the goal column */
	lchange(WFEDIT);	/* yes, we have made at least an edit */
	return true;
}

/*
 * trim trailing whitespace from the point to eol
 *
 * int f, n;		default flag and numeric repeat count
 */
int trim(int f, int n)
{
	struct line *lp;	/* current line pointer */
	int offset;	/* original line offset position */
	int length;	/* current length */
	int inc;	/* increment to next line [sgn(n)] */

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */

	if (f == false)
		n = 1;

	/* loop thru trimming n lines */
	inc = ((n > 0) ? 1 : -1);
	while (n) {
		lp = curwp->w_dotp;	/* find current line text */
		offset = curwp->w_doto;	/* save original offset */
		length = llength(lp);	/* find current length */

		/* trim the current line */
		while (length > offset) {
			if (lgetc(lp, length - 1) != ' ' &&
			    lgetc(lp, length - 1) != '\t')
				break;
			length--;
		}
		/* Delete trailing whitespace from gap buffer */
		int old_length = llength(lp);
		if (length < old_length) {
			gap_buffer_delete(lp->gb, (size_t)length, (size_t)(old_length - length));
		}

		/* advance/or back to the next line */
		cursor_down(true, inc);
		n -= inc;
	}
	lchange(WFEDIT);
	thisflag &= ~CFCPCN;	/* flag that this resets the goal column */
	return true;
}

/*
 * Open up some blank space. The basic plan is to insert a bunch of newlines,
 * and then back up over them. Everything is done by the subcommand
 * procerssors. They even handle the looping. Normally this is bound to "C-O".
 */
int openline(int f, int n)
{
	int i;
	int s;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if (n < 0)
		return false;
	if (n == 0)
		return true;
	i = n;			/* Insert newlines.     */
	do {
		s = lnewline();
	} while (s == true && --i);
	if (s == true)		/* Then back up overtop */
		s = move_char_backward(f, n);	/* of them all.         */
	return s;
}

/*
 * Insert a newline. Bound to "C-M". If we are in CMODE, do automatic
 * indentation as specified.
 */
int insert_newline(int f, int n)
{
	int s;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if (n < 0)
		return false;

	/* if we are in C mode and this is a default <NL> */
	if (n == 1 && (curbp->b_mode & MDCMOD) &&
	    curwp->w_dotp != curbp->b_linep)
		return cinsert();

	/*
	 * If a newline was typed, fill column is defined, the argument is non-
	 * negative, wrap mode is enabled, and we are now past fill column,
	 * and we are not read-only, perform word wrap.
	 */
	if ((curwp->w_bufp->b_mode & MDWRAP) && fillcol > 0 &&
	    getccol(false) > fillcol &&
	    (curwp->w_bufp->b_mode & MDVIEW) == false)
		wrapword(false, 1);

	/* insert some lines */
	while (n--) {
		if ((s = lnewline()) != true)
			return s;
	}
	invalidate_line_cache(curwp);  /* update line count after newline insertion */
	return true;
}

int cinsert(void)
{				/* insert a newline and indentation for C */
	char *cptr;	/* string pointer into text to copy */
	int tptr;	/* index to scan into line */
	int bracef;	/* was there a brace at the end of line? */
	int i;
	char ichar[NSTRING];	/* buffer to hold indent of last line */

	/* grab a pointer to text to copy indentation from */
	// Extract current line text from gap buffer
	struct line *lp = curwp->w_dotp;
	int line_len = llength(lp);
	char *line_text = safe_alloc(line_len + 1, "temp line", __FILE__, __LINE__);
	if (!line_text) return false;
	gap_buffer_get_text(lp->gb, 0, line_len, line_text, line_len + 1);
	cptr = &line_text[0];

	/* check for a brace */
	tptr = curwp->w_doto - 1;
	bracef = (tptr >= 0 && cptr[tptr] == '{');

	/* save the indent of the previous line */
	i = 0;
	while ((i < tptr) && (cptr[i] == ' ' || cptr[i] == '\t')
	       && (i < NSTRING - 1)) {
		ichar[i] = cptr[i];
		++i;
	}
	ichar[i] = 0;		/* terminate it */
	
	safe_free((void **)&line_text);

	/* put in the newline */
	if (lnewline() == false)
		return false;

	/* and the saved indentation */
	linstr(ichar);

	/* and one more tab for a brace */
	if (bracef)
		insert_tab(false, 1);

	invalidate_line_cache(curwp);  /* update line count after C-mode newline */
	return true;
}

/*
 * insert a brace into the text here...we are in CMODE
 *
 * int n;	repeat count
 * int c;	brace to insert (always } for now)
 */
int insert_brace(int n, int c)
{
	int ch;	/* last character before input */
	int oc;	/* caractere oppose a c */
	int i, count;
	int target;	/* column brace should go after */
	struct line *oldlp;
	int oldoff;

	/* if we aren't at the beginning of the line... */
	if (curwp->w_doto != 0)

		/* scan to see if all space before this is white space */
		for (i = curwp->w_doto - 1; i >= 0; --i) {
			ch = lgetc(curwp->w_dotp, i);
			if (ch != ' ' && ch != '\t')
				return linsert(n, c);
		}

	/* chercher le caractere oppose correspondant */
	switch (c) {
	case '}':
		oc = '{';
		break;
	case ']':
		oc = '[';
		break;
	case ')':
		oc = '(';
		break;
	default:
		return false;
	}

	oldlp = curwp->w_dotp;
	oldoff = curwp->w_doto;

	count = 1;
	move_char_backward(false, 1);

	while (count > 0) {
		if (curwp->w_doto == llength(curwp->w_dotp))
			ch = '\n';
		else
			ch = lgetc(curwp->w_dotp, curwp->w_doto);

		if (ch == c)
			++count;
		if (ch == oc)
			--count;

		move_char_backward(false, 1);
		if (boundry(curwp->w_dotp, curwp->w_doto, DIR_REVERSE))
			break;
	}

	if (count != 0) {	/* no match */
		curwp->w_dotp = oldlp;
		curwp->w_doto = oldoff;
		return linsert(n, c);
	}

	curwp->w_doto = 0;	/* debut de ligne */
	/* aller au debut de la ligne apres la tabulation */
	while ((ch = lgetc(curwp->w_dotp, curwp->w_doto)) == ' '
	       || ch == '\t')
		move_char_forward(false, 1);

	/* delete back first */
	target = getccol(false);	/* c'est l'indent que l'on doit avoir */
	curwp->w_dotp = oldlp;
	curwp->w_doto = oldoff;

	while (target != getccol(false)) {
		if (target < getccol(false))	/* on doit detruire des caracteres */
			while (getccol(false) > target)
				delete_char_backward(false, 1);
		else {		/* on doit en inserer */
			while (target - getccol(false) >= 8)
				linsert(1, '\t');
			linsert(target - getccol(false), ' ');
		}
	}

	/* and insert the required brace(s) */
	return linsert(n, c);
}

int insert_pound(void)
{				/* insert a # into the text here...we are in CMODE */
	int ch;	/* last character before input */
	int i;

	/* if we are at the beginning of the line, no go */
	if (curwp->w_doto == 0)
		return linsert(1, '#');

	/* scan to see if all space before this is white space */
	for (i = curwp->w_doto - 1; i >= 0; --i) {
		ch = lgetc(curwp->w_dotp, i);
		if (ch != ' ' && ch != '\t')
			return linsert(1, '#');
	}

	/* delete back first */
	while (getccol(false) >= 1)
		delete_char_backward(false, 1);

	/* and insert the required pound */
	return linsert(1, '#');
}

/*
 * Delete blank lines around dot. What this command does depends if dot is
 * sitting on a blank line. If dot is sitting on a blank line, this command
 * deletes all the blank lines above and below the current line. If it is
 * sitting on a non blank line then it deletes all of the blank lines after
 * the line. Normally this command is bound to "C-X C-O". Any argument is
 * ignored.
 */
int deblank(int f, int n)
{
	struct line *lp1;
	struct line *lp2;
	long nld;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	lp1 = curwp->w_dotp;
	while (llength(lp1) == 0 && (lp2 = lback(lp1)) != curbp->b_linep)
		lp1 = lp2;
	lp2 = lp1;
	nld = 0;
	while ((lp2 = lforw(lp2)) != curbp->b_linep && llength(lp2) == 0)
		++nld;
	if (nld == 0)
		return true;
	curwp->w_dotp = lforw(lp1);
	curwp->w_doto = 0;
	return ldelete(nld, false);
}

/*
 * Insert a newline, then enough tabs and spaces to duplicate the indentation
 * of the previous line. Assumes tabs are every eight characters. Quite simple.
 * Figure out the indentation of the current line. Insert a newline by calling
 * the standard routine. Insert the indentation by inserting the right number
 * of tabs and spaces. Return true if all ok. Return false if one of the
 * subcomands failed. Normally bound to "C-J".
 */
int indent(int f, int n)
{
	int nicol;
	int c;
	int i;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if (n < 0)
		return false;
	while (n--) {
		nicol = 0;
		for (i = 0; i < llength(curwp->w_dotp); ++i) {
			c = lgetc(curwp->w_dotp, i);
			if (c != ' ' && c != '\t')
				break;
			if (c == '\t')
				nicol |= tabmask;
			++nicol;
		}
		if (lnewline() == false
		    || ((i = nicol / 8) != 0 && linsert(i, '\t') == false)
		    || ((i = nicol % 8) != 0 && linsert(i, ' ') == false))
			return false;
	}
	return true;
}

/*
 * Delete forward. This is real easy, because the basic delete routine does
 * all of the work. Watches for negative arguments, and does the right thing.
 * If any argument is present, it kills rather than deletes, to prevent loss
 * of text if typed with a big argument. Normally bound to "C-D".
 */
int delete_char_forward(int f, int n)
{
	int s;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if (n < 0)
		return delete_char_backward(f, -n);
	if (f != false) {	/* Really a kill.       */
		if ((lastflag & CFKILL) == 0)
			kdelete();
		thisflag |= CFKILL;
	}

	/* Delete n UTF-8 characters forward */
	for (int i = 0; i < n; i++) {
		/* Calculate UTF-8 byte length at current position */
		int byte_len = 1;
		int remaining = llength(curwp->w_dotp) - curwp->w_doto;
		if (remaining > 0) {
			unsigned char first = lgetc(curwp->w_dotp, curwp->w_doto);
			if ((first & 0x80) == 0) {
				byte_len = 1;       /* ASCII */
			} else if ((first & 0xE0) == 0xC0) {
				byte_len = 2;       /* 2-byte UTF-8 */
			} else if ((first & 0xF0) == 0xE0) {
				byte_len = 3;       /* 3-byte UTF-8 (curly quotes, etc.) */
			} else if ((first & 0xF8) == 0xF0) {
				byte_len = 4;       /* 4-byte UTF-8 */
			}
			if (byte_len > remaining)
				byte_len = remaining;
		} else if (remaining == 0) {
			/* At end of line - delete newline (1 byte) */
			byte_len = 1;
		}

		s = ldelchar(byte_len, f != false);
		if (s != true)
			return s;
	}
	return true;
}

/*
 * Delete backwards. This is quite easy too, because it's all done with other
 * functions. Just move the cursor back, and delete forwards. Like delete
 * forward, this actually does a kill if presented with an argument. Bound to
 * both "RUBOUT" and "C-H".
 */
int delete_char_backward(int f, int n)
{
	int s;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if (n < 0)
		return delete_char_forward(f, -n);
	if (f != false) {	/* Really a kill.       */
		if ((lastflag & CFKILL) == 0)
			kdelete();
		thisflag |= CFKILL;
	}

	/* Delete n UTF-8 characters backwards */
	for (int i = 0; i < n; i++) {
		if ((s = move_char_backward(false, 1)) != true)
			return s;

		/* Calculate UTF-8 byte length at current position */
		int byte_len = 1;
		int remaining = llength(curwp->w_dotp) - curwp->w_doto;
		if (remaining > 0) {
			unsigned char first = lgetc(curwp->w_dotp, curwp->w_doto);
			if ((first & 0x80) == 0) {
				byte_len = 1;       /* ASCII */
			} else if ((first & 0xE0) == 0xC0) {
				byte_len = 2;       /* 2-byte UTF-8 */
			} else if ((first & 0xF0) == 0xE0) {
				byte_len = 3;       /* 3-byte UTF-8 (curly quotes, etc.) */
			} else if ((first & 0xF8) == 0xF0) {
				byte_len = 4;       /* 4-byte UTF-8 */
			}
			if (byte_len > remaining)
				byte_len = remaining;
		}

		s = ldelchar(byte_len, f != false);
		if (s != true)
			return s;
	}
	return true;
}

/*
 * Kill text. If called without an argument, it kills from dot to the end of
 * the line, unless it is at the end of the line, when it kills the newline.
 * If called with an argument of 0, it kills from the start of the line to dot.
 * If called with a positive argument, it kills from dot forward over that
 * number of newlines. If called with a negative argument it kills backwards
 * that number of newlines. Normally bound to "C-K".
 */
int kill_to_eol(int f, int n)
{
	struct line *nextp;
	long chunk;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if ((lastflag & CFKILL) == 0)	/* Clear kill buffer if */
		kdelete();	/* last wasn't a kill.  */
	thisflag |= CFKILL;
	if (f == false) {
		chunk = llength(curwp->w_dotp) - curwp->w_doto;
		if (chunk == 0)
			chunk = 1;
	} else if (n == 0) {
		chunk = curwp->w_doto;
		curwp->w_doto = 0;
	} else if (n > 0) {
		chunk = llength(curwp->w_dotp) - curwp->w_doto + 1;
		nextp = lforw(curwp->w_dotp);
		while (--n) {
			if (nextp == curbp->b_linep)
				return false;
			chunk += llength(nextp) + 1;
			nextp = lforw(nextp);
		}
	} else {
		mlwrite("NEG KILL");
		return false;
	}
	return ldelete(chunk, true);
}

/*
 * prompt and set an editor mode
 *
 * int f, n;		default and argument
 */
int setemode(int f, int n)
{
	return adjustmode(true, false);
}

/*
 * prompt and delete an editor mode
 *
 * int f, n;		default and argument
 */
int delmode(int f, int n)
{
	return adjustmode(false, false);
}

/*
 * prompt and set a global editor mode
 *
 * int f, n;		default and argument
 */
int setgmode(int f, int n)
{
	return adjustmode(true, true);
}

/*
 * prompt and delete a global editor mode
 *
 * int f, n;		default and argument
 */
int delgmode(int f, int n)
{
	return adjustmode(false, true);
}

/*
 * change the editor mode status
 *
 * int kind;		true = set,          false = delete
 * int global;		true = global flag,  false = current buffer flag
 */
int adjustmode(int kind, int global)
{
	char *scan;	/* scanning pointer to convert prompt */
	int i;		/* loop index */
	int status;	/* error return on input */
	int uflag;	/* was modename uppercase?      */
	char prompt[50];	/* string to prompt user with */
	char cbuf[NPAT];	/* buffer to recieve mode name into */

	/* build the proper prompt string */
    if (global) {
        safe_strcpy(prompt, "Global mode to ", sizeof(prompt));
    } else {
        safe_strcpy(prompt, "Mode to ", sizeof(prompt));
    }

    if (kind == true) {
        safe_strcat(prompt, "add: ", sizeof(prompt));
    } else {
        safe_strcat(prompt, "delete: ", sizeof(prompt));
    }

	/* prompt the user and get an answer */

	status = minibuf_read(prompt, cbuf, NPAT - 1);
	if (status != true)
		return status;

	/* make it uppercase */

	scan = cbuf;
	uflag = (*scan >= 'A' && *scan <= 'Z');
	while (*scan != 0) {
		if (*scan >= 'a' && *scan <= 'z')
			*scan = *scan - 32;
		scan++;
	}

	/* test it first against the colors we know */
	for (i = 0; i < NCOLORS; i++) {
		if (strcmp(cbuf, cname[i]) == 0) {
			/* finding the match, we set the global color */
			if (uflag)
				gfcolor = i;
			else
				gbcolor = i;

			curwp->w_flag |= WFHARD;
			mlerase();
			return true;
		}
	}

	/* test it against the modes we know */

	for (i = 0; i < NUMMODES; i++) {
		if (strcmp(cbuf, modename[i]) == 0) {
			/* finding a match, we process it */
			if (kind == true)
				if (global)
					gmode |= (1 << i);
				else
					curbp->b_mode |= (1 << i);
			else if (global)
				gmode &= ~(1 << i);
			else
				curbp->b_mode &= ~(1 << i);
			/* display new mode line */
			if (global == 0)
				upmode();
			mlerase();	/* erase the junk */
			return true;
		}
	}

	mlwrite("NO SUCH MODE!");
	return false;
}

/*
 * This function simply clears the message line,
 * mainly for macro usage
 *
 * int f, n;		arguments ignored
 */
int clrmes(int f, int n)
{
	mlforce("");
	return true;
}

/*
 * This function writes a string on the message line
 * mainly for macro usage
 *
 * int f, n;		arguments ignored
 */
int writemsg(int f, int n)
{
	char *sp;	/* pointer into buf to expand %s */
	char *np;	/* ptr into nbuf */
	int status;
	char buf[NPAT];		/* buffer to recieve message into */
	char nbuf[NPAT * 2];	/* buffer to expand string into */

	if ((status =
	     minibuf_read("MESSAGE TO WRITE: ", buf, NPAT - 1)) != true)
		return status;

	/* expand all '%' to "%%" so mlwrite won't expect arguments */
	sp = buf;
	np = nbuf;
	while (*sp) {
		*np++ = *sp;
		if (*sp++ == '%')
			*np++ = '%';
	}
	*np = '\0';

	/* write the message out */
	mlforce(nbuf);
	return true;
}

/*
 * the cursor is moved to a matching fence
 *
 * int f, n;		not used
 */
int getfence(int f, int n)
{
	struct line *oldlp;	/* original line pointer */
	int oldoff;	/* and offset */
	int sdir;	/* direction of search (1/-1) */
	int count;	/* current fence level count */
	char ch;	/* fence type to match against */
	char ofence;	/* open fence */
	char c;	/* current character in scan */

	/* save the original cursor position */
	oldlp = curwp->w_dotp;
	oldoff = curwp->w_doto;

	/* get the current character */
	if (oldoff == llength(oldlp))
		ch = '\n';
	else
		ch = lgetc(oldlp, oldoff);

	/* setup proper matching fence */
	switch (ch) {
	case '(':
		ofence = ')';
		sdir = DIR_FORWARD;
		break;
	case '{':
		ofence = '}';
		sdir = DIR_FORWARD;
		break;
	case '[':
		ofence = ']';
		sdir = DIR_FORWARD;
		break;
	case ')':
		ofence = '(';
		sdir = DIR_REVERSE;
		break;
	case '}':
		ofence = '{';
		sdir = DIR_REVERSE;
		break;
	case ']':
		ofence = '[';
		sdir = DIR_REVERSE;
		break;
	default:
		TTbeep();
		return false;
	}

	/* set up for scan */
	count = 1;
	if (sdir == DIR_REVERSE)
		move_char_backward(false, 1);
	else
		move_char_forward(false, 1);

	/* scan until we find it, or reach the end of file */
	while (count > 0) {
		if (curwp->w_doto == llength(curwp->w_dotp))
			c = '\n';
		else
			c = lgetc(curwp->w_dotp, curwp->w_doto);
		if (c == ch)
			++count;
		if (c == ofence)
			--count;
		if (sdir == DIR_FORWARD)
			move_char_forward(false, 1);
		else
			move_char_backward(false, 1);
		if (boundry(curwp->w_dotp, curwp->w_doto, sdir))
			break;
	}

	/* if count is zero, we have a match, move the sucker */
	if (count == 0) {
		if (sdir == DIR_FORWARD)
			move_char_backward(false, 1);
		else
			move_char_forward(false, 1);
		curwp->w_flag |= WFMOVE;
		return true;
	}

	/* restore the current position */
	curwp->w_dotp = oldlp;
	curwp->w_doto = oldoff;
	TTbeep();
	return false;
}

/*
 * Close fences are matched against their partners, and if
 * on screen the cursor briefly lights there
 *
 * char ch;			fence type to match against
 */
int fence_match(int ch)
{
	struct line *oldlp;	/* original line pointer */
	int oldoff;	/* and offset */
	struct line *toplp;	/* top line in current window */
	int count;	/* current fence level count */
	char opench;	/* open fence */
	char c;	/* current character in scan */
	int i;

	/* first get the display update out there */
	update(false);

	/* save the original cursor position */
	oldlp = curwp->w_dotp;
	oldoff = curwp->w_doto;

	/* setup proper open fence for passed close fence */
	if (ch == ')')
		opench = '(';
	else if (ch == '}')
		opench = '{';
	else
		opench = '[';

	/* find the top line and set up for scan */
	toplp = curwp->w_linep->l_bp;
	count = 1;
	move_char_backward(false, 2);

	/* scan back until we find it, or reach past the top of the window */
	while (count > 0 && curwp->w_dotp != toplp) {
		if (curwp->w_doto == llength(curwp->w_dotp))
			c = '\n';
		else
			c = lgetc(curwp->w_dotp, curwp->w_doto);
		if (c == ch)
			++count;
		if (c == opench)
			--count;
		move_char_backward(false, 1);
		if (curwp->w_dotp == curwp->w_bufp->b_linep->l_fp &&
		    curwp->w_doto == 0)
			break;
	}

	/* if count is zero, we have a match, display the sucker */
	/* Fence highlight uses term.t_pause for visible delay */
	if (count == 0) {
		move_char_forward(false, 1);
		for (i = 0; i < term.t_pause; i++)
			update(false);
	}

	/* restore the current position */
	curwp->w_dotp = oldlp;
	curwp->w_doto = oldoff;
	return true;
}

/*
 * ask for and insert a string into the current
 * buffer at the current point
 *
 * int f, n;		ignored arguments
 */
int istring(int f, int n)
{
	int status;	/* status return code */
	char tstring[NPAT + 1];	/* string to add */

	/* ask for string to insert */
	status = minibuf_read("String to insert: ", tstring, NPAT);
	if (status != true)
		return status;

	if (f == false)
		n = 1;

	if (n < 0)
		n = -n;

	/* insert it */
	while (n-- && (status = linstr(tstring)));
	return status;
}

/*
 * ask for and overwite a string into the current
 * buffer at the current point
 *
 * int f, n;		ignored arguments
 */
int ovstring(int f, int n)
{
	int status;	/* status return code */
	char tstring[NPAT + 1];	/* string to add */

	/* ask for string to insert */
	status = minibuf_read("String to overwrite: ", tstring, NPAT);
	if (status != true)
		return status;

	if (f == false)
		n = 1;

	if (n < 0)
		n = -n;

	/* insert it */
	while (n-- && (status = lover(tstring)));
	return status;
}

/*
 * Duplicate the current line below the current line.
 * Move cursor to the duplicated line.
 * Bound to Ctrl+D in modern editors.
 *
 * int f, n;		default flag and numeric repeat count
 */
int duplicate_line(int f, int n)
{
	struct line *dotp;	/* current line pointer */
	int doto;		/* current offset */
	int status;
	int i;
	
	if (n < 0)
		return false;
	
	if (n == 0)
		n = 1;
	
	/* Save current position */
	dotp = curwp->w_dotp;
	doto = curwp->w_doto;
	
	/* Repeat n times */
	while (n--) {
		/* Go to end of current line */
		curwp->w_doto = llength(curwp->w_dotp);
		
		/* Insert a newline */
		if ((status = lnewline()) != true)
			return status;
		
		/* Copy the line content */
		for (i = 0; i < llength(dotp); i++) {
			if ((status = linsert(1, lgetc(dotp, i))) != true)
				return status;
		}
		
		/* Position at the same offset on the new line */
		curwp->w_doto = (doto <= llength(curwp->w_dotp)) ? doto : llength(curwp->w_dotp);
	}
	
	return true;
}

/*
 * Transpose current line up by one line (swap with previous).
 * Bound to Alt+Up in modern editors.
 *
 * int f, n;		default flag and numeric repeat count
 */
int transpose_line_up(int f, int n)
{
	struct line *prevl;	/* previous line */
	struct line *curl;	/* current line */
	int doto;		/* current offset */

	if (n < 0)
		return transpose_line_down(f, -n);
	
	if (n == 0)
		n = 1;
	
	while (n--) {
		/* Can't move first line up */
		if (lback(curwp->w_dotp) == curbp->b_linep)
			return false;
		
		/* Save position in line */
		doto = curwp->w_doto;
		curl = curwp->w_dotp;
		prevl = lback(curl);
		
		/* Relink the lines */
		prevl->l_bp->l_fp = curl;
		curl->l_bp = prevl->l_bp;
		prevl->l_fp = curl->l_fp;
		curl->l_fp->l_bp = prevl;
		curl->l_fp = prevl;
		prevl->l_bp = curl;
		
		/* Restore position */
		curwp->w_dotp = curl;
		curwp->w_doto = (doto <= llength(curl)) ? doto : llength(curl);
		
		/* Update the display */
		curwp->w_flag |= WFHARD;
	}
	
	return true;
}

/*
 * Transpose current line down by one line (swap with next).
 * Bound to Alt+Down in modern editors.
 *
 * int f, n;		default flag and numeric repeat count
 */
int transpose_line_down(int f, int n)
{
	struct line *nextl;	/* next line */
	struct line *curl;	/* current line */
	int doto;		/* current offset */

	if (n < 0)
		return transpose_line_up(f, -n);
	
	if (n == 0)
		n = 1;
	
	while (n--) {
		/* Can't move last line down */
		if (lforw(curwp->w_dotp) == curbp->b_linep)
			return false;
		
		/* Save position in line */
		doto = curwp->w_doto;
		curl = curwp->w_dotp;
		nextl = lforw(curl);
		
		/* Relink the lines */
		curl->l_bp->l_fp = nextl;
		nextl->l_bp = curl->l_bp;
		curl->l_fp = nextl->l_fp;
		nextl->l_fp->l_bp = curl;
		nextl->l_fp = curl;
		curl->l_bp = nextl;
		
		/* Restore position */
		curwp->w_dotp = curl;
		curwp->w_doto = (doto <= llength(curl)) ? doto : llength(curl);
		
		/* Update the display */
		curwp->w_flag |= WFHARD;
	}
	
	return true;
}
