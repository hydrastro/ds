#ifndef DS_FIBONACCI_HEAP_H
#define DS_FIBONACCI_HEAP_H

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fibonacci heap: an intrusive mergeable priority queue (min-heap) with O(1)
 * amortized insert, find-min, merge, and decrease-key, and O(log n) amortized
 * extract-min. Nodes live in circular doubly linked sibling lists; extract-min
 * consolidates trees by degree, and decrease-key uses cascading cuts to bound
 * tree shape. Users embed ds_fibonacci_heap_node_t and recover the container
 * with CAST(). decrease_key requires the caller to have already lowered the key
 * before calling (the compare function must observe the new value).
 */

typedef struct fibonacci_heap_node {
  struct fibonacci_heap_node *parent;
  struct fibonacci_heap_node *child;
  struct fibonacci_heap_node *left;
  struct fibonacci_heap_node *right;
  int degree;
  bool mark;
} ds_fibonacci_heap_node_t;

typedef struct fibonacci_heap {
  ds_fibonacci_heap_node_t *min;
  ds_fibonacci_heap_node_t *nil;
  size_t size;
  int (*compare)(ds_fibonacci_heap_node_t *, ds_fibonacci_heap_node_t *);
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_fibonacci_heap_t;

ds_fibonacci_heap_t *ds_fibonacci_heap_create(
    int (*compare)(ds_fibonacci_heap_node_t *, ds_fibonacci_heap_node_t *));
ds_fibonacci_heap_t *ds_fibonacci_heap_create_alloc(
    int (*compare)(ds_fibonacci_heap_node_t *, ds_fibonacci_heap_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *));
void ds_fibonacci_heap_node_init(ds_fibonacci_heap_node_t *node);
void ds_fibonacci_heap_insert(ds_fibonacci_heap_t *heap,
                              ds_fibonacci_heap_node_t *node);
ds_fibonacci_heap_node_t *ds_fibonacci_heap_peek(ds_fibonacci_heap_t *heap);
ds_fibonacci_heap_node_t *ds_fibonacci_heap_extract(ds_fibonacci_heap_t *heap);
/* lower a node's key: caller must mutate the payload before calling */
void ds_fibonacci_heap_decrease_key(ds_fibonacci_heap_t *heap,
                                    ds_fibonacci_heap_node_t *node);
/* merge other into heap; other is emptied (but not destroyed) */
void ds_fibonacci_heap_merge(ds_fibonacci_heap_t *heap,
                             ds_fibonacci_heap_t *other);
size_t ds_fibonacci_heap_size(ds_fibonacci_heap_t *heap);
bool ds_fibonacci_heap_is_empty(ds_fibonacci_heap_t *heap);
void ds_fibonacci_heap_destroy(ds_fibonacci_heap_t *heap,
                               void (*destroy)(ds_fibonacci_heap_node_t *));

/*
 * The item heap: the same Fibonacci heap (Fredman and Tarjan) over items
 * kept by value in nodes taken from a ds_context_t, with the interface of
 * the library's other addressable heaps (a handle per item, re-keying in
 * either direction, removal, meld) and a lib/pq_iface backend. push, top,
 * meld and decrease cost O(1) amortized, pop, remove and increase O(log n)
 * amortized. Consolidation uses a degree table inside the heap, so only
 * push allocates; nothing recurses (trees can be deep: cascading cuts and
 * teardown walk them iteratively). Item pointers stay valid while the item
 * is queued, an increase reuses the item's node.
 */
#define DS_FIB_MAX_DEGREE 96 /* above log_phi of any size_t count */

typedef int (*ds_fib_compare_fn)(const void *a, const void *b, void *user);
typedef struct ds_fib_item ds_fib_item_t; /* a handle */

typedef struct ds_fib_heap {
  ds_fib_item_t *min; /* in the root ring */
  size_t size;
  size_t item_size;
  ds_fib_compare_fn compare;
  void *user;
  ds_context_t *context;
  ds_fib_item_t *table[DS_FIB_MAX_DEGREE]; /* consolidation scratch */
} ds_fib_heap_t;

ds_status_t ds_fib_heap_init(ds_fib_heap_t *h, size_t item_size,
                             ds_fib_compare_fn compare, void *user,
                             ds_context_t *context);
void ds_fib_heap_destroy(ds_fib_heap_t *h);
/* out_handle optional: valid until the item leaves */
ds_status_t ds_fib_heap_push(ds_fib_heap_t *h, const void *item,
                             ds_fib_item_t **out_handle);
/* NULL when empty */
const void *ds_fib_heap_top(const ds_fib_heap_t *h);
/* DS_EMPTY when empty; out_item optional */
ds_status_t ds_fib_heap_pop(ds_fib_heap_t *h, void *out_item);
/* re-key in either direction; never allocates */
ds_status_t ds_fib_heap_update(ds_fib_heap_t *h, ds_fib_item_t *handle,
                               const void *item);
/* out_item optional */
ds_status_t ds_fib_heap_remove(ds_fib_heap_t *h, ds_fib_item_t *handle,
                               void *out_item);
const void *ds_fib_heap_item(const ds_fib_heap_t *h,
                             const ds_fib_item_t *handle);
size_t ds_fib_heap_size(const ds_fib_heap_t *h);
/* every item of other (same item size and order, memory releasable through
 * h's context) moves into h in O(1); handles stay valid, other is left
 * empty */
void ds_fib_heap_meld(ds_fib_heap_t *h, ds_fib_heap_t *other);
/* links, heap order, degrees, marks only below the roots, the minimum and
 * the size, in O(n) */
bool ds_fib_heap_check(const ds_fib_heap_t *h);

#ifdef __cplusplus
}
#endif

#endif /* DS_FIBONACCI_HEAP_H */
