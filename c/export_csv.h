/**
 * export_csv.h — CSV export for browser history
 *
 * Exports history entries to RFC 4180-compliant CSV with proper
 * escaping for fields containing commas, quotes, and newlines.
 *
 * @version 1.0.0
 */

#ifndef EXPORT_CSV_H
#define EXPORT_CSV_H

#include "history_db.h"
#include <stdio.h>

/**
 * Export history entries to a CSV file.
 *
 * @param filepath  Output file path
 * @param result    History entries to export
 * @param include_header  1 = write header row
 * @return 0 on success, -1 on error
 */
int export_csv(const char *filepath, const HistoryResult *result,
               int include_header);

/**
 * Export history entries to an open file stream.
 */
int export_csv_stream(FILE *out, const HistoryResult *result,
                      int include_header);

/**
 * Write a single CSV field with proper escaping.
 * Wraps in quotes if the field contains commas, quotes, or newlines.
 *
 * @param out    Output file stream
 * @param field  The field value to write
 */
void csv_write_field(FILE *out, const char *field);

#endif /* EXPORT_CSV_H */
