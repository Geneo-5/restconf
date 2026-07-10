#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

/* Log levels */
#define RC_LOG_TRACE 0
#define RC_LOG_DEBUG 1
#define RC_LOG_INFO  2
#define RC_LOG_WARN  3
#define RC_LOG_ERROR 4
#define RC_LOG_FATAL 5

/* Compile-time maximum log level (default to INFO if not defined) */
#ifndef RC_COMPILE_TIME_LOG_LEVEL
#define RC_COMPILE_TIME_LOG_LEVEL RC_LOG_INFO
#endif

/* Output targets */
typedef enum {
    RC_LOG_TARGET_STDOUT,
    RC_LOG_TARGET_SYSLOG
} rc_log_target_t;

/* Initialization and control */
void rc_log_init(rc_log_target_t target, int runtime_level);
void rc_log_set_level(int level);

/* Internal print function */
void rc_log_print(int level, const char *file, int line, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

/* Macros for compile-time stripping.
 * Use __VA_ARGS__ to include the format string, which ensures standard C99
 * compliance (at least one argument is always present) and avoids the
 * GNU ##__VA_ARGS__ extension warning with -pedantic. */
#if RC_COMPILE_TIME_LOG_LEVEL <= RC_LOG_TRACE
#define RC_TRACE(...) rc_log_print(RC_LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#else
#define RC_TRACE(...) do {} while (0)
#endif

#if RC_COMPILE_TIME_LOG_LEVEL <= RC_LOG_DEBUG
#define RC_DEBUG(...) rc_log_print(RC_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#else
#define RC_DEBUG(...) do {} while (0)
#endif

#if RC_COMPILE_TIME_LOG_LEVEL <= RC_LOG_INFO
#define RC_INFO(...) rc_log_print(RC_LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#else
#define RC_INFO(...) do {} while (0)
#endif

#if RC_COMPILE_TIME_LOG_LEVEL <= RC_LOG_WARN
#define RC_WARN(...) rc_log_print(RC_LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#else
#define RC_WARN(...) do {} while (0)
#endif

#if RC_COMPILE_TIME_LOG_LEVEL <= RC_LOG_ERROR
#define RC_ERROR(...) rc_log_print(RC_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#else
#define RC_ERROR(...) do {} while (0)
#endif

#if RC_COMPILE_TIME_LOG_LEVEL <= RC_LOG_FATAL
#define RC_FATAL(...) rc_log_print(RC_LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)
#else
#define RC_FATAL(...) do {} while (0)
#endif

#endif /* LOGGER_H */
