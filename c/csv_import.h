/**
 * csv_import.h — CSV/TSV browser history importer
 *
 * Imports browser history from CSV exports (Chrome, Firefox, etc.).
 * Handles RFC 4180 quoted fields, auto-detects delimiters and date formats.
 *
 * @version 1.0.0
 */

#ifndef CSV_IMPORT_H
#define CSV_IMPORT_H

#include "history_db.h"
#include <stddef.h>

/* ── Limits ──────────────────────────────────────────────────────── */

#define CSV_MAX_FIELD_LEN   4096
#define CSV_MAX_COLUMNS       32
#define CSV_MAX_LINE_LEN   16384

/* ── Delimiter types ─────────────────────────────────────────────── */

typedef enum {
    CSV_DELIM_AUTO  = 0,    /* auto-detect */
    CSV_DELIM_COMMA = ',',
    CSV_DELIM_TAB   = '\t',
    CSV_DELIM_SEMI  = ';',
    CSV_DELIM_PIPE  = '|'
} CsvDelimiter;

/* ── Date format types ───────────────────────────────────────────── */

typedef enum {
    CSV_DATE_AUTO = 0,
    CSV_DATE_ISO8601,       /* 2026-04-27T13:38:10Z */
    CSV_DATE_US,            /* 04/27/2026 1:38:10 PM */
    CSV_DATE_EU,            /* 27/04/2026 13:38:10 */
    CSV_DATE_UNIX,          /* 1745757490 (seconds since epoch) */
    CSV_DATE_UNIX_MS,       /* 1745757490000 (milliseconds) */
    CSV_DATE_CHROMIUM       /* 13380123456789000 (microseconds since 1601) */
} CsvDateFormat;

/* ── Column mapping ──────────────────────────────────────────────── */

typedef struct {
    int url_col;            /* column index for URL (-1 = not found) */
    int title_col;          /* column index for title */
    int visit_count_col;    /* column index for visit count */
    int last_visit_col;     /* column index for last visit time */
    int typed_count_col;    /* column index for typed count */
} CsvColumnMap;

/* ── Import options ──────────────────────────────────────────────── */

typedef struct {
    CsvDelimiter  delimiter;        /* auto-detect if CSV_DELIM_AUTO */
    CsvDateFormat date_format;      /* auto-detect if CSV_DATE_AUTO */
    int           has_header;       /* 1 = first row is header, -1 = auto */
    int           skip_rows;        /* number of initial rows to skip */
    const char   *encoding;         /* NULL = auto-detect (UTF-8 default) */
} CsvImportOptions;

/* ── Parsed CSV field ────────────────────────────────────────────── */

typedef struct {
    char   value[CSV_MAX_FIELD_LEN];
    size_t length;
    int    was_quoted;      /* field was enclosed in quotes */
} CsvField;

/* ── CSV row ─────────────────────────────────────────────────────── */

typedef struct {
    CsvField fields[CSV_MAX_COLUMNS];
    int      field_count;
} CsvRow;

/* ── API ─────────────────────────────────────────────────────────── */

/**
 * Import history entries from a CSV/TSV file.
 *
 * @param filepath  Path to the CSV file
 * @param result    Output: history entries appended here
 * @param options   Import options (NULL = all auto-detect)
 * @return Number of entries imported, or -1 on error
 */
int import_csv(const char *filepath, HistoryResult *result,
               const CsvImportOptions *options);

/**
 * Import history from CSV data in memory.
 *
 * @param data      CSV data buffer
 * @param data_len  Length of data buffer
 * @param result    Output: history entries appended here
 * @param options   Import options (NULL = all auto-detect)
 * @return Number of entries imported, or -1 on error
 */
int import_csv_buffer(const char *data, size_t data_len,
                      HistoryResult *result, const CsvImportOptions *options);

/**
 * Auto-detect the delimiter used in CSV data.
 *
 * @param data      First few KB of CSV data
 * @param data_len  Length of data
 * @return Detected delimiter character
 */
char csv_detect_delimiter(const char *data, size_t data_len);

/**
 * Auto-detect the date format used in a date string.
 *
 * @param date_str  A sample date string
 * @return Detected date format
 */
CsvDateFormat csv_detect_date_format(const char *date_str);

/**
 * Parse a date string into ISO 8601 format.
 *
 * @param src       Input date string
 * @param fmt       Date format (or CSV_DATE_AUTO to auto-detect)
 * @param dst       Output buffer for ISO 8601 string
 * @param dstsize   Size of output buffer
 * @return 0 on success, -1 on parse error
 */
int csv_parse_date(const char *src, CsvDateFormat fmt,
                   char *dst, size_t dstsize);

/**
 * Parse a single CSV row from a data buffer.
 *
 * @param data      Pointer to current position in buffer
 * @param data_end  End of buffer
 * @param delim     Delimiter character
 * @param row       Output: parsed row
 * @return Pointer past the consumed row, or NULL on error
 */
const char *csv_parse_row(const char *data, const char *data_end,
                          char delim, CsvRow *row);

/**
 * Detect if the data starts with a UTF-8 BOM and skip it.
 *
 * @param data      Pointer to data (updated past BOM if found)
 * @param data_len  Length of data (updated)
 * @return 1 if BOM was found and skipped, 0 otherwise
 */
int csv_skip_bom(const char **data, size_t *data_len);

#endif /* CSV_IMPORT_H */
