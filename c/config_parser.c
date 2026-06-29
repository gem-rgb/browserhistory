/**
 * config_parser.c — INI/TOML-style configuration file parser
 *
 * Parses configuration files with the following syntax:
 *   - Sections: [section_name]
 *   - Key-value: key = value
 *   - Comments: # or ; at start of line
 *   - Multiline: trailing \ continues to next line
 *   - Includes: @include "path/to/other.conf"
 *   - Env vars: ${VAR_NAME} or %VAR_NAME%
 *   - Types: string, int, float, bool (true/false/yes/no)
 *
 * @version 1.0.0
 */

#include "config_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Environment variable expansion ──────────────────────────────── */

int config_expand_env(char *dst, size_t dstsize, const char *src) {
    if (!dst || !src || dstsize == 0) return -1;

    size_t di = 0;
    const char *p = src;

    while (*p && di < dstsize - 1) {
        if (*p == '$' && *(p + 1) == '{') {
            /* Unix-style: ${VAR_NAME} */
            const char *var_start = p + 2;
            const char *var_end = strchr(var_start, '}');
            if (var_end) {
                size_t vname_len = (size_t)(var_end - var_start);
                char varname[256];
                if (vname_len >= sizeof(varname)) vname_len = sizeof(varname) - 1;
                memcpy(varname, var_start, vname_len);
                varname[vname_len] = '\0';

                const char *val = getenv(varname);
                if (val) {
                    size_t vlen = strlen(val);
                    if (di + vlen >= dstsize) vlen = dstsize - di - 1;
                    memcpy(dst + di, val, vlen);
                    di += vlen;
                }
                p = var_end + 1;
            } else {
                dst[di++] = *p++;
            }
        }
#ifdef _WIN32
        else if (*p == '%') {
            /* Windows-style: %VAR_NAME% */
            const char *var_start = p + 1;
            const char *var_end = strchr(var_start, '%');
            if (var_end && var_end > var_start) {
                size_t vname_len = (size_t)(var_end - var_start);
                char varname[256];
                if (vname_len >= sizeof(varname)) vname_len = sizeof(varname) - 1;
                memcpy(varname, var_start, vname_len);
                varname[vname_len] = '\0';

                const char *val = getenv(varname);
                if (val) {
                    size_t vlen = strlen(val);
                    if (di + vlen >= dstsize) vlen = dstsize - di - 1;
                    memcpy(dst + di, val, vlen);
                    di += vlen;
                }
                p = var_end + 1;
            } else {
                dst[di++] = *p++;
            }
        }
#endif
        else {
            dst[di++] = *p++;
        }
    }

    dst[di] = '\0';
    return 0;
}

/* ── Type detection and coercion ─────────────────────────────────── */

static ConfigValueType detect_type(const char *value) {
    if (!value || !value[0]) return CONFIG_TYPE_STRING;

    /* Boolean check */
    if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 ||
        strcasecmp(value, "on") == 0 || strcasecmp(value, "1") == 0 ||
        strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 ||
        strcasecmp(value, "off") == 0 || strcasecmp(value, "0") == 0) {
        return CONFIG_TYPE_BOOL;
    }

    /* Array check — comma-separated values */
    if (strchr(value, ',') && !strchr(value, ' ')) {
        return CONFIG_TYPE_ARRAY;
    }

    /* Number check */
    const char *p = value;
    if (*p == '-' || *p == '+') p++;

    int has_dot = 0;
    int has_digit = 0;
    while (*p) {
        if (*p == '.') {
            if (has_dot) return CONFIG_TYPE_STRING;
            has_dot = 1;
        } else if (isdigit((unsigned char)*p)) {
            has_digit = 1;
        } else {
            return CONFIG_TYPE_STRING;
        }
        p++;
    }

    if (has_digit) {
        return has_dot ? CONFIG_TYPE_FLOAT : CONFIG_TYPE_INT;
    }

    return CONFIG_TYPE_STRING;
}

static int parse_bool(const char *value) {
    if (!value) return 0;
    if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 ||
        strcasecmp(value, "on") == 0 || strcasecmp(value, "1") == 0) {
        return 1;
    }
    return 0;
}

/* ── Trimming ────────────────────────────────────────────────────── */

