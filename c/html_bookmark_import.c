/**
 * html_bookmark_import.c — Netscape Bookmark File Format parser
 *
 * Parses the universal bookmark/history HTML export format used by
 * Chrome, Firefox, Safari, Edge, and most other browsers.
 *
 * The format uses nested <DL> (definition list) elements to represent
 * folder hierarchy, with <DT><A HREF="..." ADD_DATE="..." ...>Title</A>
 * elements for individual bookmarks.
 *
 * Multi-pass approach:
 *   1. Entity decoding and normalization
 *   2. Tag-by-tag parsing with depth tracking
 *   3. Attribute extraction and timestamp conversion
 *
 * @version 1.0.0
 */

#include "html_bookmark_import.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* ── HTML Entity Decoding ────────────────────────────────────────── */

/** Named entity lookup table */
typedef struct {
    const char *name;
    const char *replacement;
} HtmlEntity;

static const HtmlEntity NAMED_ENTITIES[] = {
    {"amp",    "&"},
    {"lt",     "<"},
    {"gt",     ">"},
    {"quot",   "\""},
    {"apos",   "'"},
    {"nbsp",   " "},
    {"ndash",  "-"},
    {"mdash",  "--"},
    {"lsquo",  "'"},
    {"rsquo",  "'"},
    {"ldquo",  "\""},
    {"rdquo",  "\""},
    {"bull",   "*"},
    {"hellip", "..."},
    {"copy",   "(c)"},
    {"reg",    "(R)"},
    {"trade",  "(TM)"},
    {"laquo",  "<<"},
    {"raquo",  ">>"},
    {"deg",    "deg"},
    {"plusmn", "+/-"},
    {"micro",  "u"},
    {"para",   "P"},
    {"middot", "."},
    {"frac12", "1/2"},
    {"frac14", "1/4"},
    {"frac34", "3/4"},
    {"times",  "x"},
    {"divide", "/"},
    {NULL, NULL}
};

/**
 * Encode a Unicode code point as UTF-8 into the destination buffer.
 * Returns the number of bytes written (1-4), or 0 on error.
 */
