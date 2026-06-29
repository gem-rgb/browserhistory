/**
 * csv_import.c — CSV/TSV browser history importer
 *
 * RFC 4180 compliant CSV parser with extensions for browser history import.
 * Handles quoted fields (with embedded delimiters, newlines, and escaped
 * quotes), auto-detects delimiter type, date formats, and character encoding.
 *
 * The parser operates in two phases:
 *   1. Header analysis: detect delimiter, identify column mapping
 *   2. Row-by-row extraction: parse each row and convert to HistoryEntry
 *
 * Supported input formats:
 *   - Chrome CSV export (url, title, visit_count, typed_count, last_visit_time)
 *   - Firefox places dump (url, title, rev_host, visit_count, last_visit_date)
 *   - Generic CSV with at least a URL column
 *
 * @version 1.0.0
 */

#include "csv_import.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* ── BOM detection ───────────────────────────────────────────────── */

int csv_skip_bom(const char **data, size_t *data_len) {
    if (!data || !*data || !data_len || *data_len < 3) return 0;

    const unsigned char *p = (const unsigned char *)*data;

    /* UTF-8 BOM: EF BB BF */
    if (p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        *data += 3;
        *data_len -= 3;
        return 1;
    }

    return 0;
}

/* ── Delimiter detection ─────────────────────────────────────────── */

/**
 * Score a candidate delimiter by counting occurrences in the first
 * few lines and checking consistency. A good delimiter appears the
 * same number of times in each line.
 */
static int score_delimiter(const char *data, size_t data_len, char delim) {
    int line_counts[16];
    int num_lines = 0;
    int current_count = 0;
    int in_quotes = 0;

    for (size_t i = 0; i < data_len && num_lines < 16; i++) {
        char c = data[i];

        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (!in_quotes) {
            if (c == delim) {
                current_count++;
            } else if (c == '\n') {
                if (current_count > 0 && num_lines < 16) {
                    line_counts[num_lines++] = current_count;
                }
                current_count = 0;
            }
        }
    }
    /* Handle last line without trailing newline */
    if (current_count > 0 && num_lines < 16) {
        line_counts[num_lines++] = current_count;
    }

    if (num_lines < 2) return 0;

    /* Score = number of consistent lines × count per line */
    int consistent = 0;
    int baseline = line_counts[0];
    if (baseline == 0) return 0;

    for (int i = 1; i < num_lines; i++) {
        if (line_counts[i] == baseline) consistent++;
    }

    return consistent * baseline;
}

char csv_detect_delimiter(const char *data, size_t data_len) {
    if (!data || data_len == 0) return ',';

    /* Limit analysis to first 8KB */
    if (data_len > 8192) data_len = 8192;

    char candidates[] = {',', '\t', ';', '|'};
    int best_score = 0;
    char best_delim = ',';

    for (int i = 0; i < 4; i++) {
        int score = score_delimiter(data, data_len, candidates[i]);
        if (score > best_score) {
            best_score = score;
            best_delim = candidates[i];
        }
    }

    return best_delim;
}

/* ── Date format detection and parsing ───────────────────────────── */

