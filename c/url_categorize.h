/**
 * url_categorize.h — URL categorization engine with rules matching
 *
 * Classifies URLs into categories using domain matching, wildcard
 * patterns, path globs, and keyword-in-title fallback.
 *
 * @version 1.0.0
 */

#ifndef URL_CATEGORIZE_H
#define URL_CATEGORIZE_H

#include "history_db.h"
#include <stddef.h>

/* ── Limits ──────────────────────────────────────────────────────── */

#define CAT_MAX_CATEGORIES       32
#define CAT_MAX_RULES_PER       128
#define CAT_MAX_NAME_LEN         64
#define CAT_MAX_DOMAIN_LEN      256
#define CAT_MAX_PATTERN_LEN     512
#define CAT_MAX_KEYWORDS         32

/* ── Rule types ──────────────────────────────────────────────────── */

typedef enum {
    CAT_RULE_DOMAIN_EXACT = 0,   /* facebook.com */
    CAT_RULE_DOMAIN_WILDCARD,    /* *.google.com */
    CAT_RULE_DOMAIN_SUFFIX,      /* amazon.* (any TLD) */
    CAT_RULE_PATH_GLOB,          /* /watch* on a specific domain */
    CAT_RULE_KEYWORD_TITLE,      /* keyword in page title */
    CAT_RULE_KEYWORD_URL,        /* keyword in URL */
    CAT_RULE_REGEX               /* regex on full URL */
} CatRuleType;

/* ── Single rule ─────────────────────────────────────────────────── */

typedef struct {
    CatRuleType type;
    char        domain[CAT_MAX_DOMAIN_LEN];     /* domain for domain rules */
    char        pattern[CAT_MAX_PATTERN_LEN];   /* glob/keyword/regex */
    int         negate;                          /* 1 = exclude match */
} CatRule;

/* ── Category definition ─────────────────────────────────────────── */

typedef struct {
    char      name[CAT_MAX_NAME_LEN];
    char      color[16];                        /* for HTML reports */
    CatRule   rules[CAT_MAX_RULES_PER];
    int       rule_count;
    char      keywords[CAT_MAX_KEYWORDS][64];
    int       keyword_count;
    int       priority;                         /* higher = checked first */
} Category;

/* ── Categorization result for a single entry ────────────────────── */

typedef struct {
    int   category_idx;          /* index into categories array, -1 = uncategorized */
    int   confidence;            /* 0-100 match confidence */
    char  category_name[CAT_MAX_NAME_LEN];
} CatMatch;

/* ── Category statistics ─────────────────────────────────────────── */

typedef struct {
    char  name[CAT_MAX_NAME_LEN];
    int   url_count;
    int   total_visits;
    char  top_domain[256];
    int   top_domain_visits;
} CatStats;

/* ── Categorization engine ───────────────────────────────────────── */

typedef struct {
    Category    categories[CAT_MAX_CATEGORIES];
    int         category_count;
    CatStats    stats[CAT_MAX_CATEGORIES + 1];  /* +1 for "Uncategorized" */
    int         total_categorized;
    int         total_uncategorized;
} CatEngine;

/* ── API ─────────────────────────────────────────────────────────── */

/**
 * Initialize the categorization engine with built-in default categories.
 *
 * @param engine  Output: initialized engine
 * @return 0 on success
 */
int cat_engine_init_defaults(CatEngine *engine);

/**
 * Load categories from a rules configuration file.
 *
 * @param engine    The engine (categories are appended/replaced)
 * @param filepath  Path to categories.conf
 * @return 0 on success, -1 on error
 */
int cat_engine_load_rules(CatEngine *engine, const char *filepath);

/**
 * Load categories from an in-memory rules buffer.
 *
 * @param engine    The engine
 * @param data      Rules file content
 * @param data_len  Length of content
 * @return 0 on success, -1 on error
 */
int cat_engine_load_rules_buffer(CatEngine *engine, const char *data,
                                  size_t data_len);

/**
 * Categorize a single URL.
 *
 * @param engine    The engine
 * @param url       URL string
 * @param title     Page title (can be NULL)
 * @param match     Output: match result
 * @return Category index, or -1 if uncategorized
 */
int cat_categorize_url(const CatEngine *engine, const char *url,
                       const char *title, CatMatch *match);

/**
 * Categorize all entries in a HistoryResult and compute statistics.
 *
 * @param engine    The engine (stats are updated)
 * @param result    History entries to categorize
 * @return Number of categorized entries
 */
int cat_categorize_all(CatEngine *engine, const HistoryResult *result);

/**
 * Match a string against a glob pattern.
 * Supports: * (any chars), ? (single char), [...] (character class)
 *
 * @param pattern  Glob pattern
 * @param str      String to match
 * @return 1 if matches, 0 if not
 */
int cat_glob_match(const char *pattern, const char *str);

/**
 * Match a domain against a pattern with wildcard support.
 * *.google.com matches mail.google.com, docs.google.com
 * amazon.* matches amazon.com, amazon.co.uk, amazon.de
 *
 * @param pattern  Domain pattern
 * @param domain   Domain to test
 * @return 1 if matches, 0 if not
 */
int cat_domain_match(const char *pattern, const char *domain);

#endif /* URL_CATEGORIZE_H */