static char *trim_whitespace(char *str) {
    while (*str && isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return str;
}

/**
 * Strip surrounding quotes from a value if present.
 */
static void strip_quotes(char *value) {
    size_t len = strlen(value);
    if (len >= 2) {
        if ((value[0] == '"' && value[len - 1] == '"') ||
            (value[0] == '\'' && value[len - 1] == '\'')) {
            memmove(value, value + 1, len - 2);
            value[len - 2] = '\0';
        }
    }
}

/* ── Include handling ────────────────────────────────────────────── */

/**
 * Check if a path is already in the include list (circular include detection).
 */
static int is_circular_include(const Config *config, const char *path) {
    for (int i = 0; i < config->include_count; i++) {
        if (strcmp(config->include_paths[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ── Forward declaration for recursive include ───────────────────── */

static int parse_buffer_internal(const char *data, size_t data_len,
                                  Config *config, const char *current_section);

/* ── Line-by-line parser ─────────────────────────────────────────── */

static int parse_buffer_internal(const char *data, size_t data_len,
                                  Config *config, const char *current_section) {
    char section[CONFIG_MAX_KEY_LEN];
    if (current_section) {
        strncpy(section, current_section, sizeof(section) - 1);
        section[sizeof(section) - 1] = '\0';
    } else {
        section[0] = '\0';
    }

    const char *p = data;
    const char *end = data + data_len;
    int line_number = 0;

    /* Multiline accumulation buffer */
    char line_buf[CONFIG_MAX_VALUE_LEN * 2];
    int continuing = 0;
    size_t accum_len = 0;

    while (p < end) {
        /* Read one line */
        const char *line_start = p;
        while (p < end && *p != '\n' && *p != '\r') p++;

        size_t line_len = (size_t)(p - line_start);
        line_number++;

        /* Skip EOL */
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;

        /* Handle multiline continuation */
        if (continuing) {
            /* Append this line to accumulation buffer */
            if (accum_len + line_len + 1 < sizeof(line_buf)) {
                memcpy(line_buf + accum_len, line_start, line_len);
                accum_len += line_len;
            }

            /* Check for trailing backslash */
            if (accum_len > 0 && line_buf[accum_len - 1] == '\\') {
                line_buf[accum_len - 1] = ' ';
                continue;  /* keep accumulating */
            }

            line_buf[accum_len] = '\0';
            continuing = 0;

            /* Process accumulated line */
            line_start = line_buf;
            line_len = accum_len;
        } else {
            /* Check for trailing backslash (continuation) */
            if (line_len > 0 && line_start[line_len - 1] == '\\') {
                if (line_len < sizeof(line_buf)) {
                    memcpy(line_buf, line_start, line_len - 1);
                    line_buf[line_len - 1] = ' ';
                    accum_len = line_len;
                    continuing = 1;
                    continue;
                }
            }
        }

        /* Copy line for safe manipulation */
        char line[CONFIG_MAX_VALUE_LEN * 2];
        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        /* Trim */
        char *trimmed = trim_whitespace(line);

        /* Skip empty lines and comments */
        if (!trimmed[0] || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        /* Handle @include directive */
        if (strncmp(trimmed, "@include", 8) == 0) {
            char *path = trimmed + 8;
            path = trim_whitespace(path);
            strip_quotes(path);

            if (path[0] && !is_circular_include(config, path)) {
                if (config->include_count < CONFIG_MAX_INCLUDES) {
                    strncpy(config->include_paths[config->include_count],
                            path, CONFIG_MAX_VALUE_LEN - 1);
                    config->include_count++;

                    /* Read and parse included file */
                    FILE *inc = fopen(path, "rb");
                    if (inc) {
                        fseek(inc, 0, SEEK_END);
                        long fsize = ftell(inc);
                        fseek(inc, 0, SEEK_SET);

                        if (fsize > 0 && fsize < 10 * 1024 * 1024) {
                            char *inc_data = malloc((size_t)fsize + 1);
                            if (inc_data) {
                                size_t rd = fread(inc_data, 1, (size_t)fsize, inc);
                                inc_data[rd] = '\0';
                                parse_buffer_internal(inc_data, rd, config, section);
                                free(inc_data);
                            }
                        }
                        fclose(inc);
                    }
                }
            }
            continue;
        }

        /* Handle section header: [section_name] */
        if (trimmed[0] == '[') {
            char *close = strchr(trimmed, ']');
            if (close) {
                size_t slen = (size_t)(close - trimmed - 1);
                if (slen >= sizeof(section)) slen = sizeof(section) - 1;
                memcpy(section, trimmed + 1, slen);
                section[slen] = '\0';

                char *sec_trimmed = trim_whitespace(section);
                memmove(section, sec_trimmed, strlen(sec_trimmed) + 1);

                /* Add to section list if new */
                int found = 0;
                for (int i = 0; i < config->section_count; i++) {
                    if (strcmp(config->sections[i], section) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found && config->section_count < CONFIG_MAX_SECTIONS) {
                    strncpy(config->sections[config->section_count],
                            section, CONFIG_MAX_KEY_LEN - 1);
                    config->section_count++;
                }
            }
            continue;
        }

        /* Handle key = value pair */
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;  /* not a valid line */

        /* Extract key */
        size_t key_len = (size_t)(eq - trimmed);
        char key[CONFIG_MAX_KEY_LEN];
        if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
        memcpy(key, trimmed, key_len);
        key[key_len] = '\0';

        char *key_trimmed = trim_whitespace(key);

        /* Extract value */
        char *val = eq + 1;
        val = trim_whitespace(val);

        /* Strip inline comments (# or ; not inside quotes) */
        int in_quotes = 0;
        for (char *sc = val; *sc; sc++) {
            if (*sc == '"' || *sc == '\'') in_quotes = !in_quotes;
            if (!in_quotes && (*sc == '#' || *sc == ';')) {
                *sc = '\0';
                break;
            }
        }
        val = trim_whitespace(val);
        strip_quotes(val);

        /* Store entry */
        if (config->entry_count >= CONFIG_MAX_KEYS) {
            snprintf(config->error_msg, sizeof(config->error_msg),
                    "Too many config entries (max %d)", CONFIG_MAX_KEYS);
            config->error_line = line_number;
            return -1;
        }

        ConfigEntry *entry = &config->entries[config->entry_count];
        memset(entry, 0, sizeof(*entry));

        strncpy(entry->section, section, sizeof(entry->section) - 1);
        strncpy(entry->key, key_trimmed, sizeof(entry->key) - 1);
        strncpy(entry->raw_value, val, sizeof(entry->raw_value) - 1);
        entry->line_number = line_number;

        /* Expand environment variables */
        config_expand_env(entry->expanded_value, sizeof(entry->expanded_value),
                         entry->raw_value);

        /* Detect and coerce type */
        entry->type = detect_type(entry->expanded_value);
        switch (entry->type) {
            case CONFIG_TYPE_INT:
                entry->int_value = atoi(entry->expanded_value);
                entry->float_value = (float)entry->int_value;
                break;
            case CONFIG_TYPE_FLOAT:
                entry->float_value = (float)atof(entry->expanded_value);
                entry->int_value = (int)entry->float_value;
                break;
            case CONFIG_TYPE_BOOL:
                entry->bool_value = parse_bool(entry->expanded_value);
                entry->int_value = entry->bool_value;
                break;
            default:
                break;
        }

        config->entry_count++;
    }

    return 0;
}

/* ── Public API ──────────────────────────────────────────────────── */

int config_parse_buffer(const char *data, size_t data_len, Config *config) {
    if (!data || data_len == 0 || !config) return -1;

    memset(config, 0, sizeof(*config));
    return parse_buffer_internal(data, data_len, config, NULL);
}

int config_parse(const char *filepath, Config *config) {
    if (!filepath || !config) return -1;

    memset(config, 0, sizeof(*config));
    strncpy(config->filepath, filepath, sizeof(config->filepath) - 1);

    /* Track this file for circular include detection */
    strncpy(config->include_paths[0], filepath, CONFIG_MAX_VALUE_LEN - 1);
    config->include_count = 1;

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        snprintf(config->error_msg, sizeof(config->error_msg),
                "Cannot open config file: %s", filepath);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {
        snprintf(config->error_msg, sizeof(config->error_msg),
                "Config file too large or empty: %ld bytes", fsize);
        fclose(f);
        return -1;
    }

    char *data = malloc((size_t)fsize + 1);
    if (!data) {
        fclose(f);
        return -1;
    }

    size_t rd = fread(data, 1, (size_t)fsize, f);
    fclose(f);
    data[rd] = '\0';

    int rc = parse_buffer_internal(data, rd, config, NULL);
    free(data);
    return rc;
}

/* ── Lookup functions ────────────────────────────────────────────── */

static const ConfigEntry *find_entry(const Config *config,
                                      const char *section, const char *key) {
    for (int i = 0; i < config->entry_count; i++) {
        const ConfigEntry *e = &config->entries[i];
        if (strcmp(e->section, section ? section : "") == 0 &&
            strcmp(e->key, key) == 0) {
            return e;
        }
    }
    return NULL;
}

const char *config_get_string(const Config *config, const char *section,
                              const char *key, const char *default_val) {
    const ConfigEntry *e = find_entry(config, section, key);
    return e ? e->expanded_value : default_val;
}

int config_get_int(const Config *config, const char *section,
                   const char *key, int default_val) {
    const ConfigEntry *e = find_entry(config, section, key);
    if (!e) return default_val;
    return (e->type == CONFIG_TYPE_INT || e->type == CONFIG_TYPE_FLOAT)
           ? e->int_value : atoi(e->expanded_value);
}

float config_get_float(const Config *config, const char *section,
                       const char *key, float default_val) {
    const ConfigEntry *e = find_entry(config, section, key);
    if (!e) return default_val;
    return (e->type == CONFIG_TYPE_FLOAT || e->type == CONFIG_TYPE_INT)
           ? e->float_value : (float)atof(e->expanded_value);
}

int config_get_bool(const Config *config, const char *section,
                    const char *key, int default_val) {
    const ConfigEntry *e = find_entry(config, section, key);
    if (!e) return default_val;
    return (e->type == CONFIG_TYPE_BOOL) ? e->bool_value : parse_bool(e->expanded_value);
}

/* ── Schema validation ───────────────────────────────────────────── */

int config_validate(Config *config, const ConfigSchema *schema) {
    if (!config || !schema) return -1;

    for (int i = 0; schema[i].key; i++) {
        const ConfigEntry *e = find_entry(config, schema[i].section, schema[i].key);

        if (!e) {
            if (schema[i].required) {
                snprintf(config->error_msg, sizeof(config->error_msg),
                        "Required key [%s].%s is missing",
                        schema[i].section ? schema[i].section : "",
                        schema[i].key);
                return -1;
            }
            continue;
        }

        /* Type check */
        if (schema[i].expected_type != CONFIG_TYPE_STRING &&
            e->type != schema[i].expected_type) {
            snprintf(config->error_msg, sizeof(config->error_msg),
                    "Key [%s].%s has wrong type (expected %d, got %d)",
                    schema[i].section ? schema[i].section : "",
                    schema[i].key, schema[i].expected_type, e->type);
            return -1;
        }

        /* Range check for integers */
        if (e->type == CONFIG_TYPE_INT) {
            if (schema[i].int_min != 0 || schema[i].int_max != 0) {
                if (e->int_value < schema[i].int_min ||
                    e->int_value > schema[i].int_max) {
                    snprintf(config->error_msg, sizeof(config->error_msg),
                            "Key [%s].%s value %d out of range [%d, %d]",
                            schema[i].section ? schema[i].section : "",
                            schema[i].key, e->int_value,
                            schema[i].int_min, schema[i].int_max);
                    return -1;
                }
            }
        }

        /* Enum check */
        if (schema[i].enum_values) {
            int found = 0;
            for (int j = 0; schema[i].enum_values[j]; j++) {
                if (strcmp(e->expanded_value, schema[i].enum_values[j]) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                snprintf(config->error_msg, sizeof(config->error_msg),
                        "Key [%s].%s value '%s' not in allowed values",
                        schema[i].section ? schema[i].section : "",
                        schema[i].key, e->expanded_value);
                return -1;
            }
        }
    }

    return 0;
}

void config_apply_defaults(Config *config, const ConfigSchema *schema) {
    if (!config || !schema) return;

    for (int i = 0; schema[i].key && config->entry_count < CONFIG_MAX_KEYS; i++) {
        if (!schema[i].default_value) continue;

        const ConfigEntry *existing = find_entry(config, schema[i].section, schema[i].key);
        if (existing) continue;

        ConfigEntry *e = &config->entries[config->entry_count];
        memset(e, 0, sizeof(*e));

        strncpy(e->section, schema[i].section ? schema[i].section : "",
                sizeof(e->section) - 1);
        strncpy(e->key, schema[i].key, sizeof(e->key) - 1);
        strncpy(e->raw_value, schema[i].default_value, sizeof(e->raw_value) - 1);
        strncpy(e->expanded_value, schema[i].default_value,
                sizeof(e->expanded_value) - 1);

        e->type = detect_type(e->expanded_value);
        switch (e->type) {
            case CONFIG_TYPE_INT:
                e->int_value = atoi(e->expanded_value);
                e->float_value = (float)e->int_value;
                break;
            case CONFIG_TYPE_FLOAT:
                e->float_value = (float)atof(e->expanded_value);
                break;
            case CONFIG_TYPE_BOOL:
                e->bool_value = parse_bool(e->expanded_value);
                break;
            default: break;
        }

        config->entry_count++;
    }
}

/* ── Platform-specific case-insensitive compare ──────────────────── */

#ifdef _WIN32
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#endif
