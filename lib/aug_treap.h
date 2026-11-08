#ifndef DS_AUG_TREAP_H
#define DS_AUG_TREAP_H

/*
 * Augmented treap: an ordered map (keys and values stored by value) whose
 * nodes also carry a user-defined summary of their subtree, maintained under
 * every insertion and removal.
 *
 *   compare(a, b)               key order
 *   summarize(out, key, value)  summary of one entry       (optional)
 *   combine(out, left, right)   associative merge          (optional)
 *
 * Built in: rank (keys smaller than a key), select (k-th entry), and the
 * summary of any key range. On top of that, ds_aug_treap_descend() lets a
 * callback steer a search: at each node it sees the entry and the summaries
 * of the two child subtrees and answers which subtrees to enter. Pruned
 * searches over summaries are expressed this way; the interval tree in
 * interval_tree.h is one such instance (summary = maximum right endpoint).
 *
 * Nodes come from the ds_context_t. Entry pointers stay valid until the
 * entry is removed (nodes never move).
 *
 * Complexity: insert, remove, get, rank, select, range summary O(log n)
 * expected. Concurrency: one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_AUG_LEFT 1
#define DS_AUG_RIGHT 2
#define DS_AUG_STOP 4

typedef struct ds_aug_node ds_aug_node_t;

/* Called at each node reached by descend(); child summaries are NULL for
 * empty subtrees. Return DS_AUG_LEFT | DS_AUG_RIGHT to enter subtrees, or
 * DS_AUG_STOP to end the whole search. */
typedef int (*ds_aug_descend_func_t)(const void *key, void *value,
                                     const void *left_summary,
                                     const void *right_summary, void *user);
typedef bool (*ds_aug_visit_func_t)(const void *key, void *value, void *user);

typedef struct ds_aug_treap_config {
  size_t key_size;
  size_t value_size;
  int (*compare)(const void *a, const void *b, void *user);
  size_t summary_size;
  void (*summarize)(void *out, const void *key, const void *value, void *user);
  void (*combine)(void *out, const void *left, const void *right, void *user);
  void *user;
  ds_context_t *context;
} ds_aug_treap_config_t;

typedef struct ds_aug_treap {
  ds_aug_node_t *root;
  ds_aug_treap_config_t config;
  size_t key_offset;
  size_t value_offset;
  size_t sum_offset;
  size_t node_bytes;
  unsigned long seed;
  unsigned char *scratch;
} ds_aug_treap_t;

void ds_aug_treap_config_init(ds_aug_treap_config_t *config);
ds_status_t ds_aug_treap_init(ds_aug_treap_t *treap,
                              const ds_aug_treap_config_t *config);
void ds_aug_treap_destroy(ds_aug_treap_t *treap);

size_t ds_aug_treap_size(const ds_aug_treap_t *treap);
ds_status_t ds_aug_treap_insert(ds_aug_treap_t *treap, const void *key,
                                const void *value);
ds_status_t ds_aug_treap_put(ds_aug_treap_t *treap, const void *key,
                             const void *value);
ds_status_t ds_aug_treap_remove(ds_aug_treap_t *treap, const void *key,
                                void *out_value);
/* The value of key (NULL if absent). It may be changed in place; if the
 * summaries depend on values, call ds_aug_treap_refresh afterwards. Keys
 * must never be changed in place. */
void *ds_aug_treap_get(const ds_aug_treap_t *treap, const void *key);
/* recompute the summaries on key's path after an in-place value change */
ds_status_t ds_aug_treap_refresh(ds_aug_treap_t *treap, const void *key);
size_t ds_aug_treap_rank(const ds_aug_treap_t *treap, const void *key);
ds_status_t ds_aug_treap_select(const ds_aug_treap_t *treap, size_t k,
                                const void **out_key, void **out_value);
ds_status_t ds_aug_treap_range_summary(const ds_aug_treap_t *treap,
                                       const void *low, const void *high,
                                       void *out_summary);
const void *ds_aug_treap_summary(const ds_aug_treap_t *treap);
ds_status_t ds_aug_treap_descend(const ds_aug_treap_t *treap,
                                 ds_aug_descend_func_t descend, void *user);
ds_status_t ds_aug_treap_visit(const ds_aug_treap_t *treap,
                               ds_aug_visit_func_t visit, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_AUG_TREAP_H */
