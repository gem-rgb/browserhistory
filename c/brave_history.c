/**
 * brave_history.c — Main entry point for ViewBrowserHistory (C Edition)
 *
 * A native C program that reads browser history databases directly from
 * disk. Supports Brave, Chrome, Chromium, Edge, Vivaldi, Opera, and
 * Firefox with automatic detection and profile enumeration.
 *
 * Features:
 *   - Multi-browser detection and querying
 *   - Extension JSON / CSV / HTML bookmark import
 *   - URL categorization with configurable rules
 *   - Full-text search with query language
 *   - Session analysis and browsing pattern detection
 *   - Multiple export formats (JSON, PDF, CSV, HTML, Markdown)
 *   - Structured logging with color support
 *
 * Usage:
 *   viewbrowserhistory                                  # auto-detect + summary
 *   viewbrowserhistory --browser brave --days 7         # Brave, last 7 days
 *   viewbrowserhistory --all-browsers --csv out.csv     # all browsers → CSV
 *   viewbrowserhistory --search "domain:github.com"     # search history
 *   viewbrowserhistory --import bookmarks.html --html r.html  # import + HTML
 *   viewbrowserhistory --categorize --markdown stats.md # categorize + export
 *
 * @version 3.0.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "history_db.h"
#include "export_json.h"
#include "export_pdf.h"
#include "export_csv.h"
#include "export_html_report.h"
#include "export_markdown.h"
#include "import_ext.h"
#include "categorize.h"
#include "url_parser.h"
#include "csv_import.h"
#include "html_bookmark_import.h"
#include "session_tracker.h"
#include "domain_trie.h"
#include "config_parser.h"
#include "url_categorize.h"
#include "search_engine.h"
#include "browser_detect.h"
#include "logging.h"

/* ── Version ─────────────────────────────────────────────────────── */

#define VERSION "3.0.0"

/* ── Usage / Help ────────────────────────────────────────────────── */

static void print_banner(void) {
    printf("\n");
    printf("  ╔══════════════════════════════════════════╗\n");
    printf("  ║  VIEWBROWSERHISTORY — C Edition v%s  ║\n", VERSION);
    printf("  ║  Multi-browser history analysis toolkit  ║\n");
    printf("  ╚══════════════════════════════════════════╝\n\n");
}

