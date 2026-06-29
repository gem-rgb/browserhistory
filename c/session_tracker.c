/**
 * session_tracker.c — Stateful browsing session analyzer
 *
 * Groups history entries into browsing sessions based on temporal gaps,
 * simulates tab contexts with navigation history, reconstructs referrer
 * chains, and tracks state machine transitions.
 *
 * Memory model: The tracker owns sessions, sessions own tabs, tabs own
 * their navigation history arrays. Complex ownership semantics create
 * realistic lifecycle management challenges.
 *
 * The event-driven interface (session_tracker_process_event) allows
 * fuzz-driven exploration of the stateful logic: the fuzzer can
 * generate arbitrary sequences of tab open/close/navigate/switch
 * events to exercise the state machine and memory management.
 *
 * @version 1.0.0
 */

#include "session_tracker.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── State names ─────────────────────────────────────────────────── */

static const char *STATE_NAMES[] = {
    "IDLE",
    "ACTIVE",
    "DEEP_BROWSE",
    "RAPID_FIRE"
};

const char *session_state_name(SessionState state) {
    if (state < 0 || state >= SESSION_STATE_COUNT) return "UNKNOWN";
    return STATE_NAMES[state];
}

/* ── Tab context management ──────────────────────────────────────── */

static TabContext *tab_create(int tab_id, long long timestamp) {
    TabContext *tab = calloc(1, sizeof(TabContext));
    if (!tab) return NULL;

    tab->tab_id = tab_id;
    tab->is_active = 1;
    tab->opened_at = timestamp;
    tab->last_activity = timestamp;
    tab->nav_capacity = 16;
    tab->nav_history = calloc((size_t)tab->nav_capacity, sizeof(char *));
    if (!tab->nav_history) {
        free(tab);
        return NULL;
    }
    tab->nav_count = 0;
    tab->nav_position = -1;
    tab->page_loads = 0;

    return tab;
}

static void tab_destroy(TabContext *tab) {
    if (!tab) return;

    /* Free all navigation history strings */
    if (tab->nav_history) {
        for (int i = 0; i < tab->nav_count; i++) {
            free(tab->nav_history[i]);
        }
        free(tab->nav_history);
    }
    free(tab);
}

/**
 * Push a URL onto the tab's navigation history.
 * If we're not at the end of the history (user navigated back then
 * to a new page), truncate the forward history.
 */
static int tab_navigate(TabContext *tab, const char *url, const char *title) {
    if (!tab || !url) return -1;

    /* Truncate forward history if we're not at the end */
    if (tab->nav_position >= 0 && tab->nav_position < tab->nav_count - 1) {
        for (int i = tab->nav_position + 1; i < tab->nav_count; i++) {
            free(tab->nav_history[i]);
            tab->nav_history[i] = NULL;
        }
        tab->nav_count = tab->nav_position + 1;
    }

    /* Grow navigation history if needed */
    if (tab->nav_count >= tab->nav_capacity) {
        int new_cap = tab->nav_capacity * 2;
        if (new_cap > SESSION_MAX_CHAIN) new_cap = SESSION_MAX_CHAIN;
        if (tab->nav_count >= new_cap) {
            /* At capacity limit — shift entries down */
            free(tab->nav_history[0]);
            memmove(tab->nav_history, tab->nav_history + 1,
                    (size_t)(tab->nav_count - 1) * sizeof(char *));
            tab->nav_count--;
        } else {
            char **new_hist = realloc(tab->nav_history,
                                      (size_t)new_cap * sizeof(char *));
            if (!new_hist) return -1;
            tab->nav_history = new_hist;
            tab->nav_capacity = new_cap;
        }
    }

    /* Store the referrer before updating current URL */
    strncpy(tab->referrer_url, tab->current_url, sizeof(tab->referrer_url) - 1);
    tab->referrer_url[sizeof(tab->referrer_url) - 1] = '\0';

    /* Add to history */
    char *url_copy = strdup(url);
    if (!url_copy) return -1;

    tab->nav_history[tab->nav_count] = url_copy;
    tab->nav_position = tab->nav_count;
    tab->nav_count++;

    /* Update current state */
    strncpy(tab->current_url, url, sizeof(tab->current_url) - 1);
    tab->current_url[sizeof(tab->current_url) - 1] = '\0';

    if (title) {
        strncpy(tab->current_title, title, sizeof(tab->current_title) - 1);
        tab->current_title[sizeof(tab->current_title) - 1] = '\0';
    }

    tab->page_loads++;
    return 0;
}

