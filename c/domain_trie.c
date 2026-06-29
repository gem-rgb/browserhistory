/**
 * domain_trie.c — Trie-based domain classification and aggregation
 *
 * Builds a reverse-dot-split prefix tree from domain names. Each domain
 * like "mail.google.com" is split into labels ["com", "google", "mail"]
 * and inserted from the root downward. This allows efficient:
 *   - Unique domain counting
 *   - Subdomain aggregation (all *.google.com traffic)
 *   - Top-K domain extraction via traversal
 *   - Serialization to a compact binary format for caching
 *
 * Memory model: each TrieNode is individually allocated. The trie owns
 * all nodes and frees them on trie_destroy(). Path compression (compaction)
 * merges single-child chains to save memory and speed traversal.
 *
 * @version 1.0.0
 */

#include "domain_trie.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Node allocation ─────────────────────────────────────────────── */

static TrieNode *node_create(const char *label, int depth) {
    TrieNode *n = calloc(1, sizeof(TrieNode));
    if (!n) return NULL;

    if (label) {
        strncpy(n->label, label, sizeof(n->label) - 1);
        n->label[sizeof(n->label) - 1] = '\0';
    }
    n->depth = depth;
    return n;
}

static void node_destroy(TrieNode *n) {
    if (!n) return;
    for (int i = 0; i < n->child_count; i++) {
        node_destroy(n->children[i]);
    }
    free(n);
}

/* ── Label splitting ─────────────────────────────────────────────── */

/**
 * Split a domain into reverse-order labels.
 * "mail.google.com" → labels[0]="com", labels[1]="google", labels[2]="mail"
 *
 * Returns the number of labels, or -1 on error.
 */
static int split_domain_reverse(const char *domain, char labels[][TRIE_MAX_LABEL],
                                 int max_labels) {
    if (!domain || !domain[0]) return -1;

    /* First, split forward */
    char forward[TRIE_MAX_DEPTH][TRIE_MAX_LABEL];
    int fwd_count = 0;

    const char *p = domain;
    while (*p && fwd_count < TRIE_MAX_DEPTH && fwd_count < max_labels) {
        const char *dot = strchr(p, '.');
        size_t llen;
        if (dot) {
            llen = (size_t)(dot - p);
        } else {
            llen = strlen(p);
        }

        if (llen == 0) {
            /* Skip empty labels (consecutive dots) */
            p = dot ? dot + 1 : p + llen;
            continue;
        }

        if (llen >= TRIE_MAX_LABEL) llen = TRIE_MAX_LABEL - 1;
        memcpy(forward[fwd_count], p, llen);
        forward[fwd_count][llen] = '\0';

        /* Lowercase */
        for (size_t i = 0; i < llen; i++) {
            forward[fwd_count][i] = (char)tolower((unsigned char)forward[fwd_count][i]);
        }

        fwd_count++;
        p = dot ? dot + 1 : p + llen;
    }

    /* Reverse into output */
    for (int i = 0; i < fwd_count; i++) {
        strncpy(labels[i], forward[fwd_count - 1 - i], TRIE_MAX_LABEL - 1);
        labels[i][TRIE_MAX_LABEL - 1] = '\0';
    }

    return fwd_count;
}

/* ── Child lookup/insert ─────────────────────────────────────────── */

static TrieNode *find_child(const TrieNode *node, const char *label) {
    for (int i = 0; i < node->child_count; i++) {
        if (strcmp(node->children[i]->label, label) == 0) {
            return node->children[i];
        }
    }
    return NULL;
}

static TrieNode *add_child(TrieNode *parent, const char *label) {
    if (parent->child_count >= TRIE_MAX_CHILDREN) return NULL;

    TrieNode *child = node_create(label, parent->depth + 1);
    if (!child) return NULL;

    parent->children[parent->child_count++] = child;
    return child;
}

/* ── Subtree count propagation ───────────────────────────────────── */

static void propagate_subtree_counts(TrieNode *node) {
    node->subtree_urls = node->url_count;
    node->subtree_visits = node->visit_count;

    for (int i = 0; i < node->child_count; i++) {
        propagate_subtree_counts(node->children[i]);
        node->subtree_urls += node->children[i]->subtree_urls;
        node->subtree_visits += node->children[i]->subtree_visits;
    }
}

/* ── Top-K helpers ───────────────────────────────────────────────── */

/**
 * Simple insertion into a sorted (descending) top-K array.
 * Maintains the array sorted by visit_count descending.
 */
