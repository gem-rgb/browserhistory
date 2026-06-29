/**
 * search_engine.c — Full-text search engine for browser history
 *
 * Provides a mini query language parser and execution engine for
 * searching through history entries with:
 *   - Substring and keyword matching
 *   - POSIX regex support
 *   - Domain and TLD filtering
 *   - Date range filtering
 *   - Visit count thresholds
 *   - Boolean combinators (AND, OR, NOT)
 *   - Relevance-based result ranking
 *
 * Query language examples:
 *   "javascript"                       → substring search
 *   regex:"pull/[0-9]+"               → regex search
 *   domain:github.com                  → domain filter
 *   tld:org                            → TLD filter
 *   after:2024-01-01 before:2024-12-31 → date range
 *   visits:5                           → min visits
 *   -"excluded term"                   → NOT
 *   term1 AND term2                    → boolean AND
 *   term1 OR term2                     → boolean OR
 *
 * @version 1.0.0
 */

#include "search_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifndef _WIN32
#include <regex.h>
#endif

/* ── Initialization ──────────────────────────────────────────────── */

void search_query_init(SearchQuery *query) {
    if (!query) return;
    memset(query, 0, sizeof(*query));
    query->max_results = SEARCH_MAX_RESULTS;
    query->rank_by_visits = 1;
    query->rank_by_recency = 1;
    query->rank_by_relevance = 1;
}

/* ── Case-insensitive substring search ───────────────────────────── */

const char *search_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;

    size_t nlen = strlen(needle);

    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            const char *h = haystack;
            const char *n = needle;
            size_t i = 0;
            while (i < nlen && *h &&
                   tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
                h++;
                n++;
                i++;
            }
            if (i == nlen) return haystack;
        }
    }

    return NULL;
}

/* ── Regex matching ──────────────────────────────────────────────── */

int search_regex_match(const char *pattern, const char *str,
                       int case_sensitive) {
    if (!pattern || !str) return -1;

#ifdef _WIN32
    /* Simple fallback for Windows: substring match */
    if (case_sensitive) {
        return strstr(str, pattern) != NULL ? 1 : 0;
    } else {
        return search_strcasestr(str, pattern) != NULL ? 1 : 0;
    }
#else
    regex_t regex;
    int flags = REG_EXTENDED | REG_NOSUB;
    if (!case_sensitive) flags |= REG_ICASE;

    int rc = regcomp(&regex, pattern, flags);
    if (rc != 0) return -1;  /* compile error */

    rc = regexec(&regex, str, 0, NULL, 0);
    regfree(&regex);

    return (rc == 0) ? 1 : 0;
#endif
}

/* ── Query parser ────────────────────────────────────────────────── */

/**
 * Parse a single token from the query string.
 * Advances *pos past the parsed token.
 */
static int parse_token(const char *query, size_t *pos, size_t len,
                        SearchTerm *term) {
    memset(term, 0, sizeof(*term));
    term->match_url = 1;
    term->match_title = 1;

    while (*pos < len && isspace((unsigned char)query[*pos])) (*pos)++;
    if (*pos >= len) return -1;

    const char *p = query + *pos;

    /* Check for negation prefix */
    if (*p == '-') {
        term->combinator = SEARCH_OP_NOT;
        p++;
        (*pos)++;
        if (*pos >= len) return -1;
    }

    /* Check for typed prefixes */
    if (strncasecmp(p, "regex:", 6) == 0) {
        term->type = SEARCH_TERM_REGEX;
        *pos += 6;
        p += 6;
    } else if (strncasecmp(p, "domain:", 7) == 0) {
        term->type = SEARCH_TERM_DOMAIN;
        *pos += 7;
        p += 7;
    } else if (strncasecmp(p, "tld:", 4) == 0) {
        term->type = SEARCH_TERM_TLD;
        *pos += 4;
        p += 4;
    } else if (strncasecmp(p, "after:", 6) == 0) {
        /* Date filter — not a search term, modify query */
        *pos += 6;
        return -2;  /* special: date_after */
    } else if (strncasecmp(p, "before:", 7) == 0) {
        *pos += 7;
        return -3;  /* special: date_before */
    } else if (strncasecmp(p, "visits:", 7) == 0) {
        *pos += 7;
        return -4;  /* special: min_visits */
    } else {
        term->type = SEARCH_TERM_SUBSTRING;
    }

    /* Extract the value (quoted or unquoted) */
    if (*pos < len && query[*pos] == '"') {
        /* Quoted string */
        (*pos)++;
        size_t start = *pos;
        while (*pos < len && query[*pos] != '"') (*pos)++;

        size_t vlen = *pos - start;
        if (vlen >= sizeof(term->pattern)) vlen = sizeof(term->pattern) - 1;
        memcpy(term->pattern, query + start, vlen);
        term->pattern[vlen] = '\0';

        if (*pos < len && query[*pos] == '"') (*pos)++;
    } else {
        /* Unquoted — read until whitespace */
        size_t start = *pos;
        while (*pos < len && !isspace((unsigned char)query[*pos])) (*pos)++;

        size_t vlen = *pos - start;
        if (vlen >= sizeof(term->pattern)) vlen = sizeof(term->pattern) - 1;
        memcpy(term->pattern, query + start, vlen);
        term->pattern[vlen] = '\0';
    }

    return 0;
}