/**
 * Navigate back in tab history.
 */
static int tab_go_back(TabContext *tab) {
    if (!tab || tab->nav_position <= 0) return -1;

    tab->nav_position--;
    const char *url = tab->nav_history[tab->nav_position];

    strncpy(tab->referrer_url, tab->current_url, sizeof(tab->referrer_url) - 1);
    tab->referrer_url[sizeof(tab->referrer_url) - 1] = '\0';

    strncpy(tab->current_url, url, sizeof(tab->current_url) - 1);
    tab->current_url[sizeof(tab->current_url) - 1] = '\0';

    return 0;
}

/**
 * Navigate forward in tab history.
 */
static int tab_go_forward(TabContext *tab) {
    if (!tab || tab->nav_position >= tab->nav_count - 1) return -1;

    tab->nav_position++;
    const char *url = tab->nav_history[tab->nav_position];

    strncpy(tab->referrer_url, tab->current_url, sizeof(tab->referrer_url) - 1);
    tab->referrer_url[sizeof(tab->referrer_url) - 1] = '\0';

    strncpy(tab->current_url, url, sizeof(tab->current_url) - 1);
    tab->current_url[sizeof(tab->current_url) - 1] = '\0';

    return 0;
}

/* ── Session management ──────────────────────────────────────────── */

static Session *session_create(int session_id, long long timestamp) {
    Session *s = calloc(1, sizeof(Session));
    if (!s) return NULL;

    s->session_id = session_id;
    s->start_time = timestamp;
    s->end_time = timestamp;
    s->state = SESSION_STATE_ACTIVE;
    s->tab_capacity = 8;
    s->tabs = calloc((size_t)s->tab_capacity, sizeof(TabContext *));
    if (!s->tabs) {
        free(s);
        return NULL;
    }
    s->tab_count = 0;
    s->active_tab_id = -1;
    s->next_tab_id = 1;

    return s;
}

static void session_destroy(Session *s) {
    if (!s) return;

    if (s->tabs) {
        for (int i = 0; i < s->tab_count; i++) {
            tab_destroy(s->tabs[i]);
        }
        free(s->tabs);
    }
    free(s);
}

/**
 * Find a tab by ID within a session. Returns NULL if not found.
 */
static TabContext *session_find_tab(Session *s, int tab_id) {
    if (!s) return NULL;
    for (int i = 0; i < s->tab_count; i++) {
        if (s->tabs[i] && s->tabs[i]->tab_id == tab_id) {
            return s->tabs[i];
        }
    }
    return NULL;
}

/**
 * Add a new tab to the session.
 */
static TabContext *session_add_tab(Session *s, long long timestamp) {
    if (!s) return NULL;

    /* Grow tab array if needed */
    if (s->tab_count >= s->tab_capacity) {
        int new_cap = s->tab_capacity * 2;
        if (new_cap > SESSION_MAX_TABS) new_cap = SESSION_MAX_TABS;
        if (s->tab_count >= new_cap) return NULL;  /* at limit */

        TabContext **new_tabs = realloc(s->tabs,
                                        (size_t)new_cap * sizeof(TabContext *));
        if (!new_tabs) return NULL;
        s->tabs = new_tabs;
        s->tab_capacity = new_cap;
    }

    TabContext *tab = tab_create(s->next_tab_id++, timestamp);
    if (!tab) return NULL;

    s->tabs[s->tab_count++] = tab;

    /* Track max concurrent tabs */
    int active_count = 0;
    for (int i = 0; i < s->tab_count; i++) {
        if (s->tabs[i] && s->tabs[i]->is_active) active_count++;
    }
    if (active_count > s->max_concurrent_tabs) {
        s->max_concurrent_tabs = active_count;
    }

    return tab;
}

/**
 * Close a tab in the session. The tab is marked inactive but kept
 * in the array for history purposes.
 */
static int session_close_tab(Session *s, int tab_id) {
    if (!s) return -1;

    for (int i = 0; i < s->tab_count; i++) {
        if (s->tabs[i] && s->tabs[i]->tab_id == tab_id) {
            s->tabs[i]->is_active = 0;

            /* If this was the active tab, switch to another */
            if (s->active_tab_id == tab_id) {
                s->active_tab_id = -1;
                for (int j = s->tab_count - 1; j >= 0; j--) {
                    if (s->tabs[j] && s->tabs[j]->is_active) {
                        s->active_tab_id = s->tabs[j]->tab_id;
                        break;
                    }
                }
            }
            return 0;
        }
    }
    return -1;  /* tab not found */
}

