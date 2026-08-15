#include "log.h"
#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

static FILE     *g_file;
static LogLevel  g_level = LOG_LEVEL_INFO;

static const char *level_name(LogLevel level)
{
    switch (level) {
    case LOG_LEVEL_DEBUG: return "debug";
    case LOG_LEVEL_INFO:  return "info";
    case LOG_LEVEL_WARN:  return "warn";
    case LOG_LEVEL_ERROR: return "error";
    }
    return "?";
}

void log_set_level(LogLevel level)
{
    g_level = level;
}

void log_open_file(const char *dir, const char *filename)
{
    char path[2048];

    if (g_file) {
        return;
    }
    if (!vfs_mkdir_p(dir)) {
        fprintf(stderr, "coab: cannot create log directory %s\n", dir);
        return;
    }
    if (!vfs_path_join(path, sizeof(path), dir, filename)) {
        fprintf(stderr, "coab: log path too long\n");
        return;
    }

    /* The only fopen in the tree that is not vfs_fopen: this one is text, and on
     * OpenVMS leaving it in the CRTL's default record format is what lets TYPE,
     * SEARCH and EDIT read it. Everything else is a byte stream - see
     * vms_compat.h. */
    g_file = fopen(path, "w");
    if (!g_file) {
        fprintf(stderr, "coab: cannot open log file %s\n", path);
    }
}

void log_close(void)
{
    if (g_file) {
        fclose(g_file);
        g_file = NULL;
    }
}

static void stamp(char *dst, size_t dst_size)
{
    time_t now = time(NULL);
    struct tm tm_buf;

    if (localtime_r(&now, &tm_buf)) {
        strftime(dst, dst_size, "%H:%M:%S", &tm_buf);
    } else {
        snprintf(dst, dst_size, "--:--:--");
    }
}

void log_vwrite(LogLevel level, const char *fmt, va_list ap)
{
    char msg[2048];
    char when[16];

    if (level < g_level) {
        return;
    }

    vsnprintf(msg, sizeof(msg), fmt, ap);

    stamp(when, sizeof(when));

    fprintf(stderr, "coab %s [%s] %s\n", when, level_name(level), msg);
    if (g_file) {
        fprintf(g_file, "%s [%s] %s\n", when, level_name(level), msg);
        fflush(g_file);
    }
}

void log_write(LogLevel level, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_vwrite(level, fmt, ap);
    va_end(ap);
}

void log_fatal(const char *fmt, ...)
{
    char msg[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    log_write(LOG_LEVEL_ERROR, "fatal: %s", msg);
    log_close();
    exit(EXIT_FAILURE);
}
