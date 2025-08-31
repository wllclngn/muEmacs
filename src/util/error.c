/*
 * error.c - Unified error handling system for μEmacs
 * 
 * Consolidates 127+ inconsistent error patterns into centralized reporting
 * Provides consistent error messages and debugging context
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "error.h"
#include "string_utils.h"
#include "file_utils.h"

/* Error context tracking */
typedef struct error_context {
    const char* function;
    const char* file;
    int line;
    time_t timestamp;
} error_context_t;

static error_context_t current_context = {0};
static error_code_t last_error = ERR_SUCCESS;

/* Error message table */
static const char* error_messages[] = {
    [ERR_SUCCESS]       = "Success",
    [ERR_MEMORY]        = "Out of memory",
    [ERR_FILE_NOT_FOUND] = "File not found",
    [ERR_FILE_READ]     = "File read error",
    [ERR_FILE_WRITE]    = "File write error",
    [ERR_FILE_PERMISSION] = "File permission denied",
    [ERR_BUFFER_INVALID] = "Invalid buffer",
    [ERR_LINE_INVALID]  = "Invalid line",
    [ERR_RANGE_INVALID] = "Invalid range",
    [ERR_SYNTAX_ERROR]  = "Syntax error",
    [ERR_COMMAND_UNKNOWN] = "Unknown command",
    [ERR_SIGNAL_INSTALL] = "Signal handler installation failed",
    [ERR_TERMINAL_INIT] = "Terminal initialization failed",
    [ERR_NULL_POINTER]  = "Null pointer"
};

/* Set error context for debugging */
void set_error_context(const char* function, const char* file, int line) {
    current_context.function = function;
    current_context.file = file;
    current_context.line = line;
    current_context.timestamp = time(NULL);
}

/* Get error message string */
const char* get_error_message(error_code_t code) {
    if (code >= 0 && code < (sizeof(error_messages) / sizeof(error_messages[0]))) {
        return error_messages[code];
    }
    return "Unknown error";
}

/* Report error with context */
bool report_error(error_code_t code, const char* context) {
    last_error = code;
    
    if (code == ERR_SUCCESS) {
        return true;  /* Not actually an error */
    }
    
    const char* base_message = get_error_message(code);
    
    if (context && *context) {
        if (current_context.function) {
            mlwrite("(%s: %s) [%s:%d in %s()]", 
                   base_message, context,
                   safe_basename(current_context.file),
                   current_context.line,
                   current_context.function);
        } else {
            mlwrite("(%s: %s)", base_message, context);
        }
    } else {
        if (current_context.function) {
            mlwrite("(%s) [%s:%d in %s()]", 
                   base_message,
                   safe_basename(current_context.file),
                   current_context.line,
                   current_context.function);
        } else {
            mlwrite("(%s)", base_message);
        }
    }
    
    return false;
}

/* Get last error code */
error_code_t get_last_error(void) {
    return last_error;
}

/* Clear error state */
void clear_error(void) {
    last_error = ERR_SUCCESS;
    memset(&current_context, 0, sizeof(current_context));
}

/* Convenient error reporting macros implementation */
bool report_memory_error(const char* context) {
    return REPORT_ERROR(ERR_MEMORY, context);
}

bool report_file_error(const char* filename, error_code_t file_error) {
    return REPORT_ERROR(file_error, filename);
}

bool report_buffer_error(const char* buffer_name) {
    return REPORT_ERROR(ERR_BUFFER_INVALID, buffer_name);
}

bool report_null_pointer_error(const char* pointer_name) {
    return REPORT_ERROR(ERR_NULL_POINTER, pointer_name);
}

/* Error logging to file (optional debug feature) */
static FILE* error_log_file = NULL;

bool enable_error_logging(const char* log_filename) {
    if (error_log_file) {
        safe_fclose(&error_log_file);
    }
    
    error_log_file = safe_fopen(log_filename, FILE_APPEND);
    return error_log_file != NULL;
}

void disable_error_logging(void) {
    if (error_log_file) {
        safe_fclose(&error_log_file);
    }
}

/* Log error to file if logging is enabled */
static void log_error(error_code_t code, const char* context) {
    if (!error_log_file) return;
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(error_log_file, "[%s] ERROR %d: %s", 
            timestamp, code, get_error_message(code));
    
    if (context && *context) {
        fprintf(error_log_file, " (%s)", context);
    }
    
    if (current_context.function) {
        fprintf(error_log_file, " at %s:%d in %s()",
                safe_basename(current_context.file),
                current_context.line,
                current_context.function);
    }
    
    fprintf(error_log_file, "\n");
    fflush(error_log_file);
}

/* Enhanced report_error that also logs */
bool report_error_with_logging(error_code_t code, const char* context) {
    log_error(code, context);
    return report_error(code, context);
}

/* Assertion failure handler */
void handle_assertion_failure(const char* expr, const char* file, int line, const char* function) {
    set_error_context(function, file, line);
    report_error(ERR_SYNTAX_ERROR, expr);
    
    /* In debug builds, we might want to abort */
#ifdef DEBUG
    abort();
#endif
}

/* Safe assertion macro */
#ifdef DEBUG
#define SAFE_ASSERT(expr) \
    ((expr) ? (void)0 : handle_assertion_failure(#expr, __FILE__, __LINE__, __func__))
#else
#define SAFE_ASSERT(expr) ((void)0)
#endif

/* Error recovery helpers */
bool is_recoverable_error(error_code_t code) {
    switch (code) {
        case ERR_FILE_NOT_FOUND:
        case ERR_FILE_PERMISSION:
        case ERR_COMMAND_UNKNOWN:
        case ERR_SYNTAX_ERROR:
            return true;
        
        case ERR_MEMORY:
        case ERR_NULL_POINTER:
        case ERR_TERMINAL_INIT:
            return false;
        
        default:
            return true;  /* Assume recoverable unless known otherwise */
    }
}