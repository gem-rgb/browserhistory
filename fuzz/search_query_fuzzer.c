/**
 * search_query_fuzzer.c — Fuzz harness for search engine query parser
 *
 * Exercises the mini query language parser and search execution:
 *   - Substring and keyword parsing
 *   - Regex compilation and matching
 *   - Boolean combinators (AND, OR, NOT)
 *   - Filter directives (domain:, tld:, after:, before:, visits:)
 *   - Quoted string handling
 *   - Relevance scoring
 *   - Result sorting
 *
 * @version 1.0.0
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "search_engine.h"
#include "history_db.h"

/* Synthetic history data for search testing */
static HistoryEntry SYNTH_ENTRIES[] = {
    {"https://www.google.com/search?q=test", "test - Google Search", 50, 10, "2026-04-27T13:38:10Z", 13380000000000000LL},
    {"https://github.com/user/repo", "GitHub - user/repo", 30, 5, "2026-04-27T12:00:00Z", 13379900000000000LL},
    {"https://stackoverflow.com/questions/12345", "How to foo in bar?", 15, 2, "2026-04-26T18:30:00Z", 13379800000000000LL},
    {"https://en.wikipedia.org/wiki/Fuzzing", "Fuzzing - Wikipedia", 8, 3, "2026-04-25T14:00:00Z", 13379700000000000LL},
    {"https://www.reddit.com/r/programming/", "r/programming", 25, 0, "2026-04-27T10:00:00Z", 13379600000000000LL},
    {"https://news.ycombinator.com/", "Hacker News", 40, 8, "2026-04-27T09:00:00Z", 13379500000000000LL},
    {"https://mail.google.com/mail/u/0/#inbox", "Inbox - Gmail", 100, 20, "2026-04-27T14:30:00Z", 13380100000000000LL},
    {"https://docs.python.org/3/library/re.html", "re — Regular Expressions", 5, 1, "2026-04-24T16:00:00Z", 13379400000000000LL},
    {"https://www.youtube.com/watch?v=dQw4w9WgXcQ", "Rick Astley - Never Gonna Give You Up", 3, 0, "2026-04-23T20:00:00Z", 13379300000000000LL},
    {"https://amazon.com/dp/B08N5WRWNW", "Product Page - Amazon", 2, 1, "2026-04-22T11:00:00Z", 13379200000000000LL},
};

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size) {
    if (size == 0 || size > 8192) return 0;

    /* Null-terminate the fuzz input */
    char *query_str = malloc(size + 1);
    if (!query_str) return 0;
    memcpy(query_str, data, size);
    query_str[size] = '\0';

    /* ── Phase 1: Parse the query ── */
    SearchQuery query;
    int parse_rc = search_parse_query(query_str, &query);

    if (parse_rc == 0) {
        /* ── Phase 2: Execute against synthetic data ── */
        HistoryResult history;
        history.entries = SYNTH_ENTRIES;
        history.count = sizeof(SYNTH_ENTRIES) / sizeof(SYNTH_ENTRIES[0]);
        history.capacity = history.count;

        SearchResults results;
        search_execute(&query, &history, &results);

        /* ── Phase 3: Score individual entries ── */
        for (int i = 0; i < history.count; i++) {
            search_compute_score(&history.entries[i], &query, 1, 0);
            search_compute_score(&history.entries[i], &query, 0, 1);
            search_compute_score(&history.entries[i], &query, 1, 1);
        }
    }

    /* ── Phase 4: Case-insensitive search standalone ── */
    search_strcasestr(query_str, "test");
    search_strcasestr("some haystack string", query_str);

    /* ── Phase 5: Regex matching standalone ── */
    if (size < 256) {
        search_regex_match(query_str, "https://www.google.com/search?q=test", 0);
        search_regex_match(query_str, "GitHub - user/repo", 1);
        search_regex_match("[a-z]+", query_str, 0);
    }

    free(query_str);
    return 0;
}
