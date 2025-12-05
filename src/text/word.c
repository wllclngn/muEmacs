/*	word.c
 *
 *      The routines in this file implement commands that work word or a
 *      paragraph at a time.  There are all sorts of word mode commands.  If I
 *      do any sentence mode commands, they are likely to be put in this file.
 *
 *	Modified by Petri Kutvonen
 */

#include <stdio.h>
#include <stdbool.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"

/* Word wrap on n-spaces. Back-over whatever precedes the point on the current
 * line and stop on the first word-break or the beginning of the line. If we
 * reach the beginning of the line, jump back to the end of the word and start
 * a new line.	Otherwise, break the line at the word-break, eat it, and jump
 * back to the end of the word.
 * Returns true on success, false on errors.
 *
 * @f: default flag.
 * @n: numeric argument.
 */
int wrapword(int f, int n)
{
	int cnt;	/* size of word wrapped to next line */
	int c;		/* charector temporary */

	/* backup from the <NL> 1 char */
	if (!backchar(0, 1))
		return false;

	/* back up until we aren't in a word,
	   make sure there is a break in the line */
	cnt = 0;
	while (((c = lgetc(curwp->w_dotp, curwp->w_doto)) != ' ')
	       && (c != '\t')) {
		cnt++;
		if (!backchar(0, 1))
			return false;
		/* if we make it to the beginning, start a new line */
		if (curwp->w_doto == 0) {
			gotoeol(false, 0);
			return lnewline();
		}
	}

	/* delete the forward white space */
	if (!forwdel(0, 1))
		return false;

	/* put in a end of line */
	if (!lnewline())
		return false;

	/* and past the first word */
	while (cnt-- > 0) {
		if (forwchar(false, 1) == false)
			return false;
	}
	return true;
}

/*
 * Move the cursor backward by "n" words. All of the details of motion are
 * performed by the "backchar" and "forwchar" routines. Error if you try to
 * move beyond the buffers.
 */
int backword(int f, int n)
{
	if (n < 0)
		return forwword(f, -n);
	if (backchar(false, 1) == false)
		return false;
	while (n--) {
		while (inword() == false) {
			if (backchar(false, 1) == false)
				return false;
		}
		while (inword() != false) {
			if (backchar(false, 1) == false)
				return false;
		}
	}
	return forwchar(false, 1);
}

/*
 * Move the cursor forward by the specified number of words. All of the motion
 * is done by "forwchar". Error if you try and move beyond the buffer's end.
 */
int forwword(int f, int n)
{
	if (n < 0)
		return backword(f, -n);
	while (n--) {
		while (inword() == true) {
			if (forwchar(false, 1) == false)
				return false;
		}

		while (inword() == false) {
			if (forwchar(false, 1) == false)
				return false;
		}
	}
	return true;
}

/*
 * Move the cursor forward by the specified number of words. As you move,
 * convert any characters to upper case. Error if you try and move beyond the
 * end of the buffer. Bound to "M-U".
 */
int upperword(int f, int n)
{
	int c;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if (n < 0)
		return false;
	while (n--) {
		while (inword() == false) {
			if (forwchar(false, 1) == false)
				return false;
		}
		while (inword() != false) {
			c = lgetc(curwp->w_dotp, curwp->w_doto);
			if (islower(c)) {
				c -= 'a' - 'A';
				lputc(curwp->w_dotp, curwp->w_doto, c);
				lchange(WFHARD);
			}
			if (forwchar(false, 1) == false)
				return false;
		}
	}
	return true;
}

/*
 * Move the cursor forward by the specified number of words. As you move
 * convert characters to lower case. Error if you try and move over the end of
 * the buffer. Bound to "M-L".
 */
int lowerword(int f, int n)
{
	int c;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if (n < 0)
		return false;
	while (n--) {
		while (inword() == false) {
			if (forwchar(false, 1) == false)
				return false;
		}
		while (inword() != false) {
			c = lgetc(curwp->w_dotp, curwp->w_doto);
			if (isupper(c)) {
				c += 'a' - 'A';
				lputc(curwp->w_dotp, curwp->w_doto, c);
				lchange(WFHARD);
			}
			if (forwchar(false, 1) == false)
				return false;
		}
	}
	return true;
}

