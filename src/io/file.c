/*	file.c
 *
 *	The routines in this file handle the reading, writing
 *	and lookup of disk files.  All of details about the
 *	reading and writing of the disk are in "fileio.c".
 *
 *	modified by Petri Kutvonen
 */

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "gapbuffer.h"
#include "util.h"
#include "error.h"
#include "file_utils.h"
#include "memory.h"
#include "string_utils.h"
#include "util/logger.h"

/* Buffer index and stats functions - declared in estruct.h */
extern int buffer_index_insert(struct buffer *bp, int line_idx, struct line *lp);
extern void buffer_update_stats_incremental(struct buffer *bp, int line_delta, long byte_delta, int word_delta);
extern void buffer_get_stats_fast(struct buffer *bp, int *line_count, long *byte_count, int *word_count);
extern int buffer_rebuild_index(struct buffer *bp);

/* Max number of lines from one file. */
#define	MAXNLINE 10000000

/*
 * Read a file into the current
 * buffer. This is really easy; all you do it
 * find the name of the file, and call the standard
 * "read a file into the current buffer" code.
 * Bound to "C-X C-R".
 */
int fileread(int f, int n)
{
	int s;
	char fname[NFILEN];

	if (restflag)		/* don't allow this command if restricted */
		return resterr();
	if ((s = minibuf_read("READ FILE: ", fname, NFILEN)) != true)
		return s;
	return readin(fname, true);
}

/*
 * Insert a file into the current
 * buffer. This is really easy; all you do it
 * find the name of the file, and call the standard
 * "insert a file into the current buffer" code.
 * Bound to "C-X C-I".
 */
int insfile(int f, int n)
{
	int s;
	char fname[NFILEN];

	if (restflag)		/* don't allow this command if restricted */
		return resterr();
	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if ((s = minibuf_read("INSERT FILE: ", fname, NFILEN)) != true)
		return s;
	if ((s = ifile(fname)) != true)
		return s;
	return reposition(true, -1);
}

/*
 * Select a file for editing.
 * Look around to see if you can find the
 * fine in another buffer; if you can find it
 * just switch to the buffer. If you cannot find
 * the file, create a new buffer, read in the
 * text, and switch to the new buffer.
 * Bound to C-X C-F.
 */
int filefind(int f, int n)
{
	char fname[NFILEN];	/* file user wishes to find */
	int s;		/* status return */

	if (restflag)		/* don't allow this command if restricted */
		return resterr();
	if ((s = minibuf_read("FIND FILE: ", fname, NFILEN)) != true)
		return s;
	return getfile(fname, true);
}

int viewfile(int f, int n)
{				/* visit a file in VIEW mode */
	char fname[NFILEN];	/* file user wishes to find */
	int s;		/* status return */
	struct window *wp;	/* scan for windows that need updating */

	if (restflag)		/* don't allow this command if restricted */
		return resterr();
	if ((s = minibuf_read("VIEW FILE: ", fname, NFILEN)) != true)
		return s;
	s = getfile(fname, false);
	if (s) {		/* if we succeed, put it in view mode */
		curwp->w_bufp->b_mode |= MDVIEW;

		/* scan through and update mode lines of all windows */
		wp = wheadp;
		while (wp != nullptr) {
			wp->w_flag |= WFMODE;
			wp = wp->w_wndp;
		}
	}
	return s;
}

/* Old CRYPT encryption removed - use encrypt.c with gpg/age/openssl */

/*
 * getfile()
 *
 * char fname[];	file name to find
 * int lockfl;		check the file for locks?
 */
