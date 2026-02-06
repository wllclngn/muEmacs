// isearch.c - Incremental forward and backward search commands

#include <stdio.h>
#include <stdbool.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "string_utils.h"
#include "terminal/input_state.h"


static int echo_char(int c, int col);
static bool isearch_read_event(input_key_event_t *out);
static int match_pat_behind(const char *patrn);

/* A couple of "own" variables for re-eat */

static int (*saved_get_char) (void);	/* Get character routine */
static int eaten_char = -1;		/* Re-eaten char */

/* A couple more "own" variables for the command string */

static int cmd_buff[CMDBUFLEN];	/* Save the command args here */
static int cmd_offset;			/* Current offset into command buff */
static int cmd_reexecute = -1;		/* > 0 if re-executing command */


/*
 * Subroutine to do incremental reverse search.  It actually uses the
 * same code as the normal incremental search, as both can go both ways.
 */
int risearch(int f, int n)
{
	struct line *curline;		/* Current line on entry              */
	int curoff;		/* Current offset on entry            */

	/* remember the initial . on entry: */

	curline = curwp->w_dotp;	/* Save the current line pointer      */
	curoff = curwp->w_doto;	/* Save the current offset            */

	/* Make sure the search doesn't match where we already are:               */

	move_char_backward(true, 1);	/* Back up a character                */

	if (!(isearch(f, -n))) {	/* Call ISearch backwards       *//* If error in search:                */
		curwp->w_dotp = curline;	/* Reset the line pointer             */
		curwp->w_doto = curoff;	/*  and the offset to original value  */
		curwp->w_flag |= WFMOVE;	/* Say we've moved                    */
		update(false);	/* And force an update                */
		mlwrite("[SEARCH FAILED]");	/* Say we died                        */
		matchlen = (unsigned int)strlen(pat);
	} else
		mlerase();	/* If happy, just erase the cmd line  */
	matchlen = (unsigned int)strlen(pat);
	return true;
}

/*
 * Again, but for the forward direction
 */
int fisearch(int f, int n)
{
	struct line *curline;		/* Current line on entry              */
	int curoff;		/* Current offset on entry            */

	/* remember the initial . on entry: */

	curline = curwp->w_dotp;	/* Save the current line pointer      */
	curoff = curwp->w_doto;	/* Save the current offset            */

	/* do the search */

	if (!(isearch(f, n))) {	/* Call ISearch forwards        *//* If error in search:                */
		curwp->w_dotp = curline;	/* Reset the line pointer             */
		curwp->w_doto = curoff;	/*  and the offset to original value  */
		curwp->w_flag |= WFMOVE;	/* Say we've moved                    */
		update(false);	/* And force an update                */
		mlwrite("[SEARCH FAILED]");	/* Say we died                        */
		matchlen = (unsigned int)strlen(pat);
	} else
		mlerase();	/* If happy, just erase the cmd line  */
	matchlen = (unsigned int)strlen(pat);
	return true;
}

/*
 * Subroutine to do an incremental search.  In general, this works similarly
 * to the older micro-emacs search function, except that the search happens
 * as each character is typed, with the screen and cursor updated with each
 * new search character.
 *
 * While searching forward, each successive character will leave the cursor
 * at the end of the entire matched string.  Typing a Control-S or Control-X
 * will cause the next occurrence of the string to be searched for (where the
 * next occurrence does NOT overlap the current occurrence).  A Control-R will
 * change to a backwards search, META will terminate the search and Control-G
 * will abort the search.  Rubout will back up to the previous match of the
 * string, or if the starting point is reached first, it will delete the
 * last character from the search string.
 *
 * While searching backward, each successive character will leave the cursor
 * at the beginning of the matched string.  Typing a Control-R will search
 * backward for the next occurrence of the string.  Control-S or Control-X
 * will revert the search to the forward direction.  In general, the reverse
 * incremental search is just like the forward incremental search inverted.
 *
 * In all cases, if the search fails, the user will be feeped, and the search
 * will stall until the pattern string is edited back into something that
 * exists (or until the search is aborted).
 */

