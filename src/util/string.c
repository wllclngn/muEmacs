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
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>
#include <libgen.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "string_utils.h"
#include "util/logger.h"
#include "memory.h"

/* Safe string copy with bounds checking */
size_t safe_strcpy(char *restrict dest, const char *restrict src, size_t dest_size) {
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
size_t safe_strcat(char *restrict dest, const char *restrict src, size_t dest_size) {
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
size_t safe_sprintf(char *restrict dest, size_t dest_size, const char *restrict format, ...) {
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
size_t safe_vsnprintf(char *restrict dest, size_t dest_size, const char *restrict format, va_list args) {
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
    if (!str) return nullptr;
    
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
        return nullptr;
    }
    
    if (str) {
        *saveptr = str;
    } else {
        str = *saveptr;
    }
    
    if (!str || *str == '\0') {
        return nullptr;
    }
    
    /* Skip leading delimiters */
    str += strspn(str, delim);
    if (*str == '\0') {
        *saveptr = str;
        return nullptr;
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

/* Terminal display utilities - bulk write for performance */
int vtputs(const char* str) {
    if (!str) return 0;
    int len = (int)strlen(str);
    /* Log escape sequences for debugging */
    if (str[0] == '\033') {
        LOG_DEBUGF("TUI: vtputs() ESC sequence len=%d: %s", len, str + 1);
    }
    ttputs(str);  /* Bulk write to terminal buffer */
    return len;
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

int vtputs_width(const char* str, int min_width, int pad_ch) {
    if (!str) return 0;

    int len = (int)strlen(str);

    /* Pad to minimum width */
    while (len < min_width) {
        TTputc((unsigned char)pad_ch);
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

/* ============================================================================
 * ENHANCED STRING OPERATIONS (merged from string_enhanced.c)
 * ============================================================================ */

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

/* ============================================================================
 * ADDITIONAL STRING UTILITIES (merged from string_missing.c)
 * ============================================================================ */

/* Safe snprintf implementation */
int safe_snprintf(char *restrict dest, size_t size, const char *restrict fmt, ...) {
    if (!dest || !fmt || size == 0) {
        return -1;
    }

    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(dest, size, fmt, args);
    va_end(args);

    /* Ensure null termination */
    if (result >= 0 && (size_t)result < size) {
        dest[result] = '\0';
    } else {
        dest[size - 1] = '\0';
    }

    return result;
}

/* Safe basename implementation */
const char* safe_basename(const char* path) {
    if (!path || *path == '\0') {
        return ".";
    }

    /* Create a copy since basename() may modify the string */
    static _Thread_local char path_copy[1024];  /* Thread-safe buffer */
    size_t len = strlen(path);
    if (len >= sizeof(path_copy)) {
        len = sizeof(path_copy) - 1;
    }

    memcpy(path_copy, path, len);
    path_copy[len] = '\0';

    return basename(path_copy);
}

/* ============================================================================
 * DISPLAY WIDTH CALCULATION (from montauk pattern)
 * ============================================================================
 *
 * Properly calculates display width of strings containing ANSI escape sequences.
 * Escape sequences are NOT counted as display characters.
 */

/*
 * Skip an ANSI CSI (Control Sequence Introducer) escape sequence.
 * CSI format: ESC [ <params> <final byte>
 * Final byte is in range 0x40-0x7E (@A-Z[\]^_`a-z{|}~)
 *
 * Returns pointer to character after the sequence, or original if not a CSI.
 */
static const char* skip_csi_sequence(const char* s) {
    if (!s || s[0] != '\033' || s[1] != '[') {
        return s;
    }

    const char* p = s + 2;  /* Skip ESC [ */

    /* Skip parameter bytes (0x30-0x3F: 0-9:;<=>?) */
    while (*p >= 0x30 && *p <= 0x3F) p++;

    /* Skip intermediate bytes (0x20-0x2F: space through /) */
    while (*p >= 0x20 && *p <= 0x2F) p++;

    /* Final byte (0x40-0x7E: @A-Z[\]^_`a-z{|}~) */
    if (*p >= 0x40 && *p <= 0x7E) {
        p++;
    }

    return p;
}

/*
 * Calculate the display width of a string, skipping ANSI escape sequences.
 * Also handles UTF-8 multibyte characters (counts each as 1 display column).
 *
 * This is essential for proper status line rendering.
 */
int display_cols(const char* str) {
    if (!str) return 0;

    int cols = 0;
    const char* p = str;

    while (*p) {
        /* Check for ANSI escape sequence */
        if (p[0] == '\033' && p[1] == '[') {
            p = skip_csi_sequence(p);
            continue;
        }

        /* Handle UTF-8 multibyte sequences */
        unsigned char c = (unsigned char)*p;
        int byte_len = 1;

        if ((c & 0x80) == 0) {
            /* ASCII (0xxxxxxx) */
            byte_len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            /* 2-byte UTF-8 (110xxxxx) */
            byte_len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            /* 3-byte UTF-8 (1110xxxx) */
            byte_len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            /* 4-byte UTF-8 (11110xxx) */
            byte_len = 4;
        }

        /* Each UTF-8 character counts as 1 display column
         * (TODO: proper wcwidth for CJK double-width chars) */
        cols++;
        p += byte_len;
    }

    return cols;
}

/*
 * Truncate string to fit within max_cols display columns.
 * Returns actual display width after truncation.
 */
int display_truncate(char* str, int max_cols) {
    if (!str || max_cols <= 0) return 0;

    int cols = 0;
    char* p = str;
    char* last_safe = str;

    while (*p && cols < max_cols) {
        /* Check for ANSI escape sequence - copy through, don't count */
        if (p[0] == '\033' && p[1] == '[') {
            const char* end = skip_csi_sequence(p);
            while (p < end) p++;
            continue;
        }

        /* Handle UTF-8 multibyte sequences */
        unsigned char c = (unsigned char)*p;
        int byte_len = 1;

        if ((c & 0x80) == 0) {
            byte_len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            byte_len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            byte_len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            byte_len = 4;
        }

        cols++;
        last_safe = p + byte_len;
        p += byte_len;
    }

    /* Truncate at the safe point */
    *last_safe = '\0';
    return cols;
}

/*
 * Pad or truncate string to exactly target_cols display columns.
 * Writes to dest buffer (must be large enough for padding + escape sequences).
 * Returns actual display width.
 */
int display_pad(char* dest, size_t dest_size, const char* src, int target_cols, char pad_char) {
    if (!dest || dest_size == 0) return 0;
    if (!src) src = "";

    /* Copy source to dest */
    safe_strcpy(dest, src, dest_size);

    /* Calculate current display width */
    int current_cols = display_cols(dest);

    if (current_cols >= target_cols) {
        /* Truncate if too long */
        display_truncate(dest, target_cols);
        return target_cols;
    }

    /* Pad to reach target width */
    size_t len = strlen(dest);
    int need_pad = target_cols - current_cols;

    while (need_pad > 0 && len < dest_size - 1) {
        dest[len++] = pad_char;
        need_pad--;
    }
    dest[len] = '\0';

    return target_cols - need_pad;
}

/* ============================================================================
 * SAFE NUMBER PARSING - Modern replacement for atoi()
 * ============================================================================ */

#include <errno.h>
#include <limits.h>

/**
 * safe_atoi - Convert string to int with error handling
 *
 * Uses strtol() internally with proper validation.
 * Returns default_val if str is nullptr, empty, or not a valid integer.
 */
int safe_atoi(const char *str, int default_val)
{
    if (!str || !*str) return default_val;

    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);

    /* Check for conversion errors */
    if (errno == ERANGE || val > INT_MAX || val < INT_MIN) {
        return default_val;  /* Overflow */
    }
    if (endptr == str) {
        return default_val;  /* No digits found */
    }
    /* Skip trailing whitespace for validity check */
    while (*endptr && isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') {
        /* Trailing garbage - still return the parsed value for compatibility */
        /* This matches atoi() behavior */
    }

    return (int)val;
}

/**
 * safe_atoi_range - Convert string to int with range checking
 *
 * Returns default_val if str is invalid or result is outside [min_val, max_val].
 */
int safe_atoi_range(const char *str, int default_val, int min_val, int max_val)
{
    int val = safe_atoi(str, default_val);

    if (val < min_val || val > max_val) {
        return default_val;
    }

    return val;
}
