#ifndef STRING_SAFE_H
#define STRING_SAFE_H

#include <stddef.h>

// Safe string operations from Phase 1 modernization
size_t safe_strcpy(char* dest, const char* src, size_t dest_size);
size_t safe_strcat(char* dest, const char* src, size_t dest_size);
int safe_snprintf(char* dest, size_t size, const char* fmt, ...);

#endif // STRING_SAFE_H