int search_parse_query(const char *query_str, SearchQuery *query) {
    if (!query_str || !query) return -1;

    search_query_init(query);

    size_t len = strlen(query_str);
    size_t pos = 0;

    SearchOperator next_op = SEARCH_OP_AND;

    while (pos < len && query->term_count < SEARCH_MAX_TERMS) {
        /* Skip whitespace */
        while (pos < len && isspace((unsigned char)query_str[pos])) pos++;
        if (pos >= len) break;

        /* Check for boolean operators */
        if (strncasecmp(query_str + pos, "AND", 3) == 0 &&
            (pos + 3 >= len || isspace((unsigned char)query_str[pos + 3]))) {
            next_op = SEARCH_OP_AND;
            pos += 3;
            continue;
        }
        if (strncasecmp(query_str + pos, "OR", 2) == 0 &&
            (pos + 2 >= len || isspace((unsigned char)query_str[pos + 2]))) {
            next_op = SEARCH_OP_OR;
            pos += 2;
            continue;
        }
        if (strncasecmp(query_str + pos, "NOT", 3) == 0 &&
            (pos + 3 >= len || isspace((unsigned char)query_str[pos + 3]))) {
            next_op = SEARCH_OP_NOT;
            pos += 3;
            continue;
        }

        SearchTerm term;
        int rc = parse_token(query_str, &pos, len, &term);

        if (rc == -2) {
            /* date_after filter */
            size_t start = pos;
            while (pos < len && !isspace((unsigned char)query_str[pos])) pos++;
            size_t vlen = pos - start;
            if (vlen >= sizeof(query->date_after)) vlen = sizeof(query->date_after) - 1;
            memcpy(query->date_after, query_str + start, vlen);
            query->date_after[vlen] = '\0';
            continue;
        }
        if (rc == -3) {
            /* date_before filter */
            size_t start = pos;
            while (pos < len && !isspace((unsigned char)query_str[pos])) pos++;
            size_t vlen = pos - start;
            if (vlen >= sizeof(query->date_before)) vlen = sizeof(query->date_before) - 1;
            memcpy(query->date_before, query_str + start, vlen);
            query->date_before[vlen] = '\0';
            continue;
        }
        if (rc == -4) {
            /* min_visits filter */
            size_t start = pos;
            while (pos < len && !isspace((unsigned char)query_str[pos])) pos++;
            char num[16] = "";
            size_t nlen = pos - start;
            if (nlen >= sizeof(num)) nlen = sizeof(num) - 1;
            memcpy(num, query_str + start, nlen);
            num[nlen] = '\0';
            query->min_visits = atoi(num);
            continue;
        }
        if (rc < 0) continue;

        if (term.combinator != SEARCH_OP_NOT) {
            term.combinator = next_op;
        }
        next_op = SEARCH_OP_AND;  /* reset to default */

        query->terms[query->term_count++] = term;
    }

    return 0;
}

/* ── Relevance scoring ───────────────────────────────────────────── */

