// buffer.c - Buffer management with hash table lookup

#include        <stdio.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "memory.h"
#include "error.h"
#include "undo.h"
#include "string_safe.h"

/*
 * Hash table functions for O(1) buffer lookup by name
 */

/*
 * Simple hash function for buffer names (FNV-1a variant)
 * Returns hash value for fast buffer lookup
 */
static uint32_t buffer_name_hash(const char *name)
{
	uint32_t hash = 2166136261U;
	while (*name) {
		hash ^= (unsigned char)*name++;
		hash *= 16777619U;
	}
	return hash & (BUFFER_HASH_SIZE - 1); // Fast modulo using power of 2
}

/*
 * Add buffer to hash table for O(1) lookup
 */
static void buffer_hash_insert(struct buffer *bp)
{
	if (!bp || !bp->b_bname[0]) return;
	
	uint32_t hash = buffer_name_hash(bp->b_bname);
	
	struct buffer_hash_entry *entry = (struct buffer_hash_entry*)
		safe_alloc(sizeof(struct buffer_hash_entry), "buffer hash entry", __FILE__, __LINE__);
	if (!entry) return;
	
	entry->buffer = bp;
	entry->next = buffer_hash_table[hash];
	buffer_hash_table[hash] = entry;
}

/*
 * Remove buffer from hash table
 */
static void buffer_hash_remove(struct buffer *bp)
{
	if (!bp || !bp->b_bname[0]) return;
	
	uint32_t hash = buffer_name_hash(bp->b_bname);
	struct buffer_hash_entry **entry = &buffer_hash_table[hash];
	
	while (*entry) {
		if ((*entry)->buffer == bp) {
			struct buffer_hash_entry *to_delete = *entry;
			*entry = (*entry)->next;
			SAFE_FREE(to_delete);
			return;
		}
		entry = &(*entry)->next;
	}
}

/*
 * Find buffer by name using O(1) hash table lookup
 * Returns buffer pointer or NULL if not found
 */
static struct buffer *buffer_hash_find(const char *bname)
{
	if (!bname || !bname[0]) return NULL;
	
	uint32_t hash = buffer_name_hash(bname);
	struct buffer_hash_entry *entry = buffer_hash_table[hash];
	
	while (entry) {
		if (strcmp(entry->buffer->b_bname, bname) == 0) {
			return entry->buffer;
		}
		entry = entry->next;
	}
	return NULL;
}

/*
 * Attach a buffer to a window. The
 * values of dot and mark come from the buffer
 * if the use count is 0. Otherwise, they come
 * from some other window.
 */
int usebuffer(int f, int n)
{
	struct buffer *bp;
	int s;
	char bufn[NBUFN];

	if ((s = mlreply("Use buffer: ", bufn, NBUFN)) != TRUE)
		return s;
	if ((bp = bfind(bufn, TRUE, 0)) == NULL) {
		REPORT_ERROR(ERR_BUFFER_INVALID, bufn);
		return FALSE;
	}
	return swbuffer(bp);
}

/*
 * switch to the next buffer in the buffer list
 *
 * int f, n;		default flag, numeric argument
 */
int nextbuffer(int f, int n)
{
	struct buffer *bp = NULL;  /* eligable buffer to switch to */
	struct buffer *bbp;        /* eligable buffer to switch to */

	/* make sure the arg is legit */
	if (f == FALSE)
		n = 1;
	if (n < 1)
		return FALSE;

	bbp = curbp;
	while (n-- > 0) {
		/* advance to the next buffer */
		bp = bbp->b_bufp;

		/* cycle through the buffers to find an eligable one */
		while (bp == NULL || bp->b_flag & BFINVS) {
			if (bp == NULL)
				bp = bheadp;
			else
				bp = bp->b_bufp;

			/* don't get caught in an infinite loop! */
			if (bp == bbp)
				return FALSE;

		}

		bbp = bp;
	}

	return swbuffer(bp);
}

