/**
 * browser_detect.h — Multi-browser detection and profile enumeration
 *
 * Scans the filesystem for installed Chromium-based browsers and Firefox,
 * enumerates profiles, and provides a unified interface for querying
 * history from any detected browser.
 *
 * @version 1.0.0
 */

#ifndef BROWSER_DETECT_H
#define BROWSER_DETECT_H

#include "history_db.h"
#include <stddef.h>

/* ── Limits ──────────────────────────────────────────────────────── */

#define BROWSER_MAX_DETECTED      16
#define BROWSER_MAX_PROFILES      16
#define BROWSER_MAX_PATH         512
#define BROWSER_NAME_LEN          64

/* ── Browser types ───────────────────────────────────────────────── */

typedef enum {
    BROWSER_UNKNOWN = 0,
    BROWSER_BRAVE,
    BROWSER_CHROME,
    BROWSER_CHROMIUM,
    BROWSER_EDGE,
    BROWSER_VIVALDI,
    BROWSER_OPERA,
    BROWSER_FIREFOX,
    BROWSER_TYPE_COUNT
} BrowserType;

/* ── Timestamp formats ───────────────────────────────────────────── */

typedef enum {
    TS_FORMAT_CHROMIUM = 0,     /* microseconds since 1601-01-01 */
    TS_FORMAT_FIREFOX,          /* microseconds since Unix epoch */
    TS_FORMAT_UNIX_SECONDS,     /* seconds since Unix epoch */
    TS_FORMAT_UNIX_MS           /* milliseconds since Unix epoch */
} TimestampFormat;

/* ── Profile info ────────────────────────────────────────────────── */

typedef struct {
    char name[BROWSER_NAME_LEN];        /* "Default", "Profile 1", etc. */
    char path[BROWSER_MAX_PATH];        /* full path to profile directory */
    char history_db[BROWSER_MAX_PATH];  /* full path to History DB file */
    int  is_valid;                      /* DB file exists and is readable */
} BrowserProfile;

/* ── Detected browser ────────────────────────────────────────────── */

typedef struct {
    BrowserType     type;
    char            name[BROWSER_NAME_LEN];     /* "Brave", "Chrome", etc. */
    char            base_path[BROWSER_MAX_PATH]; /* config directory */
    TimestampFormat ts_format;
    BrowserProfile  profiles[BROWSER_MAX_PROFILES];
    int             profile_count;
    int             is_chromium_based;           /* uses Chromium schema */
} DetectedBrowser;

/* ── Browser backend (schema abstraction) ────────────────────────── */

typedef struct BrowserBackend {
    BrowserType type;
    const char *name;

    /** Query history entries from this browser's DB */
    int (*query_history)(const char *db_path, int days_back,
                         HistoryResult *result, TimestampFormat ts_fmt);

    /** Query bookmarks from this browser's DB (optional) */
    int (*query_bookmarks)(const char *db_path, HistoryResult *result);

    /** Query downloads from this browser's DB (optional) */
    int (*query_downloads)(const char *db_path, HistoryResult *result);

    /** Get the SQL query string for history (for debugging) */
    const char *(*get_history_sql)(int days_back);

    /** Normalize a browser-specific timestamp to Unix seconds */
    long long (*normalize_timestamp)(long long raw_ts);
} BrowserBackend;

/* ── Discovery results ───────────────────────────────────────────── */

typedef struct {
    DetectedBrowser browsers[BROWSER_MAX_DETECTED];
    int             count;
    int             total_profiles;
} BrowserScanResult;

/* ── API ─────────────────────────────────────────────────────────── */

/**
 * Scan the system for installed browsers.
 *
 * @param result  Output: discovered browsers and profiles
 * @return Number of browsers found, or -1 on error
 */
int browser_scan(BrowserScanResult *result);

/**
 * Get the backend implementation for a browser type.
 *
 * @param type  Browser type
 * @return Pointer to backend (static lifetime), or NULL if unsupported
 */
const BrowserBackend *browser_get_backend(BrowserType type);

/**
 * Query history from all detected browsers and merge results.
 *
 * @param scan       Browser scan results
 * @param days_back  Number of days of history (0 = all)
 * @param result     Output: merged history entries
 * @return Total entries found, or -1 on error
 */
int browser_query_all(const BrowserScanResult *scan, int days_back,
                      HistoryResult *result);

/**
 * Query history from a specific browser type.
 *
 * @param type       Browser type to query
 * @param days_back  Number of days
 * @param result     Output: history entries
 * @return Number of entries, or -1 on error
 */
int browser_query_by_type(BrowserType type, int days_back,
                          HistoryResult *result);

/**
 * Normalize a raw timestamp from any browser to Unix seconds.
 *
 * @param raw_ts    Raw timestamp value
 * @param format    Timestamp format
 * @return Unix timestamp in seconds
 */
long long browser_normalize_timestamp(long long raw_ts, TimestampFormat format);

/**
 * Convert a browser type enum to its display name.
 */
const char *browser_type_name(BrowserType type);

/**
 * Get the default config directory for a browser.
 * Platform-aware (Linux, macOS, Windows).
 *
 * @param type  Browser type
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return 0 on success, -1 on error
 */
int browser_default_config_dir(BrowserType type, char *buf, size_t size);

/**
 * Deduplicate entries in a HistoryResult by URL.
 * For duplicates, keeps the entry with the highest visit count
 * and the latest timestamp.
 *
 * @param result  The result set to deduplicate (modified in-place)
 * @return Number of duplicates removed
 */
int browser_deduplicate(HistoryResult *result);

#endif /* BROWSER_DETECT_H */