int getfile(const char *fname, int lockfl)
{
	struct buffer *bp;
	struct line *lp;
	int i;
	int s;
	char bname[NBUFN];	/* buffer name to put file */

	for (bp = bheadp; bp != nullptr; bp = bp->b_bufp) {
		if ((bp->b_flag & BFINVS) == 0
		    && strcmp(bp->b_fname, fname) == 0) {
			swbuffer(bp);
			lp = curwp->w_dotp;
			i = curwp->w_ntrows / 2;
			while (i-- && lback(lp) != curbp->b_linep)
				lp = lback(lp);
			curwp->w_linep = lp;
			curwp->w_flag |= WFMODE | WFHARD;
			mlwrite("[OLD BUFFER]");
			return true;
		}
	}
	makename(bname, fname);	/* New buffer name.     */
	while ((bp = bfind(bname, false, 0)) != nullptr) {
		/* old buffer name conflict code */
		s = minibuf_read("BUFFER NAME: ", bname, NBUFN);
		if (s == ABORT)	/* ^G to just quit      */
			return s;
		if (s == false) {	/* CR to clobber it     */
			makename(bname, fname);
			break;
		}
	}
	if (bp == nullptr && (bp = bfind(bname, true, 0)) == nullptr) {
		REPORT_ERROR(ERR_BUFFER_INVALID, bname);
		return false;
	}
	if (--curbp->b_nwnd == 0) {	/* Undisplay.           */
		curbp->b_dotp = curwp->w_dotp;
		curbp->b_doto = curwp->w_doto;
		curbp->b_markp = curwp->w_markp;
		curbp->b_marko = curwp->w_marko;
	}
	curbp = bp;		/* Switch to it.        */
	curwp->w_bufp = bp;
	curbp->b_nwnd++;
	s = readin(fname, lockfl);	/* Read it in.          */
	return s;
}

/*
 * Read file "fname" into the current buffer, blowing away any text
 * found there.  Called by both the read and find commands.  Return
 * the final status of the read.  Also called by the mainline, to
 * read in a file specified on the command line as an argument.
 * The command bound to M-FNR is called after the buffer is set up
 * and before it is read.
 *
 * char fname[];	name of file to read
 * int lockfl;		check for file locks?
 */
int readin(const char *fname, int lockfl)
{
	struct line *lp1;
	struct line *lp2;
	int i;
	struct window *wp;
	struct buffer *bp;
	int s;
	int nbytes;
	int nline;
	char mesg[NSTRING];

	/* Old CRYPT removed */
	bp = curbp;		/* Cheap.               */
	if ((s = bclear(bp)) != true)	/* Might be old.        */
		return s;
	bp->b_flag &= ~(BFINVS | BFCHG);
	mystrscpy(bp->b_fname, fname, NFILEN);

	if ((s = ffropen(fname)) == FIOERR)	/* Hard file open.      */
		goto out;

	if (s == FIOFNF) {	/* File not found.      */
		mlwrite("[NEW FILE]");
		goto out;
	}

	/* read the file in */
	mlwrite("[READING FILE]");
	nline = 0;
	while ((s = ffgetline()) == FIOSUC) {
		nbytes = strlen(fline);
		if ((lp1 = lalloc(nbytes)) == nullptr) {
			REPORT_ERROR(ERR_MEMORY, "FAILED TO ALLOCATE MEMORY FOR FILE LINE");
			s = FIOMEM;	/* Keep message on the  */
			break;	/* display.             */
		}
		if (nline > MAXNLINE) {
			s = FIOMEM;
			break;
		}
		lp2 = lback(curbp->b_linep);
		lp2->l_fp = lp1;
		lp1->l_fp = curbp->b_linep;
		lp1->l_bp = lp2;
		curbp->b_linep->l_bp = lp1;
		for (i = 0; i < nbytes; ++i)
			lputc(lp1, i, fline[i]);
#if UEMACS_DEBUG_LOG
		/* DEBUG: Check first line bytes after lputc */
		if (nline == 0 && nbytes >= 3) {
			unsigned char b0 = (unsigned char)gap_buffer_get_char(lp1->gb, 0);
			unsigned char b1 = (unsigned char)gap_buffer_get_char(lp1->gb, 1);
			unsigned char b2 = (unsigned char)gap_buffer_get_char(lp1->gb, 2);
			unsigned char b3 = nbytes > 3 ? (unsigned char)gap_buffer_get_char(lp1->gb, 3) : 0;
			LOG_DEBUGF("AFTER_LPUTC: line=0 lp=%p gb=%p gbuf[0..3] = %02X %02X %02X %02X (gap_start=%zu raw=%02X %02X %02X %02X)",
			           (void*)lp1, (void*)lp1->gb, b0, b1, b2, b3,
			           lp1->gb->gap_start,
			           (unsigned char)lp1->gb->data[0], (unsigned char)lp1->gb->data[1],
			           (unsigned char)lp1->gb->data[2], (unsigned char)lp1->gb->data[3]);
		}
#endif
		++nline;
	}
	ffclose();		/* Ignore errors.       */
    safe_strcpy(mesg, "(", NSTRING);
	if (s == FIOERR) {
        safe_strcat(mesg, "I/O ERROR, ", NSTRING);
		curbp->b_flag |= BFTRUNC;
	}
	if (s == FIOMEM) {
        safe_strcat(mesg, "OUT OF MEMORY, ", NSTRING);
		curbp->b_flag |= BFTRUNC;
	}
	size_t mesg_len = strlen(mesg);
	safe_snprintf(&mesg[mesg_len], NSTRING - mesg_len, "Read %d line", nline);
    if (nline != 1)
        safe_strcat(mesg, "s", NSTRING);
    safe_strcat(mesg, ")", NSTRING);
	mlwrite(mesg);

	// Force recalculation of buffer statistics and rebuild line index after file load
	// This is critical: b_line_count and b_line_index must be accurate before any editing
	if (curbp) {
		buffer_mark_stats_dirty(curbp);
		// Force immediate recalculation by calling buffer_get_stats_fast
		int actual_lines = 0;
		buffer_get_stats_fast(curbp, &actual_lines, nullptr, nullptr);
		// Rebuild line index from linked list
		buffer_rebuild_index(curbp);
		LOG_INFOF("File: Loaded %d lines, b_line_count now %d", nline, actual_lines);
	}

      out:
	/* Reset horizontal scroll position - ensures cursor starts at column 1 */
	lbound = 0;

	for (wp = wheadp; wp != nullptr; wp = wp->w_wndp) {
		if (wp->w_bufp == curbp) {
			wp->w_linep = lforw(curbp->b_linep);
			wp->w_dotp = lforw(curbp->b_linep);
			wp->w_doto = 0;
			wp->w_markp = nullptr;
			wp->w_marko = 0;
			wp->w_flag |= WFMODE | WFHARD;
		}
	}
	if (s == FIOERR || s == FIOFNF) /* False if error.      */
		return false;
	/* Successful read: mark current state as saved baseline */
	undo_mark_saved(curbp);
	return true;
}

