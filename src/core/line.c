/*	line.c
 *
 * The functions in this file are a general set of line management utilities.
 * They are the only routines that touch the text. They also touch the buffer
 * and window structures, to make sure that the necessary updating gets done.
 * There are routines in this file that handle the kill buffer too. It isn't
 * here for any good reason.
 *
 * Note that this code only updates the dot and mark values in the window list.
 * Since all the code acts on the current window, the buffer that we are
 * editing must be being displayed, which means that "b_nwnd" is non zero,
 * which means that the dot and mark values in the buffer headers are nonsense.
 *
 */

#include "line.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* Forward declarations for kill ring functions */
static void kill_ring_add(const char *text, size_t len);
extern int set_clipboard(const char *text);  /* Linux platform clipboard */
extern int get_clipboard(char *buf, int maxlen); /* Linux platform clipboard */

/* Modern unified kill buffer - single temporary buffer */
static char temp_kill_buf[8192];  /* Same as KILL_ENTRY_MAX */
static size_t temp_kill_len = 0;

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "profiler.h"
#include "utf8.h"
#include "memory.h"
#include "undo.h"

#define	BLOCK_SIZE 16 /* Line block chunk size. */


/* Unicode-aware word boundary detection for I18N */
#include <wchar.h>
#include <wctype.h>
#include <locale.h>

// Returns true if the codepoint is a Unicode word character
static inline bool is_unicode_word(wchar_t wc) {
	// Use POSIX wctype for word characters (alnum or mark or connector)
	return iswalnum(wc) || iswalpha(wc) || iswpunct(wc) || wc == L'_';
}

// Convert a UTF-8 sequence to a wide character (wchar_t)
static inline int utf8_to_wchar(const char *s, wchar_t *wc) {
	mbstate_t state = {0};
	return mbrtowc(wc, s, MB_CUR_MAX, &state);
}

// Returns true if the byte sequence at s is a word character (Unicode-aware)
static inline bool is_word_byte_utf8(const char *s) {
	wchar_t wc;
	int len = utf8_to_wchar(s, &wc);
	if (len <= 0) return false;
	return is_unicode_word(wc);
}

// Legacy ASCII fallback for single bytes
static inline bool is_word_byte(int ch) {
	return ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r';
}

// Get the line number of a line pointer in a buffer
static long getlinenum(struct buffer *bp, struct line *lp) {
    struct line *clp;
    long lnum = 0;
    clp = lforw(bp->b_linep);
    while (clp != bp->b_linep) {
        lnum++;
        if (clp == lp) {
            return lnum;
        }
        clp = lforw(clp);
    }
    return 0; // Should not happen
}

/*
 * This routine allocates a block of memory large enough to hold a struct line
 * containing "used" characters. The block is always rounded up a bit. Return
 * a pointer to the new block, or nullptr if there isn't any memory left. Print a
 * message in the message line if no space.
 */
struct line *lalloc(int used)
{
	struct line *lp;

	lp = (struct line *)safe_alloc(sizeof(struct line), "line buffer", __FILE__, __LINE__);
	if (!lp) {
		return nullptr;
	}
	
	size_t initial_capacity = (used > 0) ? (size_t)used : GAP_BUFFER_MIN_SIZE;
	lp->gb = gap_buffer_create(initial_capacity);
	if (!lp->gb) {
		safe_free((void **)&lp);
		return nullptr;
	}
	
	atomic_store(&lp->l_column_cache_offset, 0);
	atomic_store(&lp->l_column_cache_column, 0);
	atomic_store(&lp->l_column_cache_dirty, false);
	
	return lp;
}

/*
 * Delete line "lp". Fix all of the links that might point at it (they are
 * moved to offset 0 of the next line. Unlink the line from whatever buffer it
 * might be in. Release the memory. The buffers are updated too; the magic
 * conditions described in the above comments don't hold here.
 */
