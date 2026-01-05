/*	line.c
 *
 * The functions in this file are a general set of line management utilities.
 * They are the only routines that touch the text. They also touch the buffer
 * and window structures, to make sure that the necessary updating gets done.
 */

#include "line.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <stdatomic.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "profiler.h"
#include "utf8.h"
#include "memory.h"
#include "undo.h"
#include "clipboard.h"
#include "internal/syntax.h"

/* Forward declarations for kill ring functions */
static void kill_ring_add(const char *text, size_t len);

#define	BLOCK_SIZE 16 /* Line block chunk size. */

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
/* is_word_byte() now in line.h for shared use with undo.c */

// Get the line number of a line pointer in a buffer
long getlinenum(struct buffer *bp, struct line *lp) {
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
    return 0; // Header line or not found
}

/*
 * This routine allocates a block of memory large enough to hold a struct line
 * containing "used" characters. The block is always rounded up a bit. Return
 * a pointer to the new block, or nullptr if there isn't any memory left.
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

	/* Initialize as a circular list of one - safe for lfree() if never linked */
	lp->l_fp = lp;
	lp->l_bp = lp;

	atomic_store(&lp->l_column_cache_offset, 0);
	atomic_store(&lp->l_column_cache_column, 0);
	atomic_store(&lp->l_column_cache_dirty, true);  /* Force recalc on first use */
	
	return lp;
}

/*
 * Delete line "lp". Fix all of the links that might point at it.
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
 * system.
 *
 * Also marks the buffer's dirty_seq for display tracking (montauk/OUROBOROS pattern).
 */
void lchange(int flag)
{
	struct window *wp;

	if (curbp->b_nwnd != 1)	/* Ensure hard.     */
		flag = WFHARD;
	if ((curbp->b_flag & BFCHG) == 0) { /* First change, so */
		curbp->b_flag |= BFCHG;
	}

	/* Mark buffer dirty for display tracking (montauk/OUROBOROS pattern) */
	mark_buffer_dirty(curbp);

	/* Invalidate syntax highlighting from current line */
	if (curbp->b_syntax && curbp->b_syntax->enabled) {
		long lnum = getlinenum(curbp, curwp->w_dotp);
		if (lnum > 0) {
			syntax_invalidate_from_line(curbp->b_syntax, (int)(lnum - 1));
		}
	}

	flag |= WFMODE;	/* Always update mode lines for instant status */
	wp = wheadp;
	while (wp != nullptr) {
		if (wp->w_bufp == curbp)
			wp->w_flag |= flag;
		wp = wp->w_wndp;
	}
}

/* Forward declaration for bulk segment insert */
static int linsert_str_segment(const char *text, size_t len);

/**
 * Bulk string insertion - O(1) per segment instead of O(n) per character.
 * Handles newlines by splitting into segments.
 */
int linsert_str(const char *str) {
    if (!str || !*str) return true;

    if (curbp->b_mode & MDVIEW)
        return rdonly();

    const char *seg_start = str;
    const char *ptr = str;

    while (*ptr) {
        if (*ptr == '\n') {
            // Insert segment before newline (if any)
            if (ptr > seg_start) {
                size_t seg_len = (size_t)(ptr - seg_start);
                if (!linsert_str_segment(seg_start, seg_len))
                    return false;
            }
            // Insert the newline
            if (!lnewline()) return false;
            seg_start = ptr + 1;
        }
        ptr++;
    }

    // Insert remaining segment after last newline
    if (ptr > seg_start) {
        size_t seg_len = (size_t)(ptr - seg_start);
        if (!linsert_str_segment(seg_start, seg_len))
            return false;
    }

    return true;
}

/**
 * Bulk insert a segment of text (no newlines) using gap buffer.
 * Same pattern as linsert() but for strings.
 */
