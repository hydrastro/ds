#ifndef DS_CRITBIT_TREE_H
#define DS_CRITBIT_TREE_H

/*
 * Crit-bit tree (Bernstein; a binary PATRICIA trie): an ordered map from
 * NUL-terminated strings to void * values.
 *
 * Each internal node records the first bit at which its two subtrees
 * differ, so a lookup walks one node per distinguishing bit and does a
 * single full string comparison at the leaf. There is no rebalancing and
 * the shape depends only on the key set, not on insertion order. Keys come
 * out in byte-wise lexicographic order (the order of strcmp), which gives
 * sorted iteration, prefix queries and min/max for free.
 *
 * Keys are copied into memory from the ds_context_t; values are borrowed.
 *
 * Complexity (L = key length in bits): insert, get, remove O(L); visit
 * O(n); prefix visit O(L + matches). Concurrency: one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_critbit_node ds_critbit_node_t;
typedef bool (*ds_critbit_visit_func_t)(const char *key, void *value,
                                        void *user);

typedef struct ds_critbit_tree {
  ds_critbit_node_t *root;
  size_t size;
  ds_context_t *context;
} ds_critbit_tree_t;

void ds_critbit_tree_init(ds_critbit_tree_t *tree, ds_context_t *context);
void ds_critbit_tree_destroy(ds_critbit_tree_t *tree);

ds_status_t ds_critbit_tree_insert(ds_critbit_tree_t *tree, const char *key,
                                   void *value);
ds_status_t ds_critbit_tree_put(ds_critbit_tree_t *tree, const char *key,
                                void *value, void **out_old);
ds_status_t ds_critbit_tree_get(const ds_critbit_tree_t *tree, const char *key,
                                void **out_value);
bool ds_critbit_tree_contains(const ds_critbit_tree_t *tree, const char *key);
ds_status_t ds_critbit_tree_remove(ds_critbit_tree_t *tree, const char *key,
                                   void **out_value);
size_t ds_critbit_tree_size(const ds_critbit_tree_t *tree);

ds_status_t ds_critbit_tree_visit(const ds_critbit_tree_t *tree,
                                  ds_critbit_visit_func_t visit, void *user);
ds_status_t ds_critbit_tree_visit_prefix(const ds_critbit_tree_t *tree,
                                         const char *prefix,
                                         ds_critbit_visit_func_t visit,
                                         void *user);
const char *ds_critbit_tree_min(const ds_critbit_tree_t *tree);
const char *ds_critbit_tree_max(const ds_critbit_tree_t *tree);

#ifdef __cplusplus
}
#endif

#endif /* DS_CRITBIT_TREE_H */