/*
 * Move the cursor forward by the specified number of words. As you move
 * convert the first character of the word to upper case, and subsequent
 * characters to lower case. Error if you try and move past the end of the
 * buffer. Bound to "M-C".
 */
int capword(int f, int n)
{
	int c;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if (n < 0)
		return false;
	while (n--) {
		while (inword() == false) {
			if (forwchar(false, 1) == false)
				return false;
		}
		if (inword() != false) {
			c = lgetc(curwp->w_dotp, curwp->w_doto);
			if (islower(c)) {
				c -= 'a' - 'A';
				lputc(curwp->w_dotp, curwp->w_doto, c);
				lchange(WFHARD);
			}
			if (forwchar(false, 1) == false)
				return false;
			while (inword() != false) {
				c = lgetc(curwp->w_dotp, curwp->w_doto);
				if (isupper(c)) {
					c += 'a' - 'A';
					lputc(curwp->w_dotp, curwp->w_doto,
					      c);
					lchange(WFHARD);
				}
				if (forwchar(false, 1) == false)
					return false;
			}
		}
	}
	return true;
}

/*
 * Kill forward by "n" words. Remember the location of dot. Move forward by
 * the right number of words. Put dot back where it was and issue the kill
 * command for the right number of characters. With a zero argument, just
 * kill one word and no whitespace. Bound to "M-D".
 */
int delfword(int f, int n)
{
	struct line *dotp;	/* original cursor line */
	int doto;	/*      and row */
	int c;		/* temp char */
	long size;		/* # of chars to delete */

	/* don't allow this command if we are in read only mode */
	if (curbp->b_mode & MDVIEW)
		return rdonly();

	/* ignore the command if there is a negative argument */
	if (n < 0)
		return false;

	/* Clear the kill buffer if last command wasn't a kill */
	if ((lastflag & CFKILL) == 0)
		kdelete();
	thisflag |= CFKILL;	/* this command is a kill */

	/* save the current cursor position */
	dotp = curwp->w_dotp;
	doto = curwp->w_doto;

	/* figure out how many characters to give the axe */
	size = 0;

	/* get us into a word.... */
	while (inword() == false) {
		if (forwchar(false, 1) == false)
			return false;
		++size;
	}

	if (n == 0) {
		/* skip one word, no whitespace! */
		while (inword() == true) {
			if (forwchar(false, 1) == false)
				return false;
			++size;
		}
	} else {
		/* skip n words.... */
		while (n--) {

			/* if we are at EOL; skip to the beginning of the next */
			while (curwp->w_doto == llength(curwp->w_dotp)) {
				if (forwchar(false, 1) == false)
					return false;
				++size;
			}

			/* move forward till we are at the end of the word */
			while (inword() == true) {
				if (forwchar(false, 1) == false)
					return false;
				++size;
			}

			/* if there are more words, skip the interword stuff */
			if (n != 0)
				while (inword() == false) {
					if (forwchar(false, 1) == false)
						return false;
					++size;
				}
		}

		/* skip whitespace and newlines */
		while ((curwp->w_doto == llength(curwp->w_dotp)) ||
		       ((c = lgetc(curwp->w_dotp, curwp->w_doto)) == ' ')
		       || (c == '\t')) {
			if (forwchar(false, 1) == false)
				break;
			++size;
		}
	}

	/* restore the original position and delete the words */
	curwp->w_dotp = dotp;
	curwp->w_doto = doto;
	return ldelete(size, true);
}

/*
 * Kill backwards by "n" words. Move backwards by the desired number of words,
 * counting the characters. When dot is finally moved to its resting place,
 * fire off the kill command. Bound to "M-Rubout" and to "M-Backspace".
 */