static int linsert_str_segment(const char *text, size_t len) {
    if (len == 0) return true;

    lchange(WFEDIT);

    struct line *lp1 = curwp->w_dotp;
    int doto = curwp->w_doto;
    long lnum = getlinenum(curbp, lp1);
    struct window *wp;

    // Handle insertion at EOF marker
    if (lp1 == curbp->b_linep) {
        if (lp1->l_fp == lp1) {
            // Empty buffer - create first line
            struct line *first = lalloc(0);
            if (first == nullptr) return false;
            first->l_bp = lp1;
            first->l_fp = lp1->l_fp;
            lp1->l_fp->l_bp = first;
            lp1->l_fp = first;
            buffer_index_insert(curbp, 0, first);

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
            lp1 = curwp->w_dotp = lp1->l_bp;
            curwp->w_doto = llength(lp1);
        }
        doto = curwp->w_doto;
        lnum = getlinenum(curbp, lp1);
    }

    // Allocate buffer for undo (need null-terminated copy)
    char *undo_buf = safe_alloc(len + 1, "undo insert str", __FILE__, __LINE__);
    if (undo_buf == nullptr) return false;
    memcpy(undo_buf, text, len);
    undo_buf[len] = '\0';

    // BULK insert via gap buffer - O(1) operation
    if (gap_buffer_insert(lp1->gb, (size_t)doto, undo_buf, len) != GAP_BUFFER_SUCCESS) {
        SAFE_FREE(undo_buf);
        return false;
    }

    // Update cursor position
    curwp->w_doto += (int)len;

    // Update other windows pointing to same position
    wp = wheadp;
    while (wp != nullptr) {
        if (wp->w_dotp == lp1 && wp->w_doto >= doto && wp != curwp) {
            wp->w_doto += (int)len;
        }
        wp = wp->w_wndp;
    }

    atomic_store(&lp1->l_column_cache_dirty, true);

    // Update stats incrementally
    buffer_update_stats_incremental(curbp, 0, (int)len, 0);
    buffer_mark_stats_dirty(curbp);

    // Single undo record for entire segment
    undo_record_insert(curbp, lnum, doto, undo_buf, (int)len);
    SAFE_FREE(undo_buf);

    return true;
}

int linstr(const char *restrict str) {
    return linsert_str(str);
}

int lgetchar(unicode_t *restrict uc) {
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

int lover(char *restrict ostr) {
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
    
    // O(1) Index Update: Insert lp2 at position of lp1 (pushing lp1 down)
    long lnum = getlinenum(curbp, lp1);
    int idx;
    if (lnum == 0 && lp1 == curbp->b_linep) {
        // Inserting at end (before header)
        // Use atomic load to get current count for thread safety
        idx = atomic_load(&curbp->b_line_count);
    } else {
        // Inserting before an existing line
        idx = (int)(lnum - 1);
    }
    
    if (idx >= 0) {
        buffer_index_insert(curbp, idx, lp2);
    }

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

/*
 * Insert "n" copies of the character "c" at the current location of dot.
 */
int linsert(int n, int c)
{
	if (curbp->b_mode & MDVIEW)
		return rdonly();
	lchange(WFEDIT);

	// --- PROFILING HOOK ---
	perf_start_timing("linsert");

	struct line *lp1;
	int doto;
	struct window *wp;
	char *inserted_text;
	long lnum;

	lp1 = curwp->w_dotp;

	// Capture state for undo
	lnum = getlinenum(curbp, lp1);
	doto = curwp->w_doto;

	inserted_text = safe_alloc((size_t)n + 1, "undo insert buffer", __FILE__, __LINE__);
	if (inserted_text == nullptr) {
		perf_end_timing("linsert");
		return false;
	}
	for (int i = 0; i < n; ++i) inserted_text[i] = c;
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
            
            // O(1) Index Update
            buffer_index_insert(curbp, 0, first);

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
	if (word_delta == 0) buffer_mark_stats_dirty(curbp);

	undo_record_insert(curbp, lnum, doto, inserted_text, n);
	SAFE_FREE(inserted_text);

	perf_end_timing("linsert");
	return true;
}

/*
 * Delete "n" bytes, starting at dot.
 */
int ldelete(long n, int kflag)
{
	struct line *dotp;
	int doto;
	int chunk;
	struct window *wp;
    char *deleted_text;
    long lnum;

	if (curbp->b_mode & MDVIEW)
		return rdonly();
	if (n < 0) return false;
	if (n == 0) return true;

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
		if (dotp == curbp->b_linep) break;
		chunk = llength(dotp) - doto;
		if (chunk > n) chunk = n;
		if (chunk == 0) { /* End of line, merge.  */
			if (ldelnewline() == false || (kflag != false && kinsert('\n') == false))
				goto undo_fail;
			--n;
		} else {
			/* Delete from gap buffer */
			if (kflag != false) {
				for (int i = 0; i < chunk; i++) {
					char ch = gap_buffer_get_char(dotp->gb, (size_t)(doto + i));
					if (kinsert(ch) == false) goto undo_fail;
				}
			}
			
			if (gap_buffer_delete(dotp->gb, (size_t)doto, (size_t)chunk) != GAP_BUFFER_SUCCESS) {
				goto undo_fail;
			}
			
			wp = wheadp;
			while (wp != nullptr) {
				if (wp->w_dotp == dotp && wp->w_doto >= doto) {
					wp->w_doto -= chunk;
					if (wp->w_doto < doto) wp->w_doto = doto;
				}
				if (wp->w_markp == dotp && wp->w_marko >= doto) {
					wp->w_marko -= chunk;
					if (wp->w_marko < doto) wp->w_marko = doto;
				}
				wp = wp->w_wndp;
			}
			
			atomic_store(&dotp->l_column_cache_dirty, true);
			n -= chunk;
		}
	}

    // Update atomic statistics
    int word_delta = 0;
    if (n == 1 && collected_len == 1 && deleted_text[0] != '\n') {
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
			if (left_is_word && right_is_word) word_delta -= 1;
		} else {
			if (!left_is_word && !right_is_word) word_delta -= 1;
		}
    }
	buffer_update_stats_incremental(curbp, 0, -collected_len, word_delta);
    if (word_delta == 0) buffer_mark_stats_dirty(curbp);

	undo_record_delete(curbp, lnum, doto, deleted_text, collected_len);
	if (kflag != false && collected_len > 0) {
		clipboard_set(deleted_text, (size_t)collected_len);
	}
    SAFE_FREE(deleted_text);
	return n == 0;

undo_fail:
    SAFE_FREE(deleted_text);
    return false;
}

