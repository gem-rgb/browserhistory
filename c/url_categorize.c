/**
 * url_categorize.c — URL categorization engine with rules matching
 *
 * Classifies URLs into categories using a multi-pass matching approach:
 *   1. Exact domain match (fastest)
 *   2. Wildcard subdomain matching (*.google.com)
 *   3. Domain suffix matching (amazon.*)
 *   4. URL path glob matching (/watch* on youtube.com)
 *   5. Keyword-in-title fallback (slowest, last resort)
 *
 * Built-in categories cover common browsing patterns (Social Media,
 * News, Shopping, Development, Streaming, etc.) and can be extended
 * via a rules configuration file.
 *
 * The rules file format is INI-like:
 *   [Category Name]
 *   domains = domain1.com, domain2.com, *.wildcard.com
 *   keywords = keyword1, keyword2
 *   paths[domain.com] = /path/glob/*
 *   color = #FF5733
 *
 * @version 1.0.0
 */

#include "url_categorize.h"
#include "url_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Glob matching ───────────────────────────────────────────────── */

int cat_glob_match(const char *pattern, const char *str) {
    if (!pattern || !str) return 0;

    const char *pp = pattern;
    const char *sp = str;
    const char *star_p = NULL;
    const char *star_s = NULL;

    while (*sp) {
        if (*pp == '*') {
            /* Star: save positions and advance pattern */
            star_p = pp++;
            star_s = sp;
        } else if (*pp == '?') {
            /* Question mark: match any single character */
            pp++;
            sp++;
        } else if (*pp == '[') {
            /* Character class: [abc] or [a-z] */
            pp++;
            int negate = 0;
            if (*pp == '!' || *pp == '^') {
                negate = 1;
                pp++;
            }

            int match = 0;
            while (*pp && *pp != ']') {
                if (pp[1] == '-' && pp[2] && pp[2] != ']') {
                    /* Range: a-z */
                    char lo = *pp;
                    char hi = pp[2];
                    if ((unsigned char)*sp >= (unsigned char)lo &&
                        (unsigned char)*sp <= (unsigned char)hi) {
                        match = 1;
                    }
                    pp += 3;
                } else {
                    if (*sp == *pp) match = 1;
                    pp++;
                }
            }
            if (*pp == ']') pp++;

            if (negate) match = !match;
            if (!match) {
                if (star_p) {
                    pp = star_p + 1;
                    sp = ++star_s;
                } else {
                    return 0;
                }
            } else {
                sp++;
            }
        } else if (tolower((unsigned char)*pp) == tolower((unsigned char)*sp)) {
            /* Exact character match (case-insensitive) */
            pp++;
            sp++;
        } else if (star_p) {
            /* Mismatch with active star: backtrack */
            pp = star_p + 1;
            sp = ++star_s;
        } else {
            return 0;
        }
    }

    /* Consume trailing stars */
    while (*pp == '*') pp++;

    return (*pp == '\0');
}

/* ── Domain matching ─────────────────────────────────────────────── */

