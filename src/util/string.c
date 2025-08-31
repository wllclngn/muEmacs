/*
 * string.c - Safe string operations for μEmacs
 * 
 * Consolidates 195+ unsafe strcpy/strcat/sprintf patterns into bounds-checked functions
 * Eliminates buffer overflows while maintaining performance
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "string_utils.h"
#include "memory.h"

/* Safe string copy with bounds checking */
size_t safe_strcpy(char* dest, const char* src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) {
        return 0;
    }
    
    size_t src_len = strlen(src);
    size_t copy_len = (src_len >= dest_size) ? dest_size - 1 : src_len;
    
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
    
    return copy_len;
}

/* Safe string concatenation with bounds checking */
size_t safe_strcat(char* dest, const char* src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) {
        return 0;
    }
    
    size_t dest_len = strnlen(dest, dest_size);
    if (dest_len >= dest_size) {
        return dest_len;  /* dest is not properly null-terminated */
    }
    
    size_t remaining = dest_size - dest_len;
    size_t src_len = strlen(src);
    size_t copy_len = (src_len >= remaining) ? remaining - 1 : src_len;
    
    memcpy(dest + dest_len, src, copy_len);
    dest[dest_len + copy_len] = '\0';
    
    return dest_len + copy_len;
}

/* Safe formatted string with bounds checking */
size_t safe_sprintf(char* dest, size_t dest_size, const char* format, ...) {
    if (!dest || dest_size == 0 || !format) {
        return 0;
    }
    
    va_list args;
    va_start(args, format);
    int result = vsnprintf(dest, dest_size, format, args);
    va_end(args);
    
    /* Ensure null termination even if truncated */
    dest[dest_size - 1] = '\0';
    
    return (result >= 0 && (size_t)result < dest_size) ? (size_t)result : dest_size - 1;
}

/* Safe vsnprintf wrapper for variadic functions */
size_t safe_vsnprintf(char* dest, size_t dest_size, const char* format, va_list args) {
    if (!dest || dest_size == 0 || !format) {
        return 0;
    }
    
    int result = vsnprintf(dest, dest_size, format, args);
    
    /* Ensure null termination even if truncated */
    dest[dest_size - 1] = '\0';
    
    return (result >= 0 && (size_t)result < dest_size) ? (size_t)result : dest_size - 1;
}

/* String validation utilities */
bool is_valid_string(const char* str, size_t max_len) {
    if (!str) return false;
    
    size_t len = 0;
    while (len < max_len && str[len] != '\0') {
        len++;
    }
    
    return len < max_len;  /* Must be null-terminated within bounds */
}

size_t safe_strlen(const char* str, size_t max_len) {
    if (!str) return 0;
    
    size_t len = 0;
    while (len < max_len && str[len] != '\0') {
        len++;
    }
    
    return len;
}

/* safe_strdup implementation in memory.c - avoiding duplicate symbol */

/* Safe buffer initialization */
void safe_string_init(char* buffer, size_t size) {
    if (buffer && size > 0) {
        buffer[0] = '\0';
        if (size > 1) {
            memset(buffer + 1, 0, size - 1);
        }
    }
}

/* String trimming (removes leading/trailing whitespace) */
char* safe_strtrim(char* str) {
    if (!str) return NULL;
    
    /* Trim leading whitespace */
    while (isspace((unsigned char)*str)) {
        str++;
    }
    
    if (*str == '\0') {
        return str;  /* String is all whitespace */
    }
    
    /* Trim trailing whitespace */
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';
    
    return str;
}

/* Safe string tokenization (thread-safe alternative to strtok) */
char* safe_strtok(char* str, const char* delim, char** saveptr) {
    if (!delim || !saveptr) {
        return NULL;
    }
    
    if (str) {
        *saveptr = str;
    } else {
        str = *saveptr;
    }
    
    if (!str || *str == '\0') {
        return NULL;
    }
    
    /* Skip leading delimiters */
    str += strspn(str, delim);
    if (*str == '\0') {
        *saveptr = str;
        return NULL;
    }
    
    /* Find end of token */
    char* token_end = str + strcspn(str, delim);
    if (*token_end != '\0') {
        *token_end = '\0';
        *saveptr = token_end + 1;
    } else {
        *saveptr = token_end;
    }
    
    return str;
}

/* Terminal display utilities */
int vtputs(const char* str) {
    if (!str) return 0;
    int count = 0;
    for (const char* p = str; *p; ++p) {
        TTputc((unsigned char)*p);
        ++count;
    }
    return count;
}

int vtprintf(const char* fmt, ...) {
    if (!fmt) return 0;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return vtputs(buf);
}

int vtputs_width(const char* str, int min_width, char pad_char) {
    if (!str) return 0;
    
    int len = (int)strlen(str);
    
    /* Pad to minimum width */
    while (len < min_width) {
        TTputc((unsigned char)pad_char);
        len++;
    }
    
    return len;
}

int vtput_separator(const char* sep, int count) {
    if (!sep || count <= 0) return 0;
    
    int total = 0;
    for (int i = 0; i < count; i++) {
        total += vtputs(sep);
    }
    return total;
}

/* Progress bar display */
int vtput_progress_bar(int percent, int width) {
    if (width < 3) return 0;  /* Too narrow for meaningful display */
    
    int filled = (percent * (width - 2)) / 100;  /* Account for brackets */
    int empty = (width - 2) - filled;
    
    int total = 0;
    
    total += TTputc('[');
    
    for (int i = 0; i < filled; i++) {
        total += TTputc('=');
    }
    
    for (int i = 0; i < empty; i++) {
        total += TTputc('-');
    }
    
    total += TTputc(']');
    
    return total;
}