CsvDateFormat csv_detect_date_format(const char *date_str) {
    if (!date_str || !date_str[0]) return CSV_DATE_AUTO;

    size_t len = strlen(date_str);

    /* Check for ISO 8601: "2026-04-27T13:38:10Z" or "2026-04-27 13:38:10" */
    if (len >= 10 && date_str[4] == '-' && date_str[7] == '-') {
        return CSV_DATE_ISO8601;
    }

    /* Check for pure numeric (Unix timestamp) */
    int all_digits = 1;
    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)date_str[i])) {
            all_digits = 0;
            break;
        }
    }

    if (all_digits && len > 0) {
        if (len >= 16) return CSV_DATE_CHROMIUM;   /* 17+ digits = Chromium microseconds */
        if (len >= 13) return CSV_DATE_UNIX_MS;     /* 13 digits = Unix milliseconds */
        if (len >= 9)  return CSV_DATE_UNIX;        /* 9-10 digits = Unix seconds */
    }

    /* Check for US format: MM/DD/YYYY */
    if (len >= 8 && date_str[2] == '/') {
        int month = atoi(date_str);
        int day = atoi(date_str + 3);
        if (month >= 1 && month <= 12 && day >= 1 && day <= 31) {
            return CSV_DATE_US;
        }
    }

    /* Check for EU format: DD/MM/YYYY or DD.MM.YYYY */
    if (len >= 8 && (date_str[2] == '/' || date_str[2] == '.')) {
        int day = atoi(date_str);
        int month = atoi(date_str + 3);
        if (day >= 1 && day <= 31 && month >= 1 && month <= 12) {
            return CSV_DATE_EU;
        }
    }

    return CSV_DATE_AUTO;
}

int csv_parse_date(const char *src, CsvDateFormat fmt, char *dst, size_t dstsize) {
    if (!src || !dst || dstsize < 21) return -1;

    if (fmt == CSV_DATE_AUTO) {
        fmt = csv_detect_date_format(src);
        if (fmt == CSV_DATE_AUTO) return -1;
    }

    switch (fmt) {
        case CSV_DATE_ISO8601: {
            /* Already in the right format, just normalize */
            size_t slen = strlen(src);
            if (slen >= dstsize) slen = dstsize - 1;
            memcpy(dst, src, slen);
            dst[slen] = '\0';
            /* Replace space with 'T' if needed */
            char *space = strchr(dst, ' ');
            if (space && (space - dst) == 10) *space = 'T';
            /* Ensure trailing Z if missing timezone */
            slen = strlen(dst);
            if (slen >= 19 && dst[slen-1] != 'Z' && dst[slen-1] != '+' &&
                !strchr(dst + 19, '+') && !strchr(dst + 19, '-')) {
                if (slen + 1 < dstsize) {
                    dst[slen] = 'Z';
                    dst[slen + 1] = '\0';
                }
            }
            return 0;
        }

        case CSV_DATE_UNIX: {
            long long ts = 0;
            for (const char *p = src; *p && isdigit((unsigned char)*p); p++) {
                ts = ts * 10 + (*p - '0');
            }
            time_t t = (time_t)ts;
            struct tm *tm = gmtime(&t);
            if (!tm) return -1;
            strftime(dst, dstsize, "%Y-%m-%dT%H:%M:%SZ", tm);
            return 0;
        }

        case CSV_DATE_UNIX_MS: {
            long long ts = 0;
            for (const char *p = src; *p && isdigit((unsigned char)*p); p++) {
                ts = ts * 10 + (*p - '0');
            }
            time_t t = (time_t)(ts / 1000);
            struct tm *tm = gmtime(&t);
            if (!tm) return -1;
            strftime(dst, dstsize, "%Y-%m-%dT%H:%M:%SZ", tm);
            return 0;
        }

        case CSV_DATE_CHROMIUM: {
            long long ts = 0;
            for (const char *p = src; *p && isdigit((unsigned char)*p); p++) {
                ts = ts * 10 + (*p - '0');
            }
            /* Convert Chromium microseconds to Unix seconds */
            time_t t = (time_t)(ts / 1000000LL - 11644473600LL);
            struct tm *tm = gmtime(&t);
            if (!tm) return -1;
            strftime(dst, dstsize, "%Y-%m-%dT%H:%M:%SZ", tm);
            return 0;
        }

        case CSV_DATE_US: {
            /* MM/DD/YYYY HH:MM:SS [AM/PM] */
            int month = 0, day = 0, year = 0;
            int hour = 0, minute = 0, second = 0;
            char ampm[4] = "";

            int n = sscanf(src, "%d/%d/%d %d:%d:%d %3s",
                          &month, &day, &year, &hour, &minute, &second, ampm);
            if (n < 3) return -1;

            /* Handle AM/PM */
            if (ampm[0] == 'P' || ampm[0] == 'p') {
                if (hour != 12) hour += 12;
            } else if (ampm[0] == 'A' || ampm[0] == 'a') {
                if (hour == 12) hour = 0;
            }

            /* Handle 2-digit year */
            if (year < 100) year += 2000;

            snprintf(dst, dstsize, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                    year, month, day, hour, minute, second);
            return 0;
        }

        case CSV_DATE_EU: {
            /* DD/MM/YYYY HH:MM:SS or DD.MM.YYYY HH:MM:SS */
            int day = 0, month = 0, year = 0;
            int hour = 0, minute = 0, second = 0;

            /* Try both / and . separators */
            int n = sscanf(src, "%d/%d/%d %d:%d:%d",
                          &day, &month, &year, &hour, &minute, &second);
            if (n < 3) {
                n = sscanf(src, "%d.%d.%d %d:%d:%d",
                          &day, &month, &year, &hour, &minute, &second);
            }
            if (n < 3) return -1;

            if (year < 100) year += 2000;

            snprintf(dst, dstsize, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                    year, month, day, hour, minute, second);
            return 0;
        }

        default:
            return -1;
    }
}