/*
 * make buffer BP current
 */
int swbuffer(struct buffer *bp)
{
	struct window *wp;

	if (--curbp->b_nwnd == 0) {	/* Last use.            */
		curbp->b_dotp = curwp->w_dotp;
		curbp->b_doto = curwp->w_doto;
		curbp->b_markp = curwp->w_markp;
		curbp->b_marko = curwp->w_marko;
	}
	curbp = bp;		/* Switch.              */
	if (curbp->b_active != TRUE) {	/* buffer not active yet */
		/* read it in and activate it */
		readin(curbp->b_fname, TRUE);
		curbp->b_dotp = lforw(curbp->b_linep);
		curbp->b_doto = 0;
		curbp->b_active = TRUE;
		curbp->b_mode |= gmode;	/* P.K. */
	}
	curwp->w_bufp = bp;
	curwp->w_linep = bp->b_linep;	/* For macros, ignored. */
	curwp->w_flag |= WFMODE | WFFORCE | WFHARD;	/* Quite nasty.         */
	if (bp->b_nwnd++ == 0) {	/* First use.           */
		curwp->w_dotp = bp->b_dotp;
		curwp->w_doto = bp->b_doto;
		curwp->w_markp = bp->b_markp;
		curwp->w_marko = bp->b_marko;
		cknewwindow();
		return TRUE;
	}
	wp = wheadp;		/* Look for old.        */
	while (wp != NULL) {
		if (wp != curwp && wp->w_bufp == bp) {
			curwp->w_dotp = wp->w_dotp;
			curwp->w_doto = wp->w_doto;
			curwp->w_markp = wp->w_markp;
			curwp->w_marko = wp->w_marko;
			break;
		}
		wp = wp->w_wndp;
	}
	cknewwindow();
	return TRUE;
}

/*
 * Dispose of a buffer, by name.
 * Ask for the name. Look it up (don't get too
 * upset if it isn't there at all!). Get quite upset
 * if the buffer is being displayed. Clear the buffer (ask
 * if the buffer has been changed). Then free the header
 * line and the buffer header. Bound to "C-X K".
 */
int killbuffer(int f, int n)
{
	struct buffer *bp;
	int s;
	char bufn[NBUFN];

	if ((s = mlreply("Kill buffer: ", bufn, NBUFN)) != TRUE)
		return s;
	if ((bp = bfind(bufn, FALSE, 0)) == NULL)	/* Easy if unknown.     */
		return TRUE;
	if (bp->b_flag & BFINVS)	/* Deal with special buffers        */
		return TRUE;	/* by doing nothing.    */
	return zotbuf(bp);
}

/*
 * kill the buffer pointed to by bp
 */
int zotbuf(struct buffer *bp)
{
	struct buffer *bp1;
	struct buffer *bp2;
	int s;

	if (bp->b_nwnd != 0) {	/* Error if on screen.  */
		REPORT_ERROR(ERR_BUFFER_INVALID, "Buffer is being displayed and cannot be killed");
		return FALSE;
	}
	if ((s = bclear(bp)) != TRUE)	/* Blow text away.      */
		return s;
	
	// Remove buffer from hash table for O(1) lookup
	buffer_hash_remove(bp);
	
	// Free undo stack and all undo/redo history
	if (bp->b_undo_stack) {
		undo_stack_destroy(bp->b_undo_stack);
		bp->b_undo_stack = NULL;
	}
	
	SAFE_FREE(bp->b_linep);	/* Release header line. */
	bp1 = NULL;		/* Find the header.     */
	bp2 = bheadp;
	while (bp2 != bp) {
		bp1 = bp2;
		bp2 = bp2->b_bufp;
	}
	bp2 = bp2->b_bufp;	/* Next one in chain.   */
	if (bp1 == NULL)	/* Unlink it.           */
		bheadp = bp2;
	else
		bp1->b_bufp = bp2;
	SAFE_FREE(bp);	/* Release buffer block */
	return TRUE;
}

