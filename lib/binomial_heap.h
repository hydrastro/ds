#ifndef DS_BINOMIAL_HEAP_H
#define DS_BINOMIAL_HEAP_H

/*
 * Binomial heap (Vuillemin, 1978): an intrusive mergeable priority queue made
 * of a forest of binomial trees B_k, at most one per order, kept in a root
 * list sorted by increasing order. B_k has 2^k nodes, so a heap of n nodes
 * has the shape of n written in binary.
 *
 * Users embed ds_binomial_heap_node_t in their own struct and recover it with
 * CAST() (or offsetof). The heap never allocates: nodes are owned by the
 * caller, which makes it usable from static or stack storage.
 *
 * The comparator orders whole nodes and receives the user pointer given at
 * init; the smallest node per the comparator is at the top (reverse it for a
 * max-heap). Node handles stay valid while the node is in the heap, so keys
 * can be changed in place:
 *   - ds_binomial_heap_decrease(): after lowering a node's key;
 *   - ds_binomial_heap_update():   after changing it in either direction;
 *   - ds_binomial_heap_remove():   delete any node.
 * Changing a key without telling the heap breaks the heap order.
 *
 * Complexity (n nodes):
 *   push, meld                 O(log n) worst case, push O(1) amortized
 *   peek                       O(1)
 *   pop, remove, update        O(log n) node visits; node positions are
 *   decrease                   swapped structurally (handles never move to
 *                              another node), which costs O(log^2 n) pointer
 *                              updates in the worst case
 * Concurrency: a heap may be used by one thread at a time.
 */

#include "common.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_binomial_heap_node {
  struct ds_binomial_heap_node *parent;
  struct ds_binomial_heap_node *child;   /* child of highest order */
  struct ds_binomial_heap_node *sibling; /* next sibling / next root */
  struct ds_binomial_heap_node *prev;    /* previous sibling / root */
  unsigned int order;
} ds_binomial_heap_node_t;

typedef int (*ds_binomial_heap_compare_func_t)(const ds_binomial_heap_node_t *a,
                                               const ds_binomial_heap_node_t *b,
                                               void *user);
/* Visitor: return DS_OK to continue or anything else (e.g. DS_STOP) to stop;
 * that status is then returned by the walk. */
typedef ds_status_t (*ds_binomial_heap_visit_func_t)(
    ds_binomial_heap_node_t *node, void *user);

typedef struct ds_binomial_heap {
  ds_binomial_heap_node_t *head; /* root list, increasing order */
  ds_binomial_heap_node_t *min;
  size_t size;
  ds_binomial_heap_compare_func_t compare;
  void *user;
} ds_binomial_heap_t;

ds_status_t ds_binomial_heap_init(ds_binomial_heap_t *heap,
                                  ds_binomial_heap_compare_func_t compare,
                                  void *user);
void ds_binomial_heap_node_init(ds_binomial_heap_node_t *node);

void ds_binomial_heap_push(ds_binomial_heap_t *heap,
                           ds_binomial_heap_node_t *node);
ds_binomial_heap_node_t *ds_binomial_heap_peek(const ds_binomial_heap_t *heap);
ds_binomial_heap_node_t *ds_binomial_heap_pop(ds_binomial_heap_t *heap);
/* Move every node of `other` into `heap`; `other` is left empty. Both heaps
 * must order nodes the same way. */
void ds_binomial_heap_meld(ds_binomial_heap_t *heap, ds_binomial_heap_t *other);
void ds_binomial_heap_decrease(ds_binomial_heap_t *heap,
                               ds_binomial_heap_node_t *node);
void ds_binomial_heap_update(ds_binomial_heap_t *heap,
                             ds_binomial_heap_node_t *node);
void ds_binomial_heap_remove(ds_binomial_heap_t *heap,
                             ds_binomial_heap_node_t *node);

size_t ds_binomial_heap_size(const ds_binomial_heap_t *heap);
bool ds_binomial_heap_is_empty(const ds_binomial_heap_t *heap);
/* Number of trees in the forest (the popcount of the size). */
size_t ds_binomial_heap_tree_count(const ds_binomial_heap_t *heap);

/* Visit every node in no particular order (parents before children). The
 * visitor must not modify the heap. */
ds_status_t ds_binomial_heap_visit(const ds_binomial_heap_t *heap,
                                   ds_binomial_heap_visit_func_t visit,
                                   void *user);
/* Empty the heap, handing each node to `release` (may be NULL) after it has
 * been detached, children before parents, so `release` may free it. */
void ds_binomial_heap_clear(ds_binomial_heap_t *heap,
                            void (*release)(ds_binomial_heap_node_t *node,
                                            void *user),
                            void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_BINOMIAL_HEAP_H */
