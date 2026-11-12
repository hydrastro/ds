#ifndef DS_PAGED_BTREE_H
#define DS_PAGED_BTREE_H

/*
 * A B+ tree stored in pages through lib/pager, so it lives on any page
 * store (memory, a file, or a custom medium) and survives reopening.
 *
 * Keys are unsigned long; values are fixed-size byte blobs. Leaves are
 * linked for range scans. Removal deletes from the leaf without
 * rebalancing (nodes may become underfull and empty leaves stay linked),
 * the usual trade-off of lazy deletion.
 *
 * File format (version 2, written by sync; version 1 is still read). All
 * integers are 8-byte little-endian fields, whatever the host's unsigned
 * long width.
 *
 *   page 0       magic "DSBPT2", value size, root, height, entry count,
 *                first leaf, largest key ever stored, page size
 *   leaf page    byte 0 = 1, entry count (4 bytes at 4), next leaf (at
 *                8, 0 at the end), then (key, value) entries from byte 16
 *   inner page   byte 0 = 2, key count, then child 0 and (key, child)
 *                pairs from byte 16; child i holds keys k with
 *                key[i - 1] <= k < key[i]
 *
 * Portability: a host whose unsigned long is narrower than 64 bits opens
 * a tree only when the metadata values and the largest stored key fit it
 * (DS_ERR_UNSUPPORTED otherwise), so every key it reads is representable.
 * Version 1 trees recorded only the writer's width and are rejected by
 * narrower hosts. A page size different from the store's is rejected.
 *
 * Integrity: pages carry no checksums. Every page is checked when read
 * (its kind, and a count that fits the page) and page numbers are checked
 * against the store, so a corrupted store gives DS_ERR_INVALID rather than
 * reads outside a page or endless loops. ds_pbtree_verify additionally
 * checks the whole structure.
 *
 * Failure guarantee: put is prepare-then-commit. Every page a split will
 * edit is pinned and every new page and buffer is obtained before any
 * reachable page changes, so a put that fails (allocation, I/O, or
 * DS_ERR_STATE when the pager lacks frames) leaves the tree as it was;
 * pages appended during a failed prepare stay unused. A split through k
 * levels pins up to 2k + 2 frames; lookups, removals and iteration pin
 * one page at a time. This is in-process failure atomicity, not crash
 * recovery: publication is not ordered on the store. sync writes the
 * metadata page; the pager writes dirty pages back in no particular
 * order, so a crash between writes can leave the metadata naming pages
 * that were never written.
 *
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
  unsigned long max_key; /* largest key ever stored (format version 2) */
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
/* every reachable page: kinds and counts, key order and separator bounds,
 * uniform leaf depth, the leaf chain in key order, no page reached twice,
 * the entry count and the recorded largest key. O(pages) time, a bitmap of
 * one bit per page from the context. DS_OK, DS_ERR_INVALID, DS_ERR_ALLOC
 * or a pager error. */
ds_status_t ds_pbtree_verify(ds_pbtree_t *tree, ds_context_t *context);
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