/*
 * Rename the current buffer
 *
 * int f, n;		default Flag & Numeric arg
 */
int namebuffer(int f, int n)
{
	struct buffer *bp;	/* pointer to scan through all buffers */
	char bufn[NBUFN];	/* buffer to hold buffer name */

	/* prompt for and get the new buffer name */
      ask:if (mlreply("Change buffer name to: ", bufn, NBUFN) !=
	    TRUE)
		return FALSE;

	/* and check for duplicates using fast O(1) lookup */
	bp = buffer_hash_find(bufn);
	if (bp != NULL && bp != curbp) {
		goto ask;  /* Name already exists, try again */
	}

	// Remove old name from hash table, update name, re-insert
	buffer_hash_remove(curbp);
	safe_strcpy(curbp->b_bname, bufn, NBUFN);	/* copy buffer name to structure */
	buffer_hash_insert(curbp);
	curwp->w_flag |= WFMODE;	/* make mode line replot */
	mlerase();
	return TRUE;
}

/*
 * List all of the active buffers.  First update the special
 * buffer that holds the list.  Next make sure at least 1
 * window is displaying the buffer list, splitting the screen
 * if this is what it takes.  Lastly, repaint all of the
 * windows that are displaying the list.  Bound to "C-X C-B". 
 *
 * A numeric argument forces it to list invisible buffers as
 * well.
 */
int listbuffers(int f, int n)
{
	struct window *wp;
	struct buffer *bp;
	int s;

	if ((s = makelist(f)) != TRUE)
		return s;
	if (blistp->b_nwnd == 0) {	/* Not on screen yet.   */
		if ((wp = wpopup()) == NULL) {
			REPORT_ERROR(ERR_MEMORY, "Failed to create popup window for buffer list");
			return FALSE;
		}
		bp = wp->w_bufp;
		if (--bp->b_nwnd == 0) {
			bp->b_dotp = wp->w_dotp;
			bp->b_doto = wp->w_doto;
			bp->b_markp = wp->w_markp;
			bp->b_marko = wp->w_marko;
		}
		wp->w_bufp = blistp;
		++blistp->b_nwnd;
	}
	wp = wheadp;
	while (wp != NULL) {
		if (wp->w_bufp == blistp) {
			wp->w_linep = lforw(blistp->b_linep);
			wp->w_dotp = lforw(blistp->b_linep);
			wp->w_doto = 0;
			wp->w_markp = NULL;
			wp->w_marko = 0;
			wp->w_flag |= WFMODE | WFHARD;
		}
		wp = wp->w_wndp;
	}
	return TRUE;
}

/*
 * This routine rebuilds the
 * text in the special secret buffer
 * that holds the buffer list. It is called
 * by the list buffers command. Return TRUE
 * if everything works. Return FALSE if there
 * is an error (if there is no memory). Iflag
 * indicates wether to list hidden buffers.
 *
 * int iflag;		list hidden buffer flag
 */
