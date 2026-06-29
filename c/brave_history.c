/**
 * brave_history.c — Main entry point for Brave History Access (C Edition)
 *
 * A native C program that directly reads Brave Browser's SQLite history
 * database from disk, bypassing all browser API limitations.
 * Can also import data from the Incognito History Tracker extension.
 *
 * Usage:
 *   brave-history                                        # summary to console
 *   brave-history --json output.json                     # export as JSON
 *   brave-history --pdf  report.pdf                      # styled PDF report
 *   brave-history --days 7                               # last 7 days only
 *   brave-history --import ext-export.json               # import extension data
 *   brave-history --import ext.json --pdf combined.pdf   # merge + export
 *
 * @version 2.0.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "history_db.h"
#include "export_json.h"
#include "export_pdf.h"
#include "import_ext.h"
#include "categorize.h"

/* ── Version ─────────────────────────────────────────────────────── */

#define VERSION "2.0.0"

/* ── Usage / Help ────────────────────────────────────────────────── */

static void print_banner(void) {
    printf("\n");
    printf("  ============================================\n");
    printf("   BRAVE HISTORY ACCESS — C Edition v%s\n", VERSION);
    printf("  ============================================\n");
    printf("   Direct SQLite database reader for Brave\n");
    printf("   browser history. No extensions required.\n");
    printf("  ============================================\n\n");
}

static void print_usage(const char *progname) {
    printf("Usage: %s [OPTIONS]\n\n", progname);
    printf("Options:\n");
    printf("  --json   <file>   Export history to JSON file\n");
    printf("  --pdf    <file>   Generate styled PDF report\n");
    printf("  --days   <N>      Only include last N days (default: all)\n");
    printf("  --db     <file>   Use a specific History database file\n");
    printf("  --import <file>   Import extension-exported JSON (can repeat)\n");
    printf("  --no-db           Skip native DB, use only --import data\n");
    printf("  --help            Show this help message\n");
    printf("  --version         Show version\n");
    printf("\nExamples:\n");
    printf("  %s --json history.json\n", progname);
    printf("  %s --pdf report.pdf --days 30\n", progname);
    printf("  %s --import tracked.json --pdf incognito-report.pdf\n", progname);
    printf("  %s --import ext.json --json combined.json --pdf combined.pdf\n", progname);
    printf("  %s --no-db --import ext.json --pdf ext-only.pdf\n", progname);
    printf("\n");
}

/* ── Print summary to console ────────────────────────────────────── */

static void print_summary(const HistoryResult *result, const HistoryStats *stats) {
    printf("\n  ── Summary ──────────────────────────────\n");
    printf("  Total URLs:       %d\n", stats->total_urls);
    printf("  Total Visits:     %d\n", stats->total_visits);
    printf("  Unique Domains:   %d\n", stats->unique_domains);
    printf("  Earliest Visit:   %s\n", stats->earliest_visit);
    printf("  Latest Visit:     %s\n", stats->latest_visit);
    printf("  ─────────────────────────────────────────\n");
    printf("  Most Visited:     %s\n", stats->most_visited_title[0] ? stats->most_visited_title : "(untitled)");
    printf("    URL:            %s\n", stats->most_visited_url);
    printf("    Visits:         %d\n", stats->most_visited_count);
    printf("  ─────────────────────────────────────────\n\n");

    /* Show top 10 entries */
    int show = result->count < 10 ? result->count : 10;
    if (show > 0) {
        printf("  ── Top %d Recent Entries ─────────────────\n\n", show);
        for (int i = 0; i < show; i++) {
            const HistoryEntry *e = &result->entries[i];
            printf("  %2d. %s\n", i + 1,
                   e->title[0] ? e->title : "(untitled)");
            printf("      %s\n", e->url);
            printf("      Visits: %d  |  Last: %s\n\n",
                   e->visit_count, e->last_visit_time);
        }
    }
}

/* ── Main ────────────────────────────────────────────────────────── */

#define MAX_IMPORTS 16