int isearch(int f, int n)
{
	int status;
	int col;
	int cpos;
	input_key_event_t evt;
	char pat_save[NPAT];
	struct line *curline;
	int curoff;
	int init_direction;

	/* Initialize starting conditions */
	cmd_reexecute = -1;
	cmd_offset = 0;
	cmd_buff[0] = '\0';
	safe_strcpy(pat_save, pat, NPAT);
	curline = curwp->w_dotp;
	curoff = curwp->w_doto;
	init_direction = n;

start_over:
	col = promptpattern("ISearch: ");
	cpos = 0;
	status = true;

	/* Get first character - if Ctrl-S/Ctrl-R, reuse old pattern */
	if (!isearch_read_event(&evt))
		return false;

	if (evt_is_ctrl(&evt, 'S') || evt_is_ctrl(&evt, 'R')) {
		/* Reuse old search string */
		for (cpos = 0; pat[cpos] != 0; cpos++)
			col = echo_char(pat[cpos], col);
		if (evt_is_ctrl(&evt, 'R')) {
			n = -1;
			move_char_backward(true, 1);
		} else {
			n = 1;
		}
		status = scanmore(pat, n);
		if (!isearch_read_event(&evt))
			return false;
	}

	/* Main search loop */
	for (;;) {
		/* ESC or Meta: quit searching successfully */
		if (evt_is_esc(&evt) || evt_has_meta(&evt))
			return true;

		/* Ctrl-G: abort search */
		if (evt_is_ctrl(&evt, 'G'))
			return false;

		/* Ctrl-S: search forward */
		if (evt_is_ctrl(&evt, 'S')) {
			n = 1;
			status = scanmore(pat, n);
			if (!isearch_read_event(&evt))
				return false;
			continue;
		}

		/* Ctrl-R: search backward */
		if (evt_is_ctrl(&evt, 'R')) {
			n = -1;
			/* If pattern matches immediately behind cursor, skip past it first */
			if (pat[0] != '\0' && match_pat_behind(pat)) {
				move_char_backward(true, (int)strlen(pat));
			}
			status = scanmore(pat, n);
			if (!isearch_read_event(&evt))
				return false;
			continue;
		}

		/* Ctrl-Q: quote next character */
		if (evt_is_ctrl(&evt, 'Q')) {
			if (!isearch_read_event(&evt))
				return false;
			/* Fall through to add the quoted char */
		}

		/* Backspace/Delete: undo last character */
		if (evt_is_backspace(&evt)) {
			if (cmd_offset <= 1)
				return true;
			--cmd_offset;
			cmd_buff[--cmd_offset] = '\0';
			curwp->w_dotp = curline;
			curwp->w_doto = curoff;
			n = init_direction;
			safe_strcpy(pat, pat_save, NPAT);
			cmd_reexecute = 0;
			goto start_over;
		}

		/* Get character code */
		int c = evt_char(&evt);

		/* Enter: treat as newline in pattern */
		if (evt_is_enter(&evt))
			c = '\n';

		/* Tab is allowed in pattern */
		if (evt_is_tab(&evt))
			c = '\t';

		/* Non-printable control char: re-eat and exit */
		if (c >= 0 && c < ' ' && c != '\n' && c != '\t') {
			reeat(c);
			return true;
		}

		/* Skip if not a valid character */
		if (c < 0) {
			if (!isearch_read_event(&evt))
				return false;
			continue;
		}

		/* Add character to search pattern */
		pat[cpos++] = (char)c;
		if (cpos >= NPAT) {
			mlwrite("? SEARCH STRING TOO LONG");
			return true;
		}
		pat[cpos] = '\0';
		col = echo_char(c, col);

		if (!status) {
			TTputc(BELL);
			TTflush();
		} else if (!(status = checknext((char)c, pat, n))) {
			status = scanmore(pat, n);
		}

		if (!isearch_read_event(&evt))
			return false;
	}
}

/*
 * Trivial routine to insure that the next character in the search string is
 * still true to whatever we're pointing to in the buffer.  This routine will
 * not attempt to move the "point" if the match fails, although it will 
 * implicitly move the "point" if we're forward searching, and find a match,
 * since that's the way forward isearch works.
 *
 * If the compare fails, we return false and assume the caller will call
 * scanmore or something.
 *
 * char chr;		Next char to look for
 * char *patrn;		The entire search string (incl chr)
 * int dir;		Search direction
 */
