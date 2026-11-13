#ifndef DS_RANK_PAIRING_HEAP_H
#define DS_RANK_PAIRING_HEAP_H

/*
 * Rank-pairing heaps (Haeupler, Sen and Tarjan, "Rank-pairing heaps",
 * SIAM J. Comput. 2011): the amortized bounds of Fibonacci heaps with the
 * simplicity of pairing heaps, and no cascading cuts.
 *
 * The heap is a list of half trees: binary trees whose root has only a
 * left child and in which every node's item is no larger than any item in
 * its left subtree (half ordering; a half tree is the binary form of a
 * heap-ordered tree, left child = first child, right child = next
 * sibling). Every node has a rank. A root's rank is one more than its left
 * child's; the rank of any other node follows from its two children's ranks r1
 * and r2 (a missing child counts as -1) by the rank rule chosen at init:
 *
 *   type 1   r1 + 1 when r1 == r2, else max(r1, r2)
 *   type 2   max(r1, r2) + 1 when |r1 - r2| <= 1, else max(r1, r2)
 *
 * Type 2 is the more relaxed rule (fewer rank changes); both give the
 * same bounds.
 *
 *   push       a one-node half tree added to the root list
 *   pop        the minimum's left spine becomes half trees; all half
 *              trees are linked in one pass, at most one link per tree,
 *              pairing equal ranks through a bucket table
 *   decrease   the node is cut with its left subtree and becomes a root;
 *              its right subtree takes its place, and ranks are lowered
 *              from the cut upward until the rule holds again (no cascade
 *              of cuts)
 *   meld       the two root lists are spliced
 *
 * Costs, worst case / amortized: push and meld O(1) / O(1); decrease
 * O(n) / O(1) (the rank walk is short on average but may climb a whole
 * half tree once); pop and remove O(n) / O(log n); increase (a removal
 * and a push of the same node) O(n) / O(log n). Ranks stay below
 * DS_RP_MAX_RANK for any size a size_t can count.
 *
 * Items are stored by value inside the nodes, and a handle is the node:
 * nodes never move, so pointers from top and item stay valid while their
 * item is queued. Every walk is iterative.
 *
 * Memory comes from the context: nodes are carved from chunks of 256,
 * reused, and returned at destroy; a meld takes over the other heap's
 * chunks, so it needs memory releasable through this heap's context.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_RP_MAX_RANK 192

typedef int (*ds_rp_compare_fn)(const void *a, const void *b, void *user);

typedef struct ds_rp_node ds_rp_node_t;
typedef struct ds_rp_chunk ds_rp_chunk_t;

typedef struct ds_rp_heap {
  ds_rp_node_t *min; /* in the circular root list */
  size_t size;
  size_t roots;
  int type; /* 1 or 2 */
  size_t item_size;
  size_t node_offset;
  size_t node_size;
  ds_rp_compare_fn compare;
  void *user;
  ds_context_t *context;
  ds_rp_chunk_t *chunks;
  ds_rp_chunk_t *chunks_tail;
  ds_rp_node_t *spare;
  ds_rp_node_t *spare_tail;
  ds_rp_node_t *bucket[DS_RP_MAX_RANK]; /* empty between calls */
} ds_rp_heap_t;

/* type 1 or 2 (DS_ERR_INVALID otherwise, and for item_size 0) */
ds_status_t ds_rp_heap_init(ds_rp_heap_t *h, size_t item_size,
                            ds_rp_compare_fn compare, void *user, int type,
                            ds_context_t *context);
void ds_rp_heap_destroy(ds_rp_heap_t *h);
/* out_node optional: the handle, valid until the item leaves */
ds_status_t ds_rp_heap_push(ds_rp_heap_t *h, const void *item,
                            ds_rp_node_t **out_node);
/* NULL when empty */
const void *ds_rp_heap_top(const ds_rp_heap_t *h);
/* DS_EMPTY when empty; out_item optional */
ds_status_t ds_rp_heap_pop(ds_rp_heap_t *h, void *out_item);
/* re-key in either direction; never fails for a queued node */
ds_status_t ds_rp_heap_update(ds_rp_heap_t *h, ds_rp_node_t *node,
                              const void *item);
/* out_item optional */
ds_status_t ds_rp_heap_remove(ds_rp_heap_t *h, ds_rp_node_t *node,
                              void *out_item);
const void *ds_rp_heap_item(const ds_rp_heap_t *h, const ds_rp_node_t *node);
size_t ds_rp_heap_size(const ds_rp_heap_t *h);
/* half trees in the root list (for inspection) */
size_t ds_rp_heap_roots(const ds_rp_heap_t *h);
/* move every item of other (same item size and order, memory releasable
 * through h's context) into h in O(1); handles stay valid; other is left
 * empty */
void ds_rp_heap_meld(ds_rp_heap_t *h, ds_rp_heap_t *other);
/* checks half ordering, the rank rule, parent links and the counts in
 * O(n) (for tests and debugging) */
bool ds_rp_heap_check(const ds_rp_heap_t *h);

#ifdef __cplusplus
}
#endif

#endif /* DS_RANK_PAIRING_HEAP_H */