int cat_domain_match(const char *pattern, const char *domain) {
    if (!pattern || !domain) return 0;

    size_t plen = strlen(pattern);
    size_t dlen = strlen(domain);

    /* Case-insensitive exact match */
    if (plen == dlen) {
        int match = 1;
        for (size_t i = 0; i < plen; i++) {
            if (tolower((unsigned char)pattern[i]) !=
                tolower((unsigned char)domain[i])) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }

    /* Wildcard subdomain: *.google.com */
    if (plen >= 2 && pattern[0] == '*' && pattern[1] == '.') {
        const char *suffix = pattern + 1;  /* ".google.com" */
        size_t slen = plen - 1;

        if (dlen >= slen) {
            /* Check if domain ends with the suffix */
            const char *domain_tail = domain + dlen - slen;
            int match = 1;
            for (size_t i = 0; i < slen; i++) {
                if (tolower((unsigned char)suffix[i]) !=
                    tolower((unsigned char)domain_tail[i])) {
                    match = 0;
                    break;
                }
            }
            if (match) return 1;
        }

        /* Also match the base domain itself (*.google.com matches google.com) */
        const char *base = pattern + 2;
        if (strcasecmp(base, domain) == 0) return 1;
    }

    /* Domain suffix: amazon.* */
    if (plen >= 2 && pattern[plen - 1] == '*' && pattern[plen - 2] == '.') {
        /* Check if domain starts with the prefix (minus ".*") */
        size_t prefix_len = plen - 1;  /* "amazon." */
        if (dlen > prefix_len) {
            int match = 1;
            for (size_t i = 0; i < prefix_len; i++) {
                if (tolower((unsigned char)pattern[i]) !=
                    tolower((unsigned char)domain[i])) {
                    match = 0;
                    break;
                }
            }
            if (match) return 1;
        }
    }

    /* Check if domain is a subdomain of pattern */
    if (dlen > plen + 1) {
        const char *domain_tail = domain + dlen - plen;
        if (domain_tail[-1] == '.' &&
            strcasecmp(domain_tail, pattern) == 0) {
            return 1;
        }
    }

    return 0;
}

/* ── Built-in categories ─────────────────────────────────────────── */

typedef struct {
    const char *name;
    const char *color;
    const char **domains;
    const char **keywords;
} BuiltinCategory;

static const char *SOCIAL_DOMAINS[] = {
    "facebook.com", "*.facebook.com", "twitter.com", "*.twitter.com",
    "x.com", "instagram.com", "*.instagram.com", "reddit.com", "*.reddit.com",
    "linkedin.com", "*.linkedin.com", "tiktok.com", "*.tiktok.com",
    "pinterest.com", "*.pinterest.com", "tumblr.com", "snapchat.com",
    "discord.com", "*.discord.com", "mastodon.social", "threads.net",
    NULL
};
static const char *SOCIAL_KEYWORDS[] = { "social", "feed", "timeline", NULL };

static const char *NEWS_DOMAINS[] = {
    "cnn.com", "*.cnn.com", "bbc.co.uk", "*.bbc.co.uk", "bbc.com",
    "reuters.com", "apnews.com", "nytimes.com", "washingtonpost.com",
    "theguardian.com", "aljazeera.com", "news.google.com",
    "news.ycombinator.com", "arstechnica.com", "techcrunch.com",
    "theverge.com", "wired.com", "bloomberg.com", "cnbc.com",
    NULL
};
static const char *NEWS_KEYWORDS[] = { "breaking", "headline", "report", NULL };

static const char *SHOPPING_DOMAINS[] = {
    "amazon.*", "ebay.com", "ebay.*", "aliexpress.com", "walmart.com",
    "target.com", "bestbuy.com", "etsy.com", "shopify.com",
    "wish.com", "newegg.com", "wayfair.com", "ikea.com",
    NULL
};
static const char *SHOPPING_KEYWORDS[] = {
    "cart", "checkout", "order", "payment", "buy", "shop", NULL
};

static const char *DEV_DOMAINS[] = {
    "github.com", "*.github.com", "gitlab.com", "*.gitlab.com",
    "stackoverflow.com", "*.stackoverflow.com",
    "developer.mozilla.org", "docs.rs", "crates.io",
    "npmjs.com", "pypi.org", "pkg.go.dev",
    "bitbucket.org", "codeberg.org", "sr.ht",
    "docs.python.org", "docs.oracle.com", "learn.microsoft.com",
    "dev.to", "medium.com", "hashnode.dev",
    NULL
};
static const char *DEV_KEYWORDS[] = {
    "documentation", "api", "reference", "tutorial", "sdk", NULL
};

static const char *STREAMING_DOMAINS[] = {
    "youtube.com", "*.youtube.com", "netflix.com", "*.netflix.com",
    "twitch.tv", "*.twitch.tv", "hulu.com", "disneyplus.com",
    "hbomax.com", "max.com", "primevideo.com", "crunchyroll.com",
    "spotify.com", "*.spotify.com", "soundcloud.com",
    "vimeo.com", "dailymotion.com", "peacocktv.com",
    NULL
};
static const char *STREAMING_KEYWORDS[] = {
    "watch", "stream", "episode", "playlist", NULL
};

static const char *SEARCH_DOMAINS[] = {
    "google.com", "www.google.com", "bing.com", "www.bing.com",
    "duckduckgo.com", "search.yahoo.com", "ecosia.org",
    "startpage.com", "brave.com/search", "yandex.com",
    NULL
};

static const char *EMAIL_DOMAINS[] = {
    "mail.google.com", "outlook.live.com", "outlook.office.com",
    "mail.yahoo.com", "proton.me", "protonmail.com",
    "mail.zoho.com", "fastmail.com", "tutanota.com",
    NULL
};

static const BuiltinCategory BUILTIN_CATEGORIES[] = {
    { "Social Media", "#E74C3C", SOCIAL_DOMAINS, SOCIAL_KEYWORDS },
    { "News",         "#3498DB", NEWS_DOMAINS,   NEWS_KEYWORDS },
    { "Shopping",     "#F39C12", SHOPPING_DOMAINS, SHOPPING_KEYWORDS },
    { "Development",  "#2ECC71", DEV_DOMAINS,    DEV_KEYWORDS },
    { "Streaming",    "#9B59B6", STREAMING_DOMAINS, STREAMING_KEYWORDS },
    { "Search",       "#1ABC9C", SEARCH_DOMAINS, NULL },
    { "Email",        "#34495E", EMAIL_DOMAINS,  NULL },
    { NULL, NULL, NULL, NULL }
};

/* ── Engine initialization ───────────────────────────────────────── */

int cat_engine_init_defaults(CatEngine *engine) {
    if (!engine) return -1;

    memset(engine, 0, sizeof(*engine));

    for (int c = 0; BUILTIN_CATEGORIES[c].name && engine->category_count < CAT_MAX_CATEGORIES; c++) {
        const BuiltinCategory *bc = &BUILTIN_CATEGORIES[c];
        Category *cat = &engine->categories[engine->category_count];

        strncpy(cat->name, bc->name, sizeof(cat->name) - 1);
        if (bc->color) strncpy(cat->color, bc->color, sizeof(cat->color) - 1);
        cat->priority = engine->category_count;

        /* Add domain rules */
        if (bc->domains) {
            for (int d = 0; bc->domains[d] && cat->rule_count < CAT_MAX_RULES_PER; d++) {
                CatRule *rule = &cat->rules[cat->rule_count];
                strncpy(rule->domain, bc->domains[d], sizeof(rule->domain) - 1);

                if (bc->domains[d][0] == '*') {
                    rule->type = CAT_RULE_DOMAIN_WILDCARD;
                } else if (bc->domains[d][strlen(bc->domains[d]) - 1] == '*') {
                    rule->type = CAT_RULE_DOMAIN_SUFFIX;
                } else {
                    rule->type = CAT_RULE_DOMAIN_EXACT;
                }

                cat->rule_count++;
            }
        }

        /* Add keyword rules */
        if (bc->keywords) {
            for (int k = 0; bc->keywords[k] && cat->keyword_count < CAT_MAX_KEYWORDS; k++) {
                strncpy(cat->keywords[cat->keyword_count],
                        bc->keywords[k], 63);
                cat->keyword_count++;
            }
        }

        engine->category_count++;
    }

    return 0;
}

/* ── Rules file loading ──────────────────────────────────────────── */

static char *trim(char *str) {
    while (*str && isspace((unsigned char)*str)) str++;
    if (!*str) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) *end-- = '\0';
    return str;
}

