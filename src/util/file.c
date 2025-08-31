/*
 * file.c - Safe file operations for μEmacs
 * 
 * Consolidates scattered fopen patterns and provides consistent file handling
 * with proper error reporting and resource management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "error.h"
#include "memory.h"
#include "string_utils.h"
#include "file_utils.h"

/* File mode mapping */
static const char* file_mode_strings[] = {
    [FILE_READ]   = "r",
    [FILE_WRITE]  = "w", 
    [FILE_APPEND] = "a"
};

/* Safe file opening with error reporting */
FILE* safe_fopen(const char* filename, file_mode_t mode) {
    CHECK_PTR_RET_NULL(filename);
    
    if (mode >= sizeof(file_mode_strings) / sizeof(file_mode_strings[0])) {
        REPORT_ERROR(ERR_SYNTAX_ERROR, "Invalid file mode");
        return NULL;
    }
    
    FILE* fp = fopen(filename, file_mode_strings[mode]);
    if (!fp) {
        error_code_t error_type;
        switch (errno) {
            case ENOENT:
                error_type = ERR_FILE_NOT_FOUND;
                break;
            case EACCES:
                error_type = ERR_FILE_PERMISSION;
                break;
            default:
                error_type = (mode == FILE_READ) ? ERR_FILE_READ : ERR_FILE_WRITE;
                break;
        }
        REPORT_ERROR(error_type, filename);
    }
    
    return fp;
}

/* Safe file closing that nullifies pointer */
bool safe_fclose(FILE** fp) {
    if (!fp || !*fp) {
        return true;  /* Already closed or NULL */
    }
    
    int result = fclose(*fp);
    *fp = NULL;  /* Prevent double close */
    
    if (result != 0) {
        REPORT_ERROR(ERR_FILE_WRITE, "Close failed");
        return false;
    }
    
    return true;
}

/* Safe line reading with bounds checking */
size_t safe_fread_line(char* buffer, size_t size, FILE* fp) {
    CHECK_PTR_RET(buffer, 0);
    CHECK_PTR_RET(fp, 0);
    
    if (size == 0) return 0;
    
    char* result = fgets(buffer, (int)size, fp);
    if (!result) {
        buffer[0] = '\0';
        return 0;
    }
    
    size_t len = strlen(buffer);
    
    /* Remove trailing newline if present */
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
        len--;
    }
    
    return len;
}

/* Check if file exists */
bool file_exists(const char* filename) {
    CHECK_PTR_RET_FALSE(filename);
    
    struct stat st;
    return stat(filename, &st) == 0;
}

/* Get file size */
size_t get_file_size(const char* filename) {
    CHECK_PTR_RET(filename, 0);
    
    struct stat st;
    if (stat(filename, &st) != 0) {
        REPORT_ERROR(ERR_FILE_NOT_FOUND, filename);
        return 0;
    }
    
    if (!S_ISREG(st.st_mode)) {
        REPORT_ERROR(ERR_SYNTAX_ERROR, "Not a regular file");
        return 0;
    }
    
    return (size_t)st.st_size;
}

/* Get file modification time */
time_t get_file_mtime(const char* filename) {
    CHECK_PTR_RET(filename, 0);
    
    struct stat st;
    if (stat(filename, &st) != 0) {
        REPORT_ERROR(ERR_FILE_NOT_FOUND, filename);
        return 0;
    }
    
    return st.st_mtime;
}

/* Check if file is readable */
bool is_file_readable(const char* filename) {
    CHECK_PTR_RET_FALSE(filename);
    
    return access(filename, R_OK) == 0;
}

/* Check if file is writable */
bool is_file_writable(const char* filename) {
    CHECK_PTR_RET_FALSE(filename);
    
    return access(filename, W_OK) == 0;
}

