/**
 * url_parser.c — Low-level URL decomposition and normalization
 *
 * Hand-rolled RFC 3986 URI parser. Splits a raw URL into scheme, authority
 * (userinfo, host, port), path, query, and fragment. Performs percent-decoding,
 * path normalization (collapse /../, /./), query parameter extraction, and
 * basic IPv4/IPv6 detection.
 *
 * The parser operates in a single pass over the input with post-processing
 * normalization steps. No dynamic allocation — all output goes into
 * fixed-size buffers in the ParsedUrl struct.
 *
 * @version 1.0.0
 */

#include "url_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Hex Decoding ────────────────────────────────────────────────── */

/**
 * Convert a single hex character to its numeric value.
 * Returns -1 if the character is not a valid hex digit.
 */
static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

/**
 * Check if a character is an unreserved character per RFC 3986.
 * unreserved = ALPHA / DIGIT / "-" / "." / "_" / "~"
 */
static int is_unreserved(unsigned char c) {
    return isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

/**
 * Check if a character is a valid scheme character.
 * scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
 */
static int is_scheme_char(char c, int first) {
    if (first) return isalpha((unsigned char)c);
    return isalnum((unsigned char)c) || c == '+' || c == '-' || c == '.';
}

/* ── Percent Encoding/Decoding ───────────────────────────────────── */

int url_percent_decode(char *str, int plus_as_space) {
    if (!str) return -1;

    char *src = str;
    char *dst = str;

    while (*src) {
        if (*src == '%') {
            /* Need at least two more characters */
            if (!src[1] || !src[2]) return -1;

            int hi = hex_digit_value(src[1]);
            int lo = hex_digit_value(src[2]);
            if (hi < 0 || lo < 0) return -1;

            unsigned char decoded = (unsigned char)((hi << 4) | lo);

            /* Don't decode NUL bytes — that would truncate the string */
            if (decoded == 0) return -1;

            *dst++ = (char)decoded;
            src += 3;
        } else if (plus_as_space && *src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return 0;
}

int url_percent_encode(char *dst, size_t dstsize, const char *src, int encode_set) {
    if (!dst || !src || dstsize == 0) return -1;

    size_t j = 0;
    static const char hex_chars[] = "0123456789ABCDEF";

    for (size_t i = 0; src[i] && j < dstsize - 1; i++) {
        unsigned char c = (unsigned char)src[i];
        int need_encode = 0;

        switch (encode_set) {
            case 0: /* Path encoding */
                need_encode = !is_unreserved(c) && c != '/' && c != ':' &&
                              c != '@' && c != '!' && c != '$' && c != '&' &&
                              c != '\'' && c != '(' && c != ')' && c != '*' &&
                              c != '+' && c != ',' && c != ';' && c != '=';
                break;
            case 1: /* Query encoding */
                need_encode = !is_unreserved(c) && c != '/' && c != '?' &&
                              c != ':' && c != '@' && c != '!' && c != '$' &&
                              c != '&' && c != '\'' && c != '(' && c != ')' &&
                              c != '*' && c != '+' && c != ',' && c != ';' &&
                              c != '=';
                break;
            default: /* Full encoding — only unreserved chars pass through */
                need_encode = !is_unreserved(c);
                break;
        }

        if (need_encode) {
            if (j + 3 >= dstsize) break;
            dst[j++] = '%';
            dst[j++] = hex_chars[(c >> 4) & 0x0F];
            dst[j++] = hex_chars[c & 0x0F];
        } else {
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
    return (int)j;
}

/* ── UTF-8 Validation ────────────────────────────────────────────── */

int url_validate_utf8(const char *str, size_t len) {
    if (!str) return 0;
    if (len == 0) len = strlen(str);

    const unsigned char *s = (const unsigned char *)str;
    size_t i = 0;

    while (i < len) {
        unsigned char c = s[i];

        if (c < 0x80) {
            /* ASCII — single byte */
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            /* 2-byte sequence: 110xxxxx 10xxxxxx */
            if (i + 1 >= len) return 0;
            if ((s[i+1] & 0xC0) != 0x80) return 0;
            /* Overlong check: must encode >= U+0080 */
            unsigned int cp = ((unsigned int)(c & 0x1F) << 6) |
                               (unsigned int)(s[i+1] & 0x3F);
            if (cp < 0x80) return 0;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            /* 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx */
            if (i + 2 >= len) return 0;
            if ((s[i+1] & 0xC0) != 0x80) return 0;
            if ((s[i+2] & 0xC0) != 0x80) return 0;
            unsigned int cp = ((unsigned int)(c & 0x0F) << 12) |
                              ((unsigned int)(s[i+1] & 0x3F) << 6) |
                               (unsigned int)(s[i+2] & 0x3F);
            if (cp < 0x800) return 0;
            /* Surrogate check */
            if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            /* 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
            if (i + 3 >= len) return 0;
            if ((s[i+1] & 0xC0) != 0x80) return 0;
            if ((s[i+2] & 0xC0) != 0x80) return 0;
            if ((s[i+3] & 0xC0) != 0x80) return 0;
            unsigned int cp = ((unsigned int)(c & 0x07) << 18) |
                              ((unsigned int)(s[i+1] & 0x3F) << 12) |
                              ((unsigned int)(s[i+2] & 0x3F) << 6) |
                               (unsigned int)(s[i+3] & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF) return 0;
            i += 4;
        } else {
            /* Invalid leading byte */
            return 0;
        }
    }
    return 1;
}

/* ── IPv4 Parsing ────────────────────────────────────────────────── */

/**
 * Try to parse an IPv4 address from the host string.
 * Returns 1 if valid IPv4, 0 otherwise.
 */
static int parse_ipv4(const char *host, unsigned char out[4]) {
    int parts[4];
    int n = 0;
    const char *p = host;

    while (*p && n < 4) {
        if (!isdigit((unsigned char)*p)) return 0;

        char *endp;
        long val = strtol(p, &endp, 10);
        if (val < 0 || val > 255) return 0;
        if (endp == p) return 0;

        parts[n++] = (int)val;
        p = endp;

        if (*p == '.') {
            p++;
            if (n >= 4) return 0;  /* too many dots */
        } else if (*p != '\0') {
            return 0;  /* invalid character */
        }
    }

    if (n != 4) return 0;

    for (int i = 0; i < 4; i++) out[i] = (unsigned char)parts[i];
    return 1;
}

/* ── IPv6 Parsing ────────────────────────────────────────────────── */

/**
 * Check if the host looks like a bracketed IPv6 address [xxxx:xxxx:...].
 * We only detect the format, not fully validate the address.
 */
static int detect_ipv6_bracket(const char *host) {
    if (!host || host[0] != '[') return 0;
    const char *close = strchr(host, ']');
    if (!close) return 0;
    /* Check that content is hex digits and colons */
    for (const char *p = host + 1; p < close; p++) {
        if (!isxdigit((unsigned char)*p) && *p != ':' && *p != '.') return 0;
    }
    return 1;
}

/* ── Scheme Extraction ───────────────────────────────────────────── */

/**
 * Extract the scheme from the beginning of a URL.
 * Returns the length of the scheme (including ://), or 0 if no scheme found.
 */
static size_t extract_scheme(const char *url, size_t len, char *scheme, size_t scheme_size) {
    size_t i = 0;

    /* Scheme must start with a letter */
    if (i >= len || !is_scheme_char(url[i], 1)) return 0;
    i++;

    /* Continue with scheme characters */
    while (i < len && is_scheme_char(url[i], 0)) {
        i++;
        if (i >= scheme_size - 1) return 0;  /* scheme too long */
    }

    /* Must be followed by :// or : */
    if (i >= len || url[i] != ':') return 0;

    /* Copy scheme (without the colon) */
    if (i >= scheme_size) i = scheme_size - 1;
    memcpy(scheme, url, i);
    scheme[i] = '\0';

    /* Lowercase the scheme */
    for (size_t j = 0; j < i; j++) {
        scheme[j] = (char)tolower((unsigned char)scheme[j]);
    }

    i++;  /* skip ':' */

    /* Skip optional "//" */
    if (i + 1 < len && url[i] == '/' && url[i+1] == '/') {
        i += 2;
    }

    return i;
}

/* ── Authority Parsing ───────────────────────────────────────────── */

/**
 * Parse the authority component: [userinfo@]host[:port]
 * Returns the number of characters consumed from the input.
 */
static size_t parse_authority(const char *auth, size_t len, ParsedUrl *out) {
    const char *p = auth;
    const char *end = auth + len;
    const char *host_start;
    const char *host_end;

    /* Look for userinfo@ */
    const char *at_sign = NULL;
    for (const char *scan = p; scan < end && *scan != '/' && *scan != '?' && *scan != '#'; scan++) {
        if (*scan == '@') {
            at_sign = scan;
            break;
        }
    }

    if (at_sign) {
        size_t ulen = (size_t)(at_sign - p);
        if (ulen >= sizeof(out->userinfo)) ulen = sizeof(out->userinfo) - 1;
        memcpy(out->userinfo, p, ulen);
        out->userinfo[ulen] = '\0';
        p = at_sign + 1;
    } else {
        out->userinfo[0] = '\0';
    }

    /* Parse host — handle IPv6 bracket notation */
    host_start = p;
    if (*p == '[') {
        /* IPv6: scan to closing bracket */
        const char *bracket_close = NULL;
        for (const char *scan = p + 1; scan < end; scan++) {
            if (*scan == ']') {
                bracket_close = scan;
                break;
            }
        }
        if (bracket_close) {
            host_end = bracket_close + 1;
        } else {
            /* Malformed — use rest of authority */
            host_end = end;
            for (const char *scan = p; scan < end; scan++) {
                if (*scan == '/' || *scan == '?' || *scan == '#') {
                    host_end = scan;
                    break;
                }
            }
        }
    } else {
        /* Regular host: scan until :, /, ?, or # */
        host_end = end;
        for (const char *scan = p; scan < end; scan++) {
            if (*scan == ':' || *scan == '/' || *scan == '?' || *scan == '#') {
                host_end = scan;
                break;
            }
        }
    }

    /* Copy host */
    size_t hlen = (size_t)(host_end - host_start);
    if (hlen >= sizeof(out->host)) hlen = sizeof(out->host) - 1;
    memcpy(out->host, host_start, hlen);
    out->host[hlen] = '\0';

    p = host_end;

    /* Parse port */
    out->port = -1;
    if (p < end && *p == ':') {
        p++;
        char port_str[URL_MAX_PORT];
        size_t pi = 0;
        while (p < end && isdigit((unsigned char)*p) && pi < sizeof(port_str) - 1) {
            port_str[pi++] = *p++;
        }
        port_str[pi] = '\0';
        if (pi > 0) {
            long port_val = strtol(port_str, NULL, 10);
            if (port_val > 0 && port_val <= 65535) {
                out->port = (int)port_val;
            }
        }
    }

    return (size_t)(p - auth);
}

/* ── Path Normalization ──────────────────────────────────────────── */

/**
 * Collapse /./ and /../ segments in a path string.
 * Also removes duplicate slashes.
 * Operates in-place on the path buffer.
 */
static void normalize_path_segments(char *path) {
    if (!path || !path[0]) return;

    /*
     * Split the path into segments, then rebuild without . and ..
     * We use a stack-based approach: push segments, pop on ..
     */
    char *segments[256];
    int seg_count = 0;
    char *working = path;

    /* Preserve leading slash */
    int has_leading_slash = (working[0] == '/');
    if (has_leading_slash) working++;

    /* Tokenize by '/' */
    char *saveptr = NULL;
    /* We need a mutable copy since strtok modifies the string */
    char temp[URL_MAX_PATH];
    strncpy(temp, working, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *token = strtok_r(temp, "/", &saveptr);
    while (token && seg_count < 255) {
        if (strcmp(token, ".") == 0) {
            /* Skip current-dir references */
        } else if (strcmp(token, "..") == 0) {
            /* Pop the last segment (don't go above root) */
            if (seg_count > 0) {
                seg_count--;
            }
        } else if (token[0] != '\0') {
            segments[seg_count++] = token;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }

    /* Rebuild path */
    char *out = path;
    if (has_leading_slash) {
        *out++ = '/';
    }
    for (int i = 0; i < seg_count; i++) {
        size_t slen = strlen(segments[i]);
        if ((size_t)(out - path) + slen + 2 >= URL_MAX_PATH) break;
        if (i > 0) *out++ = '/';
        memcpy(out, segments[i], slen);
        out += slen;
    }

    /* Preserve trailing slash if original had one and path is non-empty */
    size_t orig_len = strlen(path);
    if (orig_len > 1 && path[orig_len - 1] == '/' && out > path + 1) {
        /* Already handled by reconstruction */
    }

    *out = '\0';

    /* Ensure at least "/" for root paths */
    if (has_leading_slash && path[0] == '\0') {
        path[0] = '/';
        path[1] = '\0';
    }
}

/**
 * Remove duplicate consecutive slashes from a path.
 */
static void remove_duplicate_slashes(char *path) {
    if (!path) return;

    char *src = path;
    char *dst = path;
    int prev_slash = 0;

    while (*src) {
        if (*src == '/') {
            if (!prev_slash) {
                *dst++ = *src;
            }
            prev_slash = 1;
        } else {
            *dst++ = *src;
            prev_slash = 0;
        }
        src++;
    }
    *dst = '\0';
}

/* ── Decode unnecessary percent-encoding in paths ────────────────── */

/**
 * Selectively decode percent-encoded characters that don't need to be encoded.
 * Only decodes unreserved characters per RFC 3986.
 */
static void decode_unreserved_percent(char *str) {
    if (!str) return;

    char *src = str;
    char *dst = str;

    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            int hi = hex_digit_value(src[1]);
            int lo = hex_digit_value(src[2]);
            if (hi >= 0 && lo >= 0) {
                unsigned char decoded = (unsigned char)((hi << 4) | lo);
                if (is_unreserved(decoded)) {
                    *dst++ = (char)decoded;
                    src += 3;
                    continue;
                }
            }
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

/* ── Public API Implementation ───────────────────────────────────── */

int url_parse(const char *url_str, size_t len, ParsedUrl *out) {
    if (!url_str || !out) return -1;

    memset(out, 0, sizeof(*out));
    out->port = -1;

    if (len == 0) len = strlen(url_str);
    if (len == 0) return -1;

    /* Reject excessively long URLs */
    if (len > 16384) return -1;

    const char *p = url_str;
    const char *end = url_str + len;

    /* ── Extract scheme ── */
    size_t scheme_len = extract_scheme(p, len, out->scheme, sizeof(out->scheme));
    if (scheme_len > 0) {
        p += scheme_len;
    } else {
        /* No scheme — could be a scheme-relative URL (//host/path) or just a path */
        if (len >= 2 && p[0] == '/' && p[1] == '/') {
            out->scheme[0] = '\0';
            p += 2;
        } else {
            /* Treat as a bare path */
            out->scheme[0] = '\0';
            size_t plen = (size_t)(end - p);
            if (plen >= sizeof(out->path)) plen = sizeof(out->path) - 1;
            memcpy(out->path, p, plen);
            out->path[plen] = '\0';
            return 0;
        }
    }

    /* ── Extract authority (host, port, userinfo) ── */
    size_t remaining = (size_t)(end - p);
    /* Authority ends at first /, ?, or # */
    size_t auth_end = remaining;
    for (size_t i = 0; i < remaining; i++) {
        if (p[i] == '/' || p[i] == '?' || p[i] == '#') {
            auth_end = i;
            break;
        }
    }

    if (auth_end > 0) {
        size_t consumed = parse_authority(p, auth_end, out);
        p += consumed;
    }

    /* ── Extract path ── */
    if (p < end && *p != '?' && *p != '#') {
        const char *path_start = p;
        while (p < end && *p != '?' && *p != '#') p++;
        size_t plen = (size_t)(p - path_start);
        if (plen >= sizeof(out->path)) plen = sizeof(out->path) - 1;
        memcpy(out->path, path_start, plen);
        out->path[plen] = '\0';
    }

    /* ── Extract query ── */
    if (p < end && *p == '?') {
        p++;  /* skip '?' */
        const char *query_start = p;
        while (p < end && *p != '#') p++;
        size_t qlen = (size_t)(p - query_start);
        if (qlen >= sizeof(out->query)) qlen = sizeof(out->query) - 1;
        memcpy(out->query, query_start, qlen);
        out->query[qlen] = '\0';
    }

    /* ── Extract fragment ── */
    if (p < end && *p == '#') {
        p++;  /* skip '#' */
        size_t flen = (size_t)(end - p);
        if (flen >= sizeof(out->fragment)) flen = sizeof(out->fragment) - 1;
        memcpy(out->fragment, p, flen);
        out->fragment[flen] = '\0';
    }

    /* ── Detect IP address type ── */
    if (out->host[0]) {
        if (detect_ipv6_bracket(out->host)) {
            out->is_ipv6 = 1;
        } else {
            out->is_ipv4 = parse_ipv4(out->host, out->ipv4_addr);
        }
    }

    /* ── Copy decoded hostname ── */
    strncpy(out->hostname_decoded, out->host, sizeof(out->hostname_decoded) - 1);
    out->hostname_decoded[sizeof(out->hostname_decoded) - 1] = '\0';
    /* Lowercase the decoded hostname */
    for (size_t i = 0; out->hostname_decoded[i]; i++) {
        out->hostname_decoded[i] = (char)tolower((unsigned char)out->hostname_decoded[i]);
    }

    return 0;
}

int url_normalize(ParsedUrl *url) {
    if (!url) return -1;

    /* Lowercase scheme */
    for (size_t i = 0; url->scheme[i]; i++) {
        url->scheme[i] = (char)tolower((unsigned char)url->scheme[i]);
    }

    /* Lowercase host */
    for (size_t i = 0; url->host[i]; i++) {
        url->host[i] = (char)tolower((unsigned char)url->host[i]);
    }

    /* Remove default ports */
    if (url->port > 0) {
        if ((strcmp(url->scheme, "http") == 0 && url->port == 80) ||
            (strcmp(url->scheme, "https") == 0 && url->port == 443) ||
            (strcmp(url->scheme, "ftp") == 0 && url->port == 21)) {
            url->port = -1;
        }
    }

    /* Normalize path */
    if (url->path[0]) {
        remove_duplicate_slashes(url->path);
        normalize_path_segments(url->path);
        decode_unreserved_percent(url->path);
    } else if (url->host[0]) {
        /* Add implicit "/" for authority-bearing URIs */
        url->path[0] = '/';
        url->path[1] = '\0';
    }

    /* Decode unreserved percent-encoding in query */
    if (url->query[0]) {
        decode_unreserved_percent(url->query);
    }

    return 0;
}

int url_parse_query_params(ParsedUrl *url) {
    if (!url || !url->query[0]) return 0;

    url->param_count = 0;

    /* Work on a copy since we'll modify it during parsing */
    char query_copy[URL_MAX_QUERY];
    strncpy(query_copy, url->query, sizeof(query_copy) - 1);
    query_copy[sizeof(query_copy) - 1] = '\0';

    char *saveptr = NULL;
    char *pair = strtok_r(query_copy, "&", &saveptr);

    while (pair && url->param_count < URL_MAX_PARAMS) {
        QueryParam *param = &url->params[url->param_count];

        char *eq = strchr(pair, '=');
        if (eq) {
            size_t klen = (size_t)(eq - pair);
            if (klen >= sizeof(param->key)) klen = sizeof(param->key) - 1;
            memcpy(param->key, pair, klen);
            param->key[klen] = '\0';

            size_t vlen = strlen(eq + 1);
            if (vlen >= sizeof(param->value)) vlen = sizeof(param->value) - 1;
            memcpy(param->value, eq + 1, vlen);
            param->value[vlen] = '\0';

            /* Percent-decode key and value */
            url_percent_decode(param->key, 1);
            url_percent_decode(param->value, 1);
        } else {
            /* Key with no value */
            size_t klen = strlen(pair);
            if (klen >= sizeof(param->key)) klen = sizeof(param->key) - 1;
            memcpy(param->key, pair, klen);
            param->key[klen] = '\0';
            param->value[0] = '\0';

            url_percent_decode(param->key, 1);
        }

        url->param_count++;
        pair = strtok_r(NULL, "&", &saveptr);
    }

    return url->param_count;
}

int url_extract_domain(const ParsedUrl *url, DomainInfo *info) {
    if (!url || !info) return -1;

    memset(info, 0, sizeof(*info));

    if (!url->host[0]) return -1;

    /* Copy full host, stripping IPv6 brackets if present */
    const char *host = url->host;
    if (host[0] == '[') {
        const char *close = strchr(host, ']');
        if (close) {
            size_t hlen = (size_t)(close - host - 1);
            if (hlen >= sizeof(info->full_host)) hlen = sizeof(info->full_host) - 1;
            memcpy(info->full_host, host + 1, hlen);
            info->full_host[hlen] = '\0';
        } else {
            strncpy(info->full_host, host, sizeof(info->full_host) - 1);
        }
        /* IPv6 — no domain decomposition */
        strncpy(info->registrable_domain, info->full_host, sizeof(info->registrable_domain) - 1);
        return 0;
    }

    strncpy(info->full_host, host, sizeof(info->full_host) - 1);
    info->full_host[sizeof(info->full_host) - 1] = '\0';

    /* Lowercase */
    for (size_t i = 0; info->full_host[i]; i++) {
        info->full_host[i] = (char)tolower((unsigned char)info->full_host[i]);
    }

    /* If it's an IP address, no domain decomposition */
    unsigned char dummy[4];
    if (parse_ipv4(info->full_host, dummy)) {
        strncpy(info->registrable_domain, info->full_host, sizeof(info->registrable_domain) - 1);
        return 0;
    }

    /*
     * Extract TLD and registrable domain.
     * Simple heuristic: the TLD is the last dot-separated label,
     * and the registrable domain is the last two labels.
     *
     * For common two-part TLDs (co.uk, co.ke, com.au, etc.),
     * we use the last three labels.
     */
    const char *dots[32];
    int dot_count = 0;
    for (const char *scan = info->full_host; *scan; scan++) {
        if (*scan == '.' && dot_count < 32) {
            dots[dot_count++] = scan;
        }
    }

    if (dot_count == 0) {
        /* Single label (e.g., "localhost") */
        strncpy(info->registrable_domain, info->full_host, sizeof(info->registrable_domain) - 1);
        strncpy(info->tld, info->full_host, sizeof(info->tld) - 1);
        return 0;
    }

    /* TLD = last label */
    const char *last_dot = dots[dot_count - 1];
    strncpy(info->tld, last_dot + 1, sizeof(info->tld) - 1);
    info->tld[sizeof(info->tld) - 1] = '\0';

    /* Check for two-part TLDs */
    static const char *two_part_tlds[] = {
        "co.uk", "co.ke", "co.za", "co.in", "co.jp", "co.kr", "co.nz",
        "com.au", "com.br", "com.cn", "com.sg", "com.hk", "com.tw",
        "org.uk", "org.au", "net.au", "ac.uk", "ac.ke",
        "go.ke", "go.jp", "go.kr",
        "ne.jp", "or.jp", "or.ke",
        NULL
    };

    int is_two_part = 0;
    if (dot_count >= 2) {
        const char *second_last_dot = dots[dot_count - 2];
        const char *possible_two_part = second_last_dot + 1;
        for (int i = 0; two_part_tlds[i]; i++) {
            if (strcmp(possible_two_part, two_part_tlds[i]) == 0) {
                is_two_part = 1;
                break;
            }
        }
    }

    if (is_two_part && dot_count >= 3) {
        /* Registrable domain = last 3 labels */
        const char *reg_start = dots[dot_count - 3] + 1;
        strncpy(info->registrable_domain, reg_start, sizeof(info->registrable_domain) - 1);
        /* Subdomain = everything before the third-to-last dot */
        size_t sub_len = (size_t)(dots[dot_count - 3] - info->full_host);
        if (sub_len > 0 && sub_len < sizeof(info->subdomain)) {
            memcpy(info->subdomain, info->full_host, sub_len);
            info->subdomain[sub_len] = '\0';
        }
    } else if (is_two_part) {
        /* Only two labels but it's a two-part TLD — whole thing is the domain */
        strncpy(info->registrable_domain, info->full_host, sizeof(info->registrable_domain) - 1);
    } else if (dot_count >= 2) {
        /* Registrable domain = last 2 labels */
        const char *reg_start = dots[dot_count - 2] + 1;
        strncpy(info->registrable_domain, reg_start, sizeof(info->registrable_domain) - 1);
        /* Subdomain = everything before the second-to-last dot */
        size_t sub_len = (size_t)(dots[dot_count - 2] - info->full_host);
        if (sub_len > 0 && sub_len < sizeof(info->subdomain)) {
            memcpy(info->subdomain, info->full_host, sub_len);
            info->subdomain[sub_len] = '\0';
        }
    } else {
        /* Single dot — e.g., "example.com" */
        strncpy(info->registrable_domain, info->full_host, sizeof(info->registrable_domain) - 1);
    }

    info->registrable_domain[sizeof(info->registrable_domain) - 1] = '\0';
    return 0;
}

int url_reconstruct(const ParsedUrl *url, char *dst, size_t dstsize) {
    if (!url || !dst || dstsize == 0) return -1;

    char *p = dst;
    char *end = dst + dstsize - 1;

    /* Scheme */
    if (url->scheme[0]) {
        int n = snprintf(p, (size_t)(end - p), "%s://", url->scheme);
        if (n > 0) p += n;
    }

    /* Userinfo */
    if (url->userinfo[0]) {
        int n = snprintf(p, (size_t)(end - p), "%s@", url->userinfo);
        if (n > 0) p += n;
    }

    /* Host */
    if (url->host[0]) {
        int n = snprintf(p, (size_t)(end - p), "%s", url->host);
        if (n > 0) p += n;
    }

    /* Port */
    if (url->port > 0) {
        int n = snprintf(p, (size_t)(end - p), ":%d", url->port);
        if (n > 0) p += n;
    }

    /* Path */
    if (url->path[0]) {
        int n = snprintf(p, (size_t)(end - p), "%s", url->path);
        if (n > 0) p += n;
    }

    /* Query */
    if (url->query[0]) {
        int n = snprintf(p, (size_t)(end - p), "?%s", url->query);
        if (n > 0) p += n;
    }

    /* Fragment */
    if (url->fragment[0]) {
        int n = snprintf(p, (size_t)(end - p), "#%s", url->fragment);
        if (n > 0) p += n;
    }

    *p = '\0';
    return (int)(p - dst);
}

#if defined(_WIN32) && !defined(strtok_r)
/* Windows compatibility: strtok_r is not standard in MSVC */
static char *strtok_r_impl(char *str, const char *delim, char **saveptr) {
    if (!str) str = *saveptr;
    if (!str) return NULL;

    /* Skip leading delimiters */
    str += strspn(str, delim);
    if (*str == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    char *token = str;
    str = strpbrk(token, delim);
    if (str) {
        *str = '\0';
        *saveptr = str + 1;
    } else {
        *saveptr = NULL;
    }
    return token;
}
#undef strtok_r
#define strtok_r strtok_r_impl
#endif