/*
 * Take a file name, and from it
 * fabricate a buffer name. This routine knows
 * about the syntax of file names on the target system.
 * I suppose that this information could be put in
 * a better place than a line of code.
 */
void makename(char *bname, const char *fname)
{
	const char *cp1;
	char *cp2;

	cp1 = &fname[0];
	while (*cp1 != 0)
		++cp1;

	while (cp1 != &fname[0] && cp1[-1] != '/')
		--cp1;
	cp2 = &bname[0];
	while (cp2 != &bname[NBUFN - 1] && *cp1 != 0 && *cp1 != ';')
		*cp2++ = *cp1++;
	*cp2 = 0;
}

/*
 * make sure a buffer name is unique
 *
 * char *name;		name to check on
 */
void unqname(char *name)
{
	char *sp;

	/* check to see if it is in the buffer list */
	while (bfind(name, 0, false) != nullptr) {

		/* go to the end of the name */
		sp = name;
		while (*sp)
			++sp;
		if (sp == name || (*(sp - 1) < '0' || *(sp - 1) > '8')) {
			*sp++ = '0';
			*sp = 0;
		} else
			*(--sp) += 1;
	}
}

/*
 * Ask for a file name, and write the
 * contents of the current buffer to that file.
 * Update the remembered file name and clear the
 * buffer changed flag. This handling of file names
 * is different from the earlier versions, and
 * is more compatable with Gosling EMACS than
 * with ITS EMACS. Bound to "C-X C-W".
 */
int filewrite(int f, int n)
{
	struct window *wp;
	int s;
	char fname[NFILEN];

	if (restflag)		/* don't allow this command if restricted */
		return resterr();
	if ((s = minibuf_read("WRITE FILE: ", fname, NFILEN)) != true)
		return s;
	if ((s = writeout(fname)) == true) {
		safe_strcpy(curbp->b_fname, fname, NFILEN);
		/* Mark saved baseline so undo-to-clean clears delta */
		undo_mark_saved(curbp);
	}
	return s;
}

/*
 * Save the contents of the current
 * buffer in its associatd file. No nothing
 * if nothing has changed (this may be a bug, not a
 * feature). Error if there is no remembered file
 * name for the buffer. Bound to "C-X C-S". May
 * get called by "C-Z".
 */
