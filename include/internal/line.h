#ifndef LINE_H_
#define LINE_H_

#include "utf8.h"
#include "c23_compat.h"
#include "gapbuffer.h"

/*
 * All text is kept in circularly linked lists of "struct line" structures. These
 * begin at the header line (which is the blank line beyond the end of the
 * buffer). This line is pointed to by the "struct buffer". Each line contains a the
 * number of bytes in the line (the "used" size), the size of the text array,
 * and the text. The end of line is not stored as a byte; it's implied. Future
 * additions will include update hints, and a list of marks into the line.
 * 
 * C23 modernization: Uses flexible array member for cache-efficient text storage.
 */

struct gap_buffer;

struct line {
	struct line *l_fp;	/* Link to the next line        */
	struct line *l_bp;	/* Link to the previous line    */
	struct gap_buffer *gb;	/* Gap buffer for O(1) edits    */
	
	// Atomic column cache for instant UTF-8 cursor positioning
	_Atomic int l_column_cache_offset;  /* Last cached byte offset */
	_Atomic int l_column_cache_column;  /* Display column at offset */
	_Atomic bool l_column_cache_dirty;  /* Cache needs invalidation */
};

/* Modern inline functions - type-safe replacements for legacy macros */
static inline struct line *lforw(struct line *lp) { return lp->l_fp; }
static inline struct line *lback(struct line *lp) { return lp->l_bp; }
static inline int lgetc(struct line *lp, int n) { return gap_buffer_get_char(lp->gb, (size_t)n); }
static inline void lputc(struct line *lp, int n, int c) {
	char ch = (char)c;
	gap_buffer_delete(lp->gb, (size_t)n, 1);
	gap_buffer_insert(lp->gb, (size_t)n, &ch, 1);
}
static inline int llength(struct line *lp) { return (int)gap_buffer_size(lp->gb); }

/* Shared word-byte classification for undo grouping and word operations */
static inline bool is_word_byte(int ch) {
	return ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r';
}

extern void lfree(struct line *lp);
extern void lchange(int flag);
extern int insspace(int f, int n);
extern int linstr(const char *instr);
extern int linsert(int n, int c);
extern int linsert_str(const char *str);
extern int lowrite(int c);
extern int lover(char *ostr);
extern int lnewline(void);
extern int ldelete(long n, int kflag);
extern int ldelchar(long n, int kflag);
extern int lgetchar(unicode_t *);
extern char *getctext(void);
extern int putctext(const char *iline);
extern int ldelnewline(void);
extern void kdelete(void);
extern int kinsert(int c);
extern int yank(int f, int n);
extern int yank_clipboard(int f, int n);
extern int yankpop(int f, int n);
extern struct line *lalloc(int);  /* Allocate a line. */
extern long getlinenum(struct buffer *bp, struct line *lp);

#endif  /* LINE_H_ */