int delbword(int f, int n)
{
	long size;

	/* don't allow this command if we are in read only mode */
	if (curbp->b_mode & MDVIEW)
		return rdonly();

	/* ignore the command if there is a nonpositive argument */
	if (n <= 0)
		return false;

	/* Clear the kill buffer if last command wasn't a kill */
	if ((lastflag & CFKILL) == 0)
		kdelete();
	thisflag |= CFKILL;	/* this command is a kill */

	if (backchar(false, 1) == false)
		return false;
	size = 0;
	while (n--) {
		while (inword() == false) {
			if (backchar(false, 1) == false)
				return false;
			++size;
		}
		while (inword() != false) {
			++size;
			if (backchar(false, 1) == false)
				goto bckdel;
		}
	}
	if (forwchar(false, 1) == false)
		return false;
      bckdel:return ldelchar(size, true);
}

/*
 * Return true if the character at dot is a character that is considered to be
 * part of a word. The word character list is hard coded. Should be setable.
 */
int inword(void)
{
	int c;

	if (curwp->w_doto == llength(curwp->w_dotp))
		return false;
	c = lgetc(curwp->w_dotp, curwp->w_doto);
	if (isletter(c))
		return true;
	if (c >= '0' && c <= '9')
		return true;
	return false;
}

/*
 * Check if we're at a subword boundary (camelCase or snake_case).
 * Returns true if the cursor is at a position where a subword begins or ends.
 * For example: camelCase has boundaries at 'c' and 'C'.
 *              snake_case has boundaries at 's', '_', and 'c'.
 */
int at_subword_boundary(void)
{
	int c, prevc, nextc;
	
	if (curwp->w_doto == 0)
		return true;  /* Beginning of line is always a boundary */
	
	if (curwp->w_doto >= llength(curwp->w_dotp))
		return true;  /* End of line is always a boundary */
	
	/* Get current, previous, and next characters */
	c = lgetc(curwp->w_dotp, curwp->w_doto);
	prevc = lgetc(curwp->w_dotp, curwp->w_doto - 1);
	
	/* Check for underscore boundaries (snake_case) */
	if (c == '_' || prevc == '_')
		return true;
	
	/* Check for camelCase boundaries */
	if (curwp->w_doto + 1 < llength(curwp->w_dotp)) {
		nextc = lgetc(curwp->w_dotp, curwp->w_doto + 1);
		
		/* Lowercase to uppercase transition (camelCase) */
		if ((prevc >= 'a' && prevc <= 'z') && (c >= 'A' && c <= 'Z'))
			return true;
		
		/* Uppercase to lowercase where next is lowercase (CAPSWord -> CAPS|Word) */
		if ((prevc >= 'A' && prevc <= 'Z') && (c >= 'A' && c <= 'Z') && 
		    (nextc >= 'a' && nextc <= 'z'))
			return true;
	}
	
	/* Digit boundaries */
	if ((prevc >= '0' && prevc <= '9') != (c >= '0' && c <= '9'))
		return true;
	
	return false;
}

/*
 * Move forward by one subword. Stops at camelCase boundaries and underscores.
 * For example: "camelCaseWord" -> stops at 'C' and 'W'
 *              "snake_case_word" -> stops at each underscore and word start
 */
int forwsubword(int f, int n)
{
	if (n < 0)
		return backsubword(f, -n);
	
	while (n--) {
		/* Skip any non-word characters first */
		while (inword() == false) {
			if (forwchar(false, 1) == false)
				return false;
		}
		
		/* Now skip to next subword boundary */
		if (forwchar(false, 1) == false)
			return false;
		
		while (inword() == true && !at_subword_boundary()) {
			if (forwchar(false, 1) == false)
				return false;
		}
	}
	return true;
}

/*
 * Move backward by one subword. Stops at camelCase boundaries and underscores.
 */
int backsubword(int f, int n)
{
	if (n < 0)
		return forwsubword(f, -n);
	
	while (n--) {
		/* Move back one character first */
		if (backchar(false, 1) == false)
			return false;
		
		/* Skip any non-word characters */
		while (inword() == false) {
			if (backchar(false, 1) == false)
				return false;
		}
		
		/* Now skip to previous subword boundary */
		while (inword() == true && !at_subword_boundary()) {
			if (backchar(false, 1) == false)
				return false;
		}
	}
	return true;
}

/*
 * Fill the current paragraph according to the current
 * fill column
 *
 * f and n - deFault flag and Numeric argument
 */
