/**
 * search_engine.h — Full-text search engine for browser history
 *
 * Provides search across history entries with substring, regex,
 * domain filter, date range, visit count threshold, boolean
 * combinators, and result ranking.
 *
 * @version 1.0.0
 */

#ifndef SEARCH_ENGINE_H
#define SEARCH_ENGINE_H

#include "history_db.h"
#include <stddef.h>

/* ── Limits ──────────────────────────────────────────────────────── */

#define SEARCH_MAX_TERMS          16
#define SEARCH_MAX_PATTERN       512
#define SEARCH_MAX_DOMAIN        256
#define SEARCH_MAX_RESULTS      1000

/* ── Search term types ───────────────────────────────────────────── */

typedef enum {
    SEARCH_TERM_SUBSTRING = 0,   /* plain text match */
    SEARCH_TERM_REGEX,           /* POSIX regex */
    SEARCH_TERM_DOMAIN,          /* domain: filter */
    SEARCH_TERM_TLD,             /* tld: filter */
    SEARCH_TERM_KEYWORD,         /* title keyword */
    SEARCH_TERM_EXCLUDE          /* NOT term */
} SearchTermType;

/* ── Boolean combinators ─────────────────────────────────────────── */

typedef enum {
    SEARCH_OP_AND = 0,
    SEARCH_OP_OR,
    SEARCH_OP_NOT
} SearchOperator;

/* ── Single search term ──────────────────────────────────────────── */

typedef struct {
    SearchTermType  type;
    char            pattern[SEARCH_MAX_PATTERN];
    SearchOperator  combinator;     /* how to combine with previous term */
    int             case_sensitive;
    int             match_url;      /* search in URL */
    int             match_title;    /* search in title */
} SearchTerm;

/* ── Search query ────────────────────────────────────────────────── */

typedef struct {
    SearchTerm  terms[SEARCH_MAX_TERMS];
    int         term_count;

    /* Filters */
    char        domain_filter[SEARCH_MAX_DOMAIN];   /* restrict to domain */
    char        tld_filter[64];                     /* restrict to TLD */
    char        date_after[64];                     /* ISO 8601 */
    char        date_before[64];                    /* ISO 8601 */
    int         min_visits;                         /* minimum visit count */
    int         max_results;                        /* 0 = unlimited */

    /* Ranking */
    int         rank_by_visits;     /* weight visit count in score */
    int         rank_by_recency;    /* weight recency in score */
    int         rank_by_relevance;  /* weight match quality in score */
} SearchQuery;

/* ── Search result entry ─────────────────────────────────────────── */

typedef struct {
    int     entry_idx;          /* index into original HistoryResult */
    float   score;              /* relevance score (0.0 - 1.0) */
    int     match_count;        /* number of terms that matched */
    int     url_matched;        /* 1 if match was in URL */
    int     title_matched;      /* 1 if match was in title */
} SearchHit;

/* ── Search results ──────────────────────────────────────────────── */

typedef struct {
    SearchHit  hits[SEARCH_MAX_RESULTS];
    int        hit_count;
    int        total_scanned;
    float      elapsed_ms;
} SearchResults;

/* ── API ─────────────────────────────────────────────────────────── */

/**
 * Parse a search query string into a SearchQuery struct.
 * Supports a mini query language:
 *   "keyword"                    → substring search
 *   regex:"pattern"              → POSIX regex
 *   domain:github.com            → domain filter
 *   tld:org                      → TLD filter
 *   after:2024-01-01             → date range
 *   before:2024-06-30            → date range
 *   visits:5                     → min visits
 *   -"excluded term"             → NOT
 *   term1 AND term2              → boolean AND
 *   term1 OR term2               → boolean OR
 *
 * @param query_str  The raw query string
 * @param query      Output: parsed query
 * @return 0 on success, -1 on parse error
 */
int search_parse_query(const char *query_str, SearchQuery *query);

/**
 * Execute a search query against a history result set.
 *
 * @param query    The parsed search query
 * @param history  The history entries to search
 * @param results  Output: search results
 * @return Number of hits
 */
int search_execute(const SearchQuery *query, const HistoryResult *history,
                   SearchResults *results);

/**
 * Initialize a search query with default values.
 */
void search_query_init(SearchQuery *query);

/**
 * Case-insensitive substring search.
 *
 * @param haystack  String to search in
 * @param needle    String to search for
 * @return Pointer to match location, or NULL
 */
const char *search_strcasestr(const char *haystack, const char *needle);

/**
 * Match a string against a POSIX regex pattern.
 *
 * @param pattern  Regex pattern
 * @param str      String to match
 * @param case_sensitive  1 for case-sensitive
 * @return 1 if matches, 0 if not, -1 on regex compile error
 */
int search_regex_match(const char *pattern, const char *str,
                       int case_sensitive);

/**
 * Compute a relevance score for a match.
 *
 * @param entry     The history entry
 * @param query     The search query
 * @param url_hit   Whether the URL matched
 * @param title_hit Whether the title matched
 * @return Score from 0.0 to 1.0
 */
float search_compute_score(const HistoryEntry *entry, const SearchQuery *query,
                           int url_hit, int title_hit);

#endif /* SEARCH_ENGINE_H */
