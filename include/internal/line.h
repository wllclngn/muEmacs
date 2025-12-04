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

#define lforw(lp)       ((lp)->l_fp)
#define lback(lp)       ((lp)->l_bp)
#define lgetc(lp, n)    gap_buffer_get_char((lp)->gb, (n))
#define lputc(lp, n, c) do { char _ch = (c); gap_buffer_delete((lp)->gb, (n), 1); gap_buffer_insert((lp)->gb, (n), &_ch, 1); } while(0)
#define llength(lp)     ((int)gap_buffer_size((lp)->gb))

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

#endif  /* LINE_H_ */
