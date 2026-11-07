#ifndef DS_MWAY_BTREE_H
#define DS_MWAY_BTREE_H

/*
 * B-trees of any order m in Knuth's sense: every node has at most m
 * children (m - 1 keys) and every node but the root at least ceil(m / 2)
 * children; all leaves are at the same depth. Unlike lib/btree, whose
 * CLRS minimum degree t only allows even maximum fan-outs (2t), any m >= 3
 * works, which gives the classic small cases as presets:
 *   DS_MWAY_2_3    m = 3: 2-3 trees (one or two keys per node)
 *   DS_MWAY_2_3_4  m = 4: 2-3-4 trees (one to three keys per node)
 * Insertion splits overflowing nodes bottom-up around the median;
 * deletion borrows from a sibling or merges with it bottom-up.
 *
 * Elements are stored by value in node arrays (keys of a node contiguous),
 * so wide orders give cache-friendly nodes.
 *
 * Concurrency: a tree may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_MWAY_2_3 3
#define DS_MWAY_2_3_4 4

typedef int (*ds_mway_compare_func_t)(const void *a, const void *b, void *user);

typedef struct ds_mway_btree {
  unsigned char *keys; /* m slots per node (one spare for splitting) */
  size_t *child;       /* m + 1 slots per node */
  size_t *count;
  unsigned char *leaf;
  size_t root;
  size_t nodes;
  size_t capacity;
  size_t free_list;
  size_t size;
  size_t height;
  size_t order;
  size_t elem_size;
  size_t *path; /* scratch: node, index pairs */
  size_t path_capacity;
  unsigned char *tmp; /* one element */
  ds_mway_compare_func_t compare;
  void *user;
  ds_context_t *context;
} ds_mway_btree_t;

/* order >= 3 (DS_MWAY_2_3, DS_MWAY_2_3_4, or any wider fan-out). */
ds_status_t ds_mway_btree_init(ds_mway_btree_t *t, size_t elem_size,
                               ds_mway_compare_func_t compare, void *user,
                               size_t order, ds_context_t *context);
void ds_mway_btree_destroy(ds_mway_btree_t *t);
size_t ds_mway_btree_size(const ds_mway_btree_t *t);
size_t ds_mway_btree_height(const ds_mway_btree_t *t);
/* DS_EXISTS for a duplicate key. */
ds_status_t ds_mway_btree_insert(ds_mway_btree_t *t, const void *elem);
ds_status_t ds_mway_btree_find(const ds_mway_btree_t *t, const void *key,
                               void *out);
ds_status_t ds_mway_btree_remove(ds_mway_btree_t *t, const void *key,
                                 void *out);
ds_status_t ds_mway_btree_min(const ds_mway_btree_t *t, void *out);
ds_status_t ds_mway_btree_max(const ds_mway_btree_t *t, void *out);
void ds_mway_btree_foreach(const ds_mway_btree_t *t,
                           bool (*visit)(const void *elem, void *user),
                           void *user);
/* Key counts, ordering and equal leaf depth. */
bool ds_mway_btree_valid(const ds_mway_btree_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_MWAY_BTREE_H */