int checknext(char chr, const char *patrn, int dir)	/* Check next character in search string */
{
	struct line *curline;	/* current line during scan           */
	int curoff;	/* position within current line       */
	int buffchar;	/* character at current position      */
	int status;		/* how well things go                 */


	/* setup the local scan pointer to current "." */

	curline = curwp->w_dotp;		/* Get the current line structure     */
	curoff = curwp->w_doto;			/* Get the offset within that line    */

	if (dir > 0) {				/* If searching forward                 */
		if (curoff == llength(curline)) {		/* If at end of line                    */
			curline = lforw(curline);		/* Skip to the next line              */
			if (curline == curbp->b_linep)
				return false;			/* Abort if at end of buffer          */
			curoff = 0;				/* Start at the beginning of the line */
			buffchar = '\n';			/* And say the next char is NL        */
		} else
			buffchar = lgetc(curline, curoff++);	/* Get the next char         */
		if ((status = eq((unsigned char)buffchar, (unsigned char)chr)) != 0) {	/* Is it what we're looking for?      */
			curwp->w_dotp = curline;		/* Yes, set the buffer's point        */
			curwp->w_doto = curoff;			/*  to the matched character          */
			curwp->w_flag |= WFMOVE;		/* Say that we've moved               */
		}
		return status;		/* And return the status              */
	} else					/* Else, if reverse search:       */
		return match_pat(patrn);	/* See if we're in the right place    */
}

/*
 * This hack will search for the next occurrence of <pat> in the buffer, either
 * forward or backward.  It is called with the status of the prior search
 * attempt, so that it knows not to bother if it didn't work last time.  If
 * we can't find any more matches, "point" is left where it was before.  If
 * we do find a match, "point" will be at the end of the matched string for
 * forward searches and at the beginning of the matched string for reverse
 * searches.
 *
 * char *patrn;			string to scan for
 * int dir;			direction to search
 */
int scanmore(char *patrn, int dir)	/* search forward or back for a pattern           */
{
	int sts;		/* search status                      */

	if (dir < 0) {		/* reverse search?              */
		rvstrcpy(tap, patrn, NPAT);	/* Put reversed string in tap */
		sts = scanner(tap, DIR_REVERSE, POS_BEGIN);
	} else
		sts = scanner(patrn, DIR_FORWARD, POS_END);	/* Nope. Go forward   */

	if (!sts) {
		TTputc(BELL);	/* Feep if search fails       */
		TTflush();	/* see that the feep feeps    */
	}

	return sts;		/* else, don't even try       */
}

/*
 * Check if the pattern matches immediately BEHIND the cursor.
 * Used before reverse search to detect if we're sitting right after a match.
 * Returns true if pattern ends at current cursor position.
 */
static int match_pat_behind(const char *patrn)
{
	int patlen = (int)strlen(patrn);
	if (patlen == 0) return false;

	/* Calculate start position: patlen chars before cursor */
	struct line *lp = curwp->w_dotp;
	int off = curwp->w_doto;

	/* Move back patlen characters */
	for (int i = 0; i < patlen; i++) {
		if (off == 0) {
			lp = lback(lp);
			if (lp == curbp->b_linep)
				return false;  /* Hit beginning of buffer */
			off = llength(lp);
			/* Account for newline at end of previous line */
			if (patrn[patlen - 1 - i] != '\n')
				return false;
		} else {
			off--;
			if (lgetc(lp, off) != patrn[patlen - 1 - i] &&
			    !eq((unsigned char)lgetc(lp, off), (unsigned char)patrn[patlen - 1 - i]))
				return false;
		}
	}
	return true;
}

/*
 * The following is a worker subroutine used by the reverse search.  It
 * compares the pattern string with the characters at "." for equality. If
 * any characters mismatch, it will return false.
 *
 * This isn't used for forward searches, because forward searches leave "."
 * at the end of the search string (instead of in front), so all that needs to
 * be done is match the last char input.
 *
 * char *patrn;			String to match to buffer
 */
int match_pat(const char *patrn)	/* See if the pattern string matches string at "."   */
{
	size_t i;		/* Generic loop index/offset          */
	int buffchar;	/* character at current position      */
	struct line *curline;	/* current line during scan           */
	int curoff;	/* position within current line       */

	/* setup the local scan pointer to current "." */

	curline = curwp->w_dotp;	/* Get the current line structure     */
	curoff = curwp->w_doto;	/* Get the offset within that line    */

	/* top of per character compare loop: */

	for (i = 0; i < strlen(patrn); i++) {	/* Loop for all characters in patrn   */
		if (curoff == llength(curline)) {	/* If at end of line                    */
			curline = lforw(curline);	/* Skip to the next line              */
			curoff = 0;	/* Start at the beginning of the line */
			if (curline == curbp->b_linep)
				return false;	/* Abort if at end of buffer          */
			buffchar = '\n';	/* And say the next char is NL        */
		} else
			buffchar = lgetc(curline, curoff++);	/* Get the next char         */
		if (!eq((unsigned char)buffchar, (unsigned char)patrn[i]))	/* Is it what we're looking for?      */
			return false;	/* Nope, just punt it then            */
	}
	return true;		/* Everything matched? Let's celebrate */
}