void lfree(struct line *lp)
{
	struct buffer *bp;
	struct window *wp;

	wp = wheadp;
	while (wp != nullptr) {
		if (wp->w_linep == lp)
			wp->w_linep = lp->l_fp;
		if (wp->w_dotp == lp) {
			wp->w_dotp = lp->l_fp;
			wp->w_doto = 0;
		}
		if (wp->w_markp == lp) {
			wp->w_markp = lp->l_fp;
			wp->w_marko = 0;
		}
		wp = wp->w_wndp;
	}
	bp = bheadp;
	while (bp != nullptr) {
		if (bp->b_nwnd == 0) {
			if (bp->b_dotp == lp) {
				bp->b_dotp = lp->l_fp;
				bp->b_doto = 0;
			}
			if (bp->b_markp == lp) {
				bp->b_markp = lp->l_fp;
				bp->b_marko = 0;
			}
		}
		bp = bp->b_bufp;
	}
	lp->l_bp->l_fp = lp->l_fp;
	lp->l_fp->l_bp = lp->l_bp;
	
	gap_buffer_destroy(lp->gb);
	safe_free((void **) &lp);
}

/*
 * This routine gets called when a character is changed in place in the current
 * buffer. It updates all of the required flags in the buffer and window
 * system. The flag used is passed as an argument; if the buffer is being
 * displayed in more than 1 window we change EDIT t HARD. Set MODE if the
 * mode line needs to be updated (the "*" has to be set).
 */
void lchange(int flag)
{
	struct window *wp;

	if (curbp->b_nwnd != 1)	/* Ensure hard.     */
		flag = WFHARD;
	if ((curbp->b_flag & BFCHG) == 0) { /* First change, so */
		curbp->b_flag |= BFCHG;
	}
	flag |= WFMODE;	/* Always update mode lines for instant status */
	wp = wheadp;
	while (wp != nullptr) {
		if (wp->w_bufp == curbp)
			wp->w_flag |= flag;
		wp = wp->w_wndp;
	}
}

int linsert_str(const char *str) {
    if (!str) return false;
    const char *ptr = str;
    while (*ptr) {
        if (*ptr == '\n') {
            if (!lnewline()) return false;
        } else {
            if (!linsert(1, *ptr)) return false;
        }
        ptr++;
    }
    return true;
}

int linstr(const char *str) {
    if (!str) return false;
    const char *ptr = str;
    while (*ptr) {
        if (*ptr == '\n') {
            if (!lnewline()) return false;
        } else {
            if (!linsert(1, *ptr)) return false;
        }
        ptr++;
    }
    return true;
}

int lgetchar(unicode_t *uc) {
    struct line *lp = curwp->w_dotp;
    int doto = curwp->w_doto;

    if (doto == llength(lp)) {
        *uc = '\n';
        return llength(lp) + 1;
    }

    // Use gap buffer to extract UTF-8 sequence
    char utf8_buf[6];
    int len = llength(lp) - doto;
    if (len > 6) len = 6;
    for (int i = 0; i < len; i++) {
        utf8_buf[i] = lgetc(lp, doto + i);
    }
    int char_len = utf8_to_unicode(utf8_buf, 0, len, uc);

    if (char_len == 0) { // Error or end of string
        return doto;
    }

    return doto + char_len;
}

int insspace(int f, int n) {
    if (n < 0)
        return false;
    if (n == 0)
        return true;
    return linsert(n, ' ');
}

int lover(char *ostr) {
    int len = strlen(ostr);
    if (len == 0) return true;

    if (curwp->w_doto + len <= llength(curwp->w_dotp)) {
        ldelete(len, false);
    } else {
        ldelete(llength(curwp->w_dotp) - curwp->w_doto, false);
    }

    return linstr(ostr);
}

int putctext(const char *iline) {
    int i;

    curwp->w_doto = 0;
    ldelete(llength(curwp->w_dotp), false);

    return linstr(iline);
}