static void topk_insert(TopKEntry *results, int *count, int k,
                         const char *domain, int urls, int visits) {
    /* Find insertion point */
    int pos = *count;
    for (int i = 0; i < *count; i++) {
        if (visits > results[i].visit_count) {
            pos = i;
            break;
        }
    }

    if (pos >= k) return;  /* not in top-K */

    /* Shift entries down */
    int shift_end = (*count < k) ? *count : k - 1;
    for (int i = shift_end; i > pos; i--) {
        if (i < k) results[i] = results[i - 1];
    }

    /* Insert */
    strncpy(results[pos].domain, domain, sizeof(results[pos].domain) - 1);
    results[pos].domain[sizeof(results[pos].domain) - 1] = '\0';
    results[pos].url_count = urls;
    results[pos].visit_count = visits;

    if (*count < k) (*count)++;
}

/**
 * Recursive traversal to collect terminal domains into top-K.
 */
static void topk_traverse(const TrieNode *node, TopKEntry *results,
                           int *count, int k, char *path_buf, size_t path_len) {
    if (node->is_terminal) {
        topk_insert(results, count, k, path_buf,
                    node->url_count, node->visit_count);
    }

    for (int i = 0; i < node->child_count; i++) {
        const TrieNode *child = node->children[i];
        char new_path[512];
        if (path_len > 0) {
            snprintf(new_path, sizeof(new_path), "%s.%s", child->label, path_buf);
        } else {
            strncpy(new_path, child->label, sizeof(new_path) - 1);
            new_path[sizeof(new_path) - 1] = '\0';
        }
        topk_traverse(child, results, count, k, new_path, strlen(new_path));
    }
}

/* ── Serialization helpers ───────────────────────────────────────── */

/**
 * Recursively serialize a node and its children.
 * Format per node:
 *   - label_len (1 byte)
 *   - label (label_len bytes)
 *   - url_count (4 bytes, little-endian)
 *   - visit_count (4 bytes, little-endian)
 *   - is_terminal (1 byte)
 *   - child_count (1 byte)
 *   - children (recursive)
 */
static int serialize_node(const TrieNode *node, unsigned char *buf,
                           size_t bufsize, size_t *offset) {
    if (!node) return -1;

    size_t label_len = strlen(node->label);
    if (label_len > 255) label_len = 255;

    /* Check space: 1 + label_len + 4 + 4 + 1 + 1 = 11 + label_len */
    size_t needed = 11 + label_len;
    if (*offset + needed > bufsize) return -1;

    buf[(*offset)++] = (unsigned char)label_len;
    memcpy(buf + *offset, node->label, label_len);
    *offset += label_len;

    /* url_count (little-endian) */
    buf[(*offset)++] = (unsigned char)(node->url_count & 0xFF);
    buf[(*offset)++] = (unsigned char)((node->url_count >> 8) & 0xFF);
    buf[(*offset)++] = (unsigned char)((node->url_count >> 16) & 0xFF);
    buf[(*offset)++] = (unsigned char)((node->url_count >> 24) & 0xFF);

    /* visit_count (little-endian) */
    buf[(*offset)++] = (unsigned char)(node->visit_count & 0xFF);
    buf[(*offset)++] = (unsigned char)((node->visit_count >> 8) & 0xFF);
    buf[(*offset)++] = (unsigned char)((node->visit_count >> 16) & 0xFF);
    buf[(*offset)++] = (unsigned char)((node->visit_count >> 24) & 0xFF);

    buf[(*offset)++] = (unsigned char)node->is_terminal;
    buf[(*offset)++] = (unsigned char)node->child_count;

    for (int i = 0; i < node->child_count; i++) {
        if (serialize_node(node->children[i], buf, bufsize, offset) != 0) {
            return -1;
        }
    }

    return 0;
}

/**
 * Recursively deserialize a node and its children.
 */
