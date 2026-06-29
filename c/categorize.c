/**
 * categorize.c — AI-inspired URL categorization and browsing analysis
 *
 * Classifies URLs by domain patterns, computes time-of-day and
 * day-of-week distributions, detects browsing sessions, calculates
 * a productivity score, and generates natural-language insights.
 */
#include "categorize.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* ── Domain pattern rules ────────────────────────────────────────── */
typedef struct { const char *pattern; Category cat; } Rule;

static const Rule RULES[] = {
    /* Development */
    {"github.com",CAT_DEVELOPMENT},{"gitlab.com",CAT_DEVELOPMENT},
    {"stackoverflow.com",CAT_DEVELOPMENT},{"stackexchange.com",CAT_DEVELOPMENT},
    {"developer.",CAT_DEVELOPMENT},{"docs.python",CAT_DEVELOPMENT},
    {"npmjs.com",CAT_DEVELOPMENT},{"pypi.org",CAT_DEVELOPMENT},
    {"crates.io",CAT_DEVELOPMENT},{"dev.to",CAT_DEVELOPMENT},
    {"localhost",CAT_DEVELOPMENT},{"127.0.0.1",CAT_DEVELOPMENT},
    {"codepen.io",CAT_DEVELOPMENT},{"jsfiddle.net",CAT_DEVELOPMENT},
    {"replit.com",CAT_DEVELOPMENT},{"vercel.com",CAT_DEVELOPMENT},
    {"netlify.com",CAT_DEVELOPMENT},{"heroku.com",CAT_DEVELOPMENT},
    {"docker.com",CAT_DEVELOPMENT},{"aws.amazon.com",CAT_DEVELOPMENT},
    {"cloud.google.com",CAT_DEVELOPMENT},{"azure.microsoft.com",CAT_DEVELOPMENT},
    /* Social */
    {"twitter.com",CAT_SOCIAL},{"x.com",CAT_SOCIAL},
    {"facebook.com",CAT_SOCIAL},{"instagram.com",CAT_SOCIAL},
    {"linkedin.com",CAT_SOCIAL},{"reddit.com",CAT_SOCIAL},
    {"tiktok.com",CAT_SOCIAL},{"snapchat.com",CAT_SOCIAL},
    {"discord.com",CAT_SOCIAL},{"telegram.org",CAT_SOCIAL},
    {"whatsapp.com",CAT_SOCIAL},{"threads.net",CAT_SOCIAL},
    /* Entertainment */
    {"youtube.com",CAT_ENTERTAINMENT},{"netflix.com",CAT_ENTERTAINMENT},
    {"twitch.tv",CAT_ENTERTAINMENT},{"spotify.com",CAT_ENTERTAINMENT},
    {"soundcloud.com",CAT_ENTERTAINMENT},{"vimeo.com",CAT_ENTERTAINMENT},
    {"disneyplus.com",CAT_ENTERTAINMENT},{"hulu.com",CAT_ENTERTAINMENT},
    {"primevideo.com",CAT_ENTERTAINMENT},{"crunchyroll.com",CAT_ENTERTAINMENT},
    /* Productivity */
    {"notion.so",CAT_PRODUCTIVITY},{"trello.com",CAT_PRODUCTIVITY},
    {"asana.com",CAT_PRODUCTIVITY},{"monday.com",CAT_PRODUCTIVITY},
    {"slack.com",CAT_PRODUCTIVITY},{"zoom.us",CAT_PRODUCTIVITY},
    {"meet.google.com",CAT_PRODUCTIVITY},{"teams.microsoft.com",CAT_PRODUCTIVITY},
    {"docs.google.com",CAT_PRODUCTIVITY},{"drive.google.com",CAT_PRODUCTIVITY},
    {"office.com",CAT_PRODUCTIVITY},{"calendar.google.com",CAT_PRODUCTIVITY},
    /* Education */
    {".edu",CAT_EDUCATION},{"coursera.org",CAT_EDUCATION},
    {"udemy.com",CAT_EDUCATION},{"edx.org",CAT_EDUCATION},
    {"khanacademy.org",CAT_EDUCATION},{"wikipedia.org",CAT_EDUCATION},
    {"medium.com",CAT_EDUCATION},{"arxiv.org",CAT_EDUCATION},
    {"scholar.google",CAT_EDUCATION},{"w3schools.com",CAT_EDUCATION},
    {"freecodecamp.org",CAT_EDUCATION},{"geeksforgeeks.org",CAT_EDUCATION},
    /* Shopping */
    {"amazon.com",CAT_SHOPPING},{"ebay.com",CAT_SHOPPING},
    {"aliexpress.com",CAT_SHOPPING},{"etsy.com",CAT_SHOPPING},
    {"shopify.com",CAT_SHOPPING},{"walmart.com",CAT_SHOPPING},
    {"jumia.",CAT_SHOPPING},{"kilimall.co.ke",CAT_SHOPPING},
    /* News */
    {"bbc.com",CAT_NEWS},{"cnn.com",CAT_NEWS},
    {"reuters.com",CAT_NEWS},{"nytimes.com",CAT_NEWS},
    {"theguardian.com",CAT_NEWS},{"aljazeera.com",CAT_NEWS},
    {"nation.africa",CAT_NEWS},{"standardmedia.co.ke",CAT_NEWS},
    /* Email */
    {"mail.google.com",CAT_EMAIL},{"outlook.",CAT_EMAIL},
    {"mail.yahoo.com",CAT_EMAIL},{"protonmail.com",CAT_EMAIL},
    /* Finance */
    {"paypal.com",CAT_FINANCE},{"mpesa",CAT_FINANCE},
    {"safaricom.co.ke",CAT_FINANCE},{"equity",CAT_FINANCE},
    {"kcbgroup",CAT_FINANCE},{"banking",CAT_FINANCE},
    {"coinbase.com",CAT_FINANCE},{"binance.com",CAT_FINANCE},
    /* Search */
    {"google.com/search",CAT_SEARCH},{"bing.com/search",CAT_SEARCH},
    {"duckduckgo.com",CAT_SEARCH},{"search.brave.com",CAT_SEARCH},
    /* AI Tools */
    {"chat.openai.com",CAT_AI_TOOLS},{"chatgpt.com",CAT_AI_TOOLS},
    {"claude.ai",CAT_AI_TOOLS},{"gemini.google.com",CAT_AI_TOOLS},
    {"bard.google.com",CAT_AI_TOOLS},{"perplexity.ai",CAT_AI_TOOLS},
    {"copilot.microsoft.com",CAT_AI_TOOLS},{"huggingface.co",CAT_AI_TOOLS},
    {NULL, CAT_OTHER}
};