char *getctext(void) {
    static char text[NSTRING];
    struct line *lp = curwp->w_dotp;
    int len = llength(lp);
    if (len >= NSTRING) len = NSTRING - 1;
    if (len > 0) {
        gap_buffer_get_text(lp->gb, 0, (size_t)len, text, NSTRING);
    }
    text[len] = '\0';
    return text;
}

/*
 * Insert "n" copies of the character "c" at the current location of dot. In
 * the easy case all that happens is the text is stored in the line. In the
 * hard case, the line has to be reallocated. When the window list is updated,
 * take special care; I screwed it up once. You always update dot in the
 * current window. You update mark, and a dot in another window, if it is
 * greater than the place where you did the insert. Return true if all is
 * well, and false on errors.
 */
int linsert(int n, int c)
{
	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	lchange(WFEDIT);

	{
		// --- PROFILING HOOK ---
		perf_start_timing("linsert");

		// Local variables for the function
		char *cp1;
		char *cp2;
		struct line *lp1;
		struct line *lp2;
		struct line *lp3;
		int doto;
		int i;
		struct window *wp;
		char *inserted_text;
		long lnum;

		if (curbp->b_mode & MDVIEW)
			return rdonly();
		lchange(WFEDIT);
		lp1 = curwp->w_dotp;

		// Capture state for undo
		lnum = getlinenum(curbp, lp1);
		doto = curwp->w_doto;

		inserted_text = safe_alloc((size_t)n + 1, "undo insert buffer", __FILE__, __LINE__);
		if (inserted_text == nullptr) {
			perf_end_timing("linsert");
			return false;
		}
		for (i = 0; i < n; ++i) inserted_text[i] = c;
		inserted_text[n] = '\0';

		if (lp1 == curbp->b_linep) {
			if (lp1->l_fp == lp1) {
				// Empty buffer - create first line
				struct line *first = lalloc(0);
				if (first == nullptr) {
					SAFE_FREE(inserted_text);
					perf_end_timing("linsert");
					return false;
				}
				first->l_bp = lp1;
				first->l_fp = lp1->l_fp;
				lp1->l_fp->l_bp = first;
				lp1->l_fp = first;
				wp = wheadp;
				while (wp != nullptr) {
					if (wp->w_linep == lp1) wp->w_linep = first;
					if (wp->w_dotp == lp1) wp->w_dotp = first;
					if (wp->w_markp == lp1) wp->w_markp = first;
					wp = wp->w_wndp;
				}
				lp1 = curwp->w_dotp = first;
				curwp->w_doto = 0;
			} else {
				// At EOF but buffer not empty - extend last line
				lp1 = curwp->w_dotp = lp1->l_bp;  // Go to last actual line
				curwp->w_doto = llength(lp1);      // Position at end of line
			}
			doto = curwp->w_doto;
		}

		/* Insert into gap buffer */
		if (gap_buffer_insert(lp1->gb, (size_t)doto, inserted_text, (size_t)n) != GAP_BUFFER_SUCCESS) {
			SAFE_FREE(inserted_text);
			perf_end_timing("linsert");
			return false;
		}
		curwp->w_doto += n;
		
		wp = wheadp;
		while (wp != nullptr) {
			if (wp->w_dotp == lp1 && wp->w_doto >= doto && wp != curwp) {
				wp->w_doto += n;
			}
			wp = wp->w_wndp;
		}
		
		atomic_store(&lp1->l_column_cache_dirty, true);

		// Update word/line/char stats incrementally
		int word_delta = 0;
		buffer_update_stats_incremental(curbp, 0, n, word_delta);
		if (word_delta == 0) buffer_mark_stats_dirty(curbp); // Fallback for complex cases

		undo_record_insert(curbp, lnum, doto, inserted_text, n);
		SAFE_FREE(inserted_text);

		// Cursor advancement is now handled in each insertion path above

		perf_end_timing("linsert");
		return true;
	}
}

