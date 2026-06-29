/**
 * browser_detect.c — Multi-browser detection and profile enumeration
 *
 * Scans the filesystem for installed browsers (Brave, Chrome, Chromium,
 * Edge, Vivaldi, Opera, Firefox), enumerates user profiles, and provides
 * backend implementations for querying history from each browser's
 * SQLite database.
 *
 * Key design:
 *   - Chromium-based browsers share an identical schema (urls + visits tables,
 *     microsecond timestamps since 1601-01-01).
 *   - Firefox uses a completely different schema (moz_places + moz_historyvisits
 *     with a place_id foreign key, microsecond timestamps since Unix epoch).
 *   - The BrowserBackend struct provides function pointers so each browser
 *     plugs in its own implementation.
 *   - Multi-profile support: Chrome can have Default, Profile 1, Profile 2, etc.
 *   - Firefox uses random profile directory names like "a1b2c3d4.default-release".
 *
 * @version 1.0.0
 */

#include "browser_detect.h"
#include "platform.h"
#include "logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

/* Forward declare vendored sqlite3 */
#include "vendor/sqlite3.h"

/* ── Constants ───────────────────────────────────────────────────── */

/** Chromium epoch offset: microseconds between 1601-01-01 and 1970-01-01 */
#define CHROMIUM_EPOCH_OFFSET 11644473600000000LL

/** Browser display names */
static const char *BROWSER_NAMES[] = {
    "Unknown",
    "Brave",
    "Chrome",
    "Chromium",
    "Edge",
    "Vivaldi",
    "Opera",
    "Firefox"
};

const char *browser_type_name(BrowserType type) {
    if (type < 0 || type >= BROWSER_TYPE_COUNT) return "Unknown";
    return BROWSER_NAMES[type];
}

/* ── Timestamp normalization ─────────────────────────────────────── */

long long browser_normalize_timestamp(long long raw_ts, TimestampFormat format) {
    switch (format) {
        case TS_FORMAT_CHROMIUM:
            /* Chromium: microseconds since 1601-01-01 → Unix seconds */
            return (raw_ts - CHROMIUM_EPOCH_OFFSET) / 1000000LL;

        case TS_FORMAT_FIREFOX:
            /* Firefox: microseconds since Unix epoch → Unix seconds */
            return raw_ts / 1000000LL;

        case TS_FORMAT_UNIX_SECONDS:
            return raw_ts;

        case TS_FORMAT_UNIX_MS:
            return raw_ts / 1000LL;

        default:
            return raw_ts;
    }
}

/* ── Platform-specific path resolution ───────────────────────────── */

/**
 * Get the user's home directory path.
 */
static int get_home_dir(char *buf, size_t size) {
#ifdef _WIN32
    const char *userprofile = getenv("USERPROFILE");
    if (userprofile) {
        strncpy(buf, userprofile, size - 1);
        buf[size - 1] = '\0';
        return 0;
    }
    /* Fallback to HOMEDRIVE + HOMEPATH */
    const char *drive = getenv("HOMEDRIVE");
    const char *path = getenv("HOMEPATH");
    if (drive && path) {
        snprintf(buf, size, "%s%s", drive, path);
        return 0;
    }
    return -1;
#else
    const char *home = getenv("HOME");
    if (home) {
        strncpy(buf, home, size - 1);
        buf[size - 1] = '\0';
        return 0;
    }
    return -1;
#endif
}

