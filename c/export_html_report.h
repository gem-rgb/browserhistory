/**
 * export_html_report.h — Interactive HTML report export
 *
 * Generates a self-contained HTML report with:
 *   - Sortable tables of history entries
 *   - Category breakdown charts (embedded SVG)
 *   - Statistics summary
 *   - Dark mode styling
 *
 * @version 1.0.0
 */

#ifndef EXPORT_HTML_REPORT_H
#define EXPORT_HTML_REPORT_H

#include "history_db.h"
#include "url_categorize.h"

/**
 * Export an interactive HTML report.
 *
 * @param filepath    Output file path
 * @param result      History entries
 * @param stats       Summary statistics (can be NULL)
 * @param cat_engine  Categorization engine with stats (can be NULL)
 * @return 0 on success, -1 on error
 */
int export_html_report(const char *filepath, const HistoryResult *result,
                       const HistoryStats *stats, const CatEngine *cat_engine);

#endif /* EXPORT_HTML_REPORT_H */
