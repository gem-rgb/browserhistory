/**
 * export_markdown.h — Markdown export for browser history
 *
 * Generates formatted Markdown with tables, category sections,
 * statistics summary, and clickable links.
 *
 * @version 1.0.0
 */

#ifndef EXPORT_MARKDOWN_H
#define EXPORT_MARKDOWN_H

#include "history_db.h"
#include "url_categorize.h"

/**
 * Export history entries to a Markdown file.
 *
 * @param filepath    Output file path
 * @param result      History entries
 * @param stats       Summary statistics (can be NULL)
 * @param cat_engine  Categorization engine with stats (can be NULL)
 * @return 0 on success, -1 on error
 */
int export_markdown(const char *filepath, const HistoryResult *result,
                    const HistoryStats *stats, const CatEngine *cat_engine);

#endif /* EXPORT_MARKDOWN_H */