/* Safe file reading into buffer */
char* safe_read_file(const char* filename, size_t* file_size) {
    CHECK_PTR_RET_NULL(filename);
    
    FILE* fp = safe_fopen(filename, FILE_READ);
    if (!fp) return NULL;
    
    /* Get file size */
    size_t size = get_file_size(filename);
    if (size == 0) {
        safe_fclose(&fp);
        return NULL;
    }
    
    /* Allocate buffer with space for null terminator */
    char* buffer = SAFE_ALLOC_SIZED(char, size + 1, "file buffer");
    if (!buffer) {
        safe_fclose(&fp);
        return NULL;
    }
    
    /* Read file contents */
    size_t bytes_read = fread(buffer, 1, size, fp);
    safe_fclose(&fp);
    
    if (bytes_read != size && !feof(fp)) {
        REPORT_ERROR(ERR_FILE_READ, filename);
        SAFE_FREE(buffer);
        return NULL;
    }
    
    buffer[bytes_read] = '\0';  /* Null terminate */
    
    if (file_size) {
        *file_size = bytes_read;
    }
    
    return buffer;
}

/* Safe file writing from buffer */
bool safe_write_file(const char* filename, const char* data, size_t size) {
    CHECK_PTR_RET_FALSE(filename);
    CHECK_PTR_RET_FALSE(data);
    
    FILE* fp = safe_fopen(filename, FILE_WRITE);
    if (!fp) return false;
    
    size_t bytes_written = fwrite(data, 1, size, fp);
    bool success = (bytes_written == size);
    
    if (!success) {
        REPORT_ERROR(ERR_FILE_WRITE, filename);
    }
    
    safe_fclose(&fp);
    return success;
}

/* Create backup file */
bool create_backup(const char* filename) {
    CHECK_PTR_RET_FALSE(filename);
    
    if (!file_exists(filename)) {
        return true;  /* No backup needed for non-existent file */
    }
    
    /* Create backup filename */
    char backup_name[NFILEN];
    safe_snprintf(backup_name, sizeof(backup_name), "%s.bak", filename);
    
    /* Read original file */
    size_t file_size;
    char* data = safe_read_file(filename, &file_size);
    if (!data) return false;
    
    /* Write backup */
    bool success = safe_write_file(backup_name, data, file_size);
    SAFE_FREE(data);
    
    return success;
}

/* Safe temporary file creation */
FILE* safe_temp_file(char* temp_name, size_t name_size) {
    CHECK_PTR_RET_NULL(temp_name);
    
    /* Create template for temporary file */
    safe_snprintf(temp_name, name_size, "/tmp/uemacs_XXXXXX");
    
    int fd = mkstemp(temp_name);
    if (fd < 0) {
        REPORT_ERROR(ERR_FILE_WRITE, "Cannot create temporary file");
        return NULL;
    }
    
    FILE* fp = fdopen(fd, "w+");
    if (!fp) {
        close(fd);
        unlink(temp_name);
        REPORT_ERROR(ERR_FILE_WRITE, "Cannot open temporary file");
        return NULL;
    }
    
    return fp;
}

/* File locking helpers */
bool lock_file(FILE* fp) {
    CHECK_PTR_RET_FALSE(fp);
    
    int fd = fileno(fp);
    if (fd < 0) {
        REPORT_ERROR(ERR_FILE_WRITE, "Invalid file descriptor");
        return false;
    }
    
    struct flock lock;
    lock.l_type = F_WRLCK;    /* Write lock */
    lock.l_whence = SEEK_SET; /* From beginning */
    lock.l_start = 0;         /* Offset */
    lock.l_len = 0;          /* Entire file */
    
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        if (errno == EAGAIN || errno == EACCES) {
            REPORT_ERROR(ERR_FILE_PERMISSION, "File is locked");
        } else {
            REPORT_ERROR(ERR_FILE_WRITE, "Lock failed");
        }
        return false;
    }
    
    return true;
}

bool unlock_file(FILE* fp) {
    CHECK_PTR_RET_FALSE(fp);
    
    int fd = fileno(fp);
    if (fd < 0) return false;
    
    struct flock lock;
    lock.l_type = F_UNLCK;    /* Unlock */
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    
    return fcntl(fd, F_SETLK, &lock) >= 0;
}