static const char *CAT_NAMES[] = {
    "Development","Social Media","Entertainment","Productivity",
    "Education","Shopping","News","Email","Finance","Search",
    "AI Tools","Other"
};

static const char *CAT_COLORS[] = {
    "0.2 0.6 1.0","0.9 0.3 0.5","1.0 0.5 0.2","0.2 0.8 0.4",
    "0.5 0.3 0.9","0.9 0.7 0.2","0.4 0.4 0.4","0.3 0.7 0.8",
    "0.1 0.7 0.3","0.6 0.6 0.6","0.7 0.3 1.0","0.5 0.5 0.5"
};

/* ── Public API ──────────────────────────────────────────────────── */

Category categorize_url(const char *url, const char *title) {
    if (!url) return CAT_OTHER;
    for (int i = 0; RULES[i].pattern; i++) {
        if (strstr(url, RULES[i].pattern)) return RULES[i].cat;
    }
    /* Title-based fallback */
    if (title && title[0]) {
        const char *lc = title;
        if (strstr(lc,"tutorial") || strstr(lc,"Tutorial") ||
            strstr(lc,"course") || strstr(lc,"Course") ||
            strstr(lc,"learn") || strstr(lc,"Learn"))
            return CAT_EDUCATION;
        if (strstr(lc,"API") || strstr(lc,"documentation") ||
            strstr(lc,"Documentation"))
            return CAT_DEVELOPMENT;
    }
    return CAT_OTHER;
}

const char *category_name(Category cat) {
    if (cat < 0 || cat >= CAT_COUNT) return "Unknown";
    return CAT_NAMES[cat];
}

const char *category_color(Category cat) {
    if (cat < 0 || cat >= CAT_COUNT) return "0.5 0.5 0.5";
    return CAT_COLORS[cat];
}

/* ── Parse ISO timestamp hour ────────────────────────────────────── */
static int parse_hour(const char *ts) {
    /* "2026-04-27T13:38:10Z" → 13 */
    if (!ts || strlen(ts) < 13) return -1;
    const char *t = strchr(ts, 'T');
    if (!t) return -1;
    return atoi(t + 1);
}

static int parse_weekday(const char *ts) {
    /* Parse ISO date and compute weekday */
    if (!ts || strlen(ts) < 10) return -1;
    int y, m, d;
    if (sscanf(ts, "%d-%d-%d", &y, &m, &d) != 3) return -1;
    struct tm t = {0};
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d;
    mktime(&t);
    return t.tm_wday;
}