int cat_engine_load_rules_buffer(CatEngine *engine, const char *data,
                                  size_t data_len) {
    if (!engine || !data || data_len == 0) return -1;

    const char *p = data;
    const char *end = data + data_len;
    Category *current_cat = NULL;

    while (p < end) {
        /* Read one line */
        const char *line_start = p;
        while (p < end && *p != '\n' && *p != '\r') p++;
        size_t line_len = (size_t)(p - line_start);
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;

        char line[1024];
        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        char *trimmed = trim(line);
        if (!trimmed[0] || trimmed[0] == '#' || trimmed[0] == ';') continue;

        /* Section header: [Category Name] */
        if (trimmed[0] == '[') {
            char *close = strchr(trimmed, ']');
            if (!close) continue;

            char name[CAT_MAX_NAME_LEN];
            size_t nlen = (size_t)(close - trimmed - 1);
            if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
            memcpy(name, trimmed + 1, nlen);
            name[nlen] = '\0';

            /* Find existing category or create new */
            current_cat = NULL;
            for (int i = 0; i < engine->category_count; i++) {
                if (strcasecmp(engine->categories[i].name, trim(name)) == 0) {
                    current_cat = &engine->categories[i];
                    break;
                }
            }
            if (!current_cat && engine->category_count < CAT_MAX_CATEGORIES) {
                current_cat = &engine->categories[engine->category_count];
                memset(current_cat, 0, sizeof(*current_cat));
                strncpy(current_cat->name, trim(name), sizeof(current_cat->name) - 1);
                current_cat->priority = engine->category_count;
                engine->category_count++;
            }
            continue;
        }

        if (!current_cat) continue;

        /* Key = value */
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(trimmed);
        char *value = trim(eq + 1);

        if (strcasecmp(key, "domains") == 0) {
            /* Parse comma-separated domain list */
            char *saveptr = NULL;
            char *token = strtok_r(value, ",", &saveptr);
            while (token && current_cat->rule_count < CAT_MAX_RULES_PER) {
                char *d = trim(token);
                if (d[0]) {
                    CatRule *rule = &current_cat->rules[current_cat->rule_count];
                    memset(rule, 0, sizeof(*rule));
                    strncpy(rule->domain, d, sizeof(rule->domain) - 1);

                    if (d[0] == '*') {
                        rule->type = CAT_RULE_DOMAIN_WILDCARD;
                    } else if (d[strlen(d) - 1] == '*') {
                        rule->type = CAT_RULE_DOMAIN_SUFFIX;
                    } else {
                        rule->type = CAT_RULE_DOMAIN_EXACT;
                    }
                    current_cat->rule_count++;
                }
                token = strtok_r(NULL, ",", &saveptr);
            }
        } else if (strcasecmp(key, "keywords") == 0) {
            char *saveptr = NULL;
            char *token = strtok_r(value, ",", &saveptr);
            while (token && current_cat->keyword_count < CAT_MAX_KEYWORDS) {
                char *k = trim(token);
                if (k[0]) {
                    strncpy(current_cat->keywords[current_cat->keyword_count],
                            k, 63);
                    current_cat->keyword_count++;
                }
                token = strtok_r(NULL, ",", &saveptr);
            }
        } else if (strcasecmp(key, "color") == 0) {
            strncpy(current_cat->color, value, sizeof(current_cat->color) - 1);
        } else if (strncasecmp(key, "paths[", 6) == 0) {
            /* Path rule: paths[domain.com] = /glob/* */
            char *bracket_close = strchr(key, ']');
            if (bracket_close && current_cat->rule_count < CAT_MAX_RULES_PER) {
                CatRule *rule = &current_cat->rules[current_cat->rule_count];
                memset(rule, 0, sizeof(*rule));
                rule->type = CAT_RULE_PATH_GLOB;

                size_t dlen = (size_t)(bracket_close - key - 6);
                if (dlen >= sizeof(rule->domain)) dlen = sizeof(rule->domain) - 1;
                memcpy(rule->domain, key + 6, dlen);
                rule->domain[dlen] = '\0';

                strncpy(rule->pattern, value, sizeof(rule->pattern) - 1);
                current_cat->rule_count++;
            }
        }
    }

    return 0;
}

