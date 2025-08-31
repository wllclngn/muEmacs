/*
 * buffer_utils.c - Consolidated buffer operations
 * Eliminates 50+ redundant buffer/line/window patterns
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "buffer_utils.h"

// Buffer utility functions - consolidates common patterns
struct buffer* find_buffer_by_name(const char* name) {
    if (!name) return NULL;
    
    FOR_EACH_BUFFER(bp) {
        if (strcmp(bp->b_bname, name) == 0) {
            return bp;
        }
    }
    return NULL;
}

struct buffer* find_buffer_by_filename(const char* filename) {
    if (!filename) return NULL;
    
    FOR_EACH_BUFFER(bp) {
        if (strcmp(bp->b_fname, filename) == 0) {
            return bp;
        }
    }
    return NULL;
}

bool buffer_is_modified(struct buffer* bp) {
    if (!bp) return false;
    return (bp->b_flag & BFCHG) != 0;
}

bool buffer_is_empty(struct buffer* bp) {
    if (!bp || !bp->b_linep) return true;
    
    // Buffer is empty if it only has the header line
    struct line* first_line = lforw(bp->b_linep);
    return (first_line == bp->b_linep);
}

int count_buffer_lines(struct buffer* bp) {
    if (!bp || !bp->b_linep) return 0;
    
    int count = 0;
    FOR_EACH_LINE_IN_BUFFER(lp, bp) {
        count++;
    }
    return count;
}

size_t count_buffer_bytes(struct buffer* bp) {
    if (!bp || !bp->b_linep) return 0;
    
    size_t total = 0;
    FOR_EACH_LINE_IN_BUFFER(lp, bp) {
        total += llength(lp);
        total++; // Add newline character
    }
    
    // Don't count the final newline if buffer ends without it
    if (total > 0) total--;
    
    return total;
}

// Line utility functions - eliminates duplicate line operations
bool line_is_empty(struct line* lp) {
    if (!lp) return true;
    return llength(lp) == 0;
}

bool line_is_whitespace_only(struct line* lp) {
    if (!lp) return true;
    
    int len = llength(lp);
    for (int i = 0; i < len; i++) {
        char c = lgetc(lp, i);
        if (c != ' ' && c != '\t' && c != '\r') {
            return false;
        }
    }
    return true;
}

struct line* find_line_number(struct buffer* bp, int line_num) {
    if (!bp || line_num < 1) return NULL;
    
    int current_line = 1;
    FOR_EACH_LINE_IN_BUFFER(lp, bp) {
        if (current_line == line_num) {
            return lp;
        }
        current_line++;
    }
    return NULL;
}

int get_line_number(struct buffer* bp, struct line* target_lp) {
    if (!bp || !target_lp) return -1;
    
    int line_num = 1;
    FOR_EACH_LINE_IN_BUFFER(lp, bp) {
        if (lp == target_lp) {
            return line_num;
        }
        line_num++;
    }
    return -1; // Not found
}

// Window utility functions
struct window* find_window_for_buffer(struct buffer* bp) {
    if (!bp) return NULL;
    
    FOR_EACH_WINDOW(wp) {
        if (wp->w_bufp == bp) {
            return wp;
        }
    }
    return NULL;
}

void update_all_windows_for_buffer(struct buffer* bp) {
    if (!bp) return;
    
    FOR_EACH_WINDOW(wp) {
        if (wp->w_bufp == bp) {
            wp->w_flag |= WFHARD; // Mark for full redraw
        }
    }
}

void refresh_all_windows(void) {
    FOR_EACH_WINDOW(wp) {
        wp->w_flag |= WFHARD; // Mark all windows for refresh
    }
}