int browser_default_config_dir(BrowserType type, char *buf, size_t size) {
    char home[BROWSER_MAX_PATH];
    if (get_home_dir(home, sizeof(home)) != 0) return -1;

#ifdef _WIN32
    const char *appdata = getenv("LOCALAPPDATA");
    if (!appdata) appdata = home;

    switch (type) {
        case BROWSER_BRAVE:
            snprintf(buf, size, "%s\\BraveSoftware\\Brave-Browser\\User Data", appdata);
            break;
        case BROWSER_CHROME:
            snprintf(buf, size, "%s\\Google\\Chrome\\User Data", appdata);
            break;
        case BROWSER_CHROMIUM:
            snprintf(buf, size, "%s\\Chromium\\User Data", appdata);
            break;
        case BROWSER_EDGE:
            snprintf(buf, size, "%s\\Microsoft\\Edge\\User Data", appdata);
            break;
        case BROWSER_VIVALDI:
            snprintf(buf, size, "%s\\Vivaldi\\User Data", appdata);
            break;
        case BROWSER_OPERA:
            snprintf(buf, size, "%s\\Opera Software\\Opera Stable", appdata);
            break;
        case BROWSER_FIREFOX: {
            const char *appdata_roaming = getenv("APPDATA");
            if (appdata_roaming) {
                snprintf(buf, size, "%s\\Mozilla\\Firefox\\Profiles", appdata_roaming);
            } else {
                snprintf(buf, size, "%s\\Mozilla\\Firefox\\Profiles", appdata);
            }
            break;
        }
        default:
            return -1;
    }
#elif defined(__APPLE__)
    switch (type) {
        case BROWSER_BRAVE:
            snprintf(buf, size, "%s/Library/Application Support/BraveSoftware/Brave-Browser", home);
            break;
        case BROWSER_CHROME:
            snprintf(buf, size, "%s/Library/Application Support/Google/Chrome", home);
            break;
        case BROWSER_CHROMIUM:
            snprintf(buf, size, "%s/Library/Application Support/Chromium", home);
            break;
        case BROWSER_EDGE:
            snprintf(buf, size, "%s/Library/Application Support/Microsoft Edge", home);
            break;
        case BROWSER_VIVALDI:
            snprintf(buf, size, "%s/Library/Application Support/Vivaldi", home);
            break;
        case BROWSER_OPERA:
            snprintf(buf, size, "%s/Library/Application Support/com.operasoftware.Opera", home);
            break;
        case BROWSER_FIREFOX:
            snprintf(buf, size, "%s/Library/Application Support/Firefox/Profiles", home);
            break;
        default:
            return -1;
    }
#else
    /* Linux / BSD */
    const char *config_home = getenv("XDG_CONFIG_HOME");
    char config_dir[BROWSER_MAX_PATH];
    if (config_home) {
        strncpy(config_dir, config_home, sizeof(config_dir) - 1);
    } else {
        snprintf(config_dir, sizeof(config_dir), "%s/.config", home);
    }

    switch (type) {
        case BROWSER_BRAVE:
            snprintf(buf, size, "%s/BraveSoftware/Brave-Browser", config_dir);
            break;
        case BROWSER_CHROME:
            snprintf(buf, size, "%s/google-chrome", config_dir);
            break;
        case BROWSER_CHROMIUM:
            snprintf(buf, size, "%s/chromium", config_dir);
            break;
        case BROWSER_EDGE:
            snprintf(buf, size, "%s/microsoft-edge", config_dir);
            break;
        case BROWSER_VIVALDI:
            snprintf(buf, size, "%s/vivaldi", config_dir);
            break;
        case BROWSER_OPERA:
            snprintf(buf, size, "%s/opera", config_dir);
            break;
        case BROWSER_FIREFOX:
            snprintf(buf, size, "%s/.mozilla/firefox", home);
            break;
        default:
            return -1;
    }
#endif

    return 0;
}

/* ── File/directory existence checks ─────────────────────────────── */

static int path_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

static int is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

/* ── Profile enumeration ─────────────────────────────────────────── */

/**
 * Enumerate Chromium-based profiles in a user data directory.
 * Looks for "Default", "Profile 1", "Profile 2", etc.
 */
static int enumerate_chromium_profiles(const char *user_data_dir,
                                        DetectedBrowser *browser) {
    browser->profile_count = 0;

    /* Check "Default" profile */
    char default_path[BROWSER_MAX_PATH];
    snprintf(default_path, sizeof(default_path), "%s/Default", user_data_dir);
    if (is_directory(default_path)) {
        BrowserProfile *p = &browser->profiles[browser->profile_count];
        strncpy(p->name, "Default", sizeof(p->name) - 1);
        strncpy(p->path, default_path, sizeof(p->path) - 1);
        snprintf(p->history_db, sizeof(p->history_db), "%s/History", default_path);
        p->is_valid = path_exists(p->history_db);
        browser->profile_count++;
    }

    /* Check numbered profiles: Profile 1, Profile 2, ... */
    for (int i = 1; i <= 20 && browser->profile_count < BROWSER_MAX_PROFILES; i++) {
        char profile_path[BROWSER_MAX_PATH];
        snprintf(profile_path, sizeof(profile_path), "%s/Profile %d", user_data_dir, i);
        if (!is_directory(profile_path)) continue;

        BrowserProfile *p = &browser->profiles[browser->profile_count];
        snprintf(p->name, sizeof(p->name), "Profile %d", i);
        strncpy(p->path, profile_path, sizeof(p->path) - 1);
        snprintf(p->history_db, sizeof(p->history_db), "%s/History", profile_path);
        p->is_valid = path_exists(p->history_db);
        browser->profile_count++;
    }

    /* Also check if History exists directly in user_data_dir (Opera layout) */
    if (browser->profile_count == 0) {
        char direct_db[BROWSER_MAX_PATH];
        snprintf(direct_db, sizeof(direct_db), "%s/History", user_data_dir);
        if (path_exists(direct_db)) {
            BrowserProfile *p = &browser->profiles[0];
            strncpy(p->name, "Default", sizeof(p->name) - 1);
            strncpy(p->path, user_data_dir, sizeof(p->path) - 1);
            strncpy(p->history_db, direct_db, sizeof(p->history_db) - 1);
            p->is_valid = 1;
            browser->profile_count = 1;
        }
    }

    return browser->profile_count;
}