int cat_engine_load_rules(CatEngine *engine, const char *filepath) {
    if (!engine || !filepath) return -1;

    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {
        fclose(f);
        return -1;
    }

    char *data = malloc((size_t)fsize + 1);
    if (!data) { fclose(f); return -1; }

    size_t rd = fread(data, 1, (size_t)fsize, f);
    fclose(f);
    data[rd] = '\0';

    int rc = cat_engine_load_rules_buffer(engine, data, rd);
    free(data);
    return rc;
}

/* ── URL categorization ──────────────────────────────────────────── */

/**
 * Extract the domain from a URL for matching.
 */
static int extract_domain_from_url(const char *url, char *domain, size_t dsize,
                                    char *path, size_t psize) {
    if (!url || !domain) return -1;

    domain[0] = '\0';
    if (path) path[0] = '\0';

    /* Skip scheme */
    const char *p = strstr(url, "://");
    if (p) p += 3; else p = url;

    /* Skip userinfo@ */
    const char *at = strchr(p, '@');
    const char *slash = strchr(p, '/');
    if (at && (!slash || at < slash)) p = at + 1;

    /* Extract host (up to : or / or ?) */
    const char *host_end = p;
    while (*host_end && *host_end != '/' && *host_end != ':' &&
           *host_end != '?' && *host_end != '#') {
        host_end++;
    }

    size_t hlen = (size_t)(host_end - p);
    if (hlen >= dsize) hlen = dsize - 1;
    memcpy(domain, p, hlen);
    domain[hlen] = '\0';

    /* Lowercase */
    for (size_t i = 0; domain[i]; i++) {
        domain[i] = (char)tolower((unsigned char)domain[i]);
    }

    /* Extract path */
    if (path && psize > 0) {
        /* Skip port */
        p = host_end;
        if (*p == ':') {
            while (*p && *p != '/' && *p != '?' && *p != '#') p++;
        }
        if (*p == '/') {
            const char *path_end = p;
            while (*path_end && *path_end != '?' && *path_end != '#') path_end++;
            size_t plen = (size_t)(path_end - p);
            if (plen >= psize) plen = psize - 1;
            memcpy(path, p, plen);
            path[plen] = '\0';
        }
    }

    return 0;
}

