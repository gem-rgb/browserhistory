/**
 * url_parser.h — Low-level URL decomposition and normalization
 *
 * Parses RFC 3986 URIs into component parts, performs percent-decoding,
 * path normalization, and query parameter extraction.
 *
 * @version 1.0.0
 */

#ifndef URL_PARSER_H
#define URL_PARSER_H

#include <stddef.h>

/* ── Limits ──────────────────────────────────────────────────────── */

#define URL_MAX_SCHEME      32
#define URL_MAX_USERINFO   256
#define URL_MAX_HOST       256
#define URL_MAX_PORT        8
#define URL_MAX_PATH      2048
#define URL_MAX_QUERY     4096
#define URL_MAX_FRAGMENT   512
#define URL_MAX_PARAMS      64

/* ── Data Structures ─────────────────────────────────────────────── */

/** A single key-value pair from a query string */
typedef struct {
    char key[256];
    char value[512];
} QueryParam;

/** Fully decomposed URL */
typedef struct {
    char scheme[URL_MAX_SCHEME];
    char userinfo[URL_MAX_USERINFO];
    char host[URL_MAX_HOST];
    int  port;                          /* -1 if not specified */
    char path[URL_MAX_PATH];
    char query[URL_MAX_QUERY];
    char fragment[URL_MAX_FRAGMENT];

    /* Extracted host components */
    char hostname_decoded[URL_MAX_HOST]; /* after punycode/IDN decode */
    int  is_ipv4;
    int  is_ipv6;
    unsigned char ipv4_addr[4];

    /* Parsed query parameters */
    QueryParam params[URL_MAX_PARAMS];
    int        param_count;
} ParsedUrl;

/** Result of extracting just the domain from a URL */
typedef struct {
    char full_host[URL_MAX_HOST];       /* e.g., "mail.google.com" */
    char registrable_domain[URL_MAX_HOST]; /* e.g., "google.com" */
    char tld[64];                       /* e.g., "com" */
    char subdomain[URL_MAX_HOST];       /* e.g., "mail" */
} DomainInfo;

/* ── API ─────────────────────────────────────────────────────────── */

/**
 * Parse a URL string into its component parts.
 *
 * @param url_str   The raw URL string to parse
 * @param len       Length of the URL string (0 = use strlen)
 * @param out       Output: populated ParsedUrl structure
 * @return 0 on success, -1 on malformed URL
 */
int url_parse(const char *url_str, size_t len, ParsedUrl *out);

/**
 * Normalize a parsed URL in-place.
 * - Lowercases scheme and host
 * - Collapses /../ and /./ in paths
 * - Removes duplicate slashes
 * - Decodes unnecessary percent-encoding
 * - Removes default ports (80 for http, 443 for https)
 *
 * @param url   The parsed URL to normalize (modified in-place)
 * @return 0 on success, -1 on error
 */
int url_normalize(ParsedUrl *url);

/**
 * Percent-decode a string in-place.
 * Handles %XX hex sequences and + as space (in query context).
 *
 * @param str           String to decode (modified in-place)
 * @param plus_as_space If nonzero, decode '+' as ' '
 * @return 0 on success, -1 on invalid encoding
 */
int url_percent_decode(char *str, int plus_as_space);

/**
 * Percent-encode a string.
 *
 * @param dst       Output buffer
 * @param dstsize   Size of output buffer
 * @param src       Source string to encode
 * @param encode_set Which characters to encode (0 = path, 1 = query, 2 = full)
 * @return Number of characters written, or -1 on error
 */
int url_percent_encode(char *dst, size_t dstsize, const char *src, int encode_set);

/**
 * Extract domain information from a parsed URL.
 *
 * @param url   The parsed URL
 * @param info  Output: domain components
 * @return 0 on success, -1 on error
 */
int url_extract_domain(const ParsedUrl *url, DomainInfo *info);

/**
 * Parse query string into key-value parameters.
 * Populates url->params and url->param_count.
 *
 * @param url   The parsed URL (query field must be set)
 * @return Number of parameters parsed, or -1 on error
 */
int url_parse_query_params(ParsedUrl *url);

/**
 * Validate that a string is valid UTF-8.
 *
 * @param str   String to validate
 * @param len   Length of string (0 = use strlen)
 * @return 1 if valid UTF-8, 0 if not
 */
int url_validate_utf8(const char *str, size_t len);

/**
 * Reconstruct a URL string from its parsed components.
 *
 * @param url   The parsed URL
 * @param dst   Output buffer
 * @param dstsize Size of output buffer
 * @return Number of characters written, or -1 on error
 */
int url_reconstruct(const ParsedUrl *url, char *dst, size_t dstsize);

#endif /* URL_PARSER_H */