/* ── State machine transitions ───────────────────────────────────── */

/**
 * Determine the new session state based on recent activity patterns.
 */
static SessionState compute_state_transition(Session *s, long long timestamp,
                                              TabEventType event_type) {
    SessionState old_state = s->state;
    long long delta = timestamp - s->end_time;

    /* Time-based transitions (microseconds) */
    long long gap_us = (long long)SESSION_GAP_SECONDS * 1000000LL;

    if (delta > gap_us) {
        return SESSION_STATE_IDLE;
    }

    /* Activity-based transitions */
    switch (old_state) {
        case SESSION_STATE_IDLE:
            if (event_type == TAB_EVENT_OPEN || event_type == TAB_EVENT_NAVIGATE) {
                return SESSION_STATE_ACTIVE;
            }
            break;

        case SESSION_STATE_ACTIVE:
            /* Transition to DEEP_BROWSE if user stays on same tab for a while */
            if (s->tab_switches == 0 && s->page_count > 3) {
                return SESSION_STATE_DEEP_BROWSE;
            }
            /* Transition to RAPID_FIRE if lots of tab switches */
            if (s->tab_switches > 5 && delta < 60000000LL) {
                return SESSION_STATE_RAPID_FIRE;
            }
            break;

        case SESSION_STATE_DEEP_BROWSE:
            if (event_type == TAB_EVENT_SWITCH) {
                return SESSION_STATE_ACTIVE;
            }
            break;

        case SESSION_STATE_RAPID_FIRE:
            if (delta > 30000000LL) {  /* 30 seconds of inactivity */
                return SESSION_STATE_ACTIVE;
            }
            break;

        default:
            break;
    }

    return old_state;
}

/* ── Event processing ────────────────────────────────────────────── */

int session_tracker_process_event(SessionTracker *tracker, const TabEvent *event) {
    if (!tracker || !event) return -1;

    long long timestamp = event->timestamp;
    if (timestamp <= 0) timestamp = tracker->last_event_time + 1000000LL;

    /* Check if we need a new session (gap exceeded) */
    long long gap_us = (long long)SESSION_GAP_SECONDS * 1000000LL;
    int need_new_session = 0;

    if (!tracker->current_session) {
        need_new_session = 1;
    } else if (timestamp - tracker->current_session->end_time > gap_us) {
        need_new_session = 1;
    }

    if (need_new_session) {
        /* Finalize current session */
        if (tracker->current_session) {
            long long duration = tracker->current_session->end_time -
                                tracker->current_session->start_time;
            tracker->current_session->duration_minutes =
                (float)(duration / 1000000LL) / 60.0f;
        }

        /* Create new session */
        if (tracker->session_count >= tracker->session_capacity) {
            int new_cap = tracker->session_capacity * 2;
            if (new_cap > SESSION_MAX_SESSIONS) new_cap = SESSION_MAX_SESSIONS;
            if (tracker->session_count >= new_cap) return -1;

            Session **new_sessions = realloc(tracker->sessions,
                                              (size_t)new_cap * sizeof(Session *));
            if (!new_sessions) return -1;
            tracker->sessions = new_sessions;
            tracker->session_capacity = new_cap;
        }

        Session *new_s = session_create(tracker->session_count + 1, timestamp);
        if (!new_s) return -1;

        tracker->sessions[tracker->session_count] = new_s;
        tracker->current_session = new_s;
        tracker->session_count++;
        tracker->current_state = SESSION_STATE_ACTIVE;
    }

    Session *s = tracker->current_session;
    TabContext *tab = NULL;

    /* Process the event */
    switch (event->type) {
        case TAB_EVENT_OPEN: {
            tab = session_add_tab(s, timestamp);
            if (!tab) return -1;

            if (event->url[0]) {
                tab_navigate(tab, event->url, event->title);
                s->page_count++;
            }
            s->active_tab_id = tab->tab_id;
            break;
        }

        case TAB_EVENT_CLOSE: {
            int target_id = event->tab_id;
            if (target_id <= 0 && s->active_tab_id > 0) {
                target_id = s->active_tab_id;
            }
            if (target_id > 0) {
                session_close_tab(s, target_id);
            }
            break;
        }

        case TAB_EVENT_NAVIGATE: {
            /* Navigate in the active tab, or open a new one */
            if (s->active_tab_id > 0) {
                tab = session_find_tab(s, s->active_tab_id);
            }
            if (!tab || !tab->is_active) {
                tab = session_add_tab(s, timestamp);
                if (!tab) return -1;
                s->active_tab_id = tab->tab_id;
            }
            if (event->url[0]) {
                tab_navigate(tab, event->url, event->title);
                tab->last_activity = timestamp;
                s->page_count++;
            }
            break;
        }

        case TAB_EVENT_SWITCH: {
            int target_id = event->tab_id;
            tab = session_find_tab(s, target_id);
            if (tab && tab->is_active) {
                s->active_tab_id = target_id;
                s->tab_switches++;
                tab->last_activity = timestamp;
            }
            break;
        }

        case TAB_EVENT_RELOAD: {
            if (s->active_tab_id > 0) {
                tab = session_find_tab(s, s->active_tab_id);
                if (tab && tab->is_active) {
                    tab->page_loads++;
                    tab->last_activity = timestamp;
                    s->page_count++;
                }
            }
            break;
        }

        case TAB_EVENT_BACK: {
            if (s->active_tab_id > 0) {
                tab = session_find_tab(s, s->active_tab_id);
                if (tab && tab->is_active) {
                    tab_go_back(tab);
                    tab->last_activity = timestamp;
                }
            }
            break;
        }

        case TAB_EVENT_FORWARD: {
            if (s->active_tab_id > 0) {
                tab = session_find_tab(s, s->active_tab_id);
                if (tab && tab->is_active) {
                    tab_go_forward(tab);
                    tab->last_activity = timestamp;
                }
            }
            break;
        }

        default:
            break;
    }

    /* Update session end time */
    if (timestamp > s->end_time) {
        s->end_time = timestamp;
    }

    /* State machine transition */
    SessionState new_state = compute_state_transition(s, timestamp, event->type);
    if (new_state != s->state) {
        s->transitions[s->state][new_state]++;
        s->state = new_state;
        tracker->current_state = new_state;
    }

    tracker->last_event_time = timestamp;
    return 0;
}

