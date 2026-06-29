/**
 * bookmark_import_fuzzer.c — Fuzz harness for HTML bookmark importer
 *
 * Exercises the Netscape Bookmark File Format parser including:
 *   - HTML entity decoding (named, decimal, hex)
 *   - Tag attribute parsing
 *   - Nested <DL> folder recursion
 *   - Timestamp conversion (Unix / Chromium epochs)
 *
 * @version 1.0.0
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "html_bookmark_import.h"
#include "history_db.h"

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size) {
    if (size == 0 || size > 2 * 1024 * 1024) return 0;

    /* Initialize result set */
    HistoryResult result;
    history_result_init(&result);

    /* Use the in-memory buffer API directly — no temp file needed */
    BookmarkImportOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.max_depth = 32;
    opts.import_folders = 1;
    opts.timestamp_format = 0;  /* auto-detect */

    import_bookmarks_html_buffer((const char *)data, size, &result, &opts);

    /* Also fuzz the entity decoder standalone */
    if (size < 4096) {
        char *entity_buf = malloc(size + 1);
        if (entity_buf) {
            memcpy(entity_buf, data, size);
            entity_buf[size] = '\0';
            html_decode_entities(entity_buf);
            free(entity_buf);
        }
    }

    history_result_free(&result);
    return 0;
}