static TrieNode *deserialize_node(const unsigned char *buf, size_t bufsize,
                                    size_t *offset, int depth, int *node_count) {
    if (*offset >= bufsize) return NULL;

    /* Prevent excessive recursion depth */
    if (depth > TRIE_MAX_DEPTH + 2) return NULL;

    unsigned char label_len = buf[(*offset)++];
    if (*offset + label_len + 10 > bufsize) return NULL;

    char label[TRIE_MAX_LABEL];
    if (label_len >= TRIE_MAX_LABEL) label_len = TRIE_MAX_LABEL - 1;
    memcpy(label, buf + *offset, label_len);
    label[label_len] = '\0';
    *offset += label_len;

    TrieNode *node = node_create(label, depth);
    if (!node) return NULL;

    /* url_count */
    node->url_count = (int)(buf[*offset] |
                            ((int)buf[*offset + 1] << 8) |
                            ((int)buf[*offset + 2] << 16) |
                            ((int)buf[*offset + 3] << 24));
    *offset += 4;

    /* visit_count */
    node->visit_count = (int)(buf[*offset] |
                              ((int)buf[*offset + 1] << 8) |
                              ((int)buf[*offset + 2] << 16) |
                              ((int)buf[*offset + 3] << 24));
    *offset += 4;

    node->is_terminal = buf[(*offset)++];
    int child_count = buf[(*offset)++];

    if (child_count > TRIE_MAX_CHILDREN) child_count = TRIE_MAX_CHILDREN;

    (*node_count)++;

    for (int i = 0; i < child_count && i < TRIE_MAX_CHILDREN; i++) {
        TrieNode *child = deserialize_node(buf, bufsize, offset, depth + 1, node_count);
        if (!child) {
            node_destroy(node);
            return NULL;
        }
        node->children[node->child_count++] = child;
    }

    return node;
}

/* ── Compaction ──────────────────────────────────────────────────── */

/**
 * Recursively compact single-child chains.
 * If a node has exactly one child and is not terminal,
 * merge the child's label into this node.
 */
static void compact_node(TrieNode *node) {
    if (!node) return;

    /* First, compact all children recursively */
    for (int i = 0; i < node->child_count; i++) {
        compact_node(node->children[i]);
    }

    /* Then check if this node can be compacted */
    while (node->child_count == 1 && !node->is_terminal) {
        TrieNode *child = node->children[0];

        /* Merge child label: "parent.child" */
        char merged[TRIE_MAX_LABEL];
        if (node->label[0]) {
            snprintf(merged, sizeof(merged), "%s.%s", node->label, child->label);
        } else {
            strncpy(merged, child->label, sizeof(merged) - 1);
            merged[sizeof(merged) - 1] = '\0';
        }
        strncpy(node->label, merged, sizeof(node->label) - 1);
        node->label[sizeof(node->label) - 1] = '\0';

        /* Take over child's properties */
        node->url_count += child->url_count;
        node->visit_count += child->visit_count;
        node->is_terminal = child->is_terminal;

        /* Move grandchildren up */
        node->child_count = child->child_count;
        for (int i = 0; i < child->child_count; i++) {
            node->children[i] = child->children[i];
        }

        /* Free the merged child (but not its children, which we moved) */
        child->child_count = 0;  /* prevent recursive free of grandchildren */
        free(child);
    }
}

/* ── Domain counting ─────────────────────────────────────────────── */

static int count_terminals(const TrieNode *node) {
    int count = node->is_terminal ? 1 : 0;
    for (int i = 0; i < node->child_count; i++) {
        count += count_terminals(node->children[i]);
    }
    return count;
}

/* ── Public API ──────────────────────────────────────────────────── */

DomainTrie *trie_create(void) {
    DomainTrie *trie = calloc(1, sizeof(DomainTrie));
    if (!trie) return NULL;

    trie->root = node_create("", 0);
    if (!trie->root) {
        free(trie);
        return NULL;
    }

    trie->total_nodes = 1;
    return trie;
}

void trie_destroy(DomainTrie *trie) {
    if (!trie) return;
    node_destroy(trie->root);
    free(trie);
}

int trie_insert(DomainTrie *trie, const char *domain, int url_count, int visit_count) {
    if (!trie || !domain || !domain[0]) return -1;

    char labels[TRIE_MAX_DEPTH][TRIE_MAX_LABEL];
    int label_count = split_domain_reverse(domain, labels, TRIE_MAX_DEPTH);
    if (label_count <= 0) return -1;

    TrieNode *current = trie->root;

    for (int i = 0; i < label_count; i++) {
        TrieNode *child = find_child(current, labels[i]);
        if (!child) {
            child = add_child(current, labels[i]);
            if (!child) return -1;
            trie->total_nodes++;
        }
        current = child;
    }

    /* Mark as terminal and accumulate counts */
    if (!current->is_terminal) {
        current->is_terminal = 1;
        trie->total_domains++;
    }
    current->url_count += url_count;
    current->visit_count += visit_count;
    trie->total_urls += url_count;
    trie->total_visits += visit_count;

    return 0;
}