/* ── Row parsing ─────────────────────────────────────────────────── */

const char *csv_parse_row(const char *data, const char *data_end,
                          char delim, CsvRow *row) {
    if (!data || !row || data >= data_end) return NULL;

    memset(row, 0, sizeof(*row));

    const char *p = data;
    int field_idx = 0;

    while (p < data_end && field_idx < CSV_MAX_COLUMNS) {
        CsvField *field = &row->fields[field_idx];
        size_t vi = 0;

        if (*p == '"') {
            /* Quoted field — RFC 4180 rules:
             *   - Field starts after opening quote
             *   - Double-quote ("") inside is an escaped quote
             *   - Field ends at closing quote followed by delimiter or EOL
             */
            field->was_quoted = 1;
            p++;  /* skip opening quote */

            while (p < data_end && vi < CSV_MAX_FIELD_LEN - 1) {
                if (*p == '"') {
                    if (p + 1 < data_end && *(p + 1) == '"') {
                        /* Escaped quote — emit single quote */
                        field->value[vi++] = '"';
                        p += 2;
                    } else {
                        /* Closing quote */
                        p++;
                        break;
                    }
                } else {
                    field->value[vi++] = *p++;
                }
            }

            /* Skip to next delimiter or end of line */
            while (p < data_end && *p != delim && *p != '\n' && *p != '\r') {
                p++;
            }
        } else {
            /* Unquoted field — ends at delimiter or EOL */
            while (p < data_end && *p != delim && *p != '\n' && *p != '\r' &&
                   vi < CSV_MAX_FIELD_LEN - 1) {
                field->value[vi++] = *p++;
            }
        }

        field->value[vi] = '\0';
        field->length = vi;

        /* Trim trailing whitespace from unquoted fields */
        if (!field->was_quoted) {
            while (field->length > 0 &&
                   isspace((unsigned char)field->value[field->length - 1])) {
                field->value[--field->length] = '\0';
            }
        }

        field_idx++;

        /* Advance past delimiter */
        if (p < data_end && *p == delim) {
            p++;
        } else {
            break;
        }
    }

    row->field_count = field_idx;

    /* Skip end-of-line */
    if (p < data_end && *p == '\r') p++;
    if (p < data_end && *p == '\n') p++;

    return p;
}

/* ── Column mapping ──────────────────────────────────────────────── */

/**
 * Known column name variants for URL field.
 */
static const char *URL_NAMES[] = {
    "url", "URL", "Url", "uri", "URI", "address", "Address",
    "link", "Link", "page_url", "page", NULL
};

