/**
 * session_fuzzer.c — Fuzz harness for the stateful session tracker
 *
 * This is the highest-value harness. It interprets fuzz data as a
 * sequence of tab events (open, close, navigate, switch, back, forward)
 * and drives them through the session tracker's state machine.
 *
 * The session tracker has complex ownership semantics:
 *   - Tracker owns Sessions (dynamic array)
 *   - Sessions own Tabs (dynamic array of pointers)
 *   - Tabs own navigation history (dynamic array of strdup'd strings)
 *
 * Fuzz-driven event sequences can trigger:
 *   - Use-after-free: close tab then navigate on it
 *   - Double-free: close same tab twice
 *   - Buffer overflows: very long URL/title strings in events
 *   - Integer overflows: extreme timestamp deltas
 *   - Memory leaks: rapid session creation without destruction
 *   - State machine confusion: invalid state transitions
 *
 * @version 1.0.0
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "session_tracker.h"
#include "history_db.h"

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size) {
    if (size < 12 || size > 1024 * 1024) return 0;

    /* ── Mode 1: Event-stream driven exploration ── */

    /* Parse fuzz bytes into tab events */
    TabEvent events[256];
    int event_count = session_parse_events(data, size, events, 256);

    if (event_count > 0) {
        SessionTracker *tracker = session_tracker_create();
        if (!tracker) return 0;

        /* Process each event through the state machine */
        for (int i = 0; i < event_count; i++) {
            session_tracker_process_event(tracker, &events[i]);
        }

        /* Compute aggregate statistics */
        session_tracker_compute_stats(tracker);

        /* Verify state name lookup doesn't crash */
        session_state_name(tracker->current_state);
        session_state_name(SESSION_STATE_IDLE);
        session_state_name(SESSION_STATE_ACTIVE);
        session_state_name(SESSION_STATE_DEEP_BROWSE);
        session_state_name(SESSION_STATE_RAPID_FIRE);
        session_state_name((SessionState)99);  /* invalid state */

        session_tracker_destroy(tracker);
    }

    /* ── Mode 2: History entry analysis (different code path) ── */

    if (size >= 64) {
        /* Interpret fuzz data as a small set of fake history entries */
        int num_entries = (int)(data[0] % 16) + 1;
        size_t per_entry = (size - 1) / (size_t)num_entries;
        if (per_entry < 16) per_entry = 16;

        HistoryResult result;
        history_result_init(&result);
        result.entries = calloc((size_t)num_entries, sizeof(HistoryEntry));
        if (result.entries) {
            result.capacity = num_entries;

            for (int i = 0; i < num_entries && (size_t)(1 + i * per_entry) < size; i++) {
                HistoryEntry *e = &result.entries[i];
                size_t offset = 1 + (size_t)i * per_entry;
                size_t avail = size - offset;
                if (avail > per_entry) avail = per_entry;

                /* Construct a pseudo-URL from fuzz data */
                snprintf(e->url, sizeof(e->url), "https://fuzz%d.example.com/",
                         data[offset % size]);

                /* Copy some bytes as title */
                size_t title_len = (avail > 4) ? avail - 4 : 0;
                if (title_len >= sizeof(e->title)) title_len = sizeof(e->title) - 1;
                if (title_len > 0) {
                    memcpy(e->title, data + offset + 4, title_len);
                    e->title[title_len] = '\0';
                }

                /* Timestamp from bytes */
                e->last_visit_raw = 0;
                for (size_t b = 0; b < 8 && offset + b < size; b++) {
                    e->last_visit_raw |= ((long long)data[offset + b]) << (b * 8);
                }
                /* Keep timestamp reasonable */
                if (e->last_visit_raw < 0) e->last_visit_raw = -e->last_visit_raw;
                e->last_visit_raw = e->last_visit_raw % 100000000000000LL;

                e->visit_count = (int)(data[(offset + 2) % size]) + 1;
                result.count++;
            }

            /* Run the analysis pipeline */
            SessionTracker *tracker2 = session_tracker_create();
            if (tracker2) {
                session_tracker_analyze(tracker2, &result);
                session_tracker_compute_stats(tracker2);
                session_tracker_destroy(tracker2);
            }

            free(result.entries);
        }
    }

    return 0;
}