/*
 * Routine to prompt for I-Search string.
 */
int promptpattern(char *prompt)
{
	char tpat[NPAT + 20];

    safe_strcpy(tpat, prompt, sizeof(tpat));	/* copy prompt to output string */
    safe_strcat(tpat, " (", sizeof(tpat));	/* build new prompt string */
	expandp(pat, &tpat[strlen(tpat)], NPAT / 2);	/* add old pattern */
    safe_strcat(tpat, ")<Meta>: ", sizeof(tpat));

	/* check to see if we are executing a command line */
	if (!clexec) {
		mlwrite(tpat);
	}
	return (int)strlen(tpat);
}

/*
 * routine to echo i-search characters
 *
 * int c;		character to be echoed
 * int col;		column to be echoed in
 */
static int echo_char(int c, int col)
{
	movecursor(term.t_nrow, col);	/* Position the cursor        */
	if ((c < ' ') || (c == 0x7F)) {	/* Control character?           */
		switch (c) {	/* Yes, dispatch special cases */
		case '\n':	/* Newline                    */
			TTputc('<');
			TTputc('N');
			TTputc('L');
			TTputc('>');
			col += 3;
			break;

		case '\t':	/* Tab                        */
			TTputc('<');
			TTputc('T');
			TTputc('A');
			TTputc('B');
			TTputc('>');
			col += 4;
			break;

		case 0x7F:	/* Rubout:                    */
			TTputc('^');	/* Output a funny looking     */
			TTputc('?');	/*  indication of Rubout      */
			col++;	/* Count the extra char       */
			break;

		default:	/* Vanilla control char       */
			TTputc('^');	/* Yes, output prefix         */
			TTputc(c + 0x40);	/* Make it "^X"               */
			col++;	/* Count this char            */
		}
	} else
		TTputc(c);	/* Otherwise, output raw char */
	TTflush();		/* Flush the output           */
	return ++col;		/* return the new column no   */
}

/*
 * isearch_read_event: Get next input event for incremental search.
 *
 * Handles command re-execution for repeated searches.
 * Returns true on success, false on error/quit.
 */
static bool isearch_read_event(input_key_event_t *out)
{
	/* See if we're re-executing a saved command */
	if (cmd_reexecute >= 0) {
		int c = cmd_buff[cmd_reexecute++];
		if (c != 0) {
			out->type = KEY_CHAR;
			out->code = (uint32_t)c;
			out->modifiers = 0;
			return true;
		}
	}

	/* Not re-executing - get real input */
	cmd_reexecute = -1;
	update(false);

	if (cmd_offset >= CMDBUFLEN - 1) {
		mlwrite("? COMMAND TOO LONG");
		return false;
	}

	if (input_read_event(out) < 0)
		return false;

	/* Save character for re-execution (only simple chars) */
	int c = evt_char(out);
	if (c >= 0) {
		cmd_buff[cmd_offset++] = c;
		cmd_buff[cmd_offset] = '\0';
	}

	return true;
}

/* Legacy wrapper for backward compatibility */
int get_char(void)
{
	input_key_event_t evt;
	if (!isearch_read_event(&evt))
		return 0x1B;  /* Return ESC on error (will quit search) */
	return evt_char(&evt);
}

/*
 * Character pushback mechanism for incremental search.
 *
 * When isearch needs to "unget" a character (e.g., when the user types
 * a non-search command that should be processed by the main loop),
 * we temporarily redirect term.t_getchar to uneat(), which returns the
 * saved character once, then restores the original input function.
 *
 * This avoids modifying the terminal driver and works with any input source.
 */

/* Called on the next term.t_getchar - returns the pushed-back character: */

int uneat(void)
{
	int c;

	term.t_getchar = saved_get_char;	/* restore the routine address        */
	c = eaten_char;		/* Get the re-eaten char              */
	eaten_char = -1;	/* Clear the old char                 */
	return c;		/* and return the last char           */
}

void reeat(int c)
{
	if (eaten_char != -1)	/* If we've already been here             */
		return /*(nullptr) */ ;	/* Don't do it again                  */
	eaten_char = c;		/* Else, save the char for later      */
	saved_get_char = term.t_getchar;	/* Save the char get routine          */
	term.t_getchar = uneat;	/* Replace it with ours               */
}
