/**
 * url_parser_fuzzer.c — Fuzz harness for URL parsing and domain trie
 *
 * Exercises the URL decomposition, normalization, query parameter extraction,
 * and domain trie insertion pipeline:
 *   - Percent-decoding with UTF-8 validation
 *   - Path normalization (/../, /./, duplicate slashes)
 *   - IPv4/IPv6 address detection
 *   - Query string parsing
 *   - Domain extraction with two-part TLD support
 *   - Trie insertion, lookup, aggregation, serialization/deserialization
 *
 * @version 1.0.0
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "url_parser.h"
#include "domain_trie.h"

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size) {
    if (size == 0 || size > 16384) return 0;

    /* Null-terminate the input */
    char *url_str = malloc(size + 1);
    if (!url_str) return 0;
    memcpy(url_str, data, size);
    url_str[size] = '\0';

    /* ── Phase 1: Parse the URL ── */
    ParsedUrl parsed;
    int parse_ok = url_parse(url_str, size, &parsed);

    if (parse_ok == 0) {
        /* ── Phase 2: Normalize ── */
        url_normalize(&parsed);

        /* ── Phase 3: Parse query parameters ── */
        url_parse_query_params(&parsed);

        /* ── Phase 4: Extract domain info ── */
        DomainInfo dinfo;
        url_extract_domain(&parsed, &dinfo);

        /* ── Phase 5: Reconstruct URL ── */
        char reconstructed[8192];
        url_reconstruct(&parsed, reconstructed, sizeof(reconstructed));

        /* ── Phase 6: Feed into domain trie ── */
        if (dinfo.registrable_domain[0]) {
            DomainTrie *trie = trie_create();
            if (trie) {
                trie_insert(trie, dinfo.full_host, 1, 1);

                /* Lookup */
                trie_lookup(trie, dinfo.full_host);

                /* Aggregate */
                int urls_out, visits_out;
                trie_aggregate(trie, dinfo.registrable_domain,
                              &urls_out, &visits_out);

                /* Top-K */
                TopKEntry topk[5];
                trie_top_k(trie, topk, 5);

                /* Serialize and deserialize round-trip */
                unsigned char ser_buf[65536];
                int ser_len = trie_serialize(trie, ser_buf, sizeof(ser_buf));
                if (ser_len > 0) {
                    DomainTrie *trie2 = trie_deserialize(ser_buf, (size_t)ser_len);
                    if (trie2) {
                        trie_compact(trie2);
                        trie_destroy(trie2);
                    }
                }

                trie_destroy(trie);
            }
        }
    }

    /* ── Phase 7: Exercise percent-decode standalone ── */
    char *decode_buf = malloc(size + 1);
    if (decode_buf) {
        memcpy(decode_buf, data, size);
        decode_buf[size] = '\0';
        url_percent_decode(decode_buf, 0);
        free(decode_buf);
    }

    /* ── Phase 8: UTF-8 validation ── */
    url_validate_utf8(url_str, size);

    /* ── Phase 9: Percent-encode ── */
    char encode_buf[8192];
    url_percent_encode(encode_buf, sizeof(encode_buf), url_str, 2);

    /* ── Phase 10: Trie deserialization from raw fuzz data ── */
    if (size >= 16) {
        DomainTrie *raw_trie = trie_deserialize(data, size);
        if (raw_trie) {
            trie_unique_count(raw_trie);
            trie_compact(raw_trie);
            trie_destroy(raw_trie);
        }
    }

    free(url_str);
    return 0;
}
