/**
 * config_fuzzer.c — Fuzz harness for config parser and categorization rules
 *
 * Exercises both the INI/TOML-style config parser and the URL
 * categorization rules file loader:
 *   - Section parsing with nested brackets
 *   - Key-value extraction with inline comments
 *   - Multiline continuation with backslash
 *   - Environment variable expansion (${VAR}, %VAR%)
 *   - Type detection and coercion (int, float, bool)
 *   - Schema validation with range/enum checks
 *   - Include directive processing (circular detection)
 *   - Category rules with domain/glob/keyword lists
 *   - Glob pattern matching on generated inputs
 *   - Domain wildcard matching
 *
 * @version 1.0.0
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "config_parser.h"
#include "url_categorize.h"

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size) {
    if (size == 0 || size > 512 * 1024) return 0;

    /* ── Phase 1: Config parser ── */
    {
        Config config;
        config_parse_buffer((const char *)data, size, &config);

        /* Exercise lookup functions with various keys */
        config_get_string(&config, "", "key", "default");
        config_get_string(&config, "section", "key", "default");
        config_get_int(&config, "", "port", 8080);
        config_get_int(&config, "server", "port", 8080);
        config_get_float(&config, "limits", "threshold", 0.5f);
        config_get_bool(&config, "features", "enabled", 0);

        /* Exercise schema validation */
        ConfigSchema schema[] = {
            {"server", "port", CONFIG_TYPE_INT, 0, "8080", 1, 65535, NULL},
            {"server", "host", CONFIG_TYPE_STRING, 0, "localhost", 0, 0, NULL},
            {NULL, NULL, 0, 0, NULL, 0, 0, NULL}
        };
        config_validate(&config, schema);
        config_apply_defaults(&config, schema);

        /* Re-lookup after defaults applied */
        config_get_int(&config, "server", "port", 0);
        config_get_string(&config, "server", "host", NULL);
    }

    /* ── Phase 2: Environment variable expansion ── */
    if (size < 4096) {
        char *input = malloc(size + 1);
        if (input) {
            memcpy(input, data, size);
            input[size] = '\0';

            char expanded[4096];
            config_expand_env(expanded, sizeof(expanded), input);

            free(input);
        }
    }

    /* ── Phase 3: Category rules loading ── */
    {
        CatEngine engine;
        cat_engine_init_defaults(&engine);

        /* Load fuzz data as a rules file */
        cat_engine_load_rules_buffer(&engine, (const char *)data, size);

        /* Exercise categorization with the loaded rules */
        CatMatch match;
        cat_categorize_url(&engine, "https://www.google.com/search?q=test",
                           "test - Google Search", &match);
        cat_categorize_url(&engine, "https://github.com/user/repo",
                           "GitHub Repository", &match);
        cat_categorize_url(&engine, "https://unknown-domain.example/page",
                           "Some Page", &match);
    }

    /* ── Phase 4: Glob matching with fuzz data ── */
    if (size >= 2) {
        /* Split data into pattern + string to match */
        size_t split = size / 2;
        char *pattern = malloc(split + 1);
        char *test_str = malloc(size - split + 1);

        if (pattern && test_str) {
            memcpy(pattern, data, split);
            pattern[split] = '\0';
            memcpy(test_str, data + split, size - split);
            test_str[size - split] = '\0';

            cat_glob_match(pattern, test_str);
            cat_domain_match(pattern, test_str);
        }

        free(pattern);
        free(test_str);
    }

    return 0;
}
