/* 
 * display_width.c - C23 compliant Unicode display width calculations
 * Provides proper cursor positioning for UTF-8 terminal editors
 */

#define _XOPEN_SOURCE 700
#include <wchar.h>
#include <locale.h>
#include <stdatomic.h>
#include "display_width.h"
#include "line.h"

static int wcwidth_initialized = 0;

/*
 * Initialize display width system - call once at program startup
 */
void display_width_init(void)
{
    if (!wcwidth_initialized) {
        setlocale(LC_ALL, "");
        wcwidth_initialized = 1;
    }
}

/*
 * Get display width of a Unicode character
 * Returns: -1 for control chars, 0 for combining, 1 for normal, 2 for wide
 */
int unicode_display_width(unicode_t c)
{
    if (!wcwidth_initialized) {
        display_width_init();
    }
    
    // Handle common control characters explicitly
    if (c < 32) {
        if (c == '\t') {
            return 1; // Tab width is handled separately by caller
        }
        return 0; // Other control chars are non-displaying
    }
    
    // Use system wcwidth for proper Unicode width calculation
    int width = wcwidth((wchar_t)c);
    
    // wcwidth returns -1 for non-printable, 0 for combining, 1/2 for printable
    if (width < 0) {
        return 0; // Non-printable characters
    }
    
    return width;
}

/*
 * Calculate display column position from byte offset in UTF-8 text
 * Used for accurate cursor positioning
 */
int calculate_display_column(const char *text, int byte_offset, int tab_width)
{
    int column = 0;
    int i = 0;
    
    while (i < byte_offset && text[i]) {
        unicode_t c;
        int bytes = utf8_to_unicode((char*)text, i, byte_offset, &c);
        
        if (bytes <= 0) break; // Invalid UTF-8, stop
        
        if (c == '\t') {
            // Handle tab alignment
            column = ((column + tab_width) / tab_width) * tab_width;
        } else {
            int width = unicode_display_width(c);
            if (width > 0) {
                column += width;
            }
        }
        
        i += bytes;
    }
    
    return column;
}

/*
 * Fast cached UTF-8 column calculation using atomic line cache
 * Avoids re-parsing from line start on every cursor movement
 */
int calculate_display_column_cached(struct line *lp, int byte_offset, int tab_width)
{
    if (!lp || byte_offset < 0) return 0;
    
    // Check if we can use cached result
    int cached_offset = atomic_load(&lp->l_column_cache_offset);
    int cached_column = atomic_load(&lp->l_column_cache_column);
    bool cache_dirty = atomic_load(&lp->l_column_cache_dirty);
    
    // If cache is clean and we can use it as starting point
    if (!cache_dirty && cached_offset <= byte_offset) {
        // Calculate incrementally from cached position
        int column = cached_column;
        int i = cached_offset;
        
        while (i < byte_offset && i < lp->l_used && lp->l_text[i]) {
            unicode_t c;
            int bytes = utf8_to_unicode(lp->l_text, i, lp->l_used, &c);
            
            if (bytes <= 0) break; // Invalid UTF-8, stop
            
            if (c == '\t') {
                column = ((column + tab_width) / tab_width) * tab_width;
            } else {
                int width = unicode_display_width(c);
                if (width > 0) {
                    column += width;
                }
            }
            
            i += bytes;
        }
        
        // Update cache atomically with new position
        atomic_store(&lp->l_column_cache_offset, byte_offset);
        atomic_store(&lp->l_column_cache_column, column);
        
        return column;
    } else {
        // Cache miss - calculate from scratch and update cache
        int column = calculate_display_column(lp->l_text, byte_offset, tab_width);
        
        // Update cache atomically
        atomic_store(&lp->l_column_cache_offset, byte_offset);
        atomic_store(&lp->l_column_cache_column, column);
        atomic_store(&lp->l_column_cache_dirty, false);
        
        return column;
    }
}