/*
 * string_utils.h - Safe string operation interface for μEmacs
 *
 * This is the consolidated header for all safe string operations.
 * Combines declarations from multiple legacy headers.
 */

#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

/* ============================================================================
 * CORE SAFE STRING OPERATIONS
 * Replaces unsafe strcpy/strcat/sprintf throughout codebase
 * ============================================================================ */

size_t safe_strcpy(char *restrict dest, const char *restrict src, size_t dest_size);
size_t safe_strcat(char *restrict dest, const char *restrict src, size_t dest_size);
size_t safe_sprintf(char *restrict dest, size_t dest_size, const char *restrict format, ...);
size_t safe_vsnprintf(char *restrict dest, size_t dest_size, const char *restrict format, va_list args);
int safe_snprintf(char *restrict dest, size_t size, const char *restrict fmt, ...);

/* ============================================================================
 * STRING VALIDATION AND UTILITIES
 * ============================================================================ */

bool is_valid_string(const char *str, size_t max_len);
size_t safe_strlen(const char *str, size_t max_len);
char *safe_strdup(const char *src, const char *context);
void safe_string_init(char *buffer, size_t size);
char *safe_strtrim(char *str);
char *safe_strtok(char *str, const char *delim, char **saveptr);
int safe_stricmp(const char *s1, const char *s2);
const char *safe_basename(const char *path);

/* ============================================================================
 * SAFE NUMBER PARSING (replaces unsafe atoi)
 * ============================================================================ */

/**
 * safe_atoi - Convert string to int with error handling
 *
 * Uses strtol() internally with proper validation.
 * Returns default_val if str is NULL, empty, or not a valid integer.
 */
int safe_atoi(const char *str, int default_val);

/**
 * safe_atoi_range - Convert string to int with range checking
 *
 * Returns default_val if str is invalid or result is outside [min_val, max_val].
 */
int safe_atoi_range(const char *str, int default_val, int min_val, int max_val);

/* ============================================================================
 * BUFFER BOUNDARY CHECKING
 * ============================================================================ */

bool check_buffer_bounds(const void *buffer, size_t buffer_size, size_t access_size);
size_t get_safe_buffer_length(const char *buffer, size_t max_size);

/* ============================================================================
 * TERMINAL DISPLAY FUNCTIONS
 * ============================================================================ */

int vtputs(const char *str);
int vtprintf(const char *fmt, ...);
int vtputs_width(const char *str, int min_width, int pad_ch);
int vtput_separator(const char *sep, int count);
int vtput_progress_bar(int percent, int width);

/* ============================================================================
 * DISPLAY WIDTH CALCULATION (ANSI-aware)
 *
 * These functions properly skip ANSI escape sequences when calculating
 * display width, essential for correct status line and modeline rendering.
 * ============================================================================ */

/* Calculate display columns, skipping ANSI escape sequences */
int display_cols(const char *str);

/* Truncate string to fit within max_cols display columns */
int display_truncate(char *str, int max_cols);

/* Pad or truncate string to exactly target_cols display columns */
int display_pad(char *dest, size_t dest_size, const char *src, int target_cols, char pad_char);

/* ============================================================================
 * CONVENIENCE MACROS
 * ============================================================================ */

#define SAFE_STRCPY(dest, src) \
    safe_strcpy(dest, src, sizeof(dest))

#define SAFE_STRCAT(dest, src) \
    safe_strcat(dest, src, sizeof(dest))

#define SAFE_SNPRINTF(dest, fmt, ...) \
    safe_snprintf(dest, sizeof(dest), fmt, ##__VA_ARGS__)

#define SAFE_SPRINTF(dest, fmt, ...) \
    safe_sprintf(dest, sizeof(dest), fmt, ##__VA_ARGS__)

/* ============================================================================
 * ERROR HANDLING (Enhanced operations)
 * ============================================================================ */

typedef enum {
    STRING_SUCCESS = 0,
    STRING_ERROR_NULL_POINTER,
    STRING_ERROR_BUFFER_TOO_SMALL,
    STRING_ERROR_INVALID_FORMAT,
    STRING_ERROR_TRUNCATED
} string_result_t;

string_result_t safe_strcpy_ex(char *dest, const char *src, size_t dest_size, size_t *copied);
string_result_t safe_strcat_ex(char *dest, const char *src, size_t dest_size, size_t *appended);
string_result_t safe_sprintf_ex(char *dest, size_t dest_size, size_t *written,
                                const char *format, ...);

#endif /* STRING_UTILS_H */
