/**
 * export_csv.c — RFC 4180-compliant CSV export
 *
 * Exports history entries with proper field escaping:
 *   - Fields containing commas, quotes, or newlines are double-quoted
 *   - Embedded quotes are escaped by doubling them
 *   - UTF-8 content is preserved as-is
 *   - BOM is prepended for Excel compatibility
 *
 * @version 1.0.0
 */

#include "export_csv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Field escaping ──────────────────────────────────────────────── */

/**
 * Check if a field needs quoting.
 */
static int needs_quoting(const char *field) {
    if (!field) return 0;
    for (const char *p = field; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
            return 1;
        }
    }
    return 0;
}

void csv_write_field(FILE *out, const char *field) {
    if (!out) return;

    if (!field || !field[0]) {
        /* Empty field */
        return;
    }

    if (needs_quoting(field)) {
        fputc('"', out);
        for (const char *p = field; *p; p++) {
            if (*p == '"') {
                /* Escape quote by doubling */
                fputc('"', out);
                fputc('"', out);
            } else {
                fputc(*p, out);
            }
        }
        fputc('"', out);
    } else {
        fputs(field, out);
    }
}

/* ── CSV export ──────────────────────────────────────────────────── */

int export_csv_stream(FILE *out, const HistoryResult *result,
                      int include_header) {
    if (!out || !result) return -1;

    /* UTF-8 BOM for Excel compatibility */
    fwrite("\xEF\xBB\xBF", 1, 3, out);

    /* Header row */
    if (include_header) {
        fprintf(out, "url,title,visit_count,typed_count,last_visit_time\r\n");
    }

    /* Data rows */
    for (int i = 0; i < result->count; i++) {
        const HistoryEntry *e = &result->entries[i];

        csv_write_field(out, e->url);
        fputc(',', out);
        csv_write_field(out, e->title);
        fputc(',', out);
        fprintf(out, "%d", e->visit_count);
        fputc(',', out);
        fprintf(out, "%d", e->typed_count);
        fputc(',', out);
        csv_write_field(out, e->last_visit_time);
        fprintf(out, "\r\n");
    }

    return 0;
}

int export_csv(const char *filepath, const HistoryResult *result,
               int include_header) {
    if (!filepath || !result) return -1;

    FILE *f = fopen(filepath, "wb");
    if (!f) {
        fprintf(stderr, "[export_csv] Cannot open %s for writing\n", filepath);
        return -1;
    }

    int rc = export_csv_stream(f, result, include_header);
    fclose(f);

    if (rc == 0) {
        printf("[export_csv] Exported %d entries to %s\n", result->count, filepath);
    }

    return rc;
}