int fillpara(int f, int n)
{
	unicode_t c;		/* current char during scan    */
	unicode_t wbuf[NSTRING];/* buffer for current word      */
	int wordlen;	/* length of current word       */
	int clength;	/* position on line during fill */
	int i;		/* index during word copy       */
	int newlength;	/* tentative new line length    */
	int eopflag;	/* Are we at the End-Of-Paragraph? */
	int firstflag;	/* first word? (needs no space) */
	struct line *eopline;	/* pointer to line just past EOP */
	int dotflag;	/* was the last char a period?  */

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if (fillcol == 0) {	/* no fill column set */
		mlwrite("No fill column set");
		return false;
	}
#if	PKCODE
	justflag = false;
#endif

	/* record the pointer to the line just past the EOP */
	gotoeop(false, 1);
	eopline = lforw(curwp->w_dotp);

	/* and back top the beginning of the paragraph */
	gotobop(false, 1);

	/* initialize various info */
	clength = curwp->w_doto;
	if (clength && lgetc(curwp->w_dotp, 0) == TAB)
		clength = 8;
	wordlen = 0;
	dotflag = false;

	/* scan through lines, filling words */
	firstflag = true;
	eopflag = false;
	while (!eopflag) {
		int bytes = 1;

		/* get the next character in the paragraph */
		if (curwp->w_doto == llength(curwp->w_dotp)) {
			c = ' ';
			if (lforw(curwp->w_dotp) == eopline)
				eopflag = true;
		} else
			bytes = lgetchar(&c);

		/* and then delete it */
		ldelete(bytes, false);

		/* if not a separator, just add it in */
		if (c != ' ' && c != '\t') {
			dotflag = (c == '.');	/* was it a dot */
			if (wordlen < NSTRING - 1)
				wbuf[wordlen++] = c;
		} else if (wordlen) {
			/* at a word break with a word waiting */
			/* calculate tentitive new length with word added */
			newlength = clength + 1 + wordlen;
			if (newlength <= fillcol) {
				/* add word to current line */
				if (!firstflag) {
					linsert(1, ' ');	/* the space */
					++clength;
				}
				firstflag = false;
			} else {
				/* start a new line */
				lnewline();
				clength = 0;
			}

			/* and add the word in in either case */
			for (i = 0; i < wordlen; i++) {
				linsert(1, wbuf[i]);
				++clength;
			}
			if (dotflag) {
				linsert(1, ' ');
				++clength;
			}
			wordlen = 0;
		}
	}
	/* and add a last newline for the end of our new paragraph */
	lnewline();
	return true;
}

#if	PKCODE
/* Fill the current paragraph according to the current
 * fill column and cursor position
 *
 * int f, n;		deFault flag and Numeric argument
 */
int justpara(int f, int n)
{
	unicode_t c;		/* current char durring scan    */
	unicode_t wbuf[NSTRING];/* buffer for current word      */
	int wordlen;	/* length of current word       */
	int clength;	/* position on line during fill */
	int i;		/* index during word copy       */
	int newlength;	/* tentative new line length    */
	int eopflag;	/* Are we at the End-Of-Paragraph? */
	int firstflag;	/* first word? (needs no space) */
	struct line *eopline;	/* pointer to line just past EOP */
	int leftmarg;		/* left marginal */

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if (fillcol == 0) {	/* no fill column set */
		mlwrite("No fill column set");
		return false;
	}
	justflag = true;
	leftmarg = curwp->w_doto;
	if (leftmarg + 10 > fillcol) {
		leftmarg = 0;
		mlwrite("Column too narrow");
		return false;
	}

	/* record the pointer to the line just past the EOP */
	gotoeop(false, 1);
	eopline = lforw(curwp->w_dotp);

	/* and back top the beginning of the paragraph */
	gotobop(false, 1);

	/* initialize various info */
	if (leftmarg < llength(curwp->w_dotp))
		curwp->w_doto = leftmarg;

	clength = curwp->w_doto;
	if (clength && lgetc(curwp->w_dotp, 0) == TAB)
		clength = 8;

	wordlen = 0;

	/* scan through lines, filling words */
	firstflag = true;
	eopflag = false;
	while (!eopflag) {
		int bytes = 1;

		/* get the next character in the paragraph */
		if (curwp->w_doto == llength(curwp->w_dotp)) {
			c = ' ';
			if (lforw(curwp->w_dotp) == eopline)
				eopflag = true;
		} else
			bytes = lgetchar(&c);

		/* and then delete it */
		ldelete(bytes, false);

		/* if not a separator, just add it in */
		if (c != ' ' && c != '\t') {
			if (wordlen < NSTRING - 1)
				wbuf[wordlen++] = c;
		} else if (wordlen) {
			/* at a word break with a word waiting */
			/* calculate tentitive new length with word added */
			newlength = clength + 1 + wordlen;
			if (newlength <= fillcol) {
				/* add word to current line */
				if (!firstflag) {
					linsert(1, ' ');	/* the space */
					++clength;
				}
				firstflag = false;
			} else {
				/* start a new line */
				lnewline();
				for (i = 0; i < leftmarg; i++)
					linsert(1, ' ');
				clength = leftmarg;
			}

			/* and add the word in in either case */
			for (i = 0; i < wordlen; i++) {
				linsert(1, wbuf[i]);
				++clength;
			}
			wordlen = 0;
		}
	}
	/* and add a last newline for the end of our new paragraph */
	lnewline();

	forwword(false, 1);
	if (llength(curwp->w_dotp) > leftmarg)
		curwp->w_doto = leftmarg;
	else
		curwp->w_doto = llength(curwp->w_dotp);

	justflag = false;
	return true;
}
#endif

