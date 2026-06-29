/**
 * html_bookmark_import.h — Netscape Bookmark File Format parser
 *
 * Parses HTML bookmark exports from Chrome, Firefox, Safari, and Edge.
 * The NBFF is the universal bookmark/history export format.
 *
 * @version 1.0.0
 */

#ifndef HTML_BOOKMARK_IMPORT_H
#define HTML_BOOKMARK_IMPORT_H

#include "history_db.h"
#include <stddef.h>

/* ── Limits ──────────────────────────────────────────────────────── */

#define BOOKMARK_MAX_DEPTH       32
#define BOOKMARK_MAX_ATTR_NAME   64
#define BOOKMARK_MAX_ATTR_VALUE 2048

/* ── Bookmark entry ──────────────────────────────────────────────── */

typedef struct {
    char url[2048];
    char title[512];
    char add_date[64];           /* ISO 8601 */
    char last_visit[64];         /* ISO 8601 */
    char last_modified[64];      /* ISO 8601 */
    char icon_uri[2048];
    char folder_path[1024];      /* e.g., "Bookmarks Bar/Dev/Tools" */
    int  depth;
} BookmarkEntry;

/* ── Import options ──────────────────────────────────────────────── */

typedef struct {
    int max_depth;               /* 0 = unlimited (up to BOOKMARK_MAX_DEPTH) */
    int import_folders;          /* 1 = include folder hierarchy in title */
    int timestamp_format;        /* 0 = auto, 1 = Unix seconds, 2 = Chromium us */
} BookmarkImportOptions;

/* ── API ─────────────────────────────────────────────────────────── */

/**
 * Import history entries from a Netscape Bookmark HTML file.
 *
 * @param filepath  Path to the HTML file
 * @param result    Output: history entries appended here
 * @param options   Import options (NULL = defaults)
 * @return Number of entries imported, or -1 on error
 */
int import_bookmarks_html(const char *filepath, HistoryResult *result,
                          const BookmarkImportOptions *options);

/**
 * Import from an in-memory HTML buffer.
 *
 * @param data      HTML data buffer
 * @param data_len  Length of data
 * @param result    Output: history entries appended here
 * @param options   Import options (NULL = defaults)
 * @return Number of entries imported, or -1 on error
 */
int import_bookmarks_html_buffer(const char *data, size_t data_len,
                                  HistoryResult *result,
                                  const BookmarkImportOptions *options);

/**
 * Decode HTML entities in a string (in-place).
 * Handles: &amp; &lt; &gt; &quot; &#39; &#NNN; &#xHHHH;
 *
 * @param str  String to decode (modified in-place)
 * @return 0 on success
 */
int html_decode_entities(char *str);

#endif /* HTML_BOOKMARK_IMPORT_H */
