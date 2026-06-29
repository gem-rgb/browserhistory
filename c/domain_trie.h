/**
 * domain_trie.h — Trie-based domain classification and aggregation
 *
 * Builds a reverse-dot-split trie from domain names for efficient
 * counting, subdomain aggregation, and top-K domain extraction.
 *
 * @version 1.0.0
 */

#ifndef DOMAIN_TRIE_H
#define DOMAIN_TRIE_H

#include <stddef.h>

/* ── Limits ──────────────────────────────────────────────────────── */

#define TRIE_MAX_CHILDREN    64
#define TRIE_MAX_LABEL      128
#define TRIE_MAX_DEPTH       16
#define TRIE_TOPK_MAX        32

/* ── Data Structures ─────────────────────────────────────────────── */

/** A single node in the domain trie */
typedef struct TrieNode {
    char                label[TRIE_MAX_LABEL];
    struct TrieNode    *children[TRIE_MAX_CHILDREN];
    int                 child_count;
    int                 url_count;      /* URLs at this exact domain */
    int                 visit_count;    /* Total visits at this exact domain */
    int                 subtree_urls;   /* URLs in entire subtree */
    int                 subtree_visits; /* Visits in entire subtree */
    int                 is_terminal;    /* This node represents a complete domain */
    int                 depth;
} TrieNode;

/** Top-K result entry */
typedef struct {
    char domain[512];
    int  url_count;
    int  visit_count;
} TopKEntry;

/** The trie structure */
typedef struct {
    TrieNode   *root;
    int         total_nodes;
    int         total_domains;      /* Number of unique terminal domains */
    int         total_urls;
    int         total_visits;
} DomainTrie;

/** Serialized trie header for binary format */
typedef struct {
    char    magic[4];       /* "DTRI" */
    int     version;
    int     node_count;
    int     domain_count;
} TrieHeader;

/* ── API ─────────────────────────────────────────────────────────── */

/**
 * Create a new empty domain trie.
 * @return Pointer to the new trie, or NULL on allocation failure
 */
DomainTrie *trie_create(void);

/**
 * Free a domain trie and all its nodes.
 */
void trie_destroy(DomainTrie *trie);

/**
 * Insert a domain into the trie.
 * The domain is split on '.' and inserted in reverse order
 * (e.g., "mail.google.com" → com → google → mail).
 *
 * @param trie          The trie
 * @param domain        Domain name to insert (e.g., "mail.google.com")
 * @param url_count     Number of URLs for this domain
 * @param visit_count   Number of visits for this domain
 * @return 0 on success, -1 on error
 */
int trie_insert(DomainTrie *trie, const char *domain, int url_count, int visit_count);

/**
 * Look up a domain in the trie.
 *
 * @param trie      The trie
 * @param domain    Domain name to look up
 * @return Pointer to the terminal node, or NULL if not found
 */
TrieNode *trie_lookup(const DomainTrie *trie, const char *domain);

/**
 * Get the aggregate count for a domain and all its subdomains.
 *
 * @param trie      The trie
 * @param domain    Base domain (e.g., "google.com" includes mail.google.com)
 * @param out_urls  Output: total URL count for domain + subdomains
 * @param out_visits Output: total visit count for domain + subdomains
 * @return 0 on success, -1 if domain not found
 */
int trie_aggregate(const DomainTrie *trie, const char *domain,
                   int *out_urls, int *out_visits);

/**
 * Extract the top-K domains by visit count.
 *
 * @param trie      The trie
 * @param results   Output array (must have space for at least k entries)
 * @param k         Number of top domains to extract (max TRIE_TOPK_MAX)
 * @return Number of results written
 */
int trie_top_k(const DomainTrie *trie, TopKEntry *results, int k);

/**
 * Count the number of unique domains in the trie.
 *
 * @param trie  The trie
 * @return Number of unique terminal domains
 */
int trie_unique_count(const DomainTrie *trie);

/**
 * Serialize the trie to a binary buffer.
 *
 * @param trie      The trie to serialize
 * @param buf       Output buffer
 * @param bufsize   Size of output buffer
 * @return Number of bytes written, or -1 on error
 */
int trie_serialize(const DomainTrie *trie, unsigned char *buf, size_t bufsize);

/**
 * Deserialize a trie from a binary buffer.
 *
 * @param buf       Input buffer
 * @param bufsize   Size of input buffer
 * @return Pointer to the deserialized trie, or NULL on error
 */
DomainTrie *trie_deserialize(const unsigned char *buf, size_t bufsize);

/**
 * Compact the trie by merging single-child chains.
 * e.g., com → google → mail becomes "com.google" → mail
 *
 * @param trie  The trie to compact
 */
void trie_compact(DomainTrie *trie);

#endif /* DOMAIN_TRIE_H */