int filesave(int f, int n)
{
	struct window *wp;
	int s;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if ((curbp->b_flag & BFCHG) == 0)	/* Return, no changes.  */
		return true;
	if (curbp->b_fname[0] == 0) {	/* Must have a name.    */
		mlwrite("NO FILE NAME");
		return false;
	}

	/* complain about truncated files */
	if ((curbp->b_flag & BFTRUNC) != 0) {
		if (mlyesno("Truncated file ... write it out") == false) {
			mlwrite("[ABORTED]");
			return false;
		}
	}

	if ((s = writeout(curbp->b_fname)) == true) {
		curbp->b_flag &= ~BFCHG;
		wp = wheadp;	/* Update mode lines.   */
		while (wp != nullptr) {
			if (wp->w_bufp == curbp)
				wp->w_flag |= WFMODE;
			wp = wp->w_wndp;
		}
	}
	return s;
}

/*
 * This function performs the details of file
 * writing. Uses the file management routines in the
 * "fileio.c" package. The number of lines written is
 * displayed. Sadly, it looks inside a struct line; provide
 * a macro for this. Most of the grief is error
 * checking of some sort.
 */
/*
 * This function performs the details of file writing. Uses the file 
 * management routines in the "fileio.c" package. The number of lines 
 * written is displayed. Most of the grief is error checking of some sort.
 */
int writeout(const char *fn)
{
	int s;
	struct line *lp;
	int nline;
	char mesg[NSTRING];

	/* Pre-save processing: trim trailing whitespace if enabled */
	if (trim_trailing_whitespace) {
		struct line *tlp = lforw(curbp->b_linep);
		while (tlp != curbp->b_linep) {
			int len = llength(tlp);
			if (len > 0) {
				/* Find last non-whitespace */
				int new_len = len;
				while (new_len > 0) {
					int c = lgetc(tlp, new_len - 1);
					if (c != ' ' && c != '\t')
						break;
					new_len--;
				}
				/* Trim if needed */
				if (new_len < len) {
					gap_buffer_delete(tlp->gb, new_len, len - new_len);
				}
			}
			tlp = lforw(tlp);
		}
	}

	/* Pre-save processing: ensure final newline if enabled */
	/* Note: ffputline already adds newline after each line, so files
	 * written by μEmacs naturally end with a newline. This check ensures
	 * files with empty last lines still get proper newline treatment. */

	/* Open the file for writing */
	if ((s = ffwopen(fn)) != FIOSUC) {
		REPORT_ERROR(ERR_FILE_WRITE, fn);
		return false;
	}

	/* Write out each line */
	lp = lforw(curbp->b_linep);		/* First line */
	nline = 0;
	while (lp != curbp->b_linep) {		/* Until end of buffer */
		int len = llength(lp);
		char *text = nullptr;

		if (len > 0) {
			text = SAFE_ARRAY(char, len, "file write");
			if (!text) {
				REPORT_ERROR(ERR_MEMORY, "Memory allocation failed during write");
				return false;
			}
			size_t copied_len = gap_buffer_get_text(lp->gb, 0, len, text, len);
			if (len > 0 && copied_len == 0) {
				REPORT_ERROR(ERR_MEMORY, "FAILED TO READ LINE FROM GAP BUFFER");
				SAFE_FREE(text);
				return false;
			}
		}

		s = ffputline(text, len);

		if (text) {
			SAFE_FREE(text);
		}

		if (s != FIOSUC) {
			break;
		}
		++nline;
		lp = lforw(lp);
	}

	/* Close the file */
	if ((s = ffclose()) != FIOSUC) {
		REPORT_ERROR(ERR_FILE_WRITE, fn);
		return false;
	}

	/* Report results */
	if (s == FIOERR) {
		REPORT_ERROR(ERR_FILE_WRITE, fn);
		return false;
	}

	/* Success! */
	safe_strcpy(mesg, "[WROTE ", NSTRING);
	size_t mesg_len = strlen(mesg);
	safe_snprintf(&mesg[mesg_len], NSTRING - mesg_len, "%d LINE", nline);
	if (nline != 1)
		safe_strcat(mesg, "S", NSTRING);
	safe_strcat(mesg, "]", NSTRING);
	mlwrite(mesg);
	return true;
}


/*
 * The command allows the user
 * to modify the file name associated with
 * the current buffer. It is like the "f" command
 * in UNIX "ed". The operation is simple; just zap
 * the name in the buffer structure, and mark the windows
 * as needing an update. You can type a blank line at the
 * prompt if you wish.
 */