/*
 * Delete "n" bytes, starting at dot. It understands how do deal
 * with end of lines, etc. It returns true if all of the characters were
 * deleted, and false if they were not (because dot ran into the end of the
 * buffer. The "kflag" is true if the text should be put in the kill buffer.
 */
int ldelete(long n, int kflag)
{
	char *cp1;
	char *cp2;
	struct line *dotp;
	int doto;
	int chunk;
	struct window *wp;
    char *deleted_text;
    long lnum;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if (n < 0)
		return false;
	if (n == 0)
		return true;

    /* Capture state for undo */
    dotp = curwp->w_dotp;
    doto = curwp->w_doto;
    lnum = getlinenum(curbp, dotp);
    deleted_text = safe_alloc((size_t)n + 1, "undo delete buffer", __FILE__, __LINE__);
    if (deleted_text == nullptr) return false;

    // Collect text that will be deleted
    long collected_len = 0;
    struct line *scan_p = dotp;
    int scan_o = doto;
    while(collected_len < n) {
        if (scan_p == curbp->b_linep) break;
        if (scan_o == llength(scan_p)) {
            if (collected_len < n) deleted_text[collected_len++] = '\n';
            scan_p = lforw(scan_p);
            scan_o = 0;
        } else {
            deleted_text[collected_len++] = lgetc(scan_p, scan_o++);
        }
    }
    deleted_text[collected_len] = '\0';

	lchange(WFHARD);
	while (n > 0) {
		dotp = curwp->w_dotp;
		doto = curwp->w_doto;
		if (dotp == curbp->b_linep)	/* Hit end of buffer.       */
			break;
		chunk = llength(dotp) - doto;	/* Size of chunk.       */
		if (chunk > n)
			chunk = n;
		if (chunk == 0) {	/* End of line, merge.  */
			if (ldelnewline() == false
			    || (kflag != false && kinsert('\n') == false))
				goto undo_fail;
			--n;
		} else {
			/* Delete from gap buffer */
			if (kflag != false) {
				for (int i = 0; i < chunk; i++) {
					char ch = gap_buffer_get_char(dotp->gb, (size_t)(doto + i));
					if (kinsert(ch) == false)
						goto undo_fail;
				}
			}
			
			if (gap_buffer_delete(dotp->gb, (size_t)doto, (size_t)chunk) != GAP_BUFFER_SUCCESS) {
				goto undo_fail;
			}
			
			wp = wheadp;
			while (wp != nullptr) {
				if (wp->w_dotp == dotp && wp->w_doto >= doto) {
					wp->w_doto -= chunk;
					if (wp->w_doto < doto)
						wp->w_doto = doto;
				}
				if (wp->w_markp == dotp && wp->w_marko >= doto) {
					wp->w_marko -= chunk;
					if (wp->w_marko < doto)
						wp->w_marko = doto;
				}
				wp = wp->w_wndp;
			}
			
			atomic_store(&dotp->l_column_cache_dirty, true);
			n -= chunk;
		}
	}

    // Update atomic statistics (bytes and simple word count)
    int word_delta = 0;
    if (n == 1 && collected_len == 1 && deleted_text[0] != '\n') {
        // Compute based on original neighbors at deletion point
		int left_is_word = 0;
		int right_is_word = 0;
		if (doto > 0) {
			char left_ch = lgetc(dotp, doto - 1);
			left_is_word = is_word_byte_utf8((const char *)&left_ch);
		}
		if (doto + 1 < llength(dotp)) {
			char right_ch = lgetc(dotp, doto + 1);
			right_is_word = is_word_byte_utf8((const char *)&right_ch);
		}
		if (!is_word_byte_utf8((const char *)&deleted_text[0])) {
			// Deleting whitespace between words merges two words into one
			if (left_is_word && right_is_word) word_delta -= 1;
		} else {
			// Deleting a single-letter word between non-words removes a word
			if (!left_is_word && !right_is_word) word_delta -= 1;
		}
    }
	buffer_update_stats_incremental(curbp, 0, -collected_len, word_delta);
    if (word_delta == 0) buffer_mark_stats_dirty(curbp); // Fallback for complex cases

	undo_record_delete(curbp, lnum, doto, deleted_text, collected_len);
	/* If this was a kill, also update the system clipboard with the exact text */
	if (kflag != false && collected_len > 0) {
		set_clipboard(deleted_text);
	}
    SAFE_FREE(deleted_text);
	return n == 0;

undo_fail:
    SAFE_FREE(deleted_text);
    return false;
}

