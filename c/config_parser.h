/**
 * config_parser.h — INI/TOML-style configuration file parser
 *
 * Parses configuration files with sections, key-value pairs, comments,
 * type coercion, environment variable expansion, and include directives.
 *
 * @version 1.0.0
 */

#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stddef.h>

/* ── Limits ──────────────────────────────────────────────────────── */

#define CONFIG_MAX_SECTIONS     64
#define CONFIG_MAX_KEYS        256
#define CONFIG_MAX_KEY_LEN     128
#define CONFIG_MAX_VALUE_LEN  2048
#define CONFIG_MAX_INCLUDES      8

/* ── Value types ─────────────────────────────────────────────────── */

typedef enum {
    CONFIG_TYPE_STRING = 0,
    CONFIG_TYPE_INT,
    CONFIG_TYPE_FLOAT,
    CONFIG_TYPE_BOOL,
    CONFIG_TYPE_ARRAY
} ConfigValueType;

/* ── Config entry ────────────────────────────────────────────────── */

typedef struct {
    char             section[CONFIG_MAX_KEY_LEN];
    char             key[CONFIG_MAX_KEY_LEN];
    char             raw_value[CONFIG_MAX_VALUE_LEN];
    char             expanded_value[CONFIG_MAX_VALUE_LEN]; /* after env expansion */
    ConfigValueType  type;
    int              int_value;
    float            float_value;
    int              bool_value;
    int              line_number;
} ConfigEntry;

/* ── Config file ─────────────────────────────────────────────────── */

typedef struct {
    ConfigEntry  entries[CONFIG_MAX_KEYS];
    int          entry_count;
    char         sections[CONFIG_MAX_SECTIONS][CONFIG_MAX_KEY_LEN];
    int          section_count;
    char         include_paths[CONFIG_MAX_INCLUDES][CONFIG_MAX_VALUE_LEN];
    int          include_count;
    char         filepath[CONFIG_MAX_VALUE_LEN];
    int          error_line;
    char         error_msg[256];
} Config;

/* ── Schema validation ───────────────────────────────────────────── */

typedef struct {
    const char      *section;
    const char      *key;
    ConfigValueType  expected_type;
    int              required;
    const char      *default_value;
    int              int_min;
    int              int_max;
    const char     **enum_values;  /* NULL-terminated array, or NULL */
} ConfigSchema;

/* ── API ─────────────────────────────────────────────────────────── */

/**
 * Parse a configuration file.
 *
 * @param filepath  Path to the config file
 * @param config    Output: parsed configuration
 * @return 0 on success, -1 on error (check config->error_msg)
 */
int config_parse(const char *filepath, Config *config);

/**
 * Parse configuration from an in-memory buffer.
 *
 * @param data      Config file content
 * @param data_len  Length of content
 * @param config    Output: parsed configuration
 * @return 0 on success, -1 on error
 */
int config_parse_buffer(const char *data, size_t data_len, Config *config);

/**
 * Get a string value from the config.
 *
 * @param config    The parsed config
 * @param section   Section name (empty string for global)
 * @param key       Key name
 * @param default_val Default value if key not found
 * @return The config value, or default_val
 */
const char *config_get_string(const Config *config, const char *section,
                              const char *key, const char *default_val);

/**
 * Get an integer value from the config.
 */
int config_get_int(const Config *config, const char *section,
                   const char *key, int default_val);

/**
 * Get a float value from the config.
 */
float config_get_float(const Config *config, const char *section,
                       const char *key, float default_val);

/**
 * Get a boolean value from the config.
 */
int config_get_bool(const Config *config, const char *section,
                    const char *key, int default_val);

/**
 * Validate config against a schema.
 *
 * @param config    The parsed config
 * @param schema    Array of schema entries (terminated by entry with NULL key)
 * @return 0 if valid, -1 if validation fails (check config->error_msg)
 */
int config_validate(Config *config, const ConfigSchema *schema);

/**
 * Merge defaults from a schema into the config.
 * Only fills in values that are not already set.
 */
void config_apply_defaults(Config *config, const ConfigSchema *schema);

/**
 * Expand environment variables in a string.
 * Handles ${VAR} (Unix) and %VAR% (Windows) syntax.
 *
 * @param dst       Output buffer
 * @param dstsize   Size of output buffer
 * @param src       Input string with env var references
 * @return 0 on success, -1 on error
 */
int config_expand_env(char *dst, size_t dstsize, const char *src);

#endif /* CONFIG_PARSER_H */