static const char *TITLE_NAMES[] = {
    "title", "Title", "TITLE", "name", "Name", "page_title",
    "description", "Description", NULL
};

static const char *VISIT_COUNT_NAMES[] = {
    "visit_count", "visits", "Visits", "visit_count", "count",
    "hit_count", "frequency", NULL
};

static const char *LAST_VISIT_NAMES[] = {
    "last_visit_time", "last_visit_date", "last_visited", "date",
    "Date", "timestamp", "Timestamp", "time", "Time",
    "last_visit", "visited", "visit_date", NULL
};

static const char *TYPED_COUNT_NAMES[] = {
    "typed_count", "typed", "Typed", NULL
};

static int match_column_name(const char *field, const char **names) {
    /* Trim leading whitespace */
    while (*field && isspace((unsigned char)*field)) field++;

    for (int i = 0; names[i]; i++) {
        if (strcasecmp(field, names[i]) == 0) return 1;

        /* Also try with quotes stripped */
        size_t flen = strlen(field);
        if (flen >= 2 && field[0] == '"' && field[flen-1] == '"') {
            char stripped[256];
            size_t slen = flen - 2;
            if (slen >= sizeof(stripped)) slen = sizeof(stripped) - 1;
            memcpy(stripped, field + 1, slen);
            stripped[slen] = '\0';
            if (strcasecmp(stripped, names[i]) == 0) return 1;
        }
    }
    return 0;
}

static CsvColumnMap detect_columns(const CsvRow *header_row) {
    CsvColumnMap map = { -1, -1, -1, -1, -1 };

    for (int i = 0; i < header_row->field_count; i++) {
        const char *name = header_row->fields[i].value;

        if (map.url_col < 0 && match_column_name(name, URL_NAMES))
            map.url_col = i;
        else if (map.title_col < 0 && match_column_name(name, TITLE_NAMES))
            map.title_col = i;
        else if (map.visit_count_col < 0 && match_column_name(name, VISIT_COUNT_NAMES))
            map.visit_count_col = i;
        else if (map.last_visit_col < 0 && match_column_name(name, LAST_VISIT_NAMES))
            map.last_visit_col = i;
        else if (map.typed_count_col < 0 && match_column_name(name, TYPED_COUNT_NAMES))
            map.typed_count_col = i;
    }

    return map;
}

/* ── Header auto-detection ───────────────────────────────────────── */

/**
 * Heuristic: if the first row contains known header names, it's a header.
 * If it looks like data (contains "http" or long numeric strings), it's not.
 */
static int detect_has_header(const CsvRow *first_row) {
    /* Check for known header names */
    for (int i = 0; i < first_row->field_count; i++) {
        const char *val = first_row->fields[i].value;
        if (match_column_name(val, URL_NAMES) ||
            match_column_name(val, TITLE_NAMES) ||
            match_column_name(val, VISIT_COUNT_NAMES) ||
            match_column_name(val, LAST_VISIT_NAMES)) {
            return 1;
        }
    }

    /* Check if first field looks like a URL */
    if (first_row->field_count > 0) {
        const char *val = first_row->fields[0].value;
        if (strstr(val, "http://") || strstr(val, "https://") ||
            strstr(val, "ftp://")) {
            return 0;  /* Data, not header */
        }
    }

    return -1;  /* Can't tell */
}

/* ── Result growing ──────────────────────────────────────────────── */

static int ensure_capacity(HistoryResult *result) {
    if (result->count < result->capacity) return 0;

    int new_cap = result->capacity * 2;
    if (new_cap < 256) new_cap = 256;

    HistoryEntry *tmp = realloc(result->entries,
                                (size_t)new_cap * sizeof(HistoryEntry));
    if (!tmp) return -1;

    result->entries = tmp;
    result->capacity = new_cap;
    return 0;
}

/* ── Encoding conversion helpers ─────────────────────────────────── */

