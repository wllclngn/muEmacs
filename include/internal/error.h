/*
 * error.h - Unified error handling interface for μEmacs
 */

#ifndef ERROR_H
#define ERROR_H

#include <stdbool.h>

/* Error codes enum */
typedef enum {
    ERR_SUCCESS = 0,
    ERR_MEMORY,
    ERR_FILE_NOT_FOUND,
    ERR_FILE_READ,
    ERR_FILE_WRITE,
    ERR_FILE_PERMISSION,
    ERR_BUFFER_INVALID,
    ERR_LINE_INVALID,
    ERR_RANGE_INVALID,
    ERR_SYNTAX_ERROR,
    ERR_COMMAND_UNKNOWN,
    ERR_SIGNAL_INSTALL,
    ERR_TERMINAL_INIT,
    ERR_NULL_POINTER
} error_code_t;

/* Core error functions */
bool report_error(error_code_t code, const char* context);
void set_error_context(const char* function, const char* file, int line);
const char* get_error_message(error_code_t code);
error_code_t get_last_error(void);
void clear_error(void);

/* Convenient error reporting functions */
bool report_memory_error(const char* context);
bool report_file_error(const char* filename, error_code_t file_error);
bool report_buffer_error(const char* buffer_name);
bool report_null_pointer_error(const char* pointer_name);

/* Error logging */
bool enable_error_logging(const char* log_filename);
void disable_error_logging(void);
bool report_error_with_logging(error_code_t code, const char* context);

/* Error recovery */
bool is_recoverable_error(error_code_t code);

/* Assertion handling */
void handle_assertion_failure(const char* expr, const char* file, int line, const char* function);

/* Convenient macros */
#define REPORT_ERROR(code, ctx) \
    (set_error_context(__func__, __FILE__, __LINE__), report_error(code, ctx))

#define REPORT_ERROR_LOG(code, ctx) \
    (set_error_context(__func__, __FILE__, __LINE__), report_error_with_logging(code, ctx))

#define CHECK_PTR(ptr, action) \
    do { if (!(ptr)) { REPORT_ERROR(ERR_NULL_POINTER, #ptr); action; } } while(0)

#define CHECK_PTR_RET(ptr, retval) CHECK_PTR(ptr, return retval)
#define CHECK_PTR_RET_FALSE(ptr) CHECK_PTR_RET(ptr, false)
#define CHECK_PTR_RET_NULL(ptr) CHECK_PTR_RET(ptr, NULL)

/* Safe assertion macro */
#ifdef DEBUG
#define SAFE_ASSERT(expr) \
    ((expr) ? (void)0 : handle_assertion_failure(#expr, __FILE__, __LINE__, __func__))
#else
#define SAFE_ASSERT(expr) ((void)0)
#endif

/* Error checking return helpers */
#define RETURN_IF_ERROR(condition, error_code, context) \
    do { if (condition) return REPORT_ERROR(error_code, context); } while(0)

#define RETURN_NULL_IF_ERROR(condition, error_code, context) \
    do { if (condition) { REPORT_ERROR(error_code, context); return NULL; } } while(0)

#define RETURN_FALSE_IF_ERROR(condition, error_code, context) \
    do { if (condition) { REPORT_ERROR(error_code, context); return false; } } while(0)

#endif /* ERROR_H */