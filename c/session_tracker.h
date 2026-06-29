/**
 * session_tracker.h — Stateful browsing session analyzer
 *
 * Groups history entries into browsing sessions based on temporal proximity,
 * simulates tab contexts, reconstructs navigation chains, and computes
 * per-session statistics.
 *
 * @version 1.0.0
 */

#ifndef SESSION_TRACKER_H
#define SESSION_TRACKER_H

#include "history_db.h"
#include <stddef.h>

/* ── Limits ──────────────────────────────────────────────────────── */

#define SESSION_MAX_TABS         64
#define SESSION_MAX_CHAIN       128
#define SESSION_GAP_SECONDS    1800   /* 30 minutes between sessions */
#define SESSION_MAX_SESSIONS   1024

/* ── Session states ──────────────────────────────────────────────── */

typedef enum {
    SESSION_STATE_IDLE = 0,
    SESSION_STATE_ACTIVE,
    SESSION_STATE_DEEP_BROWSE,
    SESSION_STATE_RAPID_FIRE,      /* fast tab switching */
    SESSION_STATE_COUNT
} SessionState;

/* ── Tab event types (for fuzz-driven simulation) ────────────────── */

typedef enum {
    TAB_EVENT_OPEN = 0,
    TAB_EVENT_CLOSE,
    TAB_EVENT_NAVIGATE,
    TAB_EVENT_SWITCH,
    TAB_EVENT_RELOAD,
    TAB_EVENT_BACK,
    TAB_EVENT_FORWARD,
    TAB_EVENT_COUNT
} TabEventType;

/* ── Tab context ─────────────────────────────────────────────────── */

typedef struct TabContext {
    int              tab_id;
    int              is_active;
    char             current_url[2048];
    char             current_title[512];
    long long        opened_at;        /* Chromium-epoch microseconds */
    long long        last_activity;

    /* Navigation history within this tab */
    char           **nav_history;       /* array of URL strings */
    int              nav_count;
    int              nav_capacity;
    int              nav_position;      /* current position in history */

    /* Referrer chain */
    char             referrer_url[2048];
    int              page_loads;
} TabContext;

/* ── Session ─────────────────────────────────────────────────────── */

typedef struct {
    int              session_id;
    long long        start_time;       /* Chromium-epoch microseconds */
    long long        end_time;
    SessionState     state;

    /* Tab management */
    TabContext     **tabs;              /* array of tab pointers */
    int              tab_count;
    int              tab_capacity;
    int              active_tab_id;
    int              next_tab_id;

    /* Session statistics */
    int              page_count;
    int              unique_domains;
    int              tab_switches;
    int              max_concurrent_tabs;
    float            duration_minutes;

    /* State transition tracking */
    int              transitions[SESSION_STATE_COUNT][SESSION_STATE_COUNT];
} Session;

/* ── Session tracker (top-level manager) ─────────────────────────── */

typedef struct {
    Session        **sessions;
    int              session_count;
    int              session_capacity;
    Session         *current_session;

    /* Aggregate statistics */
    int              total_sessions;
    int              total_pages;
    float            avg_session_minutes;
    float            avg_tabs_per_session;
    int              longest_session_idx;
    int              most_tabs_session_idx;

    /* State machine */
    SessionState     current_state;
    long long        last_event_time;
} SessionTracker;

/* ── Tab event (for fuzz-driven simulation) ──────────────────────── */

typedef struct {
    TabEventType     type;
    int              tab_id;         /* which tab this event targets */
    char             url[2048];
    char             title[512];
    long long        timestamp;
} TabEvent;

/* ── API ─────────────────────────────────────────────────────────── */

/**
 * Create a new session tracker.
 * @return Pointer to tracker, or NULL on allocation failure
 */
SessionTracker *session_tracker_create(void);

/**
 * Free a session tracker and all owned sessions/tabs.
 */
void session_tracker_destroy(SessionTracker *tracker);

/**
 * Analyze a set of history entries, grouping them into sessions.
 * Entries are grouped by temporal proximity (30-minute gaps).
 *
 * @param tracker   The session tracker
 * @param result    History entries to analyze
 * @return 0 on success, -1 on error
 */
int session_tracker_analyze(SessionTracker *tracker, const HistoryResult *result);

/**
 * Process a single tab event through the session state machine.
 * Used by the fuzzer to drive stateful exploration.
 *
 * @param tracker   The session tracker
 * @param event     The tab event to process
 * @return 0 on success, -1 on error
 */
int session_tracker_process_event(SessionTracker *tracker, const TabEvent *event);

/**
 * Parse a raw byte buffer into a sequence of tab events.
 * Interprets fuzz data as a structured event stream.
 *
 * @param data      Raw byte data
 * @param data_len  Length of data
 * @param events    Output: array of parsed events
 * @param max_events Maximum number of events to parse
 * @return Number of events parsed
 */
int session_parse_events(const unsigned char *data, size_t data_len,
                         TabEvent *events, int max_events);

/**
 * Compute aggregate statistics across all sessions.
 *
 * @param tracker   The session tracker (stats fields are updated)
 */
void session_tracker_compute_stats(SessionTracker *tracker);

/**
 * Get the current session state name as a string.
 */
const char *session_state_name(SessionState state);

#endif /* SESSION_TRACKER_H */