/**
 * Enumerate Firefox profiles.
 * Firefox uses random directory names like "a1b2c3d4.default-release".
 * We scan the profiles directory for any subdirectory containing places.sqlite.
 */
static int enumerate_firefox_profiles(const char *firefox_dir,
                                       DetectedBrowser *browser) {
    browser->profile_count = 0;

#ifdef _WIN32
    WIN32_FIND_DATAA find_data;
    char search_pattern[BROWSER_MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*", firefox_dir);

    HANDLE hFind = FindFirstFileA(search_pattern, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    do {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (find_data.cFileName[0] == '.') continue;

        char profile_path[BROWSER_MAX_PATH];
        snprintf(profile_path, sizeof(profile_path), "%s\\%s",
                firefox_dir, find_data.cFileName);

        char places_db[BROWSER_MAX_PATH];
        snprintf(places_db, sizeof(places_db), "%s\\places.sqlite", profile_path);

        if (path_exists(places_db) && browser->profile_count < BROWSER_MAX_PROFILES) {
            BrowserProfile *p = &browser->profiles[browser->profile_count];

            /* Extract profile name: "a1b2c3d4.default-release" → "default-release" */
            const char *dot = strchr(find_data.cFileName, '.');
            if (dot) {
                strncpy(p->name, dot + 1, sizeof(p->name) - 1);
            } else {
                strncpy(p->name, find_data.cFileName, sizeof(p->name) - 1);
            }
            p->name[sizeof(p->name) - 1] = '\0';

            strncpy(p->path, profile_path, sizeof(p->path) - 1);
            strncpy(p->history_db, places_db, sizeof(p->history_db) - 1);
            p->is_valid = 1;
            browser->profile_count++;
        }
    } while (FindNextFileA(hFind, &find_data) && browser->profile_count < BROWSER_MAX_PROFILES);

    FindClose(hFind);
#else
    DIR *dir = opendir(firefox_dir);
    if (!dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && browser->profile_count < BROWSER_MAX_PROFILES) {
        if (entry->d_name[0] == '.') continue;

        char profile_path[BROWSER_MAX_PATH];
        snprintf(profile_path, sizeof(profile_path), "%s/%s",
                firefox_dir, entry->d_name);

        if (!is_directory(profile_path)) continue;

        char places_db[BROWSER_MAX_PATH];
        snprintf(places_db, sizeof(places_db), "%s/places.sqlite", profile_path);

        if (path_exists(places_db)) {
            BrowserProfile *p = &browser->profiles[browser->profile_count];

            /* Extract profile name from "a1b2c3d4.default-release" */
            const char *dot = strchr(entry->d_name, '.');
            if (dot) {
                strncpy(p->name, dot + 1, sizeof(p->name) - 1);
            } else {
                strncpy(p->name, entry->d_name, sizeof(p->name) - 1);
            }
            p->name[sizeof(p->name) - 1] = '\0';

            strncpy(p->path, profile_path, sizeof(p->path) - 1);
            strncpy(p->history_db, places_db, sizeof(p->history_db) - 1);
            p->is_valid = 1;
            browser->profile_count++;
        }
    }

    closedir(dir);
#endif

    return browser->profile_count;
}

/* ── Backend implementations ─────────────────────────────────────── */

/* Chromium History SQL */
static const char *chromium_history_sql(int days_back) {
    (void)days_back;
    /* The actual days_back filtering is done in the query function */
    return "SELECT u.url, u.title, u.visit_count, u.typed_count, "
           "u.last_visit_time "
           "FROM urls u "
           "ORDER BY u.last_visit_time DESC";
}

static const char *chromium_history_sql_filtered(void) {
    return "SELECT u.url, u.title, u.visit_count, u.typed_count, "
           "u.last_visit_time "
           "FROM urls u "
           "WHERE u.last_visit_time >= ? "
           "ORDER BY u.last_visit_time DESC";
}

/* Firefox History SQL */
static const char *firefox_history_sql(int days_back) {
    (void)days_back;
    return "SELECT p.url, p.title, p.visit_count, p.typed, "
           "MAX(v.visit_date) as last_visit "
           "FROM moz_places p "
           "JOIN moz_historyvisits v ON p.id = v.place_id "
           "GROUP BY p.id "
           "ORDER BY last_visit DESC";
}

static const char *firefox_history_sql_filtered(void) {
    return "SELECT p.url, p.title, p.visit_count, p.typed, "
           "MAX(v.visit_date) as last_visit "
           "FROM moz_places p "
           "JOIN moz_historyvisits v ON p.id = v.place_id "
           "WHERE v.visit_date >= ? "
           "GROUP BY p.id "
           "ORDER BY last_visit DESC";
}

/* Chromium timestamp normalization */
static long long chromium_normalize(long long raw_ts) {
    return browser_normalize_timestamp(raw_ts, TS_FORMAT_CHROMIUM);
}

/* Firefox timestamp normalization */
static long long firefox_normalize(long long raw_ts) {
    return browser_normalize_timestamp(raw_ts, TS_FORMAT_FIREFOX);
}

/**
 * Generic query function that works for both Chromium and Firefox schemas.
 */
static int generic_query_history(const char *db_path, int days_back,
                                  HistoryResult *result, TimestampFormat ts_fmt) {
    if (!db_path || !result) return -1;

    /* Copy DB to avoid locking the browser's active database */
    char tmp_path[BROWSER_MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s_tmp_copy", db_path);

    /* Simple file copy */
    FILE *src = fopen(db_path, "rb");
    if (!src) {
        fprintf(stderr, "[browser_detect] Cannot open %s\n", db_path);
        return -1;
    }
    FILE *dst = fopen(tmp_path, "wb");
    if (!dst) {
        fclose(src);
        return -1;
    }
    char copy_buf[8192];
    size_t n;
    while ((n = fread(copy_buf, 1, sizeof(copy_buf), src)) > 0) {
        fwrite(copy_buf, 1, n, dst);
    }
    fclose(src);
    fclose(dst);

    /* Open the copy */
    sqlite3 *db;
    int rc = sqlite3_open_v2(tmp_path, &db, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[browser_detect] SQLite open error: %s\n",
                sqlite3_errmsg(db));
        sqlite3_close(db);
        remove(tmp_path);
        return -1;
    }

    /* Select appropriate SQL */
    const char *sql;
    long long cutoff = 0;

    int is_firefox = (ts_fmt == TS_FORMAT_FIREFOX);

    if (days_back > 0) {
        sql = is_firefox ? firefox_history_sql_filtered()
                         : chromium_history_sql_filtered();

        /* Compute cutoff timestamp in the browser's native format */
        time_t now = time(NULL);
        long long cutoff_unix = (long long)now - (long long)days_back * 86400LL;

        if (ts_fmt == TS_FORMAT_CHROMIUM) {
            cutoff = (cutoff_unix + 11644473600LL) * 1000000LL;
        } else if (ts_fmt == TS_FORMAT_FIREFOX) {
            cutoff = cutoff_unix * 1000000LL;
        }
    } else {
        sql = is_firefox ? firefox_history_sql(0) : chromium_history_sql(0);
    }

    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[browser_detect] SQL prepare error: %s\n",
                sqlite3_errmsg(db));
        sqlite3_close(db);
        remove(tmp_path);
        return -1;
    }

    if (days_back > 0) {
        sqlite3_bind_int64(stmt, 1, cutoff);
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        /* Grow result array if needed */
        if (result->count >= result->capacity) {
            int new_cap = result->capacity * 2;
            if (new_cap < 512) new_cap = 512;
            HistoryEntry *tmp = realloc(result->entries,
                                        (size_t)new_cap * sizeof(HistoryEntry));
            if (!tmp) break;
            result->entries = tmp;
            result->capacity = new_cap;
        }

        HistoryEntry *e = &result->entries[result->count];
        memset(e, 0, sizeof(*e));

        const char *url = (const char *)sqlite3_column_text(stmt, 0);
        const char *title = (const char *)sqlite3_column_text(stmt, 1);

        if (url) {
            strncpy(e->url, url, sizeof(e->url) - 1);
            e->url[sizeof(e->url) - 1] = '\0';
        }
        if (title) {
            strncpy(e->title, title, sizeof(e->title) - 1);
            e->title[sizeof(e->title) - 1] = '\0';
        }

        e->visit_count = sqlite3_column_int(stmt, 2);
        e->typed_count = sqlite3_column_int(stmt, 3);
        e->last_visit_raw = sqlite3_column_int64(stmt, 4);

        /* Normalize timestamp to ISO 8601 */
        long long unix_ts = browser_normalize_timestamp(e->last_visit_raw, ts_fmt);
        time_t t = (time_t)unix_ts;
        struct tm *tm = gmtime(&t);
        if (tm) {
            strftime(e->last_visit_time, sizeof(e->last_visit_time),
                    "%Y-%m-%dT%H:%M:%SZ", tm);
        }

        result->count++;
        count++;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    remove(tmp_path);

    return count;
}

/* Chromium query wrapper */
static int chromium_query_history(const char *db_path, int days_back,
                                   HistoryResult *result, TimestampFormat ts_fmt) {
    (void)ts_fmt;
    return generic_query_history(db_path, days_back, result, TS_FORMAT_CHROMIUM);
}

/* Firefox query wrapper */
static int firefox_query_history(const char *db_path, int days_back,
                                  HistoryResult *result, TimestampFormat ts_fmt) {
    (void)ts_fmt;
    return generic_query_history(db_path, days_back, result, TS_FORMAT_FIREFOX);
}

/* ── Backend definitions ─────────────────────────────────────────── */

static const BrowserBackend CHROMIUM_BACKEND = {
    .type = BROWSER_CHROME,
    .name = "Chromium",
    .query_history = chromium_query_history,
    .query_bookmarks = NULL,
    .query_downloads = NULL,
    .get_history_sql = chromium_history_sql,
    .normalize_timestamp = chromium_normalize
};

static const BrowserBackend FIREFOX_BACKEND = {
    .type = BROWSER_FIREFOX,
    .name = "Firefox",
    .query_history = firefox_query_history,
    .query_bookmarks = NULL,
    .query_downloads = NULL,
    .get_history_sql = firefox_history_sql,
    .normalize_timestamp = firefox_normalize
};

const BrowserBackend *browser_get_backend(BrowserType type) {
    switch (type) {
        case BROWSER_BRAVE:
        case BROWSER_CHROME:
        case BROWSER_CHROMIUM:
        case BROWSER_EDGE:
        case BROWSER_VIVALDI:
        case BROWSER_OPERA:
            return &CHROMIUM_BACKEND;

        case BROWSER_FIREFOX:
            return &FIREFOX_BACKEND;

        default:
            return NULL;
    }
}

/* ── Browser scanning ────────────────────────────────────────────── */

int browser_scan(BrowserScanResult *result) {
    if (!result) return -1;

    memset(result, 0, sizeof(*result));

    BrowserType types_to_check[] = {
        BROWSER_BRAVE, BROWSER_CHROME, BROWSER_CHROMIUM,
        BROWSER_EDGE, BROWSER_VIVALDI, BROWSER_OPERA, BROWSER_FIREFOX
    };
    int num_types = sizeof(types_to_check) / sizeof(types_to_check[0]);

    for (int i = 0; i < num_types && result->count < BROWSER_MAX_DETECTED; i++) {
        BrowserType type = types_to_check[i];
        char config_dir[BROWSER_MAX_PATH];

        if (browser_default_config_dir(type, config_dir, sizeof(config_dir)) != 0) {
            continue;
        }

        if (!is_directory(config_dir)) continue;

        DetectedBrowser *browser = &result->browsers[result->count];
        memset(browser, 0, sizeof(*browser));

        browser->type = type;
        strncpy(browser->name, browser_type_name(type), sizeof(browser->name) - 1);
        strncpy(browser->base_path, config_dir, sizeof(browser->base_path) - 1);

        if (type == BROWSER_FIREFOX) {
            browser->is_chromium_based = 0;
            browser->ts_format = TS_FORMAT_FIREFOX;
            enumerate_firefox_profiles(config_dir, browser);
        } else {
            browser->is_chromium_based = 1;
            browser->ts_format = TS_FORMAT_CHROMIUM;
            enumerate_chromium_profiles(config_dir, browser);
        }

        if (browser->profile_count > 0) {
            result->total_profiles += browser->profile_count;
            result->count++;
            printf("[browser_detect] Found %s with %d profile(s)\n",
                   browser->name, browser->profile_count);
        }
    }

    return result->count;
}

/* ── Multi-browser query ─────────────────────────────────────────── */

int browser_query_all(const BrowserScanResult *scan, int days_back,
                      HistoryResult *result) {
    if (!scan || !result) return -1;

    int total = 0;

    for (int i = 0; i < scan->count; i++) {
        const DetectedBrowser *browser = &scan->browsers[i];
        const BrowserBackend *backend = browser_get_backend(browser->type);
        if (!backend || !backend->query_history) continue;

        for (int j = 0; j < browser->profile_count; j++) {
            const BrowserProfile *profile = &browser->profiles[j];
            if (!profile->is_valid) continue;

            printf("[browser_detect] Querying %s [%s]...\n",
                   browser->name, profile->name);

            int n = backend->query_history(profile->history_db, days_back,
                                           result, browser->ts_format);
            if (n > 0) total += n;
        }
    }

    return total;
}

int browser_query_by_type(BrowserType type, int days_back,
                          HistoryResult *result) {
    BrowserScanResult scan;
    if (browser_scan(&scan) <= 0) return -1;

    int total = 0;
    for (int i = 0; i < scan.count; i++) {
        if (scan.browsers[i].type != type) continue;

        const DetectedBrowser *browser = &scan.browsers[i];
        const BrowserBackend *backend = browser_get_backend(type);
        if (!backend || !backend->query_history) continue;

        for (int j = 0; j < browser->profile_count; j++) {
            const BrowserProfile *profile = &browser->profiles[j];
            if (!profile->is_valid) continue;

            int n = backend->query_history(profile->history_db, days_back,
                                           result, browser->ts_format);
            if (n > 0) total += n;
        }
    }

    return total;
}

/* ── Deduplication ───────────────────────────────────────────────── */

/**
 * Simple hash function for URL strings.
 */
static unsigned int url_hash(const char *url) {
    unsigned int h = 5381;
    while (*url) {
        h = ((h << 5) + h) + (unsigned char)*url;
        url++;
    }
    return h;
}

int browser_deduplicate(HistoryResult *result) {
    if (!result || result->count <= 1) return 0;

    /*
     * O(n²) deduplication with hash pre-check.
     * For each entry, check if a later entry has the same URL.
     * If so, merge visit counts and keep the latest timestamp.
     */
    int removed = 0;
    int *marks = calloc((size_t)result->count, sizeof(int));
    if (!marks) return 0;

    for (int i = 0; i < result->count; i++) {
        if (marks[i]) continue;

        unsigned int h1 = url_hash(result->entries[i].url);

        for (int j = i + 1; j < result->count; j++) {
            if (marks[j]) continue;

            unsigned int h2 = url_hash(result->entries[j].url);
            if (h1 != h2) continue;

            if (strcmp(result->entries[i].url, result->entries[j].url) == 0) {
                /* Merge: keep higher visit count, latest timestamp */
                if (result->entries[j].visit_count > result->entries[i].visit_count) {
                    result->entries[i].visit_count = result->entries[j].visit_count;
                }
                if (result->entries[j].last_visit_raw > result->entries[i].last_visit_raw) {
                    result->entries[i].last_visit_raw = result->entries[j].last_visit_raw;
                    strncpy(result->entries[i].last_visit_time,
                            result->entries[j].last_visit_time,
                            sizeof(result->entries[i].last_visit_time) - 1);
                }
                marks[j] = 1;
                removed++;
            }
        }
    }

    /* Compact the array by removing marked entries */
    int write_idx = 0;
    for (int read_idx = 0; read_idx < result->count; read_idx++) {
        if (!marks[read_idx]) {
            if (write_idx != read_idx) {
                result->entries[write_idx] = result->entries[read_idx];
            }
            write_idx++;
        }
    }
    result->count = write_idx;

    free(marks);
    return removed;
}
