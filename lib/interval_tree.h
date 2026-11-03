#ifndef DS_INTERVAL_TREE_H
#define DS_INTERVAL_TREE_H

/*
 * Interval tree: a set of closed intervals [low, high] with attached
 * values, answering "which intervals overlap [a, b]" and "which contain
 * point p" in O(log n + matches) expected.
 *
 * Endpoints are of any fixed-size type ordered by a user comparator
 * (integers, doubles, timestamps, version vectors...). Duplicate intervals
 * are allowed: every insertion returns a handle (ds_interval_id_t) that
 * identifies it for removal.
 *
 * Built on aug_treap.h: entries are ordered by (low, high, id) and each
 * subtree summarizes its maximum high endpoint, which lets overlap searches
 * skip subtrees that end before the query begins.
 */

#include "aug_treap.h"
#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long ds_interval_id_t;
typedef bool (*ds_interval_visit_func_t)(const void *low, const void *high,
                                         void *value, ds_interval_id_t id,
                                         void *user);

typedef struct ds_interval_tree {
  ds_aug_treap_t treap;
  size_t endpoint_size;
  size_t id_offset;
  int (*compare)(const void *a, const void *b, void *user);
  void *user;
  ds_interval_id_t next_id;
  unsigned char *scratch;
} ds_interval_tree_t;

ds_status_t ds_interval_tree_init(ds_interval_tree_t *tree,
                                  size_t endpoint_size, size_t value_size,
                                  int (*compare)(const void *a, const void *b,
                                                 void *user),
                                  void *user, ds_context_t *context);
void ds_interval_tree_destroy(ds_interval_tree_t *tree);

ds_status_t ds_interval_tree_insert(ds_interval_tree_t *tree, const void *low,
                                    const void *high, const void *value,
                                    ds_interval_id_t *out_id);
ds_status_t ds_interval_tree_remove(ds_interval_tree_t *tree, const void *low,
                                    const void *high, ds_interval_id_t id,
                                    void *out_value);
ds_status_t ds_interval_tree_overlaps(const ds_interval_tree_t *tree,
                                      const void *low, const void *high,
                                      ds_interval_visit_func_t visit,
                                      void *user);
ds_status_t ds_interval_tree_stab(const ds_interval_tree_t *tree,
                                  const void *point,
                                  ds_interval_visit_func_t visit, void *user);
size_t ds_interval_tree_size(const ds_interval_tree_t *tree);

#ifdef __cplusplus
}
#endif

#endif /* DS_INTERVAL_TREE_H */