/*
 * delete n paragraphs starting with the current one
 *
 * int f	default flag
 * int n	# of paras to delete
 */
int killpara(int f, int n)
{
	int status;	/* returned status of functions */

	while (n--) {		/* for each paragraph to delete */

		/* mark out the end and beginning of the para to delete */
		gotoeop(false, 1);

		/* set the mark here */
		curwp->w_markp = curwp->w_dotp;
		curwp->w_marko = curwp->w_doto;

		/* go to the beginning of the paragraph */
		gotobop(false, 1);
		curwp->w_doto = 0;	/* force us to the beginning of line */

		/* and delete it */
		if ((status = killregion(false, 1)) != true)
			return status;

		/* and clean up the 2 extra lines */
		ldelete(2L, true);
	}
	return true;
}


/*
 *	wordcount:	count the # of words in the marked region,
 *			along with average word sizes, # of chars, etc,
 *			and report on them.
 *
 * int f, n;		ignored numeric arguments
 */
int wordcount(int f, int n)
{
	struct line *lp;	/* current line to scan */
	int offset;	/* current char to scan */
	long size;		/* size of region left to count */
	int ch;	/* current character to scan */
	int wordflag;	/* are we in a word now? */
	int lastword;	/* were we just in a word? */
	long nwords;		/* total # of words */
	long nchars;		/* total number of chars */
	int nlines;		/* total number of lines in region */
	int avgch;		/* average number of chars/word */
	int status;		/* status return code */
	struct region region;		/* region to look at */

	/* make sure we have a region to count */
	if ((status = getregion(&region)) != true)
		return status;
	lp = region.r_linep;
	offset = region.r_offset;
	size = region.r_size;

	/* count up things */
	lastword = false;
	nchars = 0L;
	nwords = 0L;
	nlines = 0;
	while (size--) {

		/* get the current character */
		if (offset == llength(lp)) {	/* end of line */
			ch = '\n';
			lp = lforw(lp);
			offset = 0;
			++nlines;
		} else {
			ch = lgetc(lp, offset);
			++offset;
		}

		/* and tabulate it */
		wordflag = (
#if	PKCODE
				   (isletter(ch)) ||
#else
				   (ch >= 'a' && ch <= 'z') ||
				   (ch >= 'A' && ch <= 'Z') ||
#endif
				   (ch >= '0' && ch <= '9'));
		if (wordflag == true && lastword == false)
			++nwords;
		lastword = wordflag;
		++nchars;
	}

	/* and report on the info */
	if (nwords > 0L)
		avgch = (int) ((100L * nchars) / nwords);
	else
		avgch = 0;

	mlwrite("Words %D Chars %D Lines %d Avg chars/word %f",
		nwords, nchars, nlines + 1, avgch);
	return true;
}
