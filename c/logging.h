/**
 * logging.h — Structured logging system
 *
 * Color-coded, level-filtered logging with optional file output.
 *
 * @version 1.0.0
 */

#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <stdarg.h>

/* ── Log levels ──────────────────────────────────────────────────── */

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL,
    LOG_LEVEL_COUNT
} LogLevel;

/* ── Logger configuration ────────────────────────────────────────── */

typedef struct {
    LogLevel    min_level;          /* messages below this are suppressed */
    int         use_color;          /* 1 = ANSI color codes on stderr */
    int         show_timestamp;     /* 1 = prefix with ISO timestamp */
    int         show_source;        /* 1 = show file:line */
    FILE       *log_file;          /* optional file output (in addition to stderr) */
    char        log_path[512];     /* path to log file */
    int         quiet;             /* 1 = suppress stderr output entirely */
} Logger;

/* ── API ─────────────────────────────────────────────────────────── */

/**
 * Initialize the global logger.
 * Call once at program startup.
 */
void log_init(LogLevel min_level, int use_color);

/**
 * Set the log output file.
 * @param path  Path to log file (opened in append mode)
 * @return 0 on success, -1 on error
 */
int log_set_file(const char *path);

/**
 * Set quiet mode (suppress stderr).
 */
void log_set_quiet(int quiet);

/**
 * Get the global logger instance.
 */
Logger *log_get_instance(void);

/**
 * Log a message at the given level.
 * Use the macros below for convenience.
 */
void log_message(LogLevel level, const char *file, int line,
                 const char *fmt, ...);

/**
 * Get the name of a log level.
 */
const char *log_level_name(LogLevel level);

/**
 * Close the log file and clean up.
 */
void log_shutdown(void);

/* ── Convenience macros ──────────────────────────────────────────── */

#define LOG_DEBUG(fmt, ...)  log_message(LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)   log_message(LOG_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   log_message(LOG_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  log_message(LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...)  log_message(LOG_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif /* LOGGING_H */