static void print_usage(const char *progname) {
    printf("Usage: %s [OPTIONS]\n\n", progname);

    printf("  Data Sources:\n");
    printf("    --browser <name>   Query specific browser (brave|chrome|firefox|edge|...)\n");
    printf("    --all-browsers     Auto-detect and query all installed browsers\n");
    printf("    --db <file>        Use a specific History SQLite database file\n");
    printf("    --import <file>    Import extension JSON, CSV, or HTML bookmarks\n");
    printf("    --no-db            Skip native DB, use only imported data\n");
    printf("    --days <N>         Only include last N days (default: all)\n\n");

    printf("  Search & Filter:\n");
    printf("    --search <query>   Search with mini query language\n");
    printf("                       Supports: regex:\"...\", domain:..., tld:...,\n");
    printf("                       after:..., before:..., visits:N, AND, OR, NOT\n\n");

    printf("  Analysis:\n");
    printf("    --categorize       Categorize URLs into browsing categories\n");
    printf("    --rules <file>     Custom categorization rules file\n");
    printf("    --sessions         Analyze browsing sessions\n");
    printf("    --domains          Show domain aggregation analytics\n\n");

    printf("  Export Formats:\n");
    printf("    --json <file>      Export to JSON\n");
    printf("    --pdf  <file>      Generate styled PDF report\n");
    printf("    --csv  <file>      Export to CSV\n");
    printf("    --html <file>      Generate interactive HTML report\n");
    printf("    --markdown <file>  Export to Markdown\n\n");

    printf("  Configuration:\n");
    printf("    --config <file>    Load configuration file\n");
    printf("    --verbose          Enable debug logging\n");
    printf("    --quiet            Suppress console output\n");
    printf("    --log <file>       Log to file\n");
    printf("    --help             Show this help message\n");
    printf("    --version          Show version\n\n");

    printf("  Examples:\n");
    printf("    %s --all-browsers --categorize --html report.html\n", progname);
    printf("    %s --browser firefox --days 30 --csv history.csv\n", progname);
    printf("    %s --search \"domain:github.com regex:\\\"pull/[0-9]+\\\"\" --json results.json\n", progname);
    printf("    %s --import bookmarks.html --import tracked.json --pdf combined.pdf\n", progname);
    printf("    %s --all-browsers --sessions --domains --markdown analysis.md\n\n", progname);
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
    printf("  Most Visited:     %s\n",
           stats->most_visited_title[0] ? stats->most_visited_title : "(untitled)");
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

/* ── Print category breakdown ────────────────────────────────────── */

static void print_categories(const CatEngine *engine) {
    printf("  ── Category Breakdown ────────────────────\n\n");
    for (int i = 0; i <= engine->category_count; i++) {
        if (engine->stats[i].url_count == 0) continue;
        printf("  %-18s %5d URLs  %6d visits",
               engine->stats[i].name,
               engine->stats[i].url_count,
               engine->stats[i].total_visits);
        if (engine->stats[i].top_domain[0]) {
            printf("  (top: %s)", engine->stats[i].top_domain);
        }
        printf("\n");
    }
    printf("\n  Categorized: %d  |  Uncategorized: %d\n",
           engine->total_categorized, engine->total_uncategorized);
    printf("  ─────────────────────────────────────────\n\n");
}

/* ── Print search results ────────────────────────────────────────── */

static void print_search_results(const SearchResults *results,
                                  const HistoryResult *history) {
    printf("\n  ── Search Results (%d hits, %.1f ms) ──────\n\n",
           results->hit_count, results->elapsed_ms);

    int show = results->hit_count < 20 ? results->hit_count : 20;
    for (int i = 0; i < show; i++) {
        const SearchHit *hit = &results->hits[i];
        const HistoryEntry *e = &history->entries[hit->entry_idx];

        printf("  %2d. [%.2f] %s\n", i + 1, hit->score,
               e->title[0] ? e->title : "(untitled)");
        printf("      %s\n", e->url);
        printf("      Visits: %d  |  Last: %s", e->visit_count, e->last_visit_time);
        if (hit->url_matched) printf("  [URL match]");
        if (hit->title_matched) printf("  [Title match]");
        printf("\n\n");
    }

    if (results->hit_count > 20) {
        printf("  ... and %d more results\n\n", results->hit_count - 20);
    }
}

/* ── Print session analysis ──────────────────────────────────────── */

static void print_sessions(const SessionTracker *tracker) {
    printf("  ── Session Analysis ─────────────────────\n\n");
    printf("  Total Sessions:   %d\n", tracker->session_count);
    printf("  Current State:    %s\n", session_state_name(tracker->current_state));

    if (tracker->session_count > 0) {
        printf("\n  Recent Sessions:\n");
        int show = tracker->session_count < 5 ? tracker->session_count : 5;
        for (int i = 0; i < show; i++) {
            const Session *s = tracker->sessions[i];
            if (!s) continue;
            printf("    Session %d: %d tabs, %d pages, %.1f min\n",
                   i + 1, s->tab_count, s->page_count, s->duration_minutes);
        }
    }
    printf("  ─────────────────────────────────────────\n\n");
}

/* ── Print domain analytics ──────────────────────────────────────── */

static void print_domains(const HistoryResult *result) {
    DomainTrie *trie = trie_create();
    if (!trie) return;

    for (int i = 0; i < result->count; i++) {
        const HistoryEntry *e = &result->entries[i];

        ParsedUrl parsed;
        if (url_parse(e->url, strlen(e->url), &parsed) == 0) {
            DomainInfo dinfo;
            url_extract_domain(&parsed, &dinfo);
            if (dinfo.registrable_domain[0]) {
                trie_insert(trie, dinfo.registrable_domain,
                           e->visit_count, 1);
            }
        }
    }

    printf("  ── Domain Analytics ─────────────────────\n\n");
    printf("  Unique Domains:  %d\n\n", trie_unique_count(trie));

    TopKEntry topk[15];
    int found = trie_top_k(trie, topk, 15);

    printf("  %-30s  %8s  %8s\n", "Domain", "Visits", "URLs");
    printf("  %-30s  %8s  %8s\n", "──────", "──────", "────");
    for (int i = 0; i < found; i++) {
        printf("  %-30s  %8d  %8d\n",
               topk[i].domain, topk[i].visit_count, topk[i].url_count);
    }
    printf("  ─────────────────────────────────────────\n\n");

    trie_destroy(trie);
}

/* ── Import file by auto-detecting format ────────────────────────── */

static int auto_import(const char *filepath, HistoryResult *result) {
    if (!filepath || !result) return -1;

    size_t len = strlen(filepath);
    int before = result->count;

    /* Detect format by extension */
    if (len > 5 && (strcasecmp(filepath + len - 5, ".html") == 0 ||
                    strcasecmp(filepath + len - 4, ".htm") == 0)) {
        /* HTML bookmark import */
        int n = import_bookmarks_html(filepath, result, NULL);
        if (n >= 0) {
            printf("    [HTML] Added %d bookmark entries\n", n);
            return 0;
        }
    }

    if (len > 4 && (strcasecmp(filepath + len - 4, ".csv") == 0 ||
                    strcasecmp(filepath + len - 4, ".tsv") == 0)) {
        /* CSV import */
        CsvImportOptions opts;
        memset(&opts, 0, sizeof(opts));
        opts.delimiter = CSV_DELIM_AUTO;
        opts.date_format = CSV_DATE_AUTO;
        opts.has_header = -1;

        int n = import_csv(filepath, result, &opts);
        if (n >= 0) {
            printf("    [CSV]  Added %d entries\n", n);
            return 0;
        }
    }

    /* Default: try JSON import */
    int rc = import_extension_json(filepath, result);
    if (rc == 0) {
        printf("    [JSON] Added %d entries\n", result->count - before);
        return 0;
    }

    /* Fallback: try HTML anyway (some exports don't have .html extension) */
    int n = import_bookmarks_html(filepath, result, NULL);
    if (n >= 0) {
        printf("    [HTML] Added %d entries (fallback)\n", n);
        return 0;
    }

    fprintf(stderr, "    WARNING: Could not determine format for %s\n", filepath);
    return -1;
}

/* ── Resolve browser type from string ────────────────────────────── */

static BrowserType parse_browser_name(const char *name) {
    if (!name) return BROWSER_UNKNOWN;
    if (strcasecmp(name, "brave") == 0) return BROWSER_BRAVE;
    if (strcasecmp(name, "chrome") == 0) return BROWSER_CHROME;
    if (strcasecmp(name, "chromium") == 0) return BROWSER_CHROMIUM;
    if (strcasecmp(name, "edge") == 0) return BROWSER_EDGE;
    if (strcasecmp(name, "vivaldi") == 0) return BROWSER_VIVALDI;
    if (strcasecmp(name, "opera") == 0) return BROWSER_OPERA;
    if (strcasecmp(name, "firefox") == 0) return BROWSER_FIREFOX;
    return BROWSER_UNKNOWN;
}

/* ── Platform compat ─────────────────────────────────────────────── */

#ifdef _WIN32
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#endif

/* ── Main ────────────────────────────────────────────────────────── */

#define MAX_IMPORTS 16

int main(int argc, char *argv[]) {
    /* Output paths */
    const char *json_path     = NULL;
    const char *pdf_path      = NULL;
    const char *csv_path      = NULL;
    const char *html_path     = NULL;
    const char *markdown_path = NULL;

    /* Input options */
    const char *db_path = NULL;
    const char *import_paths[MAX_IMPORTS];
    int import_count = 0;
    int days_back = 0;
    int skip_native_db = 0;

    /* Browser selection */
    const char *browser_name = NULL;
    int all_browsers = 0;

    /* Analysis flags */
    int do_categorize = 0;
    int do_sessions = 0;
    int do_domains = 0;
    const char *rules_path = NULL;
    const char *search_query = NULL;

    /* Configuration */
    const char *config_path = NULL;
    const char *log_path = NULL;
    int verbose = 0;
    int quiet = 0;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_banner();
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("viewbrowserhistory v%s\n", VERSION);
            return 0;
        }

        /* Data sources */
        if (strcmp(argv[i], "--browser") == 0 && i + 1 < argc) {
            browser_name = argv[++i];
        } else if (strcmp(argv[i], "--all-browsers") == 0) {
            all_browsers = 1;
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
        } else if (strcmp(argv[i], "--days") == 0 && i + 1 < argc) {
            days_back = atoi(argv[++i]);
            if (days_back < 0) days_back = 0;
        }

        /* Export formats */
        else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            json_path = argv[++i];
        } else if (strcmp(argv[i], "--pdf") == 0 && i + 1 < argc) {
            pdf_path = argv[++i];
        } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (strcmp(argv[i], "--html") == 0 && i + 1 < argc) {
            html_path = argv[++i];
        } else if (strcmp(argv[i], "--markdown") == 0 && i + 1 < argc) {
            markdown_path = argv[++i];
        }

        /* Analysis */
        else if (strcmp(argv[i], "--categorize") == 0) {
            do_categorize = 1;
        } else if (strcmp(argv[i], "--rules") == 0 && i + 1 < argc) {
            rules_path = argv[++i];
            do_categorize = 1;
        } else if (strcmp(argv[i], "--sessions") == 0) {
            do_sessions = 1;
        } else if (strcmp(argv[i], "--domains") == 0) {
            do_domains = 1;
        } else if (strcmp(argv[i], "--search") == 0 && i + 1 < argc) {
            search_query = argv[++i];
        }

        /* Configuration */
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
        }

        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* ── Initialize logging ── */
    log_init(verbose ? LOG_DEBUG : LOG_INFO, !quiet);
    if (quiet) log_set_quiet(1);
    if (log_path) log_set_file(log_path);

    if (!quiet) print_banner();

    /* ── Load configuration file ── */
    Config config;
    int config_loaded = 0;
    if (config_path) {
        if (config_parse(config_path, &config) == 0) {
            config_loaded = 1;
            LOG_INFO("Loaded config from %s (%d entries)",
                     config_path, config.entry_count);

            /* Apply config values as defaults (CLI flags take precedence) */
            if (days_back == 0) {
                days_back = config_get_int(&config, "general", "default_days", 0);
            }
            if (!rules_path) {
                const char *cfg_rules = config_get_string(&config, "categories",
                                                           "config_file", NULL);
                if (cfg_rules) {
                    rules_path = cfg_rules;
                    do_categorize = 1;
                }
            }
        } else {
            LOG_WARN("Failed to load config: %s (%s)", config_path, config.error_msg);
        }
    }

    /* Detect platform */
    Platform plat = platform_detect();
    LOG_INFO("Platform: %s", platform_name(plat));

    HistoryResult result;
    history_result_init(&result);

    /* ── Query browsers ── */

    if (!skip_native_db) {
        if (all_browsers) {
            /* Auto-detect and query all browsers */
            printf("  Scanning for installed browsers...\n");
            BrowserScanResult scan;
            int found = browser_scan(&scan);

            if (found > 0) {
                printf("  Found %d browser(s) with %d profile(s)\n",
                       scan.count, scan.total_profiles);

                int n = browser_query_all(&scan, days_back, &result);
                if (n > 0) {
                    printf("  Queried %d total entries\n", n);

                    /* Deduplicate across browsers */
                    int dups = browser_deduplicate(&result);
                    if (dups > 0) {
                        printf("  Removed %d duplicates → %d unique entries\n",
                               dups, result.count);
                    }
                }
            } else {
                printf("  No browsers detected\n");
                if (import_count == 0) {
                    printf("  Use --import to load data from files\n\n");
                }
            }
        } else if (browser_name) {
            /* Query specific browser */
            BrowserType btype = parse_browser_name(browser_name);
            if (btype == BROWSER_UNKNOWN) {
                fprintf(stderr, "  ERROR: Unknown browser '%s'\n", browser_name);
                fprintf(stderr, "  Supported: brave, chrome, chromium, edge, vivaldi, opera, firefox\n\n");
                log_shutdown();
                return 1;
            }

            printf("  Querying %s...\n", browser_type_name(btype));
            int n = browser_query_by_type(btype, days_back, &result);
            if (n > 0) {
                printf("  Found %d entries from %s\n", n, browser_type_name(btype));
            } else {
                printf("  No entries found for %s\n", browser_type_name(btype));
            }
        } else if (db_path) {
            /* Use specific database file */
            printf("  Using database: %s\n", db_path);
            if (history_db_query(db_path, days_back, &result) != 0) {
                fprintf(stderr, "  ERROR: Failed to query database\n");
                if (import_count == 0) {
                    log_shutdown();
                    return 1;
                }
            } else {
                printf("  Found %d entries\n", result.count);
            }
        } else {
            /* Default: query Brave (original behavior) */
            char brave_path[1024];
            char temp_path[1024] = {0};

            if (platform_get_brave_history_path(brave_path, sizeof(brave_path)) == 0) {
                if (platform_get_temp_db_path(temp_path, sizeof(temp_path)) == 0 &&
                    platform_copy_file(brave_path, temp_path) == 0) {

                    printf("  Querying Brave history...\n");
                    if (history_db_query(temp_path, days_back, &result) == 0) {
                        printf("  Found %d entries from Brave\n", result.count);
                    }
                    platform_delete_file(temp_path);
                }
            } else if (import_count == 0) {
                printf("  Brave not found. Use --browser, --all-browsers, or --import\n\n");
            }
        }
    }

    /* ── Import data ── */

    if (import_count > 0) {
        printf("\n  Importing data...\n");
        for (int i = 0; i < import_count; i++) {
            printf("  [%d/%d] %s\n", i + 1, import_count, import_paths[i]);

            if (!platform_file_exists(import_paths[i])) {
                fprintf(stderr, "    WARNING: File not found, skipping\n");
                continue;
            }

            auto_import(import_paths[i], &result);
        }
    }

    /* ── Check we have data ── */

    if (result.count == 0) {
        printf("\n  No history entries found.\n\n");
        history_result_free(&result);
        log_shutdown();
        return 0;
    }

    printf("\n  Total entries: %d\n", result.count);

    /* ── Compute statistics ── */

    HistoryStats stats;
    history_compute_stats(&result, &stats);

    /* ── URL Categorization ── */

    CatEngine cat_engine;
    int cat_engine_ready = 0;

    if (do_categorize || html_path || markdown_path) {
        cat_engine_init_defaults(&cat_engine);

        if (rules_path) {
            if (cat_engine_load_rules(&cat_engine, rules_path) == 0) {
                LOG_INFO("Loaded rules from %s", rules_path);
            } else {
                LOG_WARN("Failed to load rules from %s", rules_path);
            }
        }

        int categorized = cat_categorize_all(&cat_engine, &result);
        LOG_INFO("Categorized %d of %d entries", categorized, result.count);
        cat_engine_ready = 1;

        if (!quiet && do_categorize) {
            print_categories(&cat_engine);
        }
    }

    /* ── Search ── */

    if (search_query) {
        SearchQuery query;
        if (search_parse_query(search_query, &query) == 0) {
            SearchResults results;
            int hits = search_execute(&query, &result, &results);
            LOG_INFO("Search found %d hits", hits);

            if (!quiet) {
                print_search_results(&results, &result);
            }
        } else {
            fprintf(stderr, "  ERROR: Invalid search query\n");
        }
    }

    /* ── Session Analysis ── */

    SessionTracker *tracker = NULL;
    if (do_sessions) {
        tracker = session_tracker_create();
        if (tracker) {
            session_tracker_analyze(tracker, &result);
            session_tracker_compute_stats(tracker);

            if (!quiet) {
                print_sessions(tracker);
            }
        }
    }

    /* ── Domain Analytics ── */

    if (do_domains && !quiet) {
        print_domains(&result);
    }

    /* ── Print summary ── */

    if (!quiet && !search_query) {
        print_summary(&result, &stats);

        /* Legacy AI insights */
        BrowsingAnalysis analysis;
        analyze_browsing(&result, &analysis);

        printf("  ── Insights ─────────────────────────────\n");
        printf("  %s\n", analysis.insight_primary);
        printf("  %s\n", analysis.insight_focus);
        printf("  %s\n", analysis.insight_habit);
        printf("  %s\n", analysis.insight_recommendation);
        printf("  ─────────────────────────────────────────\n\n");
    }

    /* ── Exports ── */

    if (json_path) {
        printf("  Exporting JSON → %s\n", json_path);
        if (export_to_json(&result, &stats, json_path) != 0) {
            LOG_ERROR("JSON export failed");
        }
    }

    if (pdf_path) {
        printf("  Generating PDF → %s\n", pdf_path);
        BrowsingAnalysis analysis;
        analyze_browsing(&result, &analysis);
        if (export_to_pdf_analyzed(&result, &stats, &analysis, pdf_path) != 0) {
            LOG_ERROR("PDF generation failed");
        }
    }

    if (csv_path) {
        printf("  Exporting CSV → %s\n", csv_path);
        if (export_csv(csv_path, &result, 1) != 0) {
            LOG_ERROR("CSV export failed");
        }
    }

    if (html_path) {
        printf("  Generating HTML report → %s\n", html_path);
        if (export_html_report(html_path, &result, &stats,
                               cat_engine_ready ? &cat_engine : NULL) != 0) {
            LOG_ERROR("HTML report generation failed");
        }
    }

    if (markdown_path) {
        printf("  Exporting Markdown → %s\n", markdown_path);
        if (export_markdown(markdown_path, &result, &stats,
                            cat_engine_ready ? &cat_engine : NULL) != 0) {
            LOG_ERROR("Markdown export failed");
        }
    }

    /* Suggest usage if no export requested */
    if (!json_path && !pdf_path && !csv_path && !html_path && !markdown_path && !quiet) {
        printf("  TIP: Use --json, --csv, --html, --pdf, or --markdown to export.\n\n");
    }

    /* ── Cleanup ── */

    if (tracker) session_tracker_destroy(tracker);
    history_result_free(&result);

    char ts[64];
    platform_timestamp_iso8601(ts, sizeof(ts));
    if (!quiet) printf("  Completed at: %s\n\n", ts);

    log_shutdown();
    return 0;
}
