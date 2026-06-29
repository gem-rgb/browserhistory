/**
 * import_json_fuzzer.c — Fuzz harness for the extension JSON importer
 *
 * Exercises the full JSON parsing pipeline in import_ext.c including
 * brace-depth counting, string extraction, escape handling, and
 * entry deserialization.
 *
 * @version 1.0.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "import_ext.h"
#include "history_db.h"

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size) {
    if (size == 0 || size > 2 * 1024 * 1024) return 0;

    /* Write fuzz data to a temporary file since import_extension_json
     * takes a filepath, not a buffer */
    char tmppath[] = "/tmp/fuzz_json_XXXXXX";
    int fd = mkstemp(tmppath);
    if (fd < 0) return 0;

    ssize_t written = write(fd, data, size);
    close(fd);
    if (written != (ssize_t)size) {
        unlink(tmppath);
        return 0;
    }

    /* Prepare result set */
    HistoryResult result;
    history_result_init(&result);

    /* Run the parser — we expect it to handle arbitrary input safely */
    import_extension_json(tmppath, &result);

    /* Cleanup */
    history_result_free(&result);
    unlink(tmppath);

    return 0;
}
