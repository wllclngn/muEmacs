/*
 * string_enhanced.c - Enhanced string operations with error reporting
 * Part of PHASE 3B: Complete bounds-checked string library
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "estruct.h"
#include "edef.h"
#include "string_utils.h"
#include <libgen.h>

/* Buffer boundary checking */
bool check_buffer_bounds(const void* buffer, size_t buffer_size, size_t access_size) {
    if (!buffer || buffer_size == 0 || access_size == 0) {
        return false;
    }
    
    return access_size <= buffer_size;
}

size_t get_safe_buffer_length(const char* buffer, size_t max_size) {
    if (!buffer || max_size == 0) {
        return 0;
    }
    
    size_t len = 0;
    while (len < max_size && buffer[len] != '\0') {
        len++;
    }
    
    return len;
}

/* Case-insensitive string comparison */
int safe_stricmp(const char* s1, const char* s2) {
    if (!s1 || !s2) {
        return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    }
    
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) {
            return c1 - c2;
        }
        s1++;
        s2++;
    }
    
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}