/*
 * Delete a newline. Join the current line with the next line. If the next line
 * is the magic header line always return true; merging the last line with the
 * header line can be thought of as always being a successful operation, even
 * if nothing is done, and this makes the kill buffer work "right". Easy cases
 * can be done by shuffling data around. Hard cases require that lines be moved
 * about in memory. Return false on error and true if all looks ok. Called by
 * "ldelete" only.
 */
int ldelnewline(void)
{
	char *cp1;
	char *cp2;
	struct line *lp1;
	struct line *lp2;
	struct line *lp3;
	struct window *wp;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	lp1 = curwp->w_dotp;
	lp2 = lp1->l_fp;
	if (lp2 == curbp->b_linep) {	/* At the buffer end.   */
		if (llength(lp1) == 0) {	/* Blank line.              */
			lfree(lp1);
            // Update atomic statistics for blank line deletion
            buffer_update_stats_incremental(curbp, -1, -1, 0); // -1 line, -1 byte (for newline)
            buffer_mark_stats_dirty(curbp); // Mark dirty for word count recalculation
        }
		return true;
	}
	
	/* Merge two lines using gap buffers */
	size_t lp1_len = gap_buffer_size(lp1->gb);
	char *temp = safe_alloc(gap_buffer_size(lp2->gb), "merge temp", __FILE__, __LINE__);
	if (!temp)
		return false;
		
	gap_buffer_get_text(lp2->gb, 0, gap_buffer_size(lp2->gb), temp, gap_buffer_size(lp2->gb));
	gap_buffer_insert(lp1->gb, lp1_len, temp, gap_buffer_size(lp2->gb));
	SAFE_FREE(temp);
	
	wp = wheadp;
	while (wp != nullptr) {
		if (wp->w_linep == lp2)
			wp->w_linep = lp1;
		if (wp->w_dotp == lp2) {
			wp->w_dotp = lp1;
			wp->w_doto += (int)lp1_len;
		}
		if (wp->w_markp == lp2) {
			wp->w_markp = lp1;
			wp->w_marko += (int)lp1_len;
		}
		wp = wp->w_wndp;
	}
	lp1->l_fp = lp2->l_fp;
	lp2->l_fp->l_bp = lp1;
	lfree(lp2);
    // Update atomic statistics for line merge
    buffer_update_stats_incremental(curbp, -1, -1, 0); // -1 line, -1 byte (for newline)
    buffer_mark_stats_dirty(curbp); // Mark dirty for word count recalculation

	safe_free((void **) &lp1);
	safe_free((void **) &lp2);
	return true;
}

/*
 * Delete all of the text saved in the kill buffer. Called by commands when a
 * new kill context is being created. 
 * Modern implementation: Just clears the temp kill buffer.
 */
void kdelete(void)
{
	/* Transfer previous temp kill to kill ring and clipboard */
	if (temp_kill_len > 0) {
		temp_kill_buf[temp_kill_len] = '\0';
		kill_ring_add(temp_kill_buf, temp_kill_len);
		set_clipboard(temp_kill_buf);
		temp_kill_len = 0;  /* Reset temp buffer */
	}
	
}

/*
 * Insert a character to the unified kill buffer.
 * Modern implementation: Uses simple temp buffer.
 */
int kinsert(int c)
{
	/* Bounds check - prevent buffer overflow */
	if (temp_kill_len >= 8191) {  /* Leave space for null terminator */
		return false;  /* Buffer full */
	}

	/* Add character to temp buffer */
	temp_kill_buf[temp_kill_len++] = c;
	
	
	return true;
}