/* ── Event parsing from raw bytes ────────────────────────────────── */

int session_parse_events(const unsigned char *data, size_t data_len,
                         TabEvent *events, int max_events) {
    if (!data || data_len == 0 || !events || max_events <= 0) return 0;

    int count = 0;
    size_t pos = 0;

    /*
     * Event wire format (variable length):
     *   Byte 0:       event type (0-6, modulo TAB_EVENT_COUNT)
     *   Byte 1:       tab_id (0-255)
     *   Bytes 2-9:    timestamp (little-endian int64, or delta from previous)
     *   Byte 10:      url_length (0-255)
     *   Bytes 11..11+url_length: URL bytes
     *   Byte after URL: title_length (0-255)
     *   Bytes after...: title bytes
     *
     * Minimum event size: 12 bytes (type + tab_id + timestamp + url_len(0) + title_len(0))
     */

    long long running_timestamp = 1000000000000000LL;  /* some base time */

    while (pos + 11 < data_len && count < max_events) {
        TabEvent *ev = &events[count];
        memset(ev, 0, sizeof(*ev));

        /* Event type */
        ev->type = (TabEventType)(data[pos] % TAB_EVENT_COUNT);
        pos++;

        /* Tab ID */
        ev->tab_id = (int)data[pos] + 1;  /* 1-based */
        pos++;

        /* Timestamp delta (8 bytes little-endian) */
        if (pos + 8 > data_len) break;
        long long delta = 0;
        for (int b = 0; b < 8 && pos < data_len; b++) {
            delta |= ((long long)data[pos++]) << (b * 8);
        }
        /* Clamp delta to reasonable range to prevent overflow */
        if (delta < 0) delta = -delta;
        delta = delta % (3600LL * 1000000LL);  /* max 1 hour delta */
        running_timestamp += delta + 1000000LL;  /* at least 1 second */
        ev->timestamp = running_timestamp;

        /* URL length */
        if (pos >= data_len) break;
        int url_len = (int)data[pos++];
        if (url_len > 0) {
            if (pos + (size_t)url_len > data_len) url_len = (int)(data_len - pos);
            if ((size_t)url_len >= sizeof(ev->url)) url_len = (int)sizeof(ev->url) - 1;
            memcpy(ev->url, data + pos, (size_t)url_len);
            ev->url[url_len] = '\0';
            pos += (size_t)url_len;
        }

        /* Title length */
        if (pos >= data_len) {
            count++;
            break;
        }
        int title_len = (int)data[pos++];
        if (title_len > 0) {
            if (pos + (size_t)title_len > data_len) title_len = (int)(data_len - pos);
            if ((size_t)title_len >= sizeof(ev->title)) title_len = (int)sizeof(ev->title) - 1;
            memcpy(ev->title, data + pos, (size_t)title_len);
            ev->title[title_len] = '\0';
            pos += (size_t)title_len;
        }

        count++;
    }

    return count;
}