#define MAXLINE	MAXCOL
int makelist(int iflag)
{
	char *cp1;
	char *cp2;
	int c;
	struct buffer *bp;
	struct line *lp;
	int s;
	int i;
	long nbytes;		/* # of bytes in current buffer */
	char b[7 + 1];
	char line[MAXLINE];

	blistp->b_flag &= ~BFCHG;	/* Don't complain!      */
	if ((s = bclear(blistp)) != TRUE)	/* Blow old text away   */
		return s;
    // No need to touch blistp->b_fname here; avoid fortify warnings on zero-sized dest
	if (addline("ACT MODES        Size Buffer        File") == FALSE
	    || addline("--- -----        ---- ------        ----") ==
	    FALSE)
		return FALSE;
	bp = bheadp;		/* For all buffers      */

	/* build line to report global mode settings */
	cp1 = &line[0];
	*cp1++ = ' ';
	*cp1++ = ' ';
	*cp1++ = ' ';
	*cp1++ = ' ';

	/* output the mode codes */
	for (i = 0; i < NUMMODES; i++)
		if (gmode & (1 << i))
			*cp1++ = modecode[i];
		else
			*cp1++ = '.';
    {
        size_t rem = sizeof(line) - (size_t)(cp1 - &line[0]);
        if (rem > 0) safe_strcpy(cp1, "         Global Modes", rem);
    }
	if (addline(line) == FALSE)
		return FALSE;

	/* output the list of buffers */
	while (bp != NULL) {
		/* skip invisable buffers if iflag is false */
		if (((bp->b_flag & BFINVS) != 0) && (iflag != TRUE)) {
			bp = bp->b_bufp;
			continue;
		}
		cp1 = &line[0];	/* Start at left edge   */

		/* output status of ACTIVE flag (has the file been read in? */
		if (bp->b_active == TRUE)	/* "@" if activated       */
			*cp1++ = '@';
		else
			*cp1++ = ' ';

		/* output status of changed flag */
		if ((bp->b_flag & BFCHG) != 0)	/* "*" if changed       */
			*cp1++ = '*';
		else
			*cp1++ = ' ';

		/* report if the file is truncated */
		if ((bp->b_flag & BFTRUNC) != 0)
			*cp1++ = '#';
		else
			*cp1++ = ' ';

		*cp1++ = ' ';	/* space */

		/* output the mode codes */
		for (i = 0; i < NUMMODES; i++) {
			if (bp->b_mode & (1 << i))
				*cp1++ = modecode[i];
			else
				*cp1++ = '.';
		}
		*cp1++ = ' ';	/* Gap.                 */
		nbytes = 0L;	/* Count bytes in buf.  */
		lp = lforw(bp->b_linep);
		while (lp != bp->b_linep) {
			nbytes += (long) llength(lp) + 1L;
			lp = lforw(lp);
		}
		snprintf(b, sizeof(b), "%6ld ", nbytes);
		cp2 = &b[0];
		while ((c = *cp2++) != 0)
			*cp1++ = c;
		*cp1++ = ' ';	/* Gap.                 */
		cp2 = &bp->b_bname[0];	/* Buffer name          */
		while ((c = *cp2++) != 0)
			*cp1++ = c;
		cp2 = &bp->b_fname[0];	/* File name            */
		if (*cp2 != 0) {
			while (cp1 < &line[3 + 1 + 5 + 1 + 6 + 4 + NBUFN])
				*cp1++ = ' ';
			while ((c = *cp2++) != 0) {
				if (cp1 < &line[MAXLINE - 1])
					*cp1++ = c;
			}
		}
		*cp1 = 0;	/* Add to the buffer.   */
		if (addline(line) == FALSE)
			return FALSE;
		bp = bp->b_bufp;
	}
	return TRUE;		/* All done             */
}


/*
 * The argument "text" points to
 * a string. Append this line to the
 * buffer list buffer. Handcraft the EOL
 * on the end. Return TRUE if it worked and
 * FALSE if you ran out of room.
 */
int addline(char *text)
{
	struct line *lp;
	int i;
	int ntext;

	ntext = strlen(text);
	if ((lp = lalloc(ntext)) == NULL) {
		REPORT_ERROR(ERR_MEMORY, "Failed to allocate line for buffer list");
		return FALSE;
	}
	for (i = 0; i < ntext; ++i)
		lputc(lp, i, text[i]);
	blistp->b_linep->l_bp->l_fp = lp;	/* Hook onto the end    */
	lp->l_bp = blistp->b_linep->l_bp;
	blistp->b_linep->l_bp = lp;
	lp->l_fp = blistp->b_linep;
	if (blistp->b_dotp == blistp->b_linep)	/* If "." is at the end */
		blistp->b_dotp = lp;	/* move it to new line  */
	return TRUE;
}

/*
 * Look through the list of
 * buffers. Return TRUE if there
 * are any changed buffers. Buffers
 * that hold magic internal stuff are
 * not considered; who cares if the
 * list of buffer names is hacked.
 * Return FALSE if no buffers
 * have been changed.
 */