/*
 * Delete a newline.
 */
int ldelnewline(void)
{
	struct line *lp1;
	struct line *lp2;
	struct window *wp;

	if (curbp->b_mode & MDVIEW) return rdonly();
    lchange(WFHARD);
    
    lp1 = curwp->w_dotp;
    lp2 = lp1->l_fp;
	if (lp2 == curbp->b_linep) {
		if (llength(lp1) == 0) {
			lfree(lp1);
            buffer_update_stats_incremental(curbp, -1, -1, 0);
            buffer_mark_stats_dirty(curbp);
        }
		return true;
	}
	
	size_t lp1_len = gap_buffer_size(lp1->gb);
	char *temp = safe_alloc(gap_buffer_size(lp2->gb), "merge temp", __FILE__, __LINE__);
	if (!temp) return false;
		
	gap_buffer_get_text(lp2->gb, 0, gap_buffer_size(lp2->gb), temp, gap_buffer_size(lp2->gb));
	gap_buffer_insert(lp1->gb, lp1_len, temp, gap_buffer_size(lp2->gb));
	SAFE_FREE(temp);
	
	wp = wheadp;
	while (wp != nullptr) {
		if (wp->w_linep == lp2) wp->w_linep = lp1;
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
    
    // O(1) Index Maintenance: Delete lp2 from index
    // lp2 is next line. getlinenum(lp1) gives current line.
    // Since indices are 0-based and lnum is 1-based...
    // If lp1 is line 1 (index 0), lp2 is line 2 (index 1).
    // We want to delete index 1.
    // So index = lnum.
    long lnum = getlinenum(curbp, lp1);
    if (lnum > 0) { // Only delete if valid line number
        buffer_index_delete(curbp, (int)lnum);
    } else if (lp1 == curbp->b_linep) {
        // Deleting newline at end of buffer (header)?
        // If lp1 is header, lp2 is first line (wrapped).
        // Merging header with first line? That's invalid.
        // ldelnewline checks if lp2 == curbp->b_linep (end) and returns true.
        // So we are here only if lp2 != header.
        // If lp1 is header, we are merging END (header) with START (lp2).
        // This effectively deletes the first line.
        // So we delete index 0.
        buffer_index_delete(curbp, 0);
    }

	lfree(lp2);
    buffer_update_stats_incremental(curbp, -1, -1, 0);
    buffer_mark_stats_dirty(curbp);

	return true;
}

/*
 * Delete all of the text saved in the kill buffer.
 */
void kdelete(void)
{
	if (temp_kill_len > 0) {
		temp_kill_buf[temp_kill_len] = '\0';
		kill_ring_add(temp_kill_buf, temp_kill_len);
		clipboard_set(temp_kill_buf, temp_kill_len);
		temp_kill_len = 0;
	}
}

/*
 * Insert a character to the unified kill buffer.
 */
int kinsert(int c)
{
	if (temp_kill_len >= 8191) return false;
	temp_kill_buf[temp_kill_len++] = c;
	return true;
}

/*
 * Yank text back from the kill buffer.
 */
int yank(int f, int n)
{
	int c;
	if (curbp->b_mode & MDVIEW) return rdonly();
	if (n < 0) return false;
	
	if (temp_kill_len == 0) return true;

	while (n--) {
		for (size_t i = 0; i < temp_kill_len; i++) {
			c = temp_kill_buf[i];
			if (c == '\n') {
				if (lnewline() == false) return false;
			} else {
				if (linsert(1, c) == false) return false;
			}
		}
	}

	thisflag |= CFYANK;
	yanked_size = (int)temp_kill_len;
	return true;
}

/* Yank directly from the system clipboard into the buffer */
int yank_clipboard(int f, int n)
{
	char buf[8192];
	(void)f; (void)n;
	if (curbp->b_mode & MDVIEW) return rdonly();
	int len = clipboard_get(buf, sizeof(buf));
	if (len <= 0) {
		mlwrite("[CLIPBOARD EMPTY]");
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
 * C23 Kill Ring Operations
 */

static void kill_ring_add(const char *text, size_t len) {
	if (len == 0 || len >= KILL_ENTRY_MAX) return;

	size_t head = atomic_fetch_add_explicit(&g_kill_ring.head, 1, memory_order_acq_rel);
	head &= (KILL_RING_MAX - 1);

	struct kill_ring_entry *entry = &g_kill_ring.entries[head];

	memcpy(entry->text, text, len);
	entry->text[len] = '\0';

	atomic_store_explicit(&entry->length, len, memory_order_release);
	atomic_store_explicit(&entry->valid, true, memory_order_release);

	atomic_fetch_add_explicit(&g_kill_ring.count, 1, memory_order_relaxed);
	atomic_store_explicit(&g_kill_ring.yank_index, head, memory_order_release);

	/* Sync to system clipboard if enabled (configurable via TOML) */
	if (g_clipboard_config.sync_kills) {
		clipboard_set(text, len);
	}
}

static const char *kill_ring_get(size_t index, size_t *out_len) {
	index &= (KILL_RING_MAX - 1);
	struct kill_ring_entry *entry = &g_kill_ring.entries[index];
	
	if (!atomic_load_explicit(&entry->valid, memory_order_acquire)) {
		if (out_len) *out_len = 0;
		return nullptr;
	}
	
	size_t len = atomic_load_explicit(&entry->length, memory_order_acquire);
	if (out_len) *out_len = len;
	return entry->text;
}

int yankpop(int f, int n) {
	size_t text_len;
	const char *text;
	
	if (curbp->b_mode & MDVIEW) return rdonly();
	if (n < 0) return false;
	
	if (!(lastflag & CFYANK)) {
		mlwrite("PREVIOUS COMMAND WAS NOT A YANK");
		return false;
	}
	
	size_t count = atomic_load_explicit(&g_kill_ring.count, memory_order_acquire);
	if (count == 0) {
		mlwrite("KILL RING IS EMPTY");  
		return false;
	}
	
	size_t current_yank = atomic_load_explicit(&g_kill_ring.yank_index, memory_order_acquire);
	size_t prev_yank = (current_yank - 1) & (KILL_RING_MAX - 1);
	
	text = kill_ring_get(prev_yank, &text_len);
	if (!text || text_len == 0) {
		mlwrite("NO PREVIOUS KILL");
		return false;
	}
	
	if (ldelete(yanked_size, false) == false) {
		return false;
	}
	
	for (size_t i = 0; i < text_len; i++) {
		char c = text[i];
		if (c == '\n') {
			if (lnewline() == false) return false;
		} else {
			if (linsert(1, c) == false) return false;
		}
	}
	
	atomic_store_explicit(&g_kill_ring.yank_index, prev_yank, memory_order_release);
	yanked_size = (int)text_len;
	thisflag |= CFYANK;
	return true;
}

int ldelchar(long n, int kflag) {
    return ldelete(n, kflag);
}
