#ifndef DS_ELASTIC_TRIE_H
#define DS_ELASTIC_TRIE_H

/*
 * Bucketed and blind tries over byte strings (keys copied, any bytes,
 * explicit lengths), each key carrying a size_t value.
 *
 * ds_bucket_trie_t  a trie whose leaves are buckets of up to b keys; a
 *                   full bucket splits into an internal node branching on
 *                   the next byte, and a subtree that shrinks to b keys
 *                   collapses back into one bucket.
 *                     b = 1        compact trie (every subtree holding a
 *                                  single key is replaced by that key)
 *                     elastic      elastic-bucket trie: bucket storage
 *                                  grows and shrinks with its contents
 *                                  (capacities 1, 2, 4, ... up to b)
 *                                  instead of always reserving b slots
 *                   Ordered iteration sorts each bucket on the fly.
 * ds_blind_trie_t   a blind trie (the node type of String B-trees): a
 *                   Patricia trie that stores only the branching position
 *                   in each internal node and the branching byte on each
 *                   edge, never the skipped characters. A search descends
 *                   blindly and then compares the query with the one key
 *                   it reached; that comparison also yields the longest
 *                   common prefix with the whole set.
 *
 * Concurrency: a trie may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*ds_trie_visit_func_t)(const unsigned char *key, size_t len,
                                     size_t value, void *user);

/* Key store shared by both tries. */
typedef struct ds_trie_keys {
  unsigned char **bytes;
  size_t *len;
  size_t *value;
  size_t slots;
  size_t capacity;
  size_t free_list;
} ds_trie_keys_t;

typedef struct ds_bucket_trie {
  /* nodes */
  unsigned char *leaf;
  size_t *depth;
  size_t *parent;
  size_t *child; /* first child */
  size_t *sibling;
  size_t *label; /* byte + 1, or 0 for "key ends here" */
  size_t *count; /* keys in the subtree */
  size_t **bucket;
  size_t *bucket_cap;
  size_t root;
  size_t node_slots;
  size_t node_capacity;
  size_t node_free;
  ds_trie_keys_t keys;
  size_t size;
  size_t b;
  bool elastic;
  ds_context_t *context;
} ds_bucket_trie_t;

/* b >= 1 (b = 1: compact trie); elastic: buckets sized to fit. */
ds_status_t ds_bucket_trie_init(ds_bucket_trie_t *t, size_t b, bool elastic,
                                ds_context_t *context);
void ds_bucket_trie_destroy(ds_bucket_trie_t *t);
size_t ds_bucket_trie_size(const ds_bucket_trie_t *t);
/* DS_EXISTS (value replaced) for a present key. */
ds_status_t ds_bucket_trie_insert(ds_bucket_trie_t *t, const unsigned char *key,
                                  size_t len, size_t value);
ds_status_t ds_bucket_trie_find(const ds_bucket_trie_t *t,
                                const unsigned char *key, size_t len,
                                size_t *out_value);
ds_status_t ds_bucket_trie_remove(ds_bucket_trie_t *t, const unsigned char *key,
                                  size_t len);
/* Every key in lexicographic order. */
ds_status_t ds_bucket_trie_foreach(ds_bucket_trie_t *t,
                                   ds_trie_visit_func_t visit, void *user);
/* Nodes in use (internal and leaves) and total bucket slots reserved. */
size_t ds_bucket_trie_nodes(const ds_bucket_trie_t *t);
size_t ds_bucket_trie_reserved(const ds_bucket_trie_t *t);

typedef struct ds_blind_trie {
  unsigned char *leaf;
  size_t *pos; /* internal: branching position; leaf: key index */
  size_t *parent;
  size_t *child;
  size_t *sibling;
  size_t *label; /* edge byte + 1, 0 = key ends */
  size_t root;
  size_t node_slots;
  size_t node_capacity;
  size_t node_free;
  ds_trie_keys_t keys;
  size_t size;
  ds_context_t *context;
} ds_blind_trie_t;

ds_status_t ds_blind_trie_init(ds_blind_trie_t *t, ds_context_t *context);
void ds_blind_trie_destroy(ds_blind_trie_t *t);
size_t ds_blind_trie_size(const ds_blind_trie_t *t);
ds_status_t ds_blind_trie_insert(ds_blind_trie_t *t, const unsigned char *key,
                                 size_t len, size_t value);
ds_status_t ds_blind_trie_find(const ds_blind_trie_t *t,
                               const unsigned char *key, size_t len,
                               size_t *out_value);
ds_status_t ds_blind_trie_remove(ds_blind_trie_t *t, const unsigned char *key,
                                 size_t len);
/* Length of the longest prefix of key shared with some stored key (0 for
 * an empty trie). */
size_t ds_blind_trie_lcp(const ds_blind_trie_t *t, const unsigned char *key,
                         size_t len);

#ifdef __cplusplus
}
#endif

#endif /* DS_ELASTIC_TRIE_H */