TrieNode *trie_lookup(const DomainTrie *trie, const char *domain) {
    if (!trie || !domain || !domain[0]) return NULL;

    char labels[TRIE_MAX_DEPTH][TRIE_MAX_LABEL];
    int label_count = split_domain_reverse(domain, labels, TRIE_MAX_DEPTH);
    if (label_count <= 0) return NULL;

    TrieNode *current = trie->root;

    for (int i = 0; i < label_count; i++) {
        TrieNode *child = find_child(current, labels[i]);
        if (!child) return NULL;
        current = child;
    }

    return current->is_terminal ? current : NULL;
}

int trie_aggregate(const DomainTrie *trie, const char *domain,
                   int *out_urls, int *out_visits) {
    if (!trie || !domain) return -1;

    char labels[TRIE_MAX_DEPTH][TRIE_MAX_LABEL];
    int label_count = split_domain_reverse(domain, labels, TRIE_MAX_DEPTH);
    if (label_count <= 0) return -1;

    TrieNode *current = trie->root;

    for (int i = 0; i < label_count; i++) {
        TrieNode *child = find_child(current, labels[i]);
        if (!child) return -1;
        current = child;
    }

    /* Propagate subtree counts from this node down */
    propagate_subtree_counts(current);

    if (out_urls) *out_urls = current->subtree_urls;
    if (out_visits) *out_visits = current->subtree_visits;

    return 0;
}

int trie_top_k(const DomainTrie *trie, TopKEntry *results, int k) {
    if (!trie || !results || k <= 0) return 0;
    if (k > TRIE_TOPK_MAX) k = TRIE_TOPK_MAX;

    int count = 0;
    char path[512] = "";

    /* Traverse from each child of root (root itself has empty label) */
    for (int i = 0; i < trie->root->child_count; i++) {
        const TrieNode *child = trie->root->children[i];
        topk_traverse(child, results, &count, k, (char *)child->label, strlen(child->label));
    }

    return count;
}

int trie_unique_count(const DomainTrie *trie) {
    if (!trie || !trie->root) return 0;
    return count_terminals(trie->root);
}

int trie_serialize(const DomainTrie *trie, unsigned char *buf, size_t bufsize) {
    if (!trie || !buf || bufsize < sizeof(TrieHeader)) return -1;

    /* Write header */
    TrieHeader hdr;
    memcpy(hdr.magic, "DTRI", 4);
    hdr.version = 1;
    hdr.node_count = trie->total_nodes;
    hdr.domain_count = trie->total_domains;

    memcpy(buf, &hdr, sizeof(hdr));
    size_t offset = sizeof(hdr);

    /* Serialize tree */
    if (serialize_node(trie->root, buf, bufsize, &offset) != 0) {
        return -1;
    }

    return (int)offset;
}

DomainTrie *trie_deserialize(const unsigned char *buf, size_t bufsize) {
    if (!buf || bufsize < sizeof(TrieHeader)) return NULL;

    /* Validate header */
    TrieHeader hdr;
    memcpy(&hdr, buf, sizeof(hdr));
    if (memcmp(hdr.magic, "DTRI", 4) != 0) return NULL;
    if (hdr.version != 1) return NULL;

    /* Sanity check counts */
    if (hdr.node_count < 0 || hdr.node_count > 1000000) return NULL;
    if (hdr.domain_count < 0 || hdr.domain_count > hdr.node_count) return NULL;

    DomainTrie *trie = calloc(1, sizeof(DomainTrie));
    if (!trie) return NULL;

    size_t offset = sizeof(hdr);
    int node_count = 0;

    trie->root = deserialize_node(buf, bufsize, &offset, 0, &node_count);
    if (!trie->root) {
        free(trie);
        return NULL;
    }

    trie->total_nodes = node_count;
    trie->total_domains = hdr.domain_count;

    /* Recalculate totals from the tree */
    propagate_subtree_counts(trie->root);
    trie->total_urls = trie->root->subtree_urls;
    trie->total_visits = trie->root->subtree_visits;

    return trie;
}

void trie_compact(DomainTrie *trie) {
    if (!trie || !trie->root) return;

    for (int i = 0; i < trie->root->child_count; i++) {
        compact_node(trie->root->children[i]);
    }

    /* Recount nodes after compaction */
    trie->total_nodes = count_terminals(trie->root);  /* approximate */
}