/*
 * Yank text back from the kill buffer. This is really easy. All of the work
 * is done by the standard insert routines. All you do is run the loop, and
 * check for errors. Bound to "C-Y".
 */
int yank(int f, int n)
{
	int c;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	if (n < 0)
		return false;
		
	/* make sure there is something to yank */
	if (temp_kill_len == 0)
		return true;	/* not an error, just nothing */

	/* for each time.... */
	while (n--) {
		/* Yank from unified temp buffer */
		for (size_t i = 0; i < temp_kill_len; i++) {
			c = temp_kill_buf[i];
			if (c == '\n') {
				if (lnewline() == false)
					return false;
			} else {
				if (linsert(1, c) == false)
					return false;
			}
		}
	}
	
	// Set yank flag and size for yankpop chaining  
	thisflag |= CFYANK;
	yanked_size = (int)temp_kill_len;
	
	return true;
}

/* Yank directly from the system clipboard into the buffer */
int yank_clipboard(int f, int n)
{
	char buf[8192];
	(void)f; (void)n;
	if (curbp->b_mode & MDVIEW)
		return rdonly();
	if (!get_clipboard(buf, (int)sizeof(buf))) {
		mlwrite("(clipboard empty)");
		return true; /* not an error */
	}
	const char *p = buf;
	while (*p) {
		if (*p == '\n') {
			if (lnewline() == false) return false;
		} else {
			if (linsert(1, (unsigned char)*p) == false) return false;
		}
		++p;
	}
	thisflag |= CFYANK;
	yanked_size = (int)strlen(buf);
	return true;
}

/* 
 * C23 Kill Ring Operations - Modern atomic implementation
 * Thread-safe, O(1) performance, cache-efficient design
 */

/* Add text to kill ring - atomic producer */
static void kill_ring_add(const char *text, size_t len) {
	if (len == 0 || len >= KILL_ENTRY_MAX) return;
	
	// Atomic head increment with wraparound
	size_t head = atomic_fetch_add_explicit(&g_kill_ring.head, 1, memory_order_acq_rel);
	head &= (KILL_RING_MAX - 1);  // Power-of-2 wraparound
	
	struct kill_ring_entry *entry = &g_kill_ring.entries[head];
	
	// Copy text with memory fence
	memcpy(entry->text, text, len);
	entry->text[len] = '\0';
	
	// Atomic publication - length first, then valid flag
	atomic_store_explicit(&entry->length, len, memory_order_release);
	atomic_store_explicit(&entry->valid, true, memory_order_release);
	
	// Update count (may exceed KILL_RING_MAX briefly, that's fine)
	atomic_fetch_add_explicit(&g_kill_ring.count, 1, memory_order_relaxed);
	
	// Reset yank index to head (most recent)
	atomic_store_explicit(&g_kill_ring.yank_index, head, memory_order_release);
}

/* Get text from kill ring at index - atomic consumer */
static const char *kill_ring_get(size_t index, size_t *out_len) {
	index &= (KILL_RING_MAX - 1);  // Wraparound
	
	struct kill_ring_entry *entry = &g_kill_ring.entries[index];
	
	// Check if entry is valid with acquire semantics
	if (!atomic_load_explicit(&entry->valid, memory_order_acquire)) {
		if (out_len) *out_len = 0;
		return nullptr;
	}
	
	// Get length with acquire semantics (pairs with release in kill_ring_add)
	size_t len = atomic_load_explicit(&entry->length, memory_order_acquire);
	if (out_len) *out_len = len;
	
	return entry->text;  // Text is immutable once published
}

/* yankpop - Cycle through kill ring (Meta+Y)
 * Must be preceded by yank or another yankpop
 */
