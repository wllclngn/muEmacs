/*
 * logger.h - Debug logging system for μEmacs
 *
 * Mirrors OUROBOROS logging architecture exactly.
 * Conditionally compiled via UEMACS_DEBUG_LOG flag.
 * Zero runtime cost when disabled.
 *
 * Usage:
 *   LOG_DEBUG("Component: Action - context=value");
 *   LOG_INFO("Component: State change");
 *   LOG_WARN("Component: Recoverable issue");
 *   LOG_ERROR("Component: Critical failure");
 *
 * Build:
 *   make debug-log   # Enables logging to /tmp/uemacs_debug.log
 *   make             # Release build (logging disabled, zero cost)
 */

#ifndef UEMACS_LOGGER_H
#define UEMACS_LOGGER_H

/* Log levels matching OUROBOROS */
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

#if UEMACS_DEBUG_LOG

/* Core API - mirrors OUROBOROS exactly */
void logger_init(void);
void logger_log(LogLevel level, const char *message);
void logger_logf(LogLevel level, const char *fmt, ...);
void logger_debug(const char *message);
void logger_info(const char *message);
void logger_warn(const char *message);
void logger_error(const char *message);
void logger_close(void);

/* Simple string logging */
#define LOG_DEBUG(msg) logger_debug(msg)
#define LOG_INFO(msg)  logger_info(msg)
#define LOG_WARN(msg)  logger_warn(msg)
#define LOG_ERROR(msg) logger_error(msg)

/* Format string logging (printf-style) */
#define LOG_DEBUGF(fmt, ...) logger_logf(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFOF(fmt, ...)  logger_logf(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARNF(fmt, ...)  logger_logf(LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_ERRORF(fmt, ...) logger_logf(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

#else /* UEMACS_DEBUG_LOG disabled */

/* No-op stubs - zero runtime cost */
static inline void logger_init(void) {}
static inline void logger_close(void) {}

#define LOG_DEBUG(msg) ((void)0)
#define LOG_INFO(msg)  ((void)0)
#define LOG_WARN(msg)  ((void)0)
#define LOG_ERROR(msg) ((void)0)

#define LOG_DEBUGF(fmt, ...) ((void)0)
#define LOG_INFOF(fmt, ...)  ((void)0)
#define LOG_WARNF(fmt, ...)  ((void)0)
#define LOG_ERRORF(fmt, ...) ((void)0)

#endif /* UEMACS_DEBUG_LOG */

#endif /* UEMACS_LOGGER_H */
