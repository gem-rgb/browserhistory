/**
 * logging.c — Structured logging system
 *
 * Provides color-coded, level-filtered logging with optional file output.
 * Thread-safe via a global logger singleton initialized at startup.
 *
 * @version 1.0.0
 */

#include "logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

/* ── ANSI color codes ────────────────────────────────────────────── */

static const char *LEVEL_COLORS[] = {
    "\033[36m",   /* DEBUG: Cyan */
    "\033[32m",   /* INFO:  Green */
    "\033[33m",   /* WARN:  Yellow */
    "\033[31m",   /* ERROR: Red */
    "\033[1;31m"  /* FATAL: Bold Red */
};

static const char *LEVEL_NAMES[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};

static const char *COLOR_RESET = "\033[0m";
static const char *COLOR_DIM   = "\033[2m";

/* ── Global logger ───────────────────────────────────────────────── */

static Logger g_logger = {
    .min_level      = LOG_INFO,
    .use_color      = 1,
    .show_timestamp = 1,
    .show_source    = 0,
    .log_file       = NULL,
    .log_path       = "",
    .quiet          = 0
};

static int g_initialized = 0;

/* ── API ─────────────────────────────────────────────────────────── */

void log_init(LogLevel min_level, int use_color) {
    g_logger.min_level = min_level;
    g_logger.use_color = use_color;
    g_logger.show_timestamp = 1;
    g_logger.show_source = (min_level <= LOG_DEBUG);
    g_logger.log_file = NULL;
    g_logger.quiet = 0;
    g_initialized = 1;

    /* Auto-detect color support */
#ifdef _WIN32
    /* Windows: check if we're on a real terminal */
    const char *term = getenv("TERM");
    const char *wt = getenv("WT_SESSION");
    if (!term && !wt) {
        g_logger.use_color = 0;
    }
#else
    /* Unix: check if stderr is a TTY */
    if (!isatty(fileno(stderr))) {
        g_logger.use_color = 0;
    }
#endif
}

int log_set_file(const char *path) {
    if (!path) return -1;

    if (g_logger.log_file) {
        fclose(g_logger.log_file);
        g_logger.log_file = NULL;
    }

    g_logger.log_file = fopen(path, "a");
    if (!g_logger.log_file) {
        fprintf(stderr, "[logging] Cannot open log file: %s\n", path);
        return -1;
    }

    strncpy(g_logger.log_path, path, sizeof(g_logger.log_path) - 1);
    g_logger.log_path[sizeof(g_logger.log_path) - 1] = '\0';

    return 0;
}

void log_set_quiet(int quiet) {
    g_logger.quiet = quiet;
}

Logger *log_get_instance(void) {
    return &g_logger;
}

const char *log_level_name(LogLevel level) {
    if (level < 0 || level >= LOG_LEVEL_COUNT) return "UNKNOWN";
    return LEVEL_NAMES[level];
}

void log_message(LogLevel level, const char *file, int line,
                 const char *fmt, ...) {
    if (!g_initialized) {
        log_init(LOG_INFO, 1);
    }

    if (level < g_logger.min_level) return;

    /* Format timestamp */
    char timestamp[32] = "";
    if (g_logger.show_timestamp) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        if (tm) {
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);
        }
    }

    /* Format the user message */
    char msg[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    /* Extract just the filename from the path */
    const char *basename = file;
    if (file) {
        const char *slash = strrchr(file, '/');
        const char *bslash = strrchr(file, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
        if (slash) basename = slash + 1;
    }

    /* Output to stderr (unless quiet) */
    if (!g_logger.quiet) {
        if (g_logger.use_color) {
            const char *color = (level < LOG_LEVEL_COUNT) ? LEVEL_COLORS[level] : "";

            if (g_logger.show_timestamp) {
                fprintf(stderr, "%s%s%s ", COLOR_DIM, timestamp, COLOR_RESET);
            }

            fprintf(stderr, "%s%-5s%s ", color, LEVEL_NAMES[level], COLOR_RESET);

            if (g_logger.show_source && basename) {
                fprintf(stderr, "%s(%s:%d)%s ", COLOR_DIM, basename, line, COLOR_RESET);
            }

            fprintf(stderr, "%s\n", msg);
        } else {
            if (g_logger.show_timestamp) {
                fprintf(stderr, "%s ", timestamp);
            }

            fprintf(stderr, "%-5s ", LEVEL_NAMES[level]);

            if (g_logger.show_source && basename) {
                fprintf(stderr, "(%s:%d) ", basename, line);
            }

            fprintf(stderr, "%s\n", msg);
        }

        fflush(stderr);
    }

    /* Output to log file (no colors) */
    if (g_logger.log_file) {
        if (g_logger.show_timestamp) {
            fprintf(g_logger.log_file, "%s ", timestamp);
        }

        fprintf(g_logger.log_file, "%-5s ", LEVEL_NAMES[level]);

        if (g_logger.show_source && basename) {
            fprintf(g_logger.log_file, "(%s:%d) ", basename, line);
        }

        fprintf(g_logger.log_file, "%s\n", msg);
        fflush(g_logger.log_file);
    }

    /* Fatal: abort after logging */
    if (level == LOG_FATAL) {
        fprintf(stderr, "FATAL error encountered. Aborting.\n");
        if (g_logger.log_file) {
            fprintf(g_logger.log_file, "FATAL error — aborting.\n");
            fflush(g_logger.log_file);
        }
    }
}

void log_shutdown(void) {
    if (g_logger.log_file) {
        fclose(g_logger.log_file);
        g_logger.log_file = NULL;
    }
    g_initialized = 0;
}

/* ── Platform compat ─────────────────────────────────────────────── */

#ifndef _WIN32
#include <unistd.h>
#endif
