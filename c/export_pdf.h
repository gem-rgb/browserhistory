/**
 * export_pdf.h — PDF report generation for history data
 *
 * @version 1.0.0
 */

#ifndef EXPORT_PDF_H
#define EXPORT_PDF_H

#include "history_db.h"
#include "categorize.h"

/**
 * Generate a styled PDF report from history results.
 *
 * The PDF includes:
 *   - Cover page with title, summary stats, and generation timestamp
 *   - History listing with URLs, titles, visit counts, and dates
 *   - Dark header bars with orange accent (matching the JS version)
 *
 * @param result   The history query results
 * @param stats    Computed statistics
 * @param filepath Output PDF file path
 * @return 0 on success, -1 on error
 */
int export_to_pdf(const HistoryResult *result, const HistoryStats *stats,
                  const char *filepath);

/**
 * Export history to a styled PDF with AI browsing analysis page.
 */
int export_to_pdf_analyzed(const HistoryResult *result, const HistoryStats *stats,
                           const BrowsingAnalysis *analysis, const char *filepath);

#endif /* EXPORT_PDF_H */