static int utf8_encode(unsigned int cp, char *dst, size_t remaining) {
    if (cp < 0x80) {
        if (remaining < 1) return 0;
        dst[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        if (remaining < 2) return 0;
        dst[0] = (char)(0xC0 | (cp >> 6));
        dst[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        if (remaining < 3) return 0;
        /* Reject surrogates */
        if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
        dst[0] = (char)(0xE0 | (cp >> 12));
        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        if (remaining < 4) return 0;
        dst[0] = (char)(0xF0 | (cp >> 18));
        dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

int html_decode_entities(char *str) {
    if (!str) return 0;

    char *src = str;
    char *dst = str;

    while (*src) {
        if (*src != '&') {
            *dst++ = *src++;
            continue;
        }

        /* Found '&' — try to decode entity */
        const char *entity_start = src + 1;
        const char *semicolon = NULL;

        /* Look for closing ';' within reasonable distance */
        for (const char *scan = entity_start;
             scan < entity_start + 12 && *scan; scan++) {
            if (*scan == ';') {
                semicolon = scan;
                break;
            }
        }

        if (!semicolon) {
            /* No semicolon found — not an entity, copy '&' literally */
            *dst++ = *src++;
            continue;
        }

        size_t entity_len = (size_t)(semicolon - entity_start);
        char entity_name[16];
        if (entity_len >= sizeof(entity_name)) {
            /* Entity too long — skip */
            *dst++ = *src++;
            continue;
        }
        memcpy(entity_name, entity_start, entity_len);
        entity_name[entity_len] = '\0';

        int decoded = 0;

        if (entity_name[0] == '#') {
            /* Numeric entity */
            unsigned int codepoint = 0;

            if (entity_name[1] == 'x' || entity_name[1] == 'X') {
                /* Hex: &#xHHHH; */
                for (size_t i = 2; i < entity_len; i++) {
                    char c = entity_name[i];
                    if (c >= '0' && c <= '9') codepoint = codepoint * 16 + (unsigned)(c - '0');
                    else if (c >= 'a' && c <= 'f') codepoint = codepoint * 16 + 10 + (unsigned)(c - 'a');
                    else if (c >= 'A' && c <= 'F') codepoint = codepoint * 16 + 10 + (unsigned)(c - 'A');
                    else break;
                }
            } else {
                /* Decimal: &#NNN; */
                for (size_t i = 1; i < entity_len; i++) {
                    if (entity_name[i] >= '0' && entity_name[i] <= '9') {
                        codepoint = codepoint * 10 + (unsigned)(entity_name[i] - '0');
                    } else {
                        break;
                    }
                }
            }

            if (codepoint > 0 && codepoint <= 0x10FFFF) {
                size_t remaining = strlen(src) + 16;  /* rough estimate */
                int bytes = utf8_encode(codepoint, dst, remaining);
                if (bytes > 0) {
                    dst += bytes;
                    decoded = 1;
                }
            }
        } else {
            /* Named entity */
            for (int i = 0; NAMED_ENTITIES[i].name; i++) {
                if (strcmp(entity_name, NAMED_ENTITIES[i].name) == 0) {
                    size_t rlen = strlen(NAMED_ENTITIES[i].replacement);
                    memcpy(dst, NAMED_ENTITIES[i].replacement, rlen);
                    dst += rlen;
                    decoded = 1;
                    break;
                }
            }
        }

        if (decoded) {
            src = semicolon + 1;  /* skip past ';' */
        } else {
            /* Unknown entity — copy literally */
            *dst++ = *src++;
        }
    }

    *dst = '\0';
    return 0;
}

/* ── HTML Tag Parsing ────────────────────────────────────────────── */

/**
 * Extract an attribute value from an HTML tag string.
 * E.g., from `<A HREF="http://example.com" ADD_DATE="123456">`,
 * extract_attribute(..., "HREF", ...) returns "http://example.com".
 *
 * Returns 0 on success, -1 if attribute not found.
 */
static int extract_attribute(const char *tag, size_t tag_len,
                              const char *attr_name,
                              char *value, size_t value_size) {
    if (!tag || !attr_name || !value || value_size == 0) return -1;

    value[0] = '\0';

    /* Search case-insensitively for the attribute name */
    size_t attr_name_len = strlen(attr_name);
    const char *end = tag + tag_len;

    for (const char *p = tag; p < end - attr_name_len; p++) {
        /* Check for attribute name match (case-insensitive) */
        int match = 1;
        for (size_t i = 0; i < attr_name_len; i++) {
            if (tolower((unsigned char)p[i]) != tolower((unsigned char)attr_name[i])) {
                match = 0;
                break;
            }
        }
        if (!match) continue;

        /* Must be preceded by whitespace or start of tag */
        if (p > tag && !isspace((unsigned char)p[-1])) continue;

        /* Must be followed by '=' */
        const char *after = p + attr_name_len;
        while (after < end && isspace((unsigned char)*after)) after++;
        if (after >= end || *after != '=') continue;
        after++;
        while (after < end && isspace((unsigned char)*after)) after++;

        /* Extract value — may or may not be quoted */
        if (after < end && (*after == '"' || *after == '\'')) {
            char quote = *after++;
            const char *val_start = after;
            while (after < end && *after != quote) after++;

            size_t vlen = (size_t)(after - val_start);
            if (vlen >= value_size) vlen = value_size - 1;
            memcpy(value, val_start, vlen);
            value[vlen] = '\0';
        } else {
            /* Unquoted value — ends at whitespace or '>' */
            const char *val_start = after;
            while (after < end && !isspace((unsigned char)*after) && *after != '>') {
                after++;
            }
            size_t vlen = (size_t)(after - val_start);
            if (vlen >= value_size) vlen = value_size - 1;
            memcpy(value, val_start, vlen);
            value[vlen] = '\0';
        }

        return 0;
    }

    return -1;
}

/**
 * Extract the text content between > and the next <.
 * E.g., from `>Page Title</A>`, extract "Page Title".
 */
static int extract_text_content(const char *p, const char *end,
                                 char *text, size_t text_size) {
    if (!p || !text || text_size == 0) return -1;

    text[0] = '\0';

    /* Find the '>' that closes the opening tag */
    while (p < end && *p != '>') p++;
    if (p >= end) return -1;
    p++;  /* skip '>' */

    /* Collect text until '<' */
    const char *text_start = p;
    while (p < end && *p != '<') p++;

    size_t tlen = (size_t)(p - text_start);
    if (tlen >= text_size) tlen = text_size - 1;
    memcpy(text, text_start, tlen);
    text[tlen] = '\0';

    /* Trim whitespace */
    while (tlen > 0 && isspace((unsigned char)text[tlen - 1])) {
        text[--tlen] = '\0';
    }
    char *ws = text;
    while (*ws && isspace((unsigned char)*ws)) ws++;
    if (ws != text) {
        memmove(text, ws, strlen(ws) + 1);
    }

    /* Decode HTML entities in the text */
    html_decode_entities(text);

    return 0;
}

/**
 * Convert a timestamp string from bookmark HTML to ISO 8601.
 * Bookmark timestamps can be:
 *   - Unix seconds (e.g., "1745757490")
 *   - Chromium microseconds (e.g., "13380123456789000")
 *   - Already formatted dates
 */
static int convert_bookmark_timestamp(const char *ts_str, int ts_format,
                                       char *dst, size_t dstsize) {
    if (!ts_str || !ts_str[0] || !dst || dstsize < 21) {
        if (dst && dstsize > 0) dst[0] = '\0';
        return -1;
    }

    /* Check if it's all digits */
    int all_digits = 1;
    size_t len = strlen(ts_str);
    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)ts_str[i])) {
            all_digits = 0;
            break;
        }
    }

    if (!all_digits) {
        /* Not a numeric timestamp — copy as-is */
        strncpy(dst, ts_str, dstsize - 1);
        dst[dstsize - 1] = '\0';
        return 0;
    }

    long long ts = 0;
    for (size_t i = 0; i < len && i < 20; i++) {
        ts = ts * 10 + (ts_str[i] - '0');
    }

    time_t unix_time;

    if (ts_format == 2 || len >= 16) {
        /* Chromium microseconds since 1601 */
        unix_time = (time_t)(ts / 1000000LL - 11644473600LL);
    } else {
        /* Unix seconds */
        unix_time = (time_t)ts;
    }

    /* Sanity check */
    if (unix_time < 0 || unix_time > 4102444800LL) {  /* year 2100 */
        dst[0] = '\0';
        return -1;
    }

    struct tm *tm = gmtime(&unix_time);
    if (!tm) {
        dst[0] = '\0';
        return -1;
    }

    strftime(dst, dstsize, "%Y-%m-%dT%H:%M:%SZ", tm);
    return 0;
}

