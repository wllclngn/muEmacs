#ifndef LINE_H_
#define LINE_H_

#include "utf8.h"
#include "c23_compat.h"
#include "text_storage.h"

/*
 * All text is kept in circularly linked lists of "struct line" structures. These
 * begin at the header line (which is the blank line beyond the end of the
 * buffer). This line is pointed to by the "struct buffer". Each line contains a the
 * number of bytes in the line (the "used" size), the size of the text array,
 * and the text. The end of line is not stored as a byte; it's implied. Future
 * additions will include update hints, and a list of marks into the line.
 *
 * C23 modernization: Uses abstract text_storage interface supporting:
 *   - Gap buffer: O(1) edits at cursor, optimal for small files
 *   - Piece table: O(1) load via mmap, optimal for large files
 */

struct text_storage;
struct buffer;

struct line {
    struct line *l_fp;	      /* Link to the next line        */
    struct line *l_bp;	      /* Link to the previous line    */
    struct text_storage *storage; /* Abstract storage backend (nullptr = view mode) */

    /* View mode: when storage is nullptr, line is a view into buffer's b_text.
    * This enables O(1) loading of large files via mmap.
    */
    size_t l_view_offset;         /* Start offset in buffer's b_text */
    size_t l_view_length;         /* Length in bytes (excluding newline) */
    struct buffer *l_bp_owner;    /* Owning buffer (for view mode b_text access) */

    // Atomic column cache for instant UTF-8 cursor positioning
    _Atomic int l_column_cache_offset;  /* Last cached byte offset */
    _Atomic int l_column_cache_column;  /* Display column at offset */
    _Atomic bool l_column_cache_dirty;  /* Cache needs invalidation */

    // Pretext-inspired wrap segment cache (lazily computed, invalidated on edit)
    struct wrap_cache *l_wrap_cache;
};

/* View mode support functions (defined in line.c) */
extern int lgetc_view(struct line *lp, int n);
extern void lputc_impl(struct line *lp, int n, int c);
extern void line_materialize(struct line *lp);  /* Convert view line to editable */
extern bool line_view_accessible(struct line *lp);  /* Check if view mode data is accessible */

/* Modern inline functions - type-safe replacements for legacy macros */
static inline struct line *lforw(struct line *lp) { return lp->l_fp; }
static inline struct line *lback(struct line *lp) { return lp->l_bp; }

/* Get character at position n - view-aware */
static inline int lgetc(struct line *lp, int n) {
    if (lp->storage) {
        return (unsigned char)TS_GET_CHAR(lp->storage, (size_t)n);
    }
    /* View mode: delegate to function that can access buffer's b_text */
    return lgetc_view(lp, n);
}

/* Put character at position n - view-aware (materializes line if needed) */
static inline void lputc(struct line *lp, int n, int c) {
    if (!lp->storage) {
        /* View mode: must materialize before editing */
        line_materialize(lp);
    }
    lputc_impl(lp, n, c);
}

/* Get line length - view-aware */
static inline int llength(struct line *lp) {
    if (lp->storage) {
        return (int)TS_SIZE(lp->storage);
    }
    /* View mode: verify backing storage is accessible */
    if (line_view_accessible(lp)) {
        return (int)lp->l_view_length;
    }
    /* View mode but backing storage inaccessible - effectively empty */
    return 0;
}

/* Shared word-byte classification for undo grouping and word operations */
UNSEQUENCED static inline bool is_word_byte(int ch) {
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
extern struct line *lalloc(int);  /* Allocate a line with own storage */
extern struct line *lalloc_view(struct buffer *bp, size_t offset, size_t length);  /* Allocate view line */
extern long getlinenum(struct buffer *bp, struct line *lp);

/* Get line text into buffer - handles both regular and view mode lines */
extern size_t lget_text(struct line *lp, size_t offset, size_t len, char *buf, size_t buf_size);

#endif  /* LINE_H_ */