/* ── Full Analysis ───────────────────────────────────────────────── */

void analyze_browsing(const HistoryResult *result, BrowsingAnalysis *a) {
    memset(a, 0, sizeof(*a));
    a->total_entries = result->count;

    /* Categorize each entry */
    for (int i = 0; i < result->count; i++) {
        const HistoryEntry *e = &result->entries[i];
        Category cat = categorize_url(e->url, e->title);
        CategoryStats *cs = &a->cats[cat];

        cs->count++;
        cs->visits += e->visit_count;
        if (e->visit_count > cs->top_visits) {
            cs->top_visits = e->visit_count;
            strncpy(cs->top_url, e->url, sizeof(cs->top_url)-1);
            strncpy(cs->top_title, e->title, sizeof(cs->top_title)-1);
        }

        /* Time distribution */
        int hour = parse_hour(e->last_visit_time);
        if (hour >= 0 && hour < 24) a->hourly_visits[hour] += e->visit_count;

        int wday = parse_weekday(e->last_visit_time);
        if (wday >= 0 && wday < 7) a->daily_visits[wday] += e->visit_count;
    }

    /* Find peak/quiet hours */
    int max_h = 0, min_h = 999999;
    for (int h = 0; h < 24; h++) {
        if (a->hourly_visits[h] > max_h) { max_h = a->hourly_visits[h]; a->peak_hour = h; }
        if (a->hourly_visits[h] < min_h) { min_h = a->hourly_visits[h]; a->quiet_hour = h; }
    }

    /* Find peak day */
    int max_d = 0;
    for (int d = 0; d < 7; d++) {
        if (a->daily_visits[d] > max_d) { max_d = a->daily_visits[d]; a->peak_day = d; }
    }

    /* Productivity score */
    int productive = a->cats[CAT_DEVELOPMENT].visits +
                     a->cats[CAT_PRODUCTIVITY].visits +
                     a->cats[CAT_EDUCATION].visits +
                     a->cats[CAT_AI_TOOLS].visits;
    int distracting = a->cats[CAT_SOCIAL].visits +
                      a->cats[CAT_ENTERTAINMENT].visits;
    int total_v = productive + distracting + 1;
    a->productivity_score = (int)(100.0f * (float)productive / (float)total_v);
    if (a->productivity_score > 100) a->productivity_score = 100;

    /* Find dominant category */
    int dom_cat = CAT_OTHER, dom_count = 0;
    for (int c = 0; c < CAT_COUNT; c++) {
        if (a->cats[c].visits > dom_count) {
            dom_count = a->cats[c].visits;
            dom_cat = c;
        }
    }

    /* Generate natural-language insights */
    const char *days[] = {"Sunday","Monday","Tuesday","Wednesday",
                          "Thursday","Friday","Saturday"};

    snprintf(a->insight_primary, sizeof(a->insight_primary),
        "Your browsing is dominated by %s (%d%% of tracked visits).",
        category_name((Category)dom_cat),
        a->total_entries > 0 ? (dom_count * 100) / (a->total_entries + 1) : 0);

    snprintf(a->insight_focus, sizeof(a->insight_focus),
        "Peak activity at %d:00 on %ss. Quietest hour: %d:00.",
        a->peak_hour, days[a->peak_day], a->quiet_hour);

    if (a->productivity_score >= 70) {
        snprintf(a->insight_habit, sizeof(a->insight_habit),
            "Productivity score: %d/100 — Highly focused browsing pattern.",
            a->productivity_score);
    } else if (a->productivity_score >= 40) {
        snprintf(a->insight_habit, sizeof(a->insight_habit),
            "Productivity score: %d/100 — Balanced between work and leisure.",
            a->productivity_score);
    } else {
        snprintf(a->insight_habit, sizeof(a->insight_habit),
            "Productivity score: %d/100 — Leisure-heavy browsing detected.",
            a->productivity_score);
    }

    if (a->cats[CAT_AI_TOOLS].count > 0) {
        snprintf(a->insight_recommendation, sizeof(a->insight_recommendation),
            "AI tool usage detected (%d visits). You're leveraging AI effectively.",
            a->cats[CAT_AI_TOOLS].visits);
    } else {
        snprintf(a->insight_recommendation, sizeof(a->insight_recommendation),
            "Consider using AI tools like ChatGPT or Claude to boost productivity.");
    }
}