int anycb(void)
{
	struct buffer *bp;

	bp = bheadp;
	while (bp != NULL) {
		if ((bp->b_flag & BFINVS) == 0
		    && (bp->b_flag & BFCHG) != 0)
			return TRUE;
		bp = bp->b_bufp;
	}
	return FALSE;
}

/*
 * Find a buffer, by name. Return a pointer
 * to the buffer structure associated with it.
 * If the buffer is not found
 * and the "cflag" is TRUE, create it. The "bflag" is
 * the settings for the flags in in buffer.
 */
struct buffer *bfind(char *bname, int cflag, int bflag)
{
	struct buffer *bp;
	struct buffer *sb;	/* buffer to insert after */
	struct line *lp;

	// Use O(1) hash table lookup instead of O(n) linear search
	bp = buffer_hash_find(bname);
	if (bp != NULL) {
		return bp;
	}
	if (cflag != FALSE) {
		if ((bp = (struct buffer *)safe_alloc(sizeof(struct buffer), "buffer allocation", __FILE__, __LINE__)) == NULL) {
			REPORT_ERROR(ERR_MEMORY, "Failed to allocate buffer structure");
			return NULL;
		}
		if ((lp = lalloc(0)) == NULL) {
			REPORT_ERROR(ERR_MEMORY, "Failed to allocate header line for buffer");
			SAFE_FREE(bp);
			return NULL;
		}
		/* find the place in the list to insert this buffer */
		if (bheadp == NULL || strcmp(bheadp->b_bname, bname) > 0) {
			/* insert at the beginning */
			bp->b_bufp = bheadp;
			bheadp = bp;
		} else {
			sb = bheadp;
			while (sb->b_bufp != NULL) {
				if (strcmp(sb->b_bufp->b_bname, bname) > 0)
					break;
				sb = sb->b_bufp;
			}

			/* and insert it */
			bp->b_bufp = sb->b_bufp;
			sb->b_bufp = bp;
		}

		/* and set up the other buffer fields */
		bp->b_active = TRUE;
		bp->b_dotp = lp;
		bp->b_doto = 0;
		bp->b_markp = NULL;
		bp->b_marko = 0;
		bp->b_flag = bflag;
		bp->b_mode = gmode;
		bp->b_nwnd = 0;
		bp->b_linep = lp;
        safe_strcpy(bp->b_fname, "", NFILEN);
        safe_strcpy(bp->b_bname, bname, NBUFN);
		
		// Initialize cached statistics for instant status line updates
		atomic_store(&bp->b_line_count, 1);  // Empty buffer has 1 line
		atomic_store(&bp->b_byte_count, 0);  // No bytes initially
		atomic_store(&bp->b_word_count, 0);  // No words initially
		atomic_store(&bp->b_stats_dirty, false); // Clean initially
		
		// Initialize atomic undo/redo system for this buffer
		bp->b_undo_stack = undo_stack_create();
		if (!bp->b_undo_stack) {
			REPORT_ERROR(ERR_MEMORY, "Failed to allocate undo stack for buffer");
            SAFE_FREE(bp->b_linep);
            SAFE_FREE(bp);
            return NULL;
		}

		// Set initial saved baseline to the current undo version (clean buffer)
		atomic_store(&bp->b_saved_version_id, 1);
		
		// Add buffer to hash table for instant O(1) lookup
		buffer_hash_insert(bp);
		
#if	CRYPT
		bp->b_key[0] = 0;
#endif
		lp->l_fp = lp;
		lp->l_bp = lp;
	}
	return bp;
}

/*
 * This routine blows away all of the text
 * in a buffer. If the buffer is marked as changed
 * then we ask if it is ok to blow it away; this is
 * to save the user the grief of losing text. The
 * window chain is nearly always wrong if this gets
 * called; the caller must arrange for the updates
 * that are required. Return TRUE if everything
 * looks good.
 */
