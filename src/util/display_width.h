/*
 * display_width.h - Unicode display width calculations for C23 editors
 */
#ifndef DISPLAY_WIDTH_H
#define DISPLAY_WIDTH_H

#include "utf8.h"  // For unicode_t and utf8_to_unicode

/*
 * Initialize display width system - call once at program startup
 */
void display_width_init(void);

/*
 * Get display width of a Unicode character
 * Returns: -1 for control chars, 0 for combining, 1 for normal, 2 for wide
 */
int unicode_display_width(unicode_t c);

/*
 * Calculate display column position from byte offset in UTF-8 text
 * Used for accurate cursor positioning  
 */
int calculate_display_column(const char *text, int byte_offset, int tab_width);

/*
 * Fast cached UTF-8 column calculation using atomic line cache
 * Avoids re-parsing from line start on every cursor movement
 */
struct line; // Forward declaration
int calculate_display_column_cached(struct line *lp, int byte_offset, int tab_width);

#endif /* DISPLAY_WIDTH_H */