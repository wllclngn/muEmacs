/*
 * file_utils.h - Safe file operation interface for μEmacs
 */

#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

/* File mode enumeration */
typedef enum {
    FILE_READ,
    FILE_WRITE, 
    FILE_APPEND
} file_mode_t;

/* Core file operations */
FILE* safe_fopen(const char* filename, file_mode_t mode);
bool safe_fclose(FILE** fp);
size_t safe_fread_line(char* buffer, size_t size, FILE* fp);

/* File information */
bool file_exists(const char* filename);
size_t get_file_size(const char* filename);
time_t get_file_mtime(const char* filename);
bool is_file_readable(const char* filename);
bool is_file_writable(const char* filename);

/* File I/O utilities */
char* safe_read_file(const char* filename, size_t* file_size);
bool safe_write_file(const char* filename, const char* data, size_t size);
bool create_backup(const char* filename);

/* Temporary files */
FILE* safe_temp_file(char* temp_name, size_t name_size);

/* File locking */
bool lock_file(FILE* fp);
bool unlock_file(FILE* fp);

#endif /* FILE_UTILS_H */