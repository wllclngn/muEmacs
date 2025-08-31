/*
 * μEmacs - C23 buffer utilities
 * Consolidates 10+ redundant buffer iteration patterns
 */

#ifndef UEMACS_BUFFER_UTILS_H
#define UEMACS_BUFFER_UTILS_H

#include "core.h"

// Modern C23 buffer iteration - eliminates redundant loop patterns
#define FOR_EACH_BUFFER(bp) \
    for (struct buffer *bp = bheadp; bp != NULL; bp = bp->b_bufp)

#define FOR_EACH_LINE_IN_BUFFER(lp, bp) \
    for (struct line *lp = lforw((bp)->b_linep); lp != (bp)->b_linep; lp = lforw(lp))

#define FOR_EACH_LINE_REVERSE_IN_BUFFER(lp, bp) \
    for (struct line *lp = lback((bp)->b_linep); lp != (bp)->b_linep; lp = lback(lp))

#define FOR_EACH_WINDOW(wp) \
    for (struct window *wp = wheadp; wp != NULL; wp = wp->w_wndp)

// Buffer utility functions - consolidates common patterns
struct buffer* find_buffer_by_name(const char* name);
struct buffer* find_buffer_by_filename(const char* filename);
bool buffer_is_modified(struct buffer* bp);
bool buffer_is_empty(struct buffer* bp);
int count_buffer_lines(struct buffer* bp);
size_t count_buffer_bytes(struct buffer* bp);

// Line utility functions - eliminates duplicate line operations
bool line_is_empty(struct line* lp);
bool line_is_whitespace_only(struct line* lp);
struct line* find_line_number(struct buffer* bp, int line_num);
int get_line_number(struct buffer* bp, struct line* target_lp);

// Window utility functions  
struct window* find_window_for_buffer(struct buffer* bp);
void update_all_windows_for_buffer(struct buffer* bp);
void refresh_all_windows(void);

#endif // UEMACS_BUFFER_UTILS_H