int filename(int f, int n)
{
	struct window *wp;
	int s;
	char fname[NFILEN];

	if (restflag)		/* don't allow this command if restricted */
		return resterr();
	if ((s = minibuf_read("NAME: ", fname, NFILEN)) == ABORT)
		return s;
    if (s == false) {
        safe_strcpy(curbp->b_fname, "", NFILEN);
    } else {
        safe_strcpy(curbp->b_fname, fname, NFILEN);
    }
	wp = wheadp;		/* Update mode lines.   */
	while (wp != nullptr) {
		if (wp->w_bufp == curbp)
			wp->w_flag |= WFMODE;
		wp = wp->w_wndp;
	}
	curbp->b_mode &= ~MDVIEW;	/* no longer read only mode */
	return true;
}

/*
 * Insert file "fname" into the current
 * buffer, Called by insert file command. Return the final
 * status of the read.
 */
int ifile(const char *fname)
{
	struct line *lp0;
	struct line *lp1;
	struct line *lp2;
	int i;
	struct buffer *bp;
	int s;
	int nbytes;
	int nline;
	char mesg[NSTRING];

	bp = curbp;		/* Cheap.               */
	bp->b_flag |= BFCHG;	/* we have changed      */
	bp->b_flag &= ~BFINVS;	/* and are not temporary */
	if ((s = ffropen(fname)) == FIOERR)	/* Hard file open.      */
		goto out;
	if (s == FIOFNF) {	/* File not found.      */
		mlwrite("[NO SUCH FILE]");
		return false;
	}
	mlwrite("[INSERTING FILE]");

	/* Old CRYPT removed */
	/* back up a line and save the mark here */
	curwp->w_dotp = lback(curwp->w_dotp);
	curwp->w_doto = 0;
	curwp->w_markp = curwp->w_dotp;
	curwp->w_marko = 0;

	nline = 0;
	while ((s = ffgetline()) == FIOSUC) {
		nbytes = strlen(fline);
		if ((lp1 = lalloc(nbytes)) == nullptr) {
			REPORT_ERROR(ERR_MEMORY, "FAILED TO ALLOCATE MEMORY FOR FILE LINE");
			s = FIOMEM;	/* Keep message on the  */
			break;	/* display.             */
		}
		lp0 = curwp->w_dotp;	/* line previous to insert */
		lp2 = lp0->l_fp;	/* line after insert */

		/* re-link new line between lp0 and lp2 */
		lp2->l_bp = lp1;
		lp0->l_fp = lp1;
		lp1->l_bp = lp0;
		lp1->l_fp = lp2;

		/* and advance and write out the current line */
		curwp->w_dotp = lp1;
		for (i = 0; i < nbytes; ++i)
			lputc(lp1, i, fline[i]);
		++nline;
	}
	ffclose();		/* Ignore errors.       */
	curwp->w_markp = lforw(curwp->w_markp);
    safe_strcpy(mesg, "(", NSTRING);
	if (s == FIOERR) {
        safe_strcat(mesg, "I/O ERROR, ", NSTRING);
		curbp->b_flag |= BFTRUNC;
	}
	if (s == FIOMEM) {
        safe_strcat(mesg, "OUT OF MEMORY, ", NSTRING);
		curbp->b_flag |= BFTRUNC;
	}
	size_t mesg_len = strlen(mesg);
	safe_snprintf(&mesg[mesg_len], NSTRING - mesg_len, "Inserted %d line", nline);
    if (nline > 1)
        safe_strcat(mesg, "s", NSTRING);
    safe_strcat(mesg, ")", NSTRING);
	mlwrite(mesg);

	// Force recalculation of buffer statistics and rebuild line index
	// This is critical since we bypassed normal line insertion
	if (curbp && nline > 0) {
		buffer_mark_stats_dirty(curbp);
		buffer_get_stats_fast(curbp, nullptr, nullptr, nullptr);
		buffer_rebuild_index(curbp);
	}

      out:
	/* advance to the next line and mark the window for changes */
	curwp->w_dotp = lforw(curwp->w_dotp);
	curwp->w_flag |= WFHARD | WFMODE;

	/* copy window parameters back to the buffer structure */
	curbp->b_dotp = curwp->w_dotp;
	curbp->b_doto = curwp->w_doto;
	curbp->b_markp = curwp->w_markp;
	curbp->b_marko = curwp->w_marko;

	if (s == FIOERR)	/* False if error.      */
		return false;
	return true;
}