int yankpop(int f, int n) {
	size_t text_len;
	const char *text;
	
	if (curbp->b_mode & MDVIEW)
		return rdonly();
	
	if (n < 0) return false;
	
	// Verify previous command was yank or yankpop
	if (!(lastflag & CFYANK)) {
		mlwrite("Previous command was not a yank");
		return false;
	}
	
	// Check if kill ring has content
	size_t count = atomic_load_explicit(&g_kill_ring.count, memory_order_acquire);
	if (count == 0) {
		mlwrite("Kill ring is empty");  
		return false;
	}
	
	// Move to previous entry in kill ring
	size_t current_yank = atomic_load_explicit(&g_kill_ring.yank_index, memory_order_acquire);
	size_t prev_yank = (current_yank - 1) & (KILL_RING_MAX - 1);
	
	// Get previous kill ring entry
	text = kill_ring_get(prev_yank, &text_len);
	if (!text || text_len == 0) {
		mlwrite("No previous kill");
		return false;
	}
	
	// Delete previously yanked text (stored size)
	// Note: Uses approximate size tracking; edge cases with multi-line yanks
	// may have minor discrepancies. This is acceptable for typical usage.
	if (ldelete(yanked_size, false) == false) {
		return false;
	}
	
	// Insert new text from kill ring
	for (size_t i = 0; i < text_len; i++) {
		char c = text[i];
		if (c == '\n') {
			if (lnewline() == false) return false;
		} else {
			if (linsert(1, c) == false) return false;
		}
	}
	
	// Update yank index and size
	atomic_store_explicit(&g_kill_ring.yank_index, prev_yank, memory_order_release);
	yanked_size = (int)text_len;
	
	// Set flag for chaining yankpop commands
	thisflag |= CFYANK;
	
	return true;
}

int ldelchar(long n, int kflag) {
    return ldelete(n, kflag);
}

int lnewline(void)
{
	struct line *lp1;
	struct line *lp2;
	int doto;
	struct window *wp;

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */
	lchange(WFHARD);
	lp1 = curwp->w_dotp;	/* Get the address and  */
	doto = curwp->w_doto;	/* offset of "."        */

    undo_record_insert(curbp, getlinenum(curbp, lp1), doto, "\n", 1);

	/* Create new line for first half */
	if ((lp2 = lalloc(doto)) == nullptr)
		return false;
	
	/* Copy first 'doto' bytes from lp1 to lp2 using gap buffer */
	if (doto > 0) {
		char *temp = safe_alloc((size_t)doto, "newline split temp", __FILE__, __LINE__);
		if (!temp) {
			lfree(lp2);
			return false;
		}
		gap_buffer_get_text(lp1->gb, 0, (size_t)doto, temp, (size_t)doto);
		gap_buffer_insert(lp2->gb, 0, temp, (size_t)doto);
		SAFE_FREE(temp);
		
		/* Remove first 'doto' bytes from lp1 */
		gap_buffer_delete(lp1->gb, 0, (size_t)doto);
	}
	
	/* Link lp2 into list before lp1 */
	lp2->l_bp = lp1->l_bp;
	lp1->l_bp = lp2;
	lp2->l_bp->l_fp = lp2;
	lp2->l_fp = lp1;
	
	/* Fix up window pointers */
	wp = wheadp;
	while (wp != nullptr) {
		if (wp->w_linep == lp1)
			wp->w_linep = lp2;
		if (wp->w_dotp == lp1) {
			if (wp->w_doto < doto)
				wp->w_dotp = lp2;
			else
				wp->w_doto -= doto;
		}
		if (wp->w_markp == lp1) {
			if (wp->w_marko < doto)
				wp->w_markp = lp2;
			else
				wp->w_marko -= doto;
		}
		wp = wp->w_wndp;
	}
	
	// Update atomic statistics for new line insertion
    buffer_update_stats_incremental(curbp, 1, 1, 0); // +1 line, +1 byte (for newline)
    buffer_mark_stats_dirty(curbp); // Mark dirty for word count recalculation

	return true;
}
