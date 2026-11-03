#ifndef DS_FIBONACCI_HEAP_H
#define DS_FIBONACCI_HEAP_H

#include "common.h"
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

#ifdef __cplusplus
}
#endif

#endif /* DS_FIBONACCI_HEAP_H */
