/*
 * logger.c - Debug logging implementation for μEmacs
 *
 * Mirrors OUROBOROS logging architecture:
 * - Thread-safe with pthread_mutex
 * - File kept open for performance
 * - Flush after every write for immediate visibility
 * - Truncate on init, fallback to append if init missed
 *
 * Conditionally compiled: entire file is empty when UEMACS_DEBUG_LOG=0
 */

#include "util/logger.h"

#if UEMACS_DEBUG_LOG

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

#define LOG_BUFFER_SIZE 1024

/* Static file handle - kept open for performance (OUROBOROS pattern) */
static FILE *log_file = nullptr;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *LOG_PATH = "/tmp/uemacs_debug.log";

void logger_init(void) {
    pthread_mutex_lock(&log_mutex);
    if (log_file) {
        fclose(log_file);
    }
    /* Truncate mode - matches OUROBOROS */
    log_file = fopen(LOG_PATH, "w");
    pthread_mutex_unlock(&log_mutex);
}

void logger_log(LogLevel level, const char *message) {
    pthread_mutex_lock(&log_mutex);

    /* Fallback if init() wasn't called */
    if (!log_file) {
        log_file = fopen(LOG_PATH, "a");
    }
    if (!log_file) {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    /* Timestamp [HH:MM:SS] */
    time_t now = time(nullptr);
    struct tm tm_buf;
    struct tm *tm_info = localtime_r(&now, &tm_buf);

    /* Level string - padded for alignment */
    const char *level_str;
    switch (level) {
        case LOG_LEVEL_DEBUG: level_str = "[DEBUG] "; break;
        case LOG_LEVEL_INFO:  level_str = "[INFO]  "; break;
        case LOG_LEVEL_WARN:  level_str = "[WARN]  "; break;
        case LOG_LEVEL_ERROR: level_str = "[ERROR] "; break;
        default:              level_str = "[?????] "; break;
    }

    fprintf(log_file, "[%02d:%02d:%02d] %s%s\n",
            tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
            level_str, message);
    fflush(log_file);  /* Immediate visibility - OUROBOROS pattern */

    pthread_mutex_unlock(&log_mutex);
}

void logger_logf(LogLevel level, const char *fmt, ...) {
    char buffer[LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    logger_log(level, buffer);
}

void logger_debug(const char *message) {
    logger_log(LOG_LEVEL_DEBUG, message);
}

void logger_info(const char *message) {
    logger_log(LOG_LEVEL_INFO, message);
}

void logger_warn(const char *message) {
    logger_log(LOG_LEVEL_WARN, message);
}

void logger_error(const char *message) {
    logger_log(LOG_LEVEL_ERROR, message);
}

void logger_close(void) {
    pthread_mutex_lock(&log_mutex);
    if (log_file) {
        fclose(log_file);
        log_file = nullptr;
    }
    pthread_mutex_unlock(&log_mutex);
}

#endif /* UEMACS_DEBUG_LOG */