int cat_categorize_url(const CatEngine *engine, const char *url,
                       const char *title, CatMatch *match) {
    if (!engine || !url || !match) return -1;

    memset(match, 0, sizeof(*match));
    match->category_idx = -1;
    strcpy(match->category_name, "Uncategorized");

    char domain[CAT_MAX_DOMAIN_LEN];
    char path[CAT_MAX_PATTERN_LEN];
    extract_domain_from_url(url, domain, sizeof(domain), path, sizeof(path));

    if (!domain[0]) return -1;

    /* Pass 1: Domain matching (fast) */
    for (int c = 0; c < engine->category_count; c++) {
        const Category *cat = &engine->categories[c];

        for (int r = 0; r < cat->rule_count; r++) {
            const CatRule *rule = &cat->rules[r];

            int matched = 0;
            switch (rule->type) {
                case CAT_RULE_DOMAIN_EXACT:
                case CAT_RULE_DOMAIN_WILDCARD:
                case CAT_RULE_DOMAIN_SUFFIX:
                    matched = cat_domain_match(rule->domain, domain);
                    break;

                case CAT_RULE_PATH_GLOB:
                    if (cat_domain_match(rule->domain, domain) && path[0]) {
                        matched = cat_glob_match(rule->pattern, path);
                    }
                    break;

                case CAT_RULE_KEYWORD_URL:
                    if (rule->pattern[0]) {
                        /* Case-insensitive substring in URL */
                        char url_lower[2048];
                        strncpy(url_lower, url, sizeof(url_lower) - 1);
                        url_lower[sizeof(url_lower) - 1] = '\0';
                        for (size_t i = 0; url_lower[i]; i++)
                            url_lower[i] = (char)tolower((unsigned char)url_lower[i]);

                        char pat_lower[CAT_MAX_PATTERN_LEN];
                        strncpy(pat_lower, rule->pattern, sizeof(pat_lower) - 1);
                        pat_lower[sizeof(pat_lower) - 1] = '\0';
                        for (size_t i = 0; pat_lower[i]; i++)
                            pat_lower[i] = (char)tolower((unsigned char)pat_lower[i]);

                        matched = (strstr(url_lower, pat_lower) != NULL);
                    }
                    break;

                default:
                    break;
            }

            if (matched && !rule->negate) {
                match->category_idx = c;
                match->confidence = 90;
                strncpy(match->category_name, cat->name, sizeof(match->category_name) - 1);
                return c;
            }
        }
    }

    /* Pass 2: Keyword matching in title (slow fallback) */
    if (title && title[0]) {
        char title_lower[512];
        strncpy(title_lower, title, sizeof(title_lower) - 1);
        title_lower[sizeof(title_lower) - 1] = '\0';
        for (size_t i = 0; title_lower[i]; i++) {
            title_lower[i] = (char)tolower((unsigned char)title_lower[i]);
        }

        for (int c = 0; c < engine->category_count; c++) {
            const Category *cat = &engine->categories[c];

            for (int k = 0; k < cat->keyword_count; k++) {
                char kw_lower[64];
                strncpy(kw_lower, cat->keywords[k], sizeof(kw_lower) - 1);
                kw_lower[sizeof(kw_lower) - 1] = '\0';
                for (size_t i = 0; kw_lower[i]; i++)
                    kw_lower[i] = (char)tolower((unsigned char)kw_lower[i]);

                if (strstr(title_lower, kw_lower)) {
                    match->category_idx = c;
                    match->confidence = 50;  /* lower confidence for keyword match */
                    strncpy(match->category_name, cat->name,
                            sizeof(match->category_name) - 1);
                    return c;
                }
            }
        }
    }

    return -1;  /* Uncategorized */
}

