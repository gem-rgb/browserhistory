/**
 * csv_import_fuzzer.c — Fuzz harness for CSV/TSV history importer
 *
 * Exercises the RFC 4180 CSV parsing pipeline including:
 *   - Delimiter auto-detection
 *   - Quoted field parsing (embedded delimiters, newlines, escaped quotes)
 *   - Date format auto-detection and conversion
 *   - Header detection and column mapping
 *   - Character encoding validation and Latin-1 fallback
 *
 * @version 1.0.0
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "csv_import.h"
#include "history_db.h"

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size) {
    if (size == 0 || size > 2 * 1024 * 1024) return 0;

    /* Initialize result set */
    HistoryResult result;
    history_result_init(&result);

    /* Test with auto-detect options */
    CsvImportOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.delimiter = CSV_DELIM_AUTO;
    opts.date_format = CSV_DATE_AUTO;
    opts.has_header = -1;  /* auto-detect */

    import_csv_buffer((const char *)data, size, &result, &opts);

    history_result_free(&result);

    /* Also exercise standalone sub-components */

    /* Delimiter detection */
    csv_detect_delimiter((const char *)data, size);

    /* Date format detection on first line */
    if (size < 256) {
        char date_buf[256];
        size_t copy_len = (size < 255) ? size : 255;
        memcpy(date_buf, data, copy_len);
        date_buf[copy_len] = '\0';

        CsvDateFormat fmt = csv_detect_date_format(date_buf);
        if (fmt != CSV_DATE_AUTO) {
            char iso_buf[64];
            csv_parse_date(date_buf, fmt, iso_buf, sizeof(iso_buf));
        }
    }

    /* Row parsing with each delimiter type */
    if (size < 8192) {
        CsvRow row;
        char delims[] = {',', '\t', ';', '|'};
        for (int d = 0; d < 4; d++) {
            const char *row_data = (const char *)data;
            const char *row_end = row_data + size;
            /* Parse up to 10 rows */
            for (int r = 0; r < 10 && row_data && row_data < row_end; r++) {
                row_data = csv_parse_row(row_data, row_end, delims[d], &row);
            }
        }
    }

    return 0;
}
