/*
 * string_utils.h - Safe string operation interface for μEmacs
 */

#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <stddef.h>

/* Safe string operations */
size_t safe_strcpy(char* dest, const char* src, size_t dest_size);
size_t safe_strcat(char* dest, const char* src, size_t dest_size);
int safe_snprintf(char* dest, size_t size, const char* fmt, ...);
const char* safe_basename(const char* path);
int safe_stricmp(const char* s1, const char* s2);
char* safe_strtrim(char* str);
char* safe_strtok(char* str, const char* delim, char** saveptr);

/* Terminal display functions */
int vtputs(const char* str);
int vtprintf(const char* fmt, ...);
int vtputs_width(const char* str, int min_width, char pad_char);
int vtput_separator(const char* sep, int count);
int vtput_progress_bar(int percent, int width);

/* Convenience macros */
#define SAFE_STRCPY(dest, src) \
    safe_strcpy(dest, src, sizeof(dest))

#define SAFE_STRCAT(dest, src) \
    safe_strcat(dest, src, sizeof(dest))

#define SAFE_SNPRINTF(dest, fmt, ...) \
    safe_snprintf(dest, sizeof(dest), fmt, __VA_ARGS__)

#endif /* STRING_UTILS_H */