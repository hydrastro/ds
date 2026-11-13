#ifndef DS_HOLLOW_HEAP_H
#define DS_HOLLOW_HEAP_H

/*
 * Hollow heap (Hansen, Kaplan, Tarjan and Zwick, "Hollow heaps", 2015):
 * the bounds of Fibonacci heaps from a simpler structure built on lazy
 * deletion. The one-tree version is used here.
 *
 * Items are stored by value (item_size bytes) inside the nodes of a
 * heap-ordered DAG and ordered by a three-way comparator with a user
 * pointer, smallest on top. A handle is the item's record, which points at
 * the node currently holding the item.
 *
 *   push       a node for the item, linked with the root (one comparison)
 *   decrease   a fresh node takes the item and the old node is left
 *              hollow, keeping its children and becoming the new node's
 *              child: no cut, no cascading
 *   remove     the item's node is left hollow; only removing the minimum
 *              does real work
 *   pop        the root goes hollow; hollow roots are dissolved, the full
 *              nodes they leave without a parent linked by rank as in a
 *              Fibonacci heap, then the survivors linked into one tree
 *   meld       one link
 *
 * Costs, worst case / amortized, with N the number of nodes (full and
 * hollow): push, decrease and meld O(1) / O(1); pop and removing the
 * minimum O(N) / O(log N); remove and increase (a remove and a push) of
 * any other item O(1), paid for at a later pop. A hollow node is freed
 * when it becomes a root; once hollow nodes outnumber items (N > 2 n + 64
 * after an update, remove or pop) the heap is rebuilt from its items in
 * O(N) without allocating, which the operations that made those nodes
 * hollow pay for, so N stays O(n) and log N is O(log n).
 *
 * Items move: a decrease or increase gives the item a new node, so a
 * pointer from top or item is valid only until the next push, pop,
 * update or remove. Handles stay valid until their item leaves.
 *
 * Every walk is iterative: no recursion on the DAG. A node with two
 * parents is the last child of its second parent, and each walk detaches
 * it from whichever parent goes first, so every node is visited once.
 *
 * Memory comes from the context: nodes and records are carved from chunks
 * of 256, reused, and returned at destroy (a meld takes over the other
 * heap's chunks), so steady work allocates nothing.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ranks stay below log_phi(N) + 2, under 96 for any N a 64-bit size_t can
 * count; the table has room to spare */
#define DS_HOLLOW_MAX_RANK 128

typedef int (*ds_hollow_compare_fn)(const void *a, const void *b, void *user);

typedef struct ds_hollow_node ds_hollow_node_t;
typedef struct ds_hollow_chunk ds_hollow_chunk_t;
/* a handle: an item's record */
typedef struct ds_hollow_item ds_hollow_item_t;

typedef struct ds_hollow_heap {
  ds_hollow_node_t *root;
  size_t size;  /* items */
  size_t nodes; /* nodes in the heap, full and hollow */
  size_t item_size;
  size_t node_offset; /* of the item inside a node */
  size_t node_size;   /* node stride inside a chunk */
  ds_hollow_compare_fn compare;
  void *user;
  ds_context_t *context;
  unsigned long rebuilds;
  ds_hollow_chunk_t *chunks; /* node and record chunks, released at destroy */
  ds_hollow_chunk_t *chunks_tail;
  ds_hollow_node_t *spare; /* free nodes */
  ds_hollow_node_t *spare_tail;
  ds_hollow_item_t *spare_items; /* free records */
  ds_hollow_item_t *spare_items_tail;
  ds_hollow_node_t *ranks[DS_HOLLOW_MAX_RANK]; /* empty between calls */
} ds_hollow_heap_t;

/* DS_ERR_NULL without compare, DS_ERR_INVALID for item_size 0 */
ds_status_t ds_hollow_heap_init(ds_hollow_heap_t *h, size_t item_size,
                                ds_hollow_compare_fn compare, void *user,
                                ds_context_t *context);
void ds_hollow_heap_destroy(ds_hollow_heap_t *h);
/* out_handle optional; it stays valid until the item is popped or
 * removed */
ds_status_t ds_hollow_heap_push(ds_hollow_heap_t *h, const void *item,
                                ds_hollow_item_t **out_handle);
/* NULL when empty */
const void *ds_hollow_heap_top(const ds_hollow_heap_t *h);
/* DS_EMPTY when empty; out_item optional */
ds_status_t ds_hollow_heap_pop(ds_hollow_heap_t *h, void *out_item);
/* re-key in either direction; DS_ERR_ALLOC (nothing changed) when a node
 * cannot be allocated */
ds_status_t ds_hollow_heap_update(ds_hollow_heap_t *h, ds_hollow_item_t *it,
                                  const void *item);
/* out_item optional */
ds_status_t ds_hollow_heap_remove(ds_hollow_heap_t *h, ds_hollow_item_t *it,
                                  void *out_item);
const void *ds_hollow_heap_item(const ds_hollow_heap_t *h,
                                const ds_hollow_item_t *it);
size_t ds_hollow_heap_size(const ds_hollow_heap_t *h);
/* nodes, full and hollow (for inspection) */
size_t ds_hollow_heap_nodes(const ds_hollow_heap_t *h);
/* move every item of other (same item size and order, memory releasable
 * through h's context) into h in O(1); handles stay valid; other is left
 * empty */
void ds_hollow_heap_meld(ds_hollow_heap_t *h, ds_hollow_heap_t *other);
/* free every hollow node now, in O(N) without allocating */
void ds_hollow_heap_rebuild(ds_hollow_heap_t *h);

#ifdef __cplusplus
}
#endif

#endif /* DS_HOLLOW_HEAP_H */
