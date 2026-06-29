/**
 * export_html_report.c — Interactive HTML report generation
 *
 * Generates a self-contained HTML report with:
 *   - Responsive dark-mode design with CSS grid
 *   - Statistics dashboard with key metrics
 *   - Category breakdown with embedded SVG bar chart
 *   - Sortable history table with click-to-sort headers
 *   - Inline JavaScript for interactivity (no external dependencies)
 *
 * @version 1.0.0
 */

#include "export_html_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── HTML entity escaping ────────────────────────────────────────── */

static void html_escape(FILE *out, const char *str) {
    if (!str) return;
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '&':  fputs("&amp;",  out); break;
            case '<':  fputs("&lt;",   out); break;
            case '>':  fputs("&gt;",   out); break;
            case '"':  fputs("&quot;", out); break;
            case '\'': fputs("&#39;",  out); break;
            default:   fputc(*p, out); break;
        }
    }
}

/* Truncate a URL for display, preserving domain */
static void html_truncated_url(FILE *out, const char *url, int max_len) {
    if (!url) return;
    int len = (int)strlen(url);
    if (len <= max_len) {
        html_escape(out, url);
    } else {
        char buf[256];
        int head = max_len - 3;
        if (head < 10) head = 10;
        if (head > (int)sizeof(buf) - 4) head = (int)sizeof(buf) - 4;
        memcpy(buf, url, (size_t)head);
        buf[head] = '\0';
        strcat(buf, "...");
        html_escape(out, buf);
    }
}

/* ── Report generation ───────────────────────────────────────────── */

