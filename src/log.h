/* log.h - diagnostics. Ported from Logging/Logging.cs.
 *
 * Messages always go to stderr; once log_open_file() has been called they are
 * also appended to a log file. log_fatal() never returns.
 */
#ifndef COAB_LOG_H
#define COAB_LOG_H

#include "coab.h"

#include <stdarg.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

void log_set_level(LogLevel level);
void log_open_file(const char *dir, const char *filename);
void log_close(void);

void log_write(LogLevel level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* For the callers that have a va_list already, such as the ECL trace. */
void log_vwrite(LogLevel level, const char *fmt, va_list ap);

void log_fatal(const char *fmt, ...)
    __attribute__((format(printf, 1, 2), noreturn));

#define log_debug(...) log_write(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define log_info(...)  log_write(LOG_LEVEL_INFO,  __VA_ARGS__)
#define log_warn(...)  log_write(LOG_LEVEL_WARN,  __VA_ARGS__)
#define log_error(...) log_write(LOG_LEVEL_ERROR, __VA_ARGS__)

#endif /* COAB_LOG_H */
