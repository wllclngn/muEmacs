// buffer.c - Buffer management with hash table lookup

#include        <stdio.h>
#include        <signal.h>
#include        <sys/stat.h>
#include        <sys/wait.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "memory.h"
#include "error.h"
#include "undo.h"
#include "string_utils.h"
#include "util/logger.h"
#include "terminal/terminal.h"
#include "internal/syntax.h"
#include "internal/text_storage.h"
#include "internal/event_bus.h"
#include "util/file_preload.h"

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
	if (!entry) {
		LOG_ERROR("Buffer: Hash entry allocation failed");
		return;
	}
	
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
 * Returns buffer pointer or nullptr if not found
 */
static struct buffer *buffer_hash_find(const char *bname)
{
	if (!bname || !bname[0]) return nullptr;
	
	uint32_t hash = buffer_name_hash(bname);
	struct buffer_hash_entry *entry = buffer_hash_table[hash];
	
	while (entry) {
		if (strcmp(entry->buffer->b_bname, bname) == 0) {
			return entry->buffer;
		}
		entry = entry->next;
	}
	return nullptr;
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

	if ((s = minibuf_read("USE BUFFER: ", bufn, NBUFN)) != true)
		return s;
	if ((bp = bfind(bufn, true, 0)) == nullptr) {
		REPORT_ERROR(ERR_BUFFER_INVALID, bufn);
		return false;
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
	struct buffer *bp = nullptr;  /* eligable buffer to switch to */
	struct buffer *bbp;        /* eligable buffer to switch to */

	/* make sure the argument is legit */
	if (f == false)
		n = 1;
	if (n < 1)
		return false;

	bbp = curbp;
	while (n-- > 0) {
		/* advance to the next buffer */
		bp = bbp->b_bufp;

		/* cycle through the buffers to find an eligable one */
		while (bp == nullptr || bp->b_flag & BFINVS) {
			if (bp == nullptr)
				bp = bheadp;
			else
				bp = bp->b_bufp;

			/* don't get caught in an infinite loop! */
			if (bp == bbp)
				return false;

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

	if (--curbp->b_nwnd == 0) { // Last use.
		curbp->b_dotp = curwp->w_dotp;
		curbp->b_doto = curwp->w_doto;
		curbp->b_markp = curwp->w_markp;
		curbp->b_marko = curwp->w_marko;
	}
	curbp = bp; // Switch.
	if (curbp->b_active != true) { // buffer not active yet
		int status;
		/* Check file size - large files use piece table (mmap) for O(1) loading */
		struct stat st;
		bool use_piece_table = (stat(curbp->b_fname, &st) == 0 &&
		                        st.st_size >= (off_t)STORAGE_THRESHOLD_DEFAULT);

		if (use_piece_table) {
			/* Large file: skip preload, use mmap + piece table */
			status = readin(curbp->b_fname, true);
		} else {
			/* Try preloaded data first (async file read) */
			char *preload_data = nullptr;
			size_t preload_size = 0;
			if (file_preload_get(curbp->b_fname, &preload_data, &preload_size) == 0) {
				/* Use preloaded data - no disk I/O needed */
				status = readin_from_memory(preload_data, preload_size);
				free(preload_data);
			} else {
				/* Fall back to synchronous disk read */
				status = readin(curbp->b_fname, true);
			}
		}
		curbp->b_dotp = lforw(curbp->b_linep);
		curbp->b_doto = 0;
		curbp->b_active = true;
		curbp->b_mode |= (uint32_t)gmode; // P.K.
		if (status != true)
			mlwrite("[Error reading %s]", curbp->b_fname);
	}
	curwp->w_bufp = bp;
	curwp->w_linep = bp->b_linep; // For macros, ignored.
	curwp->w_flag |= WFMODE | WFFORCE | WFHARD; // Quite nasty.
	if (bp->b_nwnd++ == 0) { // First use.
		curwp->w_dotp = bp->b_dotp;
		curwp->w_doto = bp->b_doto;
		curwp->w_markp = bp->b_markp;
		curwp->w_marko = bp->b_marko;
		/* Emit buffer switch event so extensions can react */
		event_bus_emit(EVT_BUFFER_SWITCH, bp, sizeof(struct buffer *));
		return true;
	}
	wp = wheadp; // Look for old.
	while (wp != nullptr) {
		if (wp != curwp && wp->w_bufp == bp) {
			curwp->w_dotp = wp->w_dotp;
			curwp->w_doto = wp->w_doto;
			curwp->w_markp = wp->w_markp;
			curwp->w_marko = wp->w_marko;
			break;
		}
		wp = wp->w_wndp;
	}
	/* Emit buffer switch event so extensions can react */
	event_bus_emit(EVT_BUFFER_SWITCH, bp, sizeof(struct buffer *));
	return true;
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
	char prompt[NBUFN + 32];

	/* Show current buffer name as default */
	snprintf(prompt, sizeof(prompt), "KILL BUFFER [%s]: ", curbp->b_bname);

	s = minibuf_read(prompt, bufn, NBUFN);
	if (s == ABORT)
		return s;

	/* If empty input, default to current buffer */
	if (s == false || bufn[0] == '\0') {
		bp = curbp;
	} else {
		bp = bfind(bufn, false, 0);
		if (bp == nullptr) {
			mlwrite("[No such buffer: %s]", bufn);
			return false;
		}
	}

	if (bp->b_flag & BFINVS) { // Deal with special buffers
		mlwrite("[Cannot kill special buffer]");
		return false;
	}
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

	if (bp->b_nwnd != 0) { // Error if on screen.
		REPORT_ERROR(ERR_BUFFER_INVALID, "BUFFER IS BEING DISPLAYED AND CANNOT BE KILLED");
		return false;
	}
	if ((s = bclear(bp)) != true) // Blow text away.
		return s;
	
	// Remove buffer from hash table for O(1) lookup
	buffer_hash_remove(bp);
	
	// Free undo stack and all undo/redo history
	if (bp->b_undo_stack) {
		undo_stack_destroy(bp->b_undo_stack);
		bp->b_undo_stack = nullptr;
	}

	/* If this is a terminal buffer, cleanup PTY and state */
	if (bp->b_mode & MDTBUFFER) {
		terminal_buffer_close(bp);
	}

	/* Free syntax highlighting state */
	if (bp->b_syntax) {
		syntax_free(bp->b_syntax);
		bp->b_syntax = nullptr;
	}

	/* Release undo stack memory */
    if (bp->b_line_index) {
        SAFE_FREE(bp->b_line_index);
    }

	/* Release piece table storage for large file support */
	if (bp->b_text) {
		TS_DESTROY(bp->b_text);
		bp->b_text = nullptr;
	}

	SAFE_FREE(bp->b_linep); // Release header line.
	bp1 = nullptr; // Find the header.
	bp2 = bheadp;
	while (bp2 != bp) {
		bp1 = bp2;
		bp2 = bp2->b_bufp;
	}
	bp2 = bp2->b_bufp; // Next one in chain.
	if (bp1 == nullptr) // Unlink it.
		bheadp = bp2;
	else
		bp1->b_bufp = bp2;
	SAFE_FREE(bp); // Release buffer block
	return true;
}

/*
 * Rename the current buffer
 * 
 * int f, n;		default Flag & Numeric arg
 */
int namebuffer(int f, int n)
{
	struct buffer *bp; // pointer to scan through all buffers
	char bufn[NBUFN]; // buffer to hold buffer name

	/* prompt for and get the new buffer name */
      ask:if (minibuf_read("CHANGE BUFFER NAME TO: ", bufn, NBUFN) !=
	    true)
		return false;

	/* and check for duplicates using fast O(1) lookup */
	bp = buffer_hash_find(bufn);
	if (bp != nullptr && bp != curbp) {
		goto ask;  // Name already exists, try again
	}

	// Remove old name from hash table, update name, re-insert
	buffer_hash_remove(curbp);
	safe_strcpy(curbp->b_bname, bufn, NBUFN); // copy buffer name to structure
	buffer_hash_insert(curbp);
	curwp->w_flag |= WFMODE; // make mode line replot
	mlerase();
	return true;
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

	if ((s = makelist(f)) != true)
		return s;
	if (blistp->b_nwnd == 0) { // Not on screen yet.
		if ((wp = wpopup()) == nullptr) {
			REPORT_ERROR(ERR_MEMORY, "FAILED TO CREATE POPUP WINDOW FOR BUFFER LIST");
			return false;
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
	while (wp != nullptr) {
		if (wp->w_bufp == blistp) {
			wp->w_linep = lforw(blistp->b_linep);
			wp->w_dotp = lforw(blistp->b_linep);
			wp->w_doto = 0;
			wp->w_markp = nullptr;
			wp->w_marko = 0;
			wp->w_flag |= WFMODE | WFHARD;
		}
		wp = wp->w_wndp;
	}
	return true;
}

/*
 * This routine rebuilds the
 * text in the special secret buffer
 * that holds the buffer list. It is called
 * by the list buffers command. Return true
 * if everything works. Return false if there
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

	blistp->b_flag &= ~BFCHG; // Don't complain!
	if ((s = bclear(blistp)) != true) // Blow old text away
		return s;
    // No need to touch blistp->b_fname here; avoid fortify warnings on zero-sized dest
	if (addline("ACT MODES        Size Buffer        File") == false
	    || addline("--- -----        ---- ------        ----") ==
	    false)
		return false;
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
	if (addline(line) == false)
		return false;

	/* output the list of buffers */
	while (bp != nullptr) {
		/* skip invisable buffers if iflag is false */
		if (((bp->b_flag & BFINVS) != 0) && (iflag != true)) {
			bp = bp->b_bufp;
			continue;
		}
		cp1 = &line[0]; // Start at left edge

		/* output status of ACTIVE flag (has the file been read in? */
		if (bp->b_active == true) // "@" if activated
			*cp1++ = '@';
		else
			*cp1++ = ' ';

		/* output status of changed flag */
		if ((bp->b_flag & BFCHG) != 0) // "*" if changed
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
			*cp1++ = (char)c;
		*cp1++ = ' ';	/* Gap.                 */
		cp2 = &bp->b_bname[0];	/* Buffer name          */
		while ((c = *cp2++) != 0)
			*cp1++ = (char)c;
		cp2 = &bp->b_fname[0];	/* File name            */
		if (*cp2 != 0) {
			while (cp1 < &line[3 + 1 + 5 + 1 + 6 + 4 + NBUFN])
				*cp1++ = ' ';
			while ((c = *cp2++) != 0) {
				if (cp1 < &line[MAXLINE - 1])
					*cp1++ = (char)c;
			}
		}
		*cp1 = 0;	/* Add to the buffer.   */
		if (addline(line) == false)
			return false;
		bp = bp->b_bufp;
	}
	return true;		/* All done             */
}


/*
 * The argument "text" points to
 * a string. Append this line to the
 * buffer list buffer. Handcraft the EOL
 * on the end. Return true if it worked and
 * false if you ran out of room.
 */
int addline(const char *text)
{
	struct line *lp;
	int i;
	int ntext;

	ntext = (int)strlen(text);
	if ((lp = lalloc(ntext)) == nullptr) {
		REPORT_ERROR(ERR_MEMORY, "FAILED TO ALLOCATE LINE FOR BUFFER LIST");
		return false;
	}
	for (i = 0; i < ntext; ++i)
		lputc(lp, i, text[i]);
	blistp->b_linep->l_bp->l_fp = lp;	/* Hook onto the end    */

lp->l_bp = blistp->b_linep->l_bp;
	blistp->b_linep->l_bp = lp;
lp->l_fp = blistp->b_linep;
	if (blistp->b_dotp == blistp->b_linep) // If "." is at the end
		blistp->b_dotp = lp;	/* move it to new line  */
	return true;
}

/*
 * Check if any user-visible buffers have been modified.
 *
 * Returns true if any non-invisible buffer has the BFCHG (changed) flag set.
 * Invisible buffers (BFINVS) like the buffer list are excluded since they
 * are internal data structures that the user doesn't need to save.
 */
int anycb(void)
{
	struct buffer *bp;

	bp = bheadp;
	while (bp != nullptr) {
		if ((bp->b_flag & BFINVS) == 0
		    && (bp->b_flag & BFCHG) != 0)
			return true;
		bp = bp->b_bufp;
	}
	return false;
}

/*
 * Find a buffer, by name. Return a pointer
 * to the buffer structure associated with it.
 * If the buffer is not found
 * and the "cflag" is true, create it. The "bflag" is
 * the settings for the flags in in buffer.
 */
struct buffer *bfind(const char *bname, int cflag, int bflag)
{
	struct buffer *bp;
	struct buffer *sb; // buffer to insert after
	struct line *lp;

	// Use O(1) hash table lookup instead of O(n) linear search
	bp = buffer_hash_find(bname);
	if (bp != nullptr) {
		return bp;
	}
	if (cflag != false) {
		if ((bp = (struct buffer *)safe_alloc(sizeof(struct buffer), "buffer allocation", __FILE__, __LINE__)) == nullptr) {
			REPORT_ERROR(ERR_MEMORY, "FAILED TO ALLOCATE BUFFER STRUCTURE");
			return nullptr;
		}
		if ((lp = lalloc(0)) == nullptr) {
			REPORT_ERROR(ERR_MEMORY, "FAILED TO ALLOCATE HEADER LINE FOR BUFFER");
			SAFE_FREE(bp);
			return nullptr;
		}
		/* find the place in the list to insert this buffer */
		if (bheadp == nullptr || strcmp(bheadp->b_bname, bname) > 0) {
			/* insert at the beginning */
			bp->b_bufp = bheadp;
			bheadp = bp;
		} else {
			sb = bheadp;
			while (sb->b_bufp != nullptr) {
				if (strcmp(sb->b_bufp->b_bname, bname) > 0)
					break;
				sb = sb->b_bufp;
			}

			/* and insert it */
			bp->b_bufp = sb->b_bufp;
			sb->b_bufp = bp;
		}

		/* and set up the other buffer fields */
		bp->b_active = true;
		bp->b_dotp = lp;
		bp->b_doto = 0;
		bp->b_markp = nullptr;
		bp->b_marko = 0;
		bp->b_flag = (uint8_t)bflag;
		bp->b_mode = (uint32_t)gmode;
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
			REPORT_ERROR(ERR_MEMORY, "FAILED TO ALLOCATE UNDO STACK FOR BUFFER");
            SAFE_FREE(bp->b_linep);
            SAFE_FREE(bp);
            return nullptr;
		}

		// Set initial saved baseline to the current undo version (clean buffer)
		atomic_store(&bp->b_saved_version_id, 1);
		
        // Initialize O(1) Line Index (Phase 2)
        bp->b_line_capacity = 128;
        bp->b_line_index = (struct line **)safe_alloc(sizeof(struct line *) * bp->b_line_capacity, "line index", __FILE__, __LINE__);
        if (!bp->b_line_index) {
            REPORT_ERROR(ERR_MEMORY, "FAILED TO ALLOCATE LINE INDEX");
            undo_stack_destroy(bp->b_undo_stack);
            SAFE_FREE(bp->b_linep);
            SAFE_FREE(bp);
            return nullptr;
        }
        bp->b_line_index[0] = lp; // Header line

		// Initialize terminal integration fields (used only when MDTBUFFER set)
		bp->b_term_data = nullptr;

		// Initialize syntax highlighting fields
		bp->b_syntax = nullptr;
		bp->b_lang_id = -1;

		// Add buffer to hash table for instant O(1) lookup
		buffer_hash_insert(bp);
		
		/* Old CRYPT removed - encryption now via external tools */

lp->l_fp = lp;
lp->l_bp = lp;
	}
	return bp;
}

/*
 * This routine blows away all of the text
 * in a buffer. If the buffer is marked as changed
 * then we ask if it is ok to blow it away; this is
 * to save the user the grief of losing text.
 * The window chain is nearly always wrong if this gets
 * called; the caller must arrange for the updates
 * that are required. Return true if everything
 * looks good.
 */
int bclear(struct buffer *bp)
{
	struct line *lp;
	int s;

	if ((bp->b_flag & BFINVS) == 0 // Not scratch buffer.
	    && (bp->b_flag & BFCHG) != 0 // Something changed
	    && (s = mlyesno("Discard changes")) != true)
		return s;
	bp->b_flag &= ~BFCHG; // Not changed
	while ((lp = lforw(bp->b_linep)) != bp->b_linep)
		lfree(lp);

	/* Release piece table storage for large file support */
	if (bp->b_text) {
		TS_DESTROY(bp->b_text);
		bp->b_text = nullptr;
	}
	bp->b_dotp = bp->b_linep; // Fix "."
	bp->b_doto = 0;
	bp->b_markp = nullptr; // Invalidate "mark"
	bp->b_marko = 0;

	// Reset cached statistics after clearing buffer
	atomic_store(&bp->b_line_count, 1);  // Empty buffer has 1 line
	atomic_store(&bp->b_byte_count, 0);  // No bytes
	atomic_store(&bp->b_word_count, 0);  // No words
	atomic_store(&bp->b_stats_dirty, false); // Clean

    // Reset Line Index
    // Since we cleared everything but header, just keep header at index 0
    if (bp->b_line_index) {
        bp->b_line_index[0] = bp->b_linep;
    }

    // Clear undo stack to prevent stale operations
    if (bp->b_undo_stack) {
        undo_stack_clear(bp->b_undo_stack);
    }

	return true;
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
			int line_len = llength(lp);
			bytes += line_len + 1; // +1 for newline
			
			// Count words in line
			bool in_word = false;
			for (int i = 0; i < line_len; i++) {
				char c = (char)lgetc(lp, i);
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
	return true;
}

/*
 * O(1) Line Index Management (Phase 2)
 */

/*
 * Insert a line into the index at the specified position.
 * Grows the index array if necessary.
 */
int buffer_index_insert(struct buffer *bp, int line_idx, struct line *lp)
{
	if (!bp) return false;

	// Ensure capacity - we need space for current_count + 1 after insertion
	int current_count = atomic_load(&bp->b_line_count);
	size_t capacity = bp->b_line_capacity;

	// Bounds validation
	if (line_idx < 0 || line_idx > current_count) {
		LOG_ERRORF("BufferIndex: Invalid line_idx=%d (count=%d)", line_idx, current_count);
		return false;
	}

	// Note: line_idx is 0-based index. current_count is number of items.
	// If inserting at end, line_idx == current_count.
	// After insert, we'll have current_count + 1 items, needing indices 0 to current_count.
	// If line_idx < current_count, memmove writes to index current_count.
	// So we need capacity > current_count to have space for the write.

	if ((size_t)current_count >= capacity) {
		size_t new_capacity = capacity * 3 / 2;
		if (new_capacity < 128) new_capacity = 128;

		struct line **new_index = (struct line **)safe_alloc(sizeof(struct line *) * new_capacity, "line index resize", __FILE__, __LINE__);
		if (!new_index) {
			LOG_ERRORF("BufferIndex: Resize allocation failed (%zu -> %zu)", capacity, new_capacity);
			return false;
		}

		LOG_DEBUGF("BufferIndex: Resizing %zu -> %zu (count=%d)", capacity, new_capacity, current_count);

		if (bp->b_line_index) {
			memcpy(new_index, bp->b_line_index, sizeof(struct line *) * (size_t)current_count);
			SAFE_FREE(bp->b_line_index);
		}
		bp->b_line_index = new_index;
		bp->b_line_capacity = new_capacity;
		capacity = new_capacity;
	}

	// Shift elements if not inserting at end
	if (line_idx < current_count) {
		// Verify bounds: we're writing to indices line_idx+1 through current_count
		if ((size_t)current_count >= capacity) {
			LOG_ERRORF("BufferIndex: OVERFLOW - writing to index %d with capacity %zu", current_count, capacity);
			return false;
		}
		memmove(&bp->b_line_index[line_idx + 1],
				&bp->b_line_index[line_idx],
				sizeof(struct line *) * (size_t)(current_count - line_idx));
	}
	
	bp->b_line_index[line_idx] = lp;
    // Count update handled by buffer_update_stats_incremental usually,
    // but here we strictly manage the index.
    // The atomic line count is the source of truth for the index size.
	// buffer_update_stats_incremental increments it.
    // But we must ensure this function is called IN SYNC with that increment.
	
	return true;
}

/*
 * Delete a line from the index.
 */
int buffer_index_delete(struct buffer *bp, int line_idx)
{
	if (!bp || !bp->b_line_index) return false;
	
	int current_count = atomic_load(&bp->b_line_count);
	if (line_idx < 0 || line_idx >= current_count) return false;
	
	// Shift elements down
	if (line_idx < current_count - 1) {
		memmove(&bp->b_line_index[line_idx],
				&bp->b_line_index[line_idx + 1],
				sizeof(struct line *) * (size_t)(current_count - 1 - line_idx));
	}
	
	// Clear last element (optional, but safe)
	bp->b_line_index[current_count - 1] = nullptr;
	
    // Count decremented by caller via buffer_update_stats_incremental
	return true;
}

/*
 * Get a line pointer by index O(1).
 */
struct line *buffer_index_get(struct buffer *bp, int line_idx)
{
	if (!bp || !bp->b_line_index) return nullptr;
	int current_count = atomic_load(&bp->b_line_count);
	if (line_idx < 0 || line_idx >= current_count) return nullptr;

	return bp->b_line_index[line_idx];
}

/*
 * Rebuild the line index from scratch by walking the linked list.
 * Call this after operations that bypass the incremental index updates,
 * such as file reading.
 */
int buffer_rebuild_index(struct buffer *bp)
{
	if (!bp || !bp->b_linep) return false;

	// Count actual lines by walking the list
	int line_count = 0;
	struct line *lp = bp->b_linep;
	while ((lp = lforw(lp)) != bp->b_linep) {
		line_count++;
	}

	// Ensure we have enough capacity
	if ((size_t)line_count > bp->b_line_capacity) {
		size_t new_capacity = (size_t)line_count * 3 / 2;
		if (new_capacity < 128) new_capacity = 128;

		struct line **new_index = (struct line **)safe_alloc(sizeof(struct line *) * new_capacity, "line index rebuild", __FILE__, __LINE__);
		if (!new_index) {
			LOG_ERROR("BufferIndex: Rebuild allocation failed");
			return false;
		}

		if (bp->b_line_index) {
			SAFE_FREE(bp->b_line_index);
		}
		bp->b_line_index = new_index;
		bp->b_line_capacity = new_capacity;
	}

	// Populate the index
	int idx = 0;
	lp = bp->b_linep;
	while ((lp = lforw(lp)) != bp->b_linep) {
		bp->b_line_index[idx++] = lp;
	}

	LOG_DEBUGF("BufferIndex: Rebuilt with %d lines (capacity %zu)", line_count, bp->b_line_capacity);
	return true;
}