int export_html_report(const char *filepath, const HistoryResult *result,
                       const HistoryStats *stats, const CatEngine *cat_engine) {
    if (!filepath || !result) return -1;

    FILE *f = fopen(filepath, "w");
    if (!f) {
        fprintf(stderr, "[export_html] Cannot open %s for writing\n", filepath);
        return -1;
    }

    /* Get generation timestamp */
    char gen_time[64];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm) {
        strftime(gen_time, sizeof(gen_time), "%Y-%m-%d %H:%M:%S", tm);
    } else {
        strcpy(gen_time, "Unknown");
    }

    /* ── HTML Head ── */
    fprintf(f, "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n");
    fprintf(f, "<meta charset=\"UTF-8\">\n");
    fprintf(f, "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n");
    fprintf(f, "<title>Browser History Report</title>\n");

    /* ── CSS ── */
    fprintf(f, "<style>\n");
    fprintf(f, ":root{--bg:#0f0f0f;--card:#1a1a2e;--border:#2a2a4a;--text:#e0e0e0;"
               "--accent:#7c3aed;--accent2:#06b6d4;--muted:#888;--success:#22c55e;"
               "--warning:#f59e0b;--danger:#ef4444}\n");
    fprintf(f, "*{margin:0;padding:0;box-sizing:border-box}\n");
    fprintf(f, "body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;"
               "background:var(--bg);color:var(--text);line-height:1.6;padding:2rem}\n");
    fprintf(f, ".container{max-width:1200px;margin:0 auto}\n");
    fprintf(f, "h1{font-size:2rem;font-weight:700;margin-bottom:.5rem;"
               "background:linear-gradient(135deg,var(--accent),var(--accent2));"
               "-webkit-background-clip:text;-webkit-text-fill-color:transparent}\n");
    fprintf(f, ".subtitle{color:var(--muted);margin-bottom:2rem}\n");

    /* Stats grid */
    fprintf(f, ".stats-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));"
               "gap:1rem;margin-bottom:2rem}\n");
    fprintf(f, ".stat-card{background:var(--card);border:1px solid var(--border);"
               "border-radius:12px;padding:1.5rem;text-align:center}\n");
    fprintf(f, ".stat-value{font-size:2rem;font-weight:700;color:var(--accent)}\n");
    fprintf(f, ".stat-label{font-size:.85rem;color:var(--muted);margin-top:.25rem}\n");

    /* Category chart */
    fprintf(f, ".chart-container{background:var(--card);border:1px solid var(--border);"
               "border-radius:12px;padding:1.5rem;margin-bottom:2rem}\n");
    fprintf(f, ".chart-title{font-size:1.2rem;font-weight:600;margin-bottom:1rem}\n");
    fprintf(f, ".bar-row{display:flex;align-items:center;margin:.5rem 0}\n");
    fprintf(f, ".bar-label{width:140px;font-size:.85rem;color:var(--muted)}\n");
    fprintf(f, ".bar-track{flex:1;height:24px;background:#222;border-radius:4px;overflow:hidden}\n");
    fprintf(f, ".bar-fill{height:100%%;border-radius:4px;transition:width .6s ease}\n");
    fprintf(f, ".bar-count{width:60px;text-align:right;font-size:.85rem;color:var(--muted)}\n");

    /* Table */
    fprintf(f, ".table-container{background:var(--card);border:1px solid var(--border);"
               "border-radius:12px;overflow:hidden;margin-bottom:2rem}\n");
    fprintf(f, ".table-header{padding:1rem 1.5rem;border-bottom:1px solid var(--border);"
               "display:flex;justify-content:space-between;align-items:center}\n");
    fprintf(f, ".table-header h2{font-size:1.2rem;font-weight:600}\n");
    fprintf(f, "table{width:100%%;border-collapse:collapse}\n");
    fprintf(f, "th{background:#16162a;padding:.75rem 1rem;text-align:left;font-size:.8rem;"
               "color:var(--muted);text-transform:uppercase;letter-spacing:.05em;"
               "cursor:pointer;user-select:none;border-bottom:1px solid var(--border)}\n");
    fprintf(f, "th:hover{color:var(--accent)}\n");
    fprintf(f, "td{padding:.6rem 1rem;border-bottom:1px solid var(--border);font-size:.9rem}\n");
    fprintf(f, "tr:hover td{background:#16162a}\n");
    fprintf(f, "a{color:var(--accent2);text-decoration:none}\n");
    fprintf(f, "a:hover{text-decoration:underline}\n");
    fprintf(f, ".footer{text-align:center;color:var(--muted);font-size:.8rem;margin-top:2rem}\n");
    fprintf(f, "</style>\n</head>\n<body>\n<div class=\"container\">\n");

    /* ── Header ── */
    fprintf(f, "<h1>Browser History Report</h1>\n");
    fprintf(f, "<p class=\"subtitle\">Generated on %s — %d entries</p>\n",
            gen_time, result->count);

    /* ── Statistics Dashboard ── */
    if (stats) {
        fprintf(f, "<div class=\"stats-grid\">\n");

        fprintf(f, "<div class=\"stat-card\"><div class=\"stat-value\">%d</div>"
                "<div class=\"stat-label\">Total URLs</div></div>\n", stats->total_urls);
        fprintf(f, "<div class=\"stat-card\"><div class=\"stat-value\">%d</div>"
                "<div class=\"stat-label\">Total Visits</div></div>\n", stats->total_visits);
        fprintf(f, "<div class=\"stat-card\"><div class=\"stat-value\">%d</div>"
                "<div class=\"stat-label\">Unique Domains</div></div>\n", stats->unique_domains);

        if (stats->most_visited_title[0]) {
            fprintf(f, "<div class=\"stat-card\"><div class=\"stat-value\">%d</div>"
                    "<div class=\"stat-label\">Top: ",
                    stats->most_visited_count);
            html_escape(f, stats->most_visited_title);
            fprintf(f, "</div></div>\n");
        }

        fprintf(f, "</div>\n");
    }

    /* ── Category Chart ── */
    if (cat_engine && cat_engine->category_count > 0) {
        fprintf(f, "<div class=\"chart-container\">\n");
        fprintf(f, "<div class=\"chart-title\">Category Breakdown</div>\n");

        /* Find max for scaling */
        int max_count = 1;
        for (int i = 0; i <= cat_engine->category_count; i++) {
            if (cat_engine->stats[i].url_count > max_count) {
                max_count = cat_engine->stats[i].url_count;
            }
        }

        /* Default category colors */
        const char *colors[] = {
            "#E74C3C", "#3498DB", "#F39C12", "#2ECC71", "#9B59B6",
            "#1ABC9C", "#34495E", "#E67E22", "#2980B9", "#8E44AD",
            "#16A085", "#C0392B", "#27AE60", "#D35400", "#7F8C8D"
        };

        for (int i = 0; i <= cat_engine->category_count; i++) {
            if (cat_engine->stats[i].url_count == 0) continue;

            int pct = (cat_engine->stats[i].url_count * 100) / max_count;
            const char *color = (i < cat_engine->category_count && cat_engine->categories[i].color[0])
                ? cat_engine->categories[i].color
                : colors[i % 15];

            fprintf(f, "<div class=\"bar-row\">\n");
            fprintf(f, "  <div class=\"bar-label\">");
            html_escape(f, cat_engine->stats[i].name);
            fprintf(f, "</div>\n");
            fprintf(f, "  <div class=\"bar-track\"><div class=\"bar-fill\" "
                    "style=\"width:%d%%;background:%s\"></div></div>\n", pct, color);
            fprintf(f, "  <div class=\"bar-count\">%d</div>\n",
                    cat_engine->stats[i].url_count);
            fprintf(f, "</div>\n");
        }

        fprintf(f, "</div>\n");
    }

    /* ── History Table ── */
    int max_display = result->count;
    if (max_display > 500) max_display = 500;

    fprintf(f, "<div class=\"table-container\">\n");
    fprintf(f, "<div class=\"table-header\"><h2>History Entries</h2>"
            "<span style=\"color:var(--muted)\">Showing %d of %d</span></div>\n",
            max_display, result->count);

    fprintf(f, "<table id=\"history-table\">\n<thead>\n<tr>\n");
    fprintf(f, "<th onclick=\"sortTable(0)\">Title &#x25B4;&#x25BE;</th>\n");
    fprintf(f, "<th onclick=\"sortTable(1)\">URL</th>\n");
    fprintf(f, "<th onclick=\"sortTable(2)\">Visits &#x25B4;&#x25BE;</th>\n");
    fprintf(f, "<th onclick=\"sortTable(3)\">Last Visit &#x25B4;&#x25BE;</th>\n");
    fprintf(f, "</tr>\n</thead>\n<tbody>\n");

    for (int i = 0; i < max_display; i++) {
        const HistoryEntry *e = &result->entries[i];

        fprintf(f, "<tr>\n<td>");
        if (e->title[0]) {
            html_escape(f, e->title);
        } else {
            fprintf(f, "<em style=\"color:var(--muted)\">No title</em>");
        }
        fprintf(f, "</td>\n<td><a href=\"");
        html_escape(f, e->url);
        fprintf(f, "\" target=\"_blank\" rel=\"noopener\">");
        html_truncated_url(f, e->url, 60);
        fprintf(f, "</a></td>\n");
        fprintf(f, "<td>%d</td>\n", e->visit_count);
        fprintf(f, "<td>%s</td>\n", e->last_visit_time);
        fprintf(f, "</tr>\n");
    }

    fprintf(f, "</tbody>\n</table>\n</div>\n");

    /* ── JavaScript for sorting ── */
    fprintf(f, "<script>\n");
    fprintf(f, "let sortDir={};\n");
    fprintf(f, "function sortTable(col){\n");
    fprintf(f, "  const t=document.getElementById('history-table');\n");
    fprintf(f, "  const rows=Array.from(t.tBodies[0].rows);\n");
    fprintf(f, "  sortDir[col]=!sortDir[col];\n");
    fprintf(f, "  rows.sort((a,b)=>{\n");
    fprintf(f, "    let va=a.cells[col].textContent,vb=b.cells[col].textContent;\n");
    fprintf(f, "    if(col===2){va=parseInt(va)||0;vb=parseInt(vb)||0;}\n");
    fprintf(f, "    if(va<vb)return sortDir[col]?-1:1;\n");
    fprintf(f, "    if(va>vb)return sortDir[col]?1:-1;\n");
    fprintf(f, "    return 0;\n");
    fprintf(f, "  });\n");
    fprintf(f, "  rows.forEach(r=>t.tBodies[0].appendChild(r));\n");
    fprintf(f, "}\n");
    fprintf(f, "</script>\n");

    /* ── Footer ── */
    fprintf(f, "<div class=\"footer\">Generated by ViewBrowserHistory v1.0.0</div>\n");
    fprintf(f, "</div>\n</body>\n</html>\n");

    fclose(f);
    printf("[export_html] Generated report with %d entries at %s\n",
           result->count, filepath);

    return 0;
}
