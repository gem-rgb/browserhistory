/**
 * categorize.h — URL categorization engine
 */
#ifndef CATEGORIZE_H
#define CATEGORIZE_H
#include "history_db.h"

typedef enum {
    CAT_DEVELOPMENT=0, CAT_SOCIAL=1, CAT_ENTERTAINMENT=2,
    CAT_PRODUCTIVITY=3, CAT_EDUCATION=4, CAT_SHOPPING=5,
    CAT_NEWS=6, CAT_EMAIL=7, CAT_FINANCE=8, CAT_SEARCH=9,
    CAT_AI_TOOLS=10, CAT_OTHER=11, CAT_COUNT=12
} Category;

typedef struct {
    int count, visits;
    char top_url[2048], top_title[512];
    int top_visits;
} CategoryStats;

typedef struct {
    CategoryStats cats[CAT_COUNT];
    int total_entries;
    int hourly_visits[24], peak_hour, quiet_hour;
    int daily_visits[7], peak_day;
    int session_count;
    float avg_session_minutes;
    int productivity_score;
    char insight_primary[256];
    char insight_focus[256];
    char insight_habit[256];
    char insight_recommendation[256];
} BrowsingAnalysis;

Category categorize_url(const char *url, const char *title);
const char *category_name(Category cat);
const char *category_color(Category cat);
void analyze_browsing(const HistoryResult *result, BrowsingAnalysis *a);

#endif
