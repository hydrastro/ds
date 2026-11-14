#ifndef DS_FUSION_TREE_H
#define DS_FUSION_TREE_H

/*
 * Fusion trees (Fredman and Willard, 1993): an ordered map from machine
 * words (unsigned long) to fixed-size values, kept as a B-tree whose nodes
 * hold up to K keys (K = 8 when unsigned long has 64 bits, 5 when it has
 * 32: K fields of K bits fill a word) and are searched without comparing
 * the query against each key.
 *
 * A node keeps the r <= K - 1 bit positions where its sorted keys branch
 * (the highest differing bit of each adjacent pair) and the sketch of every
 * key: its bits at those positions, so sketches are ordered like the keys.
 * The sketches sit in one word, each in a K-bit field under a set sentinel
 * bit. Searching for q, its sketch is copied into every field by one
 * multiplication and subtracted from the packed word: a field keeps its
 * sentinel exactly when that key's sketch is not smaller, and a second
 * multiplication counts the sentinels, ranking the sketch. The key sharing
 * the longest prefix with q is one of the sketch's two neighbours; at the
 * first bit where q and that key differ, q is rounded to the last (or
 * first) word of its side of the branch, whose sketch ranks exactly like q
 * among the keys, and a second parallel comparison gives q's rank in the
 * node. So a node is searched with a constant number of word operations,
 * plus the sketch of q, gathered bit by bit (r steps; a bit-extract
 * instruction would do it in one, and the paper's multiplication gives an
 * approximate sketch only when w >= K^5), and the highest set bit of a
 * word, found by halving (log w steps).
 *
 * Search, predecessor and successor take O(log n / log K) node searches;
 * insert and remove split, borrow or merge nodes as a B-tree does (every
 * node but the root holds at least K / 2 keys) and recompute the sketches
 * of the O(log n / log K) nodes they change, O(K^2) word operations each.
 * Subtree sizes give rank and select. Nodes come from the context; nothing
 * recurses more than O(log n) deep.
 */

#include "context.h"
#include "status.h"
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if ULONG_MAX > 4294967295ul
#define DS_FUSION_K 8
#else
#define DS_FUSION_K 5
#endif
#define DS_FUSION_MIN (DS_FUSION_K / 2)
#define DS_FUSION_MAX_HEIGHT 48

typedef struct ds_fusion_node {
  /* one more key and child than a node keeps, while it splits */
  struct ds_fusion_node *child[DS_FUSION_K + 2]; /* all NULL in a leaf */
  unsigned long key[DS_FUSION_K + 1];
  unsigned long packed;           /* the sketches under their sentinels */
  size_t size;                    /* keys in the subtree */
  unsigned char count;            /* keys in the node */
  unsigned char bits;             /* distinguishing bit positions */
  unsigned char bit[DS_FUSION_K]; /* most significant first */
  unsigned char sketch[DS_FUSION_K + 1]; /* of each key */
} ds_fusion_node_t;

typedef struct ds_fusion_tree {
  ds_fusion_node_t *root;
  size_t value_size; /* 0: a set */
  size_t value_at;   /* offset of the values in a node allocation */
  ds_context_t *context;
} ds_fusion_tree_t;

/* value_size 0 makes a set (values are ignored and may be NULL) */
ds_status_t ds_fusion_init(ds_fusion_tree_t *t, size_t value_size,
                           ds_context_t *context);
void ds_fusion_destroy(ds_fusion_tree_t *t);
void ds_fusion_clear(ds_fusion_tree_t *t);
/* from n distinct keys in increasing order (values: n of them, or NULL for
 * zeroed values); replaces the contents in O(n); DS_ERR_INVALID if the keys
 * are not increasing */
ds_status_t ds_fusion_build(ds_fusion_tree_t *t, const unsigned long *keys,
                            const void *values, size_t n);

/* DS_OK, DS_EXISTS (value unchanged) or DS_ERR_ALLOC */
ds_status_t ds_fusion_insert(ds_fusion_tree_t *t, unsigned long key,
                             const void *value);
/* insert or overwrite the value */
ds_status_t ds_fusion_put(ds_fusion_tree_t *t, unsigned long key,
                          const void *value);
/* DS_OK or DS_NOT_FOUND; out_value optional */
ds_status_t ds_fusion_remove(ds_fusion_tree_t *t, unsigned long key,
                             void *out_value);
bool ds_fusion_contains(const ds_fusion_tree_t *t, unsigned long key);
/* the stored value, or for a set the stored key (valid until the next
 * insert or remove); NULL if absent */
void *ds_fusion_get(const ds_fusion_tree_t *t, unsigned long key);

/* DS_OK or DS_NOT_FOUND; out_value optional (copied) */
/* largest key < q */
ds_status_t ds_fusion_predecessor(const ds_fusion_tree_t *t, unsigned long q,
                                  unsigned long *out_key, void *out_value);
/* smallest key > q */
ds_status_t ds_fusion_successor(const ds_fusion_tree_t *t, unsigned long q,
                                unsigned long *out_key, void *out_value);
/* largest key <= q */
ds_status_t ds_fusion_floor(const ds_fusion_tree_t *t, unsigned long q,
                            unsigned long *out_key, void *out_value);
/* smallest key >= q */
ds_status_t ds_fusion_ceiling(const ds_fusion_tree_t *t, unsigned long q,
                              unsigned long *out_key, void *out_value);
/* DS_OK or DS_EMPTY */
ds_status_t ds_fusion_min(const ds_fusion_tree_t *t, unsigned long *out_key,
                          void *out_value);
ds_status_t ds_fusion_max(const ds_fusion_tree_t *t, unsigned long *out_key,
                          void *out_value);
/* number of keys < q */
size_t ds_fusion_rank(const ds_fusion_tree_t *t, unsigned long q);
/* the i-th smallest key (from 0): DS_OK or DS_ERR_RANGE */
ds_status_t ds_fusion_select(const ds_fusion_tree_t *t, size_t i,
                             unsigned long *out_key, void *out_value);
size_t ds_fusion_size(const ds_fusion_tree_t *t);
/* levels of nodes (0 when empty) */
size_t ds_fusion_height(const ds_fusion_tree_t *t);

/* in increasing key order, with each value (for a set, the stored key);
 * a non-zero return stops the walk and is returned */
typedef int (*ds_fusion_visit_fn)(unsigned long key, void *value, void *user);
int ds_fusion_foreach(const ds_fusion_tree_t *t, ds_fusion_visit_fn visit,
                      void *user);

/* the rank of q among a node's keys by the sketch search (the number of
 * keys <= q, *out_equal when q is one of them): the node search itself,
 * exposed for inspection */
size_t ds_fusion_node_rank(const ds_fusion_node_t *x, unsigned long q,
                           bool *out_equal);
/* the B-tree shape (equal leaf depths, K / 2 to K keys below the root),
 * key order, subtree sizes, and every node's distinguishing bits, sketches
 * and packed word against a fresh computation, with the sketch search
 * agreeing with a plain search at and next to every key; O(n K^2) */
ds_status_t ds_fusion_check(const ds_fusion_tree_t *t, bool *out_valid);

#ifdef __cplusplus
}
#endif

#endif /* DS_FUSION_TREE_H */
