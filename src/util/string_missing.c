/*
 * string_missing.c - Missing string utility implementations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <libgen.h>

#include "estruct.h"
#include "edef.h"
#include "string_utils.h"

/* Safe snprintf implementation */
int safe_snprintf(char* dest, size_t size, const char* fmt, ...) {
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
    static char path_copy[1024];
    size_t len = strlen(path);
    if (len >= sizeof(path_copy)) {
        len = sizeof(path_copy) - 1;
    }
    
    memcpy(path_copy, path, len);
    path_copy[len] = '\0';
    
    return basename(path_copy);
}