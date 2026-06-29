/**
 * export_markdown.c — Markdown export for browser history
 *
 * Generates formatted Markdown with:
 *   - Statistics summary table
 *   - Category breakdown table
 *   - History entries with GFM-style tables
 *   - Clickable URL links
 *   - Table of contents
 *
 * @version 1.0.0
 */

#include "export_markdown.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Markdown escaping ───────────────────────────────────────────── */

/**
 * Escape characters that have special meaning in Markdown table cells.
 */
static void md_escape(FILE *out, const char *str, int max_len) {
    if (!str) return;

    int written = 0;
    for (const char *p = str; *p && (max_len <= 0 || written < max_len); p++) {
        switch (*p) {
            case '|':  fputs("\\|", out); break;
            case '\\': fputs("\\\\", out); break;
            case '[':  fputs("\\[", out); break;
            case ']':  fputs("\\]", out); break;
            case '*':  fputs("\\*", out); break;
            case '_':  fputs("\\_", out); break;
            case '`':  fputs("\\`", out); break;
            case '\n': fputs(" ", out); break;
            case '\r': break;
            default:   fputc(*p, out); break;
        }
        written++;
    }

    if (max_len > 0 && (int)strlen(str) > max_len) {
        fputs("...", out);
    }
}

/* ── Markdown generation ─────────────────────────────────────────── */

int export_markdown(const char *filepath, const HistoryResult *result,
                    const HistoryStats *stats, const CatEngine *cat_engine) {
    if (!filepath || !result) return -1;

    FILE *f = fopen(filepath, "w");
    if (!f) {
        fprintf(stderr, "[export_md] Cannot open %s for writing\n", filepath);
        return -1;
    }

    /* Timestamp */
    char gen_time[64];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm) {
        strftime(gen_time, sizeof(gen_time), "%Y-%m-%d %H:%M:%S", tm);
    } else {
        strcpy(gen_time, "Unknown");
    }

    /* ── Title ── */
    fprintf(f, "# Browser History Report\n\n");
    fprintf(f, "> Generated on %s — %d entries\n\n", gen_time, result->count);

    /* ── Table of Contents ── */
    fprintf(f, "## Table of Contents\n\n");
    if (stats) fprintf(f, "- [Statistics](#statistics)\n");
    if (cat_engine) fprintf(f, "- [Categories](#categories)\n");
    fprintf(f, "- [History Entries](#history-entries)\n");
    fprintf(f, "\n---\n\n");

    /* ── Statistics ── */
    if (stats) {
        fprintf(f, "## Statistics\n\n");
        fprintf(f, "| Metric | Value |\n");
        fprintf(f, "|--------|-------|\n");
        fprintf(f, "| Total URLs | %d |\n", stats->total_urls);
        fprintf(f, "| Total Visits | %d |\n", stats->total_visits);
        fprintf(f, "| Unique Domains | %d |\n", stats->unique_domains);
        fprintf(f, "| Earliest Visit | %s |\n", stats->earliest_visit);
        fprintf(f, "| Latest Visit | %s |\n", stats->latest_visit);

        if (stats->most_visited_title[0]) {
            fprintf(f, "| Most Visited | ");
            md_escape(f, stats->most_visited_title, 50);
            fprintf(f, " (%d visits) |\n", stats->most_visited_count);
        }

        fprintf(f, "\n");
    }

    /* ── Categories ── */
    if (cat_engine && cat_engine->category_count > 0) {
        fprintf(f, "## Categories\n\n");
        fprintf(f, "| Category | URLs | Total Visits | Top Domain |\n");
        fprintf(f, "|----------|------|--------------|------------|\n");

        for (int i = 0; i <= cat_engine->category_count; i++) {
            if (cat_engine->stats[i].url_count == 0) continue;

            fprintf(f, "| ");
            md_escape(f, cat_engine->stats[i].name, 0);
            fprintf(f, " | %d | %d | ",
                    cat_engine->stats[i].url_count,
                    cat_engine->stats[i].total_visits);
            md_escape(f, cat_engine->stats[i].top_domain, 30);
            fprintf(f, " |\n");
        }

        fprintf(f, "\n**Categorized:** %d | **Uncategorized:** %d\n\n",
                cat_engine->total_categorized, cat_engine->total_uncategorized);
    }

    /* ── History Entries ── */
    fprintf(f, "## History Entries\n\n");

    int max_display = result->count;
    if (max_display > 200) {
        fprintf(f, "> Showing first 200 of %d entries\n\n", result->count);
        max_display = 200;
    }

    fprintf(f, "| # | Title | URL | Visits | Last Visit |\n");
    fprintf(f, "|---|-------|-----|--------|------------|\n");

    for (int i = 0; i < max_display; i++) {
        const HistoryEntry *e = &result->entries[i];

        fprintf(f, "| %d | ", i + 1);

        /* Title */
        if (e->title[0]) {
            md_escape(f, e->title, 40);
        } else {
            fprintf(f, "*No title*");
        }

        /* URL as link */
        fprintf(f, " | [link](");
        /* Escape parentheses in URL for markdown link syntax */
        for (const char *u = e->url; *u; u++) {
            if (*u == ')') fputs("%%29", f);
            else if (*u == '(') fputs("%%28", f);
            else fputc(*u, f);
        }
        fprintf(f, ") | %d | %s |\n",
                e->visit_count, e->last_visit_time);
    }

    fprintf(f, "\n---\n\n");
    fprintf(f, "*Generated by ViewBrowserHistory v1.0.0*\n");

    fclose(f);
    printf("[export_md] Exported %d entries to %s\n", result->count, filepath);

    return 0;
}