float search_compute_score(const HistoryEntry *entry, const SearchQuery *query,
                           int url_hit, int title_hit) {
    float score = 0.0f;

    if (query->rank_by_relevance) {
        if (url_hit && title_hit) score += 0.4f;
        else if (title_hit) score += 0.3f;
        else if (url_hit) score += 0.2f;
    }

    if (query->rank_by_visits && entry->visit_count > 0) {
        /* Logarithmic scaling: more visits → higher score, diminishing returns */
        float visit_score = 0.0f;
        int vc = entry->visit_count;
        if (vc >= 100) visit_score = 0.3f;
        else if (vc >= 50) visit_score = 0.25f;
        else if (vc >= 20) visit_score = 0.2f;
        else if (vc >= 10) visit_score = 0.15f;
        else if (vc >= 5) visit_score = 0.1f;
        else visit_score = 0.05f;
        score += visit_score;
    }

    if (query->rank_by_recency && entry->last_visit_raw > 0) {
        /* Recency: more recent → higher score */
        time_t now = time(NULL);
        long long raw = entry->last_visit_raw;

        /* Assume Chromium timestamp for normalization */
        long long unix_ts;
        if (raw > 1000000000000000LL) {
            /* Chromium microseconds */
            unix_ts = (raw / 1000000LL) - 11644473600LL;
        } else if (raw > 1000000000000LL) {
            /* Milliseconds */
            unix_ts = raw / 1000LL;
        } else {
            unix_ts = raw;
        }

        long long age_days = ((long long)now - unix_ts) / 86400LL;
        float recency_score = 0.0f;
        if (age_days <= 1) recency_score = 0.3f;
        else if (age_days <= 7) recency_score = 0.25f;
        else if (age_days <= 30) recency_score = 0.2f;
        else if (age_days <= 90) recency_score = 0.15f;
        else if (age_days <= 365) recency_score = 0.1f;
        else recency_score = 0.05f;
        score += recency_score;
    }

    if (score > 1.0f) score = 1.0f;
    return score;
}

/* ── Search execution ────────────────────────────────────────────── */

/**
 * Check if a single entry matches all date/visit filters.
 */
static int passes_filters(const HistoryEntry *entry, const SearchQuery *query) {
    /* Visit count filter */
    if (query->min_visits > 0 && entry->visit_count < query->min_visits) {
        return 0;
    }

    /* Date range filters — compare ISO 8601 strings lexicographically */
    if (query->date_after[0] && entry->last_visit_time[0]) {
        if (strcmp(entry->last_visit_time, query->date_after) < 0) {
            return 0;
        }
    }
    if (query->date_before[0] && entry->last_visit_time[0]) {
        if (strcmp(entry->last_visit_time, query->date_before) > 0) {
            return 0;
        }
    }

    /* Domain filter */
    if (query->domain_filter[0]) {
        /* Extract domain from URL */
        const char *p = strstr(entry->url, "://");
        if (p) p += 3; else p = entry->url;

        const char *host_end = p;
        while (*host_end && *host_end != '/' && *host_end != ':' &&
               *host_end != '?' && *host_end != '#') {
            host_end++;
        }

        size_t hlen = (size_t)(host_end - p);
        char host[256];
        if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';

        /* Case-insensitive compare */
        int found = 0;
        if (strcasecmp(host, query->domain_filter) == 0) {
            found = 1;
        } else {
            /* Check if host ends with .domain_filter */
            size_t flen = strlen(query->domain_filter);
            if (hlen > flen + 1) {
                if (host[hlen - flen - 1] == '.' &&
                    strcasecmp(host + hlen - flen, query->domain_filter) == 0) {
                    found = 1;
                }
            }
        }
        if (!found) return 0;
    }

    return 1;
}

/**
 * Test if a single search term matches an entry.
 */
