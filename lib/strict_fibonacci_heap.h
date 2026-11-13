#ifndef DS_STRICT_FIBONACCI_HEAP_H
#define DS_STRICT_FIBONACCI_HEAP_H

/*
 * Strict Fibonacci heaps (Brodal, Lagogiannis and Tarjan, 2012): the
 * Fibonacci heap bounds as worst-case bounds in a pointer machine. push,
 * top, meld and decrease take O(1) worst-case time, pop and remove
 * O(log n) worst case; an increase is a removal and a push of the same
 * item, O(log n).
 *
 * The heap is one heap-ordered tree with a passive root. Every other node
 * is active or passive; an active node whose parent is passive is an
 * active root, an active node's rank is its number of active children,
 * and its loss counts the active children it has lost since it last was an
 * active root. Active children come first in every child list; the root's
 * children run active, then passive with active children, then linkable
 * (passive with passive children only). All nodes but the root sit in a
 * queue Q. Four transformations keep the invariants: an active root
 * reduction links two active roots of equal rank; a root degree reduction
 * turns the root's three last linkable children into an active root of
 * rank one; a one-node loss reduction moves a node of loss two or more up
 * to the root; a two-node loss reduction links two nodes of loss one and
 * equal rank.
 *
 * The invariants, with R = 2 lg n + 6: at most R + 1 active roots, total
 * loss at most R + 1, root degree at most R + 3, every other degree at
 * most 2 lg n + 12, and the i-th active child from the right of an active
 * node has rank plus loss at least i - 1 (so every rank is at most R).
 * decrease does one loss reduction, then up to six active root and four
 * root degree reductions, an active root reduction first whenever one
 * applies; meld does up to one of each the same way; pop cycles two nodes
 * of Q (each giving its two last passive children to the new root), then
 * does one loss reduction and reductions until none applies.
 *
 * Melding makes the smaller heap's nodes passive at once: active nodes
 * point to a shared record whose flag is cleared, and drop it when next
 * seen. Decreasing a key below the root's swaps the two items between
 * their nodes; items therefore live in cells of their own, and a handle is
 * the cell, valid (and the item at one address) until the item leaves the
 * heap. Equal items are ordered by their cells' addresses, so the order is
 * strict and no link can close a cycle.
 *
 * Ranks are records in a list per heap, each holding its active roots and
 * its nodes of loss one; the ranks holding two of either sit in two lists,
 * so every reduction is found in O(1). A heap of n items keeps
 * min(R, n - 1) + 2 rank records: push adds at most two, and meld takes
 * what it needs from the smaller heap's list, which always suffices; the
 * rest is garbage that later operations release two records at a time.
 *
 * Memory comes from the context: a node and a cell per item, the rank
 * records, and one small record per heap that has had items. A meld
 * releases the absorbed heap's memory through h's context. Nothing
 * recurses.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ds_sf_compare_fn)(const void *a, const void *b, void *user);
typedef struct ds_sf_node ds_sf_node_t;
typedef struct ds_sf_cell ds_sf_cell_t; /* a handle */
typedef struct ds_sf_record ds_sf_record_t;
typedef struct ds_sf_rank ds_sf_rank_t;

typedef struct ds_sf_heap {
  ds_sf_node_t *root;
  ds_sf_node_t *queue; /* front of Q */
  size_t size;
  size_t item_size;
  ds_sf_compare_fn compare;
  void *user;
  ds_context_t *context;
  ds_sf_record_t *record; /* the active flag of this heap's active nodes */
  /* the root's leftmost passive child with active children, else its
   * rightmost active child, else NULL */
  ds_sf_node_t *boundary;
  size_t active_roots;
  size_t total_loss;
  ds_sf_rank_t *rank0; /* the rank list, rank 0 first */
  ds_sf_rank_t *top;
  /* ranks holding two or more active roots, and nodes of loss one */
  ds_sf_rank_t *pairs[2];
  ds_sf_node_t *loss2;   /* nodes of loss two or more */
  ds_sf_rank_t *garbage; /* rank records left by melds */
  ds_sf_rank_t *garbage_tail;
} ds_sf_heap_t;

ds_status_t ds_sf_heap_init(ds_sf_heap_t *h, size_t item_size,
                            ds_sf_compare_fn compare, void *user,
                            ds_context_t *context);
void ds_sf_heap_destroy(ds_sf_heap_t *h);
/* out_handle optional: valid until the item leaves */
ds_status_t ds_sf_heap_push(ds_sf_heap_t *h, const void *item,
                            ds_sf_cell_t **out_handle);
/* NULL when empty */
const void *ds_sf_heap_top(const ds_sf_heap_t *h);
/* DS_EMPTY when empty; out_item optional */
ds_status_t ds_sf_heap_pop(ds_sf_heap_t *h, void *out_item);
/* re-key in either direction; never fails for a valid handle: an increase
 * reuses the item's node */
ds_status_t ds_sf_heap_update(ds_sf_heap_t *h, ds_sf_cell_t *handle,
                              const void *item);
/* out_item optional */
ds_status_t ds_sf_heap_remove(ds_sf_heap_t *h, ds_sf_cell_t *handle,
                              void *out_item);
const void *ds_sf_heap_item(const ds_sf_heap_t *h, const ds_sf_cell_t *handle);
size_t ds_sf_heap_size(const ds_sf_heap_t *h);
/* move every item of other (same item size and order, memory releasable
 * through h's context) into h in O(1); handles stay valid, other is left
 * empty and reusable */
void ds_sf_heap_meld(ds_sf_heap_t *h, ds_sf_heap_t *other);

/* the structure and every invariant above (heap order, links, ranks,
 * losses, Q, the rank lists and the bounds) in O(n) */
bool ds_sf_heap_check(const ds_sf_heap_t *h);
/* current measures, for inspection: active roots, total loss, root degree
 * and the largest degree of another node (O(n)) */
void ds_sf_heap_measures(const ds_sf_heap_t *h, size_t *out_active_roots,
                         size_t *out_total_loss, size_t *out_root_degree,
                         size_t *out_max_degree);

#ifdef __cplusplus
}
#endif

#endif /* DS_STRICT_FIBONACCI_HEAP_H */