int cat_categorize_all(CatEngine *engine, const HistoryResult *result) {
    if (!engine || !result) return -1;

    /* Reset stats */
    memset(engine->stats, 0, sizeof(engine->stats));
    engine->total_categorized = 0;
    engine->total_uncategorized = 0;

    /* Initialize stat names */
    for (int c = 0; c < engine->category_count; c++) {
        strncpy(engine->stats[c].name, engine->categories[c].name,
                sizeof(engine->stats[c].name) - 1);
    }
    strncpy(engine->stats[engine->category_count].name, "Uncategorized",
            sizeof(engine->stats[0].name) - 1);

    int categorized = 0;

    for (int i = 0; i < result->count; i++) {
        const HistoryEntry *e = &result->entries[i];
        CatMatch match;

        int cat_idx = cat_categorize_url(engine, e->url, e->title, &match);

        int stat_idx;
        if (cat_idx >= 0 && cat_idx < engine->category_count) {
            stat_idx = cat_idx;
            engine->total_categorized++;
            categorized++;
        } else {
            stat_idx = engine->category_count;  /* Uncategorized */
            engine->total_uncategorized++;
        }

        engine->stats[stat_idx].url_count++;
        engine->stats[stat_idx].total_visits += e->visit_count;

        /* Track top domain per category */
        if (e->visit_count > engine->stats[stat_idx].top_domain_visits) {
            engine->stats[stat_idx].top_domain_visits = e->visit_count;

            /* Extract domain for stats */
            char domain[256];
            extract_domain_from_url(e->url, domain, sizeof(domain), NULL, 0);
            strncpy(engine->stats[stat_idx].top_domain, domain,
                    sizeof(engine->stats[stat_idx].top_domain) - 1);
        }
    }

    return categorized;
}

/* ── Platform compat ─────────────────────────────────────────────── */

#ifdef _WIN32
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
#ifndef strtok_r
static char *strtok_r_cat(char *str, const char *delim, char **saveptr) {
    if (!str) str = *saveptr;
    if (!str) return NULL;
    str += strspn(str, delim);
    if (!*str) { *saveptr = NULL; return NULL; }
    char *tok = str;
    str = strpbrk(tok, delim);
    if (str) { *str = '\0'; *saveptr = str + 1; }
    else { *saveptr = NULL; }
    return tok;
}
#define strtok_r strtok_r_cat
#endif
#endif