static int term_matches(const SearchTerm *term, const HistoryEntry *entry,
                         int *url_hit, int *title_hit) {
    *url_hit = 0;
    *title_hit = 0;

    switch (term->type) {
        case SEARCH_TERM_SUBSTRING:
        case SEARCH_TERM_KEYWORD: {
            if (term->match_url && search_strcasestr(entry->url, term->pattern)) {
                *url_hit = 1;
            }
            if (term->match_title && entry->title[0] &&
                search_strcasestr(entry->title, term->pattern)) {
                *title_hit = 1;
            }
            return *url_hit || *title_hit;
        }

        case SEARCH_TERM_REGEX: {
            if (term->match_url) {
                int rc = search_regex_match(term->pattern, entry->url, 0);
                if (rc == 1) *url_hit = 1;
            }
            if (term->match_title && entry->title[0]) {
                int rc = search_regex_match(term->pattern, entry->title, 0);
                if (rc == 1) *title_hit = 1;
            }
            return *url_hit || *title_hit;
        }

        case SEARCH_TERM_DOMAIN: {
            /* Domain match in URL */
            const char *p = strstr(entry->url, "://");
            if (p) p += 3; else p = entry->url;

            const char *host_end = p;
            while (*host_end && *host_end != '/' && *host_end != ':') {
                host_end++;
            }

            size_t hlen = (size_t)(host_end - p);
            char host[256];
            if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
            memcpy(host, p, hlen);
            host[hlen] = '\0';

            size_t plen = strlen(term->pattern);
            if (strcasecmp(host, term->pattern) == 0) {
                *url_hit = 1;
                return 1;
            }
            if (hlen > plen + 1 && host[hlen - plen - 1] == '.' &&
                strcasecmp(host + hlen - plen, term->pattern) == 0) {
                *url_hit = 1;
                return 1;
            }
            return 0;
        }

        case SEARCH_TERM_TLD: {
            /* TLD match */
            const char *p = strstr(entry->url, "://");
            if (p) p += 3; else p = entry->url;

            const char *host_end = p;
            while (*host_end && *host_end != '/' && *host_end != ':') {
                host_end++;
            }

            /* Find last dot */
            const char *dot = NULL;
            for (const char *s = p; s < host_end; s++) {
                if (*s == '.') dot = s;
            }
            if (dot) {
                const char *tld = dot + 1;
                size_t tlen = (size_t)(host_end - tld);
                if (tlen == strlen(term->pattern) &&
                    strncasecmp(tld, term->pattern, tlen) == 0) {
                    *url_hit = 1;
                    return 1;
                }
            }
            return 0;
        }

        case SEARCH_TERM_EXCLUDE:
            return 0;

        default:
            return 0;
    }
}

int search_execute(const SearchQuery *query, const HistoryResult *history,
                   SearchResults *results) {
    if (!query || !history || !results) return -1;

    memset(results, 0, sizeof(*results));
    results->total_scanned = history->count;

    clock_t start = clock();

    for (int i = 0; i < history->count; i++) {
        const HistoryEntry *entry = &history->entries[i];

        /* Apply filters first (fast reject) */
        if (!passes_filters(entry, query)) continue;

        /* If no search terms, everything passes */
        if (query->term_count == 0) {
            if (results->hit_count >= query->max_results) break;

            SearchHit *hit = &results->hits[results->hit_count];
            hit->entry_idx = i;
            hit->score = search_compute_score(entry, query, 0, 0);
            hit->match_count = 0;
            results->hit_count++;
            continue;
        }

        /* Evaluate search terms with boolean logic */
        int overall_match = 0;
        int any_url_hit = 0;
        int any_title_hit = 0;
        int total_matches = 0;

        for (int t = 0; t < query->term_count; t++) {
            const SearchTerm *term = &query->terms[t];
            int url_hit = 0, title_hit = 0;

            int matched = term_matches(term, entry, &url_hit, &title_hit);

            /* Apply NOT combinator */
            if (term->combinator == SEARCH_OP_NOT) {
                matched = !matched;
            }

            /* Combine with previous result */
            if (t == 0) {
                overall_match = matched;
            } else {
                switch (term->combinator) {
                    case SEARCH_OP_AND:
                    case SEARCH_OP_NOT:
                        overall_match = overall_match && matched;
                        break;
                    case SEARCH_OP_OR:
                        overall_match = overall_match || matched;
                        break;
                }
            }

            if (url_hit) any_url_hit = 1;
            if (title_hit) any_title_hit = 1;
            if (matched) total_matches++;
        }

        if (overall_match) {
            if (results->hit_count >= query->max_results) break;

            SearchHit *hit = &results->hits[results->hit_count];
            hit->entry_idx = i;
            hit->match_count = total_matches;
            hit->url_matched = any_url_hit;
            hit->title_matched = any_title_hit;
            hit->score = search_compute_score(entry, query,
                                               any_url_hit, any_title_hit);
            results->hit_count++;
        }
    }

    /* Sort results by score (descending) — simple insertion sort */
    for (int i = 1; i < results->hit_count; i++) {
        SearchHit key = results->hits[i];
        int j = i - 1;
        while (j >= 0 && results->hits[j].score < key.score) {
            results->hits[j + 1] = results->hits[j];
            j--;
        }
        results->hits[j + 1] = key;
    }

    clock_t end = clock();
    results->elapsed_ms = (float)(end - start) * 1000.0f / (float)CLOCKS_PER_SEC;

    return results->hit_count;
}

/* ── Platform compat ─────────────────────────────────────────────── */

#ifdef _WIN32
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
#endif
