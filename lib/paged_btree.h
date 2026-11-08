#ifndef DS_PAGED_BTREE_H
#define DS_PAGED_BTREE_H

/*
 * A B+ tree stored in pages through lib/pager, so it lives on any page
 * store (memory, a file, or a custom medium) and survives reopening.
 *
 * Keys are unsigned long (stored as 8 little-endian bytes); values are
 * fixed-size byte blobs. Page 0 holds the metadata; leaves are linked for
 * range scans. Insert splits full nodes on the way back up; removal
 * deletes from the leaf without rebalancing (nodes may become underfull
 * and empty leaves stay linked), the usual trade-off of lazy deletion.
 * All page formats are portable (fixed little-endian layout).
 *
 * Descents unpin each page before visiting the next, so a pager with
 * three frames suffices at any height (DS_ERR_STATE when frames run out).
 * Split buffers come from the pager's context.
 */

#include "pager.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_pbtree {
  ds_pager_t *pager;
  size_t value_size;
  size_t leaf_cap;
  size_t inner_cap;
  unsigned long root;
  unsigned long height; /* 1: the root is a leaf */
  unsigned long count;
  unsigned long first_leaf;
} ds_pbtree_t;

typedef struct ds_pbtree_iter {
  ds_pbtree_t *tree;
  unsigned long leaf; /* 0 when exhausted */
  size_t index;
  unsigned long key;
  unsigned char *value; /* copy of the current value */
} ds_pbtree_iter_t;

/* formats an empty store or opens an existing tree (value_size must match) */
ds_status_t ds_pbtree_open(ds_pbtree_t *tree, ds_pager_t *pager,
                           size_t value_size);
/* writes the metadata page (call before flushing the pager) */
ds_status_t ds_pbtree_sync(ds_pbtree_t *tree);
/* DS_OK when added, DS_EXISTS when replaced */
ds_status_t ds_pbtree_put(ds_pbtree_t *tree, unsigned long key,
                          const void *value);
ds_status_t ds_pbtree_get(ds_pbtree_t *tree, unsigned long key, void *out);
ds_status_t ds_pbtree_remove(ds_pbtree_t *tree, unsigned long key);
unsigned long ds_pbtree_size(const ds_pbtree_t *tree);

/* first entry with key >= from; the iterator copies values (context) */
ds_status_t ds_pbtree_iter_init(ds_pbtree_iter_t *it, ds_pbtree_t *tree,
                                unsigned long from);
bool ds_pbtree_iter_valid(const ds_pbtree_iter_t *it);
ds_status_t ds_pbtree_iter_next(ds_pbtree_iter_t *it);
void ds_pbtree_iter_destroy(ds_pbtree_iter_t *it);

#ifdef __cplusplus
}
#endif

#endif /* DS_PAGED_BTREE_H */