int main(int argc, char *argv[]) {
    const char *json_path = NULL;
    const char *pdf_path  = NULL;
    const char *db_path   = NULL;
    const char *import_paths[MAX_IMPORTS];
    int import_count = 0;
    int days_back = 0;
    int skip_native_db = 0;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_banner();
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("brave-history v%s\n", VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            json_path = argv[++i];
        } else if (strcmp(argv[i], "--pdf") == 0 && i + 1 < argc) {
            pdf_path = argv[++i];
        } else if (strcmp(argv[i], "--days") == 0 && i + 1 < argc) {
            days_back = atoi(argv[++i]);
            if (days_back < 0) days_back = 0;
        } else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        } else if (strcmp(argv[i], "--import") == 0 && i + 1 < argc) {
            if (import_count < MAX_IMPORTS) {
                import_paths[import_count++] = argv[++i];
            } else {
                fprintf(stderr, "  WARNING: Too many --import files (max %d)\n", MAX_IMPORTS);
                i++;
            }
        } else if (strcmp(argv[i], "--no-db") == 0) {
            skip_native_db = 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    print_banner();

    /* Detect platform */
    Platform plat = platform_detect();
    printf("  Platform: %s\n", platform_name(plat));

    HistoryResult result;
    history_result_init(&result);

    int have_native = 0;
    char temp_path[1024] = {0};

    /* ── Native Brave DB ─────────────────────────────────────────── */

    if (!skip_native_db) {
        char brave_path[1024];
        int using_custom_db = 0;

        if (db_path) {
            strncpy(brave_path, db_path, sizeof(brave_path) - 1);
            brave_path[sizeof(brave_path) - 1] = '\0';

            if (!platform_file_exists(brave_path)) {
                fprintf(stderr, "\n  ERROR: Specified database not found: %s\n\n", brave_path);
                if (import_count == 0) return 1;
                printf("  Continuing with import-only mode...\n");
                goto skip_db;
            }
            printf("  Database: %s (user-specified)\n", brave_path);
            using_custom_db = 1;
        } else {
            if (platform_get_brave_history_path(brave_path, sizeof(brave_path)) != 0) {
                if (import_count > 0) {
                    printf("  Brave DB not found — using import-only mode\n");
                    goto skip_db;
                }
                fprintf(stderr, "\n  ERROR: Brave Browser history database not found.\n");
                fprintf(stderr, "  Make sure Brave is installed, or use --import / --db.\n\n");
                return 1;
            }
            printf("  Database: %s\n", brave_path);
        }

        /* Copy the database (Brave locks it while running) */
        if (using_custom_db) {
            strncpy(temp_path, brave_path, sizeof(temp_path) - 1);
        } else {
            if (platform_get_temp_db_path(temp_path, sizeof(temp_path)) != 0) {
                fprintf(stderr, "\n  ERROR: Cannot determine temp path\n\n");
                if (import_count == 0) return 1;
                goto skip_db;
            }

            printf("  Copying database to: %s\n", temp_path);
            if (platform_copy_file(brave_path, temp_path) != 0) {
                fprintf(stderr, "  WARNING: Cannot copy history database (Brave may be running)\n");
                if (import_count == 0) {
                    fprintf(stderr, "  Try closing Brave, or use --db / --import.\n\n");
                    return 1;
                }
                printf("  Continuing with import-only mode...\n");
                temp_path[0] = '\0';
                goto skip_db;
            }
        }

        printf("  Days back: %s\n", days_back > 0 ? "" : "all");
        if (days_back > 0) printf("  %d days\n", days_back);

        printf("\n  Querying native database...\n");
        if (history_db_query(temp_path, days_back, &result) != 0) {
            fprintf(stderr, "  WARNING: Failed to query native database\n");
            if (import_count == 0) {
                if (!using_custom_db && temp_path[0]) platform_delete_file(temp_path);
                return 1;
            }
        } else {
            have_native = 1;
            printf("  Native DB: %d entries\n", result.count);
        }
    }

skip_db:

    /* ── Import extension data ───────────────────────────────────── */

    if (import_count > 0) {
        printf("\n  Importing extension data...\n");
        for (int i = 0; i < import_count; i++) {
            printf("  [%d/%d] %s\n", i + 1, import_count, import_paths[i]);

            if (!platform_file_exists(import_paths[i])) {
                fprintf(stderr, "    WARNING: File not found, skipping\n");
                continue;
            }

            int before = result.count;
            if (import_extension_json(import_paths[i], &result) == 0) {
                printf("    Added %d entries\n", result.count - before);
            } else {
                fprintf(stderr, "    WARNING: Import failed\n");
            }
        }
    }

    /* ── Check we have data ──────────────────────────────────────── */

    if (result.count == 0) {
        printf("\n  No history entries found.\n\n");
        history_result_free(&result);
        if (!skip_native_db && temp_path[0]) platform_delete_file(temp_path);
        return 0;
    }

    /* Compute stats */
    HistoryStats stats;
    history_compute_stats(&result, &stats);

    /* AI-powered browsing analysis */
    BrowsingAnalysis analysis;
    analyze_browsing(&result, &analysis);

    /* Print summary to console */
    print_summary(&result, &stats);

    /* Print AI insights */
    printf("  ── AI Insights ──────────────────────────\n");
    printf("  %s\n", analysis.insight_primary);
    printf("  %s\n", analysis.insight_focus);
    printf("  %s\n", analysis.insight_habit);
    printf("  %s\n", analysis.insight_recommendation);
    printf("  ─────────────────────────────────────────\n");
    printf("\n  ── Category Breakdown ────────────────────\n");
    for (int c = 0; c < CAT_COUNT; c++) {
        if (analysis.cats[c].count > 0) {
            printf("  %-16s %4d URLs  %5d visits\n",
                   category_name((Category)c),
                   analysis.cats[c].count,
                   analysis.cats[c].visits);
        }
    }
    printf("  ─────────────────────────────────────────\n\n");

    /* Export JSON if requested */
    if (json_path) {
        printf("  Exporting JSON...\n");
        if (export_to_json(&result, &stats, json_path) != 0) {
            fprintf(stderr, "  WARNING: JSON export failed\n");
        }
    }

    /* Export PDF if requested */
    if (pdf_path) {
        printf("  Generating PDF with AI analysis...\n");
        if (export_to_pdf_analyzed(&result, &stats, &analysis, pdf_path) != 0) {
            fprintf(stderr, "  WARNING: PDF generation failed\n");
        }
    }

    /* If no export requested, suggest usage */
    if (!json_path && !pdf_path) {
        printf("  TIP: Use --json <file> and/or --pdf <file> to export results.\n\n");
    }

    /* Cleanup */
    history_result_free(&result);
    if (!skip_native_db && temp_path[0]) {
        platform_delete_file(temp_path);
    }

    char ts[64];
    platform_timestamp_iso8601(ts, sizeof(ts));
    printf("  Completed at: %s\n\n", ts);

    return 0;
}