/* ── Result growing ──────────────────────────────────────────────── */

static int bm_ensure_capacity(HistoryResult *result) {
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

/* ── Main Parser ─────────────────────────────────────────────────── */

int import_bookmarks_html_buffer(const char *data, size_t data_len,
                                  HistoryResult *result,
                                  const BookmarkImportOptions *options) {
    if (!data || data_len == 0 || !result) return -1;

    /* Default options */
    BookmarkImportOptions opts;
    if (options) {
        opts = *options;
    } else {
        memset(&opts, 0, sizeof(opts));
        opts.max_depth = BOOKMARK_MAX_DEPTH;
        opts.import_folders = 1;
        opts.timestamp_format = 0;  /* auto */
    }
    if (opts.max_depth <= 0 || opts.max_depth > BOOKMARK_MAX_DEPTH) {
        opts.max_depth = BOOKMARK_MAX_DEPTH;
    }

    const char *p = data;
    const char *end = data + data_len;

    /* Skip UTF-8 BOM */
    if (data_len >= 3 &&
        (unsigned char)p[0] == 0xEF &&
        (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF) {
        p += 3;
    }

    /* Validate this looks like a bookmark file */
    int found_dt = 0;
    {
        const char *scan = p;
        size_t scan_limit = (data_len > 4096) ? 4096 : data_len;
        const char *scan_end = p + scan_limit;
        while (scan < scan_end) {
            if (*scan == '<' && scan + 3 < scan_end) {
                if ((scan[1] == 'D' || scan[1] == 'd') &&
                    (scan[2] == 'T' || scan[2] == 't' ||
                     scan[2] == 'L' || scan[2] == 'l')) {
                    found_dt = 1;
                    break;
                }
            }
            scan++;
        }
    }
    if (!found_dt) {
        fprintf(stderr, "[bookmark_import] File does not appear to be NBFF HTML\n");
        return -1;
    }

    /* Folder path stack for nested DL elements */
    char folder_stack[BOOKMARK_MAX_DEPTH][256];
    int folder_depth = 0;
    memset(folder_stack, 0, sizeof(folder_stack));

    int imported = 0;
    char current_folder[256] = "";

    while (p < end) {
        /* Scan for next '<' */
        while (p < end && *p != '<') p++;
        if (p >= end) break;

        const char *tag_start = p;
        p++;  /* skip '<' */

        /* Skip comments */
        if (p + 3 < end && p[0] == '!' && p[1] == '-' && p[2] == '-') {
            const char *comment_end = strstr(p, "-->");
            if (comment_end) {
                p = comment_end + 3;
            } else {
                p = end;
            }
            continue;
        }

        /* Find end of tag */
        const char *tag_end = p;
        int in_quotes = 0;
        char quote_char = 0;
        while (tag_end < end) {
            if (in_quotes) {
                if (*tag_end == quote_char) in_quotes = 0;
            } else {
                if (*tag_end == '"' || *tag_end == '\'') {
                    in_quotes = 1;
                    quote_char = *tag_end;
                } else if (*tag_end == '>') {
                    break;
                }
            }
            tag_end++;
        }
        if (tag_end >= end) break;

        size_t tag_len = (size_t)(tag_end - p);

        /* Determine tag type (case-insensitive first 2-3 chars) */
        char tag_upper[8] = "";
        for (size_t i = 0; i < tag_len && i < 7; i++) {
            tag_upper[i] = (char)toupper((unsigned char)p[i]);
        }

        /* Handle <DL> — push folder depth */
        if (strncmp(tag_upper, "DL", 2) == 0 &&
            (tag_len == 2 || isspace((unsigned char)p[2]) || p[2] == '>')) {
            if (folder_depth < opts.max_depth) {
                if (current_folder[0] && folder_depth < BOOKMARK_MAX_DEPTH) {
                    strncpy(folder_stack[folder_depth], current_folder,
                            sizeof(folder_stack[0]) - 1);
                }
                folder_depth++;
            }
            p = tag_end + 1;
            continue;
        }

        /* Handle </DL> — pop folder depth */
        if (strncmp(tag_upper, "/DL", 3) == 0) {
            if (folder_depth > 0) {
                folder_depth--;
                if (folder_depth > 0 && folder_depth < BOOKMARK_MAX_DEPTH) {
                    strncpy(current_folder, folder_stack[folder_depth - 1],
                            sizeof(current_folder) - 1);
                } else {
                    current_folder[0] = '\0';
                }
            }
            p = tag_end + 1;
            continue;
        }

        /* Handle <H3> — folder name (inside <DT>) */
        if (strncmp(tag_upper, "H3", 2) == 0 ||
            strncmp(tag_upper, "H1", 2) == 0) {
            char folder_name[256];
            if (extract_text_content(p - 1, end, folder_name, sizeof(folder_name)) == 0) {
                strncpy(current_folder, folder_name, sizeof(current_folder) - 1);
                current_folder[sizeof(current_folder) - 1] = '\0';

                if (folder_depth > 0 && folder_depth <= BOOKMARK_MAX_DEPTH) {
                    strncpy(folder_stack[folder_depth - 1], folder_name,
                            sizeof(folder_stack[0]) - 1);
                }
            }
            p = tag_end + 1;
            continue;
        }

        /* Handle <A HREF="..."> — bookmark entry */
        if (tag_upper[0] == 'A' && (tag_len == 1 || isspace((unsigned char)p[1]))) {
            char href[2048] = "";
            char add_date_str[64] = "";
            char last_visit_str[64] = "";
            char last_modified_str[64] = "";
            char icon_uri[2048] = "";
            char title[512] = "";

            /* Extract attributes */
            extract_attribute(p, tag_len, "HREF", href, sizeof(href));
            extract_attribute(p, tag_len, "href", href[0] ? href : href, sizeof(href));
            extract_attribute(p, tag_len, "ADD_DATE", add_date_str, sizeof(add_date_str));
            extract_attribute(p, tag_len, "add_date", add_date_str[0] ? add_date_str : add_date_str, sizeof(add_date_str));
            extract_attribute(p, tag_len, "LAST_VISIT", last_visit_str, sizeof(last_visit_str));
            extract_attribute(p, tag_len, "last_visit", last_visit_str[0] ? last_visit_str : last_visit_str, sizeof(last_visit_str));
            extract_attribute(p, tag_len, "LAST_MODIFIED", last_modified_str, sizeof(last_modified_str));
            extract_attribute(p, tag_len, "ICON_URI", icon_uri, sizeof(icon_uri));
            extract_attribute(p, tag_len, "ICON", icon_uri[0] ? icon_uri : icon_uri, sizeof(icon_uri));

            /* Extract title text */
            extract_text_content(p - 1, end, title, sizeof(title));

            /* Decode entities in extracted fields */
            html_decode_entities(href);
            html_decode_entities(title);

            /* Only add if we have a URL */
            if (href[0]) {
                if (bm_ensure_capacity(result) != 0) break;

                HistoryEntry *e = &result->entries[result->count];
                memset(e, 0, sizeof(*e));

                strncpy(e->url, href, sizeof(e->url) - 1);
                e->url[sizeof(e->url) - 1] = '\0';

                /* Build title with folder prefix if requested */
                if (opts.import_folders && current_folder[0] && title[0]) {
                    char full_title[512];
                    snprintf(full_title, sizeof(full_title), "[%s] %s",
                            current_folder, title);
                    strncpy(e->title, full_title, sizeof(e->title) - 1);
                } else {
                    strncpy(e->title, title, sizeof(e->title) - 1);
                }
                e->title[sizeof(e->title) - 1] = '\0';

                /* Convert timestamps */
                if (last_visit_str[0]) {
                    convert_bookmark_timestamp(last_visit_str, opts.timestamp_format,
                                               e->last_visit_time,
                                               sizeof(e->last_visit_time));
                } else if (add_date_str[0]) {
                    convert_bookmark_timestamp(add_date_str, opts.timestamp_format,
                                               e->last_visit_time,
                                               sizeof(e->last_visit_time));
                }

                e->visit_count = 1;
                e->typed_count = 0;
                e->last_visit_raw = 0;

                result->count++;
                imported++;
            }

            p = tag_end + 1;
            continue;
        }

        /* Skip other tags */
        p = tag_end + 1;
    }

    printf("[bookmark_import] Imported %d entries from HTML bookmarks\n", imported);
    return imported;
}

int import_bookmarks_html(const char *filepath, HistoryResult *result,
                          const BookmarkImportOptions *options) {
    if (!filepath || !result) return -1;

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "[bookmark_import] Cannot open %s\n", filepath);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 100 * 1024 * 1024) {
        fprintf(stderr, "[bookmark_import] File too large or empty: %ld bytes\n", fsize);
        fclose(f);
        return -1;
    }

    char *data = malloc((size_t)fsize + 1);
    if (!data) {
        fprintf(stderr, "[bookmark_import] Out of memory\n");
        fclose(f);
        return -1;
    }

    size_t read_bytes = fread(data, 1, (size_t)fsize, f);
    fclose(f);
    data[read_bytes] = '\0';

    int result_count = import_bookmarks_html_buffer(data, read_bytes, result, options);

    free(data);
    return result_count;
}