/**
 * Simple Latin-1 to UTF-8 conversion for fields that aren't valid UTF-8.
 * Converts in-place (UTF-8 may be up to 2x longer, so dst must be large enough).
 */
static void latin1_to_utf8(char *dst, size_t dstsize, const char *src) {
    size_t j = 0;

    for (size_t i = 0; src[i] && j < dstsize - 2; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c < 0x80) {
            dst[j++] = (char)c;
        } else {
            /* Latin-1 byte → 2-byte UTF-8 */
            dst[j++] = (char)(0xC0 | (c >> 6));
            dst[j++] = (char)(0x80 | (c & 0x3F));
        }
    }
    dst[j] = '\0';
}

/**
 * Validate and potentially fix encoding of a field.
 * If it's valid UTF-8, leave it. Otherwise, assume Latin-1 and convert.
 */
static void fix_field_encoding(char *field, size_t field_size) {
    /* Quick check: is it already valid UTF-8? */
    const unsigned char *p = (const unsigned char *)field;
    int needs_conversion = 0;

    while (*p) {
        if (*p < 0x80) {
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            if ((p[1] & 0xC0) != 0x80) { needs_conversion = 1; break; }
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) {
                needs_conversion = 1; break;
            }
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 ||
                (p[3] & 0xC0) != 0x80) {
                needs_conversion = 1; break;
            }
            p += 4;
        } else {
            needs_conversion = 1;
            break;
        }
    }

    if (needs_conversion) {
        char temp[CSV_MAX_FIELD_LEN * 2];
        latin1_to_utf8(temp, sizeof(temp), field);
        strncpy(field, temp, field_size - 1);
        field[field_size - 1] = '\0';
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

int import_csv_buffer(const char *data, size_t data_len,
                      HistoryResult *result, const CsvImportOptions *options) {
    if (!data || data_len == 0 || !result) return -1;

    /* Default options */
    CsvImportOptions opts;
    if (options) {
        opts = *options;
    } else {
        memset(&opts, 0, sizeof(opts));
        opts.has_header = -1;  /* auto-detect */
    }

    const char *p = data;
    size_t remaining = data_len;

    /* Skip BOM */
    csv_skip_bom(&p, &remaining);

    const char *end = p + remaining;

    /* Detect delimiter */
    char delim;
    if (opts.delimiter == CSV_DELIM_AUTO) {
        delim = csv_detect_delimiter(p, remaining);
    } else {
        delim = (char)opts.delimiter;
    }

    /* Parse first row to detect header / column mapping */
    CsvRow first_row;
    const char *after_first = csv_parse_row(p, end, delim, &first_row);
    if (!after_first) return -1;

    int has_header;
    CsvColumnMap col_map;

    if (opts.has_header == 1) {
        has_header = 1;
    } else if (opts.has_header == 0) {
        has_header = 0;
    } else {
        has_header = detect_has_header(&first_row);
        if (has_header < 0) has_header = 1;  /* default to header */
    }

    if (has_header) {
        col_map = detect_columns(&first_row);
        p = after_first;
    } else {
        /* No header — use positional mapping (URL first, then title, etc.) */
        col_map.url_col = 0;
        col_map.title_col = (first_row.field_count > 1) ? 1 : -1;
        col_map.visit_count_col = (first_row.field_count > 2) ? 2 : -1;
        col_map.last_visit_col = (first_row.field_count > 3) ? 3 : -1;
        col_map.typed_count_col = -1;
        /* Don't skip first row — it's data */
        p = data;
        csv_skip_bom(&p, &remaining);
    }

    /* Must have at least a URL column */
    if (col_map.url_col < 0) {
        fprintf(stderr, "[csv_import] No URL column found\n");
        return -1;
    }

    /* Skip requested rows */
    for (int i = 0; i < opts.skip_rows && p < end; i++) {
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }

    /* Detect date format from first data row if needed */
    CsvDateFormat date_fmt = opts.date_format;

    /* Parse data rows */
    int imported = 0;
    CsvRow row;

    while (p < end) {
        p = csv_parse_row(p, end, delim, &row);
        if (!p || row.field_count == 0) break;

        /* Skip empty rows */
        if (row.field_count == 1 && row.fields[0].length == 0) continue;

        /* Ensure URL field exists */
        if (col_map.url_col >= row.field_count) continue;
        if (row.fields[col_map.url_col].length == 0) continue;

        /* Grow result array if needed */
        if (ensure_capacity(result) != 0) break;

        HistoryEntry *e = &result->entries[result->count];
        memset(e, 0, sizeof(*e));

        /* Extract URL */
        strncpy(e->url, row.fields[col_map.url_col].value, sizeof(e->url) - 1);
        e->url[sizeof(e->url) - 1] = '\0';
        fix_field_encoding(e->url, sizeof(e->url));

        /* Validate it looks like a URL */
        if (!strstr(e->url, "://") && strncmp(e->url, "about:", 6) != 0 &&
            strncmp(e->url, "file:", 5) != 0 && strncmp(e->url, "data:", 5) != 0) {
            continue;  /* Skip non-URL rows */
        }

        /* Extract title */
        if (col_map.title_col >= 0 && col_map.title_col < row.field_count) {
            strncpy(e->title, row.fields[col_map.title_col].value,
                    sizeof(e->title) - 1);
            e->title[sizeof(e->title) - 1] = '\0';
            fix_field_encoding(e->title, sizeof(e->title));
        }

        /* Extract visit count */
        if (col_map.visit_count_col >= 0 &&
            col_map.visit_count_col < row.field_count) {
            e->visit_count = atoi(row.fields[col_map.visit_count_col].value);
        }
        if (e->visit_count <= 0) e->visit_count = 1;

        /* Extract typed count */
        if (col_map.typed_count_col >= 0 &&
            col_map.typed_count_col < row.field_count) {
            e->typed_count = atoi(row.fields[col_map.typed_count_col].value);
        }

        /* Extract and parse last visit time */
        if (col_map.last_visit_col >= 0 &&
            col_map.last_visit_col < row.field_count) {
            const char *date_str = row.fields[col_map.last_visit_col].value;

            /* Auto-detect date format on first encounter */
            if (date_fmt == CSV_DATE_AUTO && date_str[0]) {
                date_fmt = csv_detect_date_format(date_str);
            }

            if (csv_parse_date(date_str, date_fmt,
                              e->last_visit_time, sizeof(e->last_visit_time)) != 0) {
                /* Failed to parse — copy raw */
                strncpy(e->last_visit_time, date_str, sizeof(e->last_visit_time) - 1);
                e->last_visit_time[sizeof(e->last_visit_time) - 1] = '\0';
            }
        }

        e->last_visit_raw = 0;
        result->count++;
        imported++;
    }

    printf("[csv_import] Imported %d entries from CSV\n", imported);
    return imported;
}

int import_csv(const char *filepath, HistoryResult *result,
               const CsvImportOptions *options) {
    if (!filepath || !result) return -1;

    /* Read entire file */
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "[csv_import] Cannot open %s\n", filepath);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 100 * 1024 * 1024) {
        fprintf(stderr, "[csv_import] File too large or empty: %ld bytes\n", fsize);
        fclose(f);
        return -1;
    }

    char *data = malloc((size_t)fsize + 1);
    if (!data) {
        fprintf(stderr, "[csv_import] Out of memory\n");
        fclose(f);
        return -1;
    }

    size_t read_bytes = fread(data, 1, (size_t)fsize, f);
    fclose(f);
    data[read_bytes] = '\0';

    int result_count = import_csv_buffer(data, read_bytes, result, options);

    free(data);
    return result_count;
}

/* ── Platform-specific case-insensitive compare ──────────────────── */

#ifdef _WIN32
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#endif
