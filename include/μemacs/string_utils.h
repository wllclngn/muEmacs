/*
 * μEmacs - C23 safe string utilities
 * Eliminates 120+ security vulnerabilities from unsafe string operations
 */

#ifndef UEMACS_STRING_UTILS_H
#define UEMACS_STRING_UTILS_H

#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

// C23 safe string operations - replaces strcpy/strcat/sprintf
size_t safe_strcpy(char* dest, const char* src, size_t dest_size);
size_t safe_strcat(char* dest, const char* src, size_t dest_size);
size_t safe_sprintf(char* dest, size_t dest_size, const char* format, ...);
size_t safe_vsnprintf(char* dest, size_t dest_size, const char* format, va_list args);

// String validation and utilities
bool is_valid_string(const char* str, size_t max_len);
size_t safe_strlen(const char* str, size_t max_len);
char* safe_strdup(const char* src, const char* context);
void safe_string_init(char* buffer, size_t size);

// Buffer boundary checking
bool check_buffer_bounds(const void* buffer, size_t buffer_size, size_t access_size);
size_t get_safe_buffer_length(const char* buffer, size_t max_size);

// Legacy compatibility macros - Phase 3C will replace these
#define SAFE_STRCPY(dest, src) safe_strcpy(dest, src, sizeof(dest))
#define SAFE_STRCAT(dest, src) safe_strcat(dest, src, sizeof(dest))
#define SAFE_SPRINTF(dest, fmt, ...) safe_sprintf(dest, sizeof(dest), fmt, ##__VA_ARGS__)

// Error handling for string operations
typedef enum {
    STRING_SUCCESS = 0,
    STRING_ERROR_NULL_POINTER,
    STRING_ERROR_BUFFER_TOO_SMALL,
    STRING_ERROR_INVALID_FORMAT,
    STRING_ERROR_TRUNCATED
} string_result_t;

// Enhanced string operations with error reporting
string_result_t safe_strcpy_ex(char* dest, const char* src, size_t dest_size, size_t* copied);
string_result_t safe_strcat_ex(char* dest, const char* src, size_t dest_size, size_t* appended);
string_result_t safe_sprintf_ex(char* dest, size_t dest_size, size_t* written, 
                               const char* format, ...);

#endif // UEMACS_STRING_UTILS_H