/* ── Analysis from history entries ───────────────────────────────── */

/**
 * Compare function for sorting entries by timestamp (descending).
 */
static int compare_by_time_desc(const void *a, const void *b) {
    const HistoryEntry *ea = (const HistoryEntry *)a;
    const HistoryEntry *eb = (const HistoryEntry *)b;

    if (ea->last_visit_raw < eb->last_visit_raw) return 1;
    if (ea->last_visit_raw > eb->last_visit_raw) return -1;
    return 0;
}

int session_tracker_analyze(SessionTracker *tracker, const HistoryResult *result) {
    if (!tracker || !result || result->count == 0) return -1;

    /* Make a sorted copy of entries by timestamp */
    HistoryEntry *sorted = malloc((size_t)result->count * sizeof(HistoryEntry));
    if (!sorted) return -1;

    memcpy(sorted, result->entries, (size_t)result->count * sizeof(HistoryEntry));
    qsort(sorted, (size_t)result->count, sizeof(HistoryEntry), compare_by_time_desc);

    /* Process entries in reverse chronological order (oldest first) */
    for (int i = result->count - 1; i >= 0; i--) {
        const HistoryEntry *entry = &sorted[i];

        TabEvent event;
        memset(&event, 0, sizeof(event));
        event.type = TAB_EVENT_NAVIGATE;
        event.timestamp = entry->last_visit_raw;
        strncpy(event.url, entry->url, sizeof(event.url) - 1);
        strncpy(event.title, entry->title, sizeof(event.title) - 1);

        /* Use URL hash as pseudo-tab-id for grouping related navigations */
        unsigned int hash = 0;
        const char *p = entry->url;
        /* Hash the domain portion only */
        const char *proto_end = strstr(p, "://");
        if (proto_end) {
            p = proto_end + 3;
            while (*p && *p != '/' && *p != '?' && *p != '#') {
                hash = hash * 31 + (unsigned char)*p;
                p++;
            }
        }
        event.tab_id = (int)(hash % SESSION_MAX_TABS) + 1;

        session_tracker_process_event(tracker, &event);
    }

    free(sorted);

    /* Finalize the last session */
    if (tracker->current_session) {
        Session *s = tracker->current_session;
        long long duration = s->end_time - s->start_time;
        s->duration_minutes = (float)(duration / 1000000LL) / 60.0f;
    }

    session_tracker_compute_stats(tracker);

    return 0;
}

/* ── Statistics computation ──────────────────────────────────────── */

void session_tracker_compute_stats(SessionTracker *tracker) {
    if (!tracker || tracker->session_count == 0) return;

    tracker->total_sessions = tracker->session_count;
    tracker->total_pages = 0;

    float total_duration = 0.0f;
    float total_tabs = 0.0f;
    float longest = 0.0f;
    int most_tabs = 0;

    for (int i = 0; i < tracker->session_count; i++) {
        Session *s = tracker->sessions[i];
        if (!s) continue;

        tracker->total_pages += s->page_count;
        total_duration += s->duration_minutes;
        total_tabs += (float)s->tab_count;

        if (s->duration_minutes > longest) {
            longest = s->duration_minutes;
            tracker->longest_session_idx = i;
        }
        if (s->max_concurrent_tabs > most_tabs) {
            most_tabs = s->max_concurrent_tabs;
            tracker->most_tabs_session_idx = i;
        }
    }

    tracker->avg_session_minutes = total_duration / (float)tracker->session_count;
    tracker->avg_tabs_per_session = total_tabs / (float)tracker->session_count;
}

/* ── Lifecycle management ────────────────────────────────────────── */

SessionTracker *session_tracker_create(void) {
    SessionTracker *tracker = calloc(1, sizeof(SessionTracker));
    if (!tracker) return NULL;

    tracker->session_capacity = 32;
    tracker->sessions = calloc((size_t)tracker->session_capacity, sizeof(Session *));
    if (!tracker->sessions) {
        free(tracker);
        return NULL;
    }

    tracker->current_state = SESSION_STATE_IDLE;
    return tracker;
}

void session_tracker_destroy(SessionTracker *tracker) {
    if (!tracker) return;

    if (tracker->sessions) {
        for (int i = 0; i < tracker->session_count; i++) {
            session_destroy(tracker->sessions[i]);
        }
        free(tracker->sessions);
    }
    free(tracker);
}
