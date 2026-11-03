#ifndef DS_WEAK_HEAP_H
#define DS_WEAK_HEAP_H

/*
 * Weak heap (Dutton, 1993; Edelkamp and Wegener): a priority queue of
 * fixed-size elements stored by value in one array plus one "reverse" bit
 * per element.
 *
 * A weak heap relaxes the heap order: every element is no greater than the
 * elements of its right subtree only, and the root has no left subtree, so
 * the root is the minimum. Flipping a node's reverse bit swaps its subtrees
 * in O(1), which is what lets a weak heap need fewer comparisons than a
 * binary heap: building takes n - 1 comparisons and pop at most
 * ceil(log2 n). Weak-heap sort uses at most n log2 n - 0.9n comparisons,
 * close to the information-theoretic bound.
 *
 * Storage: dynamic (ds_weak_heap_init, through a ds_context_t) or a fixed
 * caller element buffer plus a bit buffer of DS_WEAK_HEAP_BITS_SIZE(capacity)
 * bytes (ds_weak_heap_init_buffer, never allocates; DS_ERR_OVERFLOW when
 * full).
 *
 * Complexity: push O(log n) (O(1) comparisons on average), pop O(log n),
 * peek O(1), assign/build O(n).
 * Concurrency: a heap may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include "vec.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_WEAK_HEAP_BITS_SIZE(capacity) (((capacity) + 7) / 8)

typedef int (*ds_weak_heap_compare_func_t)(const void *a, const void *b,
                                           void *user);

typedef struct ds_weak_heap {
  ds_vec_t items;
  unsigned char *bits;  /* reverse bits, one per element */
  size_t bits_capacity; /* in elements */
  ds_weak_heap_compare_func_t compare;
  void *user;
  ds_context_t *context;
  bool fixed;
} ds_weak_heap_t;

ds_status_t ds_weak_heap_init(ds_weak_heap_t *heap, size_t elem_size,
                              ds_weak_heap_compare_func_t compare, void *user,
                              ds_context_t *context);
ds_status_t ds_weak_heap_init_buffer(ds_weak_heap_t *heap, size_t elem_size,
                                     ds_weak_heap_compare_func_t compare,
                                     void *user, void *buffer, size_t capacity,
                                     unsigned char *bits_buffer);
void ds_weak_heap_destroy(ds_weak_heap_t *heap);

ds_status_t ds_weak_heap_push(ds_weak_heap_t *heap, const void *elem);
const void *ds_weak_heap_peek(const ds_weak_heap_t *heap);
ds_status_t ds_weak_heap_pop(ds_weak_heap_t *heap, void *out);
/* Replace the contents with `count` elements and heapify in O(n). */
ds_status_t ds_weak_heap_assign(ds_weak_heap_t *heap, const void *elems,
                                size_t count);
size_t ds_weak_heap_size(const ds_weak_heap_t *heap);
bool ds_weak_heap_is_empty(const ds_weak_heap_t *heap);
void ds_weak_heap_clear(ds_weak_heap_t *heap);

/*
 * Weak-heap sort (Dutton): sort `count` elements ascending in place.
 * `bits` is scratch of DS_WEAK_HEAP_BITS_SIZE(count) bytes; pass NULL to
 * allocate it from `context` (NULL for the default). Not stable.
 */
ds_status_t ds_weak_heap_sort(void *base, size_t count, size_t elem_size,
                              ds_weak_heap_compare_func_t compare, void *user,
                              unsigned char *bits, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_WEAK_HEAP_H */