int bclear(struct buffer *bp)
{
	struct line *lp;
	int s;

	if ((bp->b_flag & BFINVS) == 0	/* Not scratch buffer.  */
	    && (bp->b_flag & BFCHG) != 0	/* Something changed    */
	    && (s = mlyesno("Discard changes")) != TRUE)
		return s;
	bp->b_flag &= ~BFCHG;	/* Not changed          */
	while ((lp = lforw(bp->b_linep)) != bp->b_linep)
		lfree(lp);
	bp->b_dotp = bp->b_linep;	/* Fix "."              */
	bp->b_doto = 0;
	bp->b_markp = NULL;	/* Invalidate "mark"    */
	bp->b_marko = 0;
	
	// Reset cached statistics after clearing buffer
	atomic_store(&bp->b_line_count, 1);  // Empty buffer has 1 line
	atomic_store(&bp->b_byte_count, 0);  // No bytes
	atomic_store(&bp->b_word_count, 0);  // No words
	atomic_store(&bp->b_stats_dirty, false); // Clean
	return TRUE;
}

/*
 * Update buffer statistics incrementally for instant status line updates
 * Called when text is inserted/deleted to maintain cached counts
 */
void buffer_update_stats_incremental(struct buffer *bp, int line_delta, long byte_delta, int word_delta)
{
	if (!bp) return;
	
	// Update cached statistics atomically
	atomic_fetch_add(&bp->b_line_count, line_delta);
	atomic_fetch_add(&bp->b_byte_count, byte_delta);
	atomic_fetch_add(&bp->b_word_count, word_delta);
	
	// Mark as clean since we're maintaining accuracy
	atomic_store(&bp->b_stats_dirty, false);
}

/*
 * Mark buffer statistics as dirty - forces recalculation on next access
 * Used when we can't easily compute incremental changes
 */
void buffer_mark_stats_dirty(struct buffer *bp)
{
	if (!bp) return;
	atomic_store(&bp->b_stats_dirty, true);
}

/*
 * Get buffer statistics instantly using cached values
 * Recalculates only if marked dirty
 */
void buffer_get_stats_fast(struct buffer *bp, int *line_count, long *byte_count, int *word_count)
{
	if (!bp) {
		if (line_count) *line_count = 0;
		if (byte_count) *byte_count = 0;
		if (word_count) *word_count = 0;
		return;
	}
	
	// Check if recalculation needed
	if (atomic_load(&bp->b_stats_dirty)) {
		// Recalculate stats (only when dirty)
		struct line *lp;
		int lines = 0;
		long bytes = 0;
		int words = 0;
		
		lp = bp->b_linep;
		while ((lp = lforw(lp)) != bp->b_linep) {
			lines++;
			bytes += lp->l_used + 1; // +1 for newline
			
			// Count words in line
			bool in_word = false;
			for (int i = 0; i < lp->l_used; i++) {
				char c = lp->l_text[i];
				if (c == ' ' || c == '\t' || c == '\n') {
					in_word = false;
				} else if (!in_word) {
					in_word = true;
					words++;
				}
			}
		}
		if (bytes > 0) bytes--; // Remove last newline
		
		// Update cache atomically
		atomic_store(&bp->b_line_count, lines);
		atomic_store(&bp->b_byte_count, bytes);
		atomic_store(&bp->b_word_count, words);
		atomic_store(&bp->b_stats_dirty, false);
	}
	
	// Return cached values
	if (line_count) *line_count = atomic_load(&bp->b_line_count);
	if (byte_count) *byte_count = atomic_load(&bp->b_byte_count);
	if (word_count) *word_count = atomic_load(&bp->b_word_count);
}

/*
 * unmark the current buffers change flag
 *
 * int f, n;		unused command arguments
 */
int unmark(int f, int n)
{
	curbp->b_flag &= ~BFCHG;
	curwp->w_flag |= WFMODE;
	return TRUE;
}
