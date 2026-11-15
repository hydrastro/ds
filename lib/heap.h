#ifndef DS_HEAP_H
#define DS_HEAP_H

#include "common.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct heap_node {
  size_t index;
} ds_heap_node_t;

typedef struct heap {
  ds_heap_node_t **data;
  size_t size;
  size_t capacity;
  ds_heap_node_t *nil;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_heap_t;

void swap(ds_heap_node_t **a, ds_heap_node_t **b);
void heapify_up(ds_heap_t *heap, size_t index,
                int (*compare)(ds_heap_node_t *, ds_heap_node_t *));
void heapify_down(ds_heap_t *heap, size_t index,
                  int (*compare)(ds_heap_node_t *, ds_heap_node_t *));
/* NULL when memory runs out, for a capacity whose array cannot be sized,
 * or without an allocator and a deallocator */
ds_heap_t *ds_heap_create(size_t capacity);
ds_heap_t *ds_heap_create_alloc(size_t capacity, void *(*allocator)(size_t),
                                void (*deallocator)(void *));
/* Legacy insert: when the array cannot grow, the node is not inserted
 * (heap->size is unchanged); insert_ex reports it */
void ds_heap_insert(ds_heap_t *heap, ds_heap_node_t *node,
                    int (*compare)(ds_heap_node_t *, ds_heap_node_t *));
/* DS_ERR_ALLOC (or DS_ERR_OVERFLOW) when the array cannot grow, with the
 * heap unchanged */
ds_status_t ds_heap_insert_ex(ds_heap_t *heap, ds_heap_node_t *node,
                              int (*compare)(ds_heap_node_t *,
                                             ds_heap_node_t *));
void *ds_heap_extract_root(ds_heap_t *heap,
                           int (*compare)(ds_heap_node_t *, ds_heap_node_t *));
void *ds_heap_peek_root(ds_heap_t *heap);
bool ds_heap_is_empty(ds_heap_t *heap);
void ds_heap_destroy(ds_heap_t *heap, void (*destroy)(ds_heap_node_t *));
/* Legacy clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_heap_t *ds_heap_clone(ds_heap_t *heap,
                         ds_heap_node_t *(*clone_node)(ds_heap_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t ds_heap_clone_ex(const ds_heap_t *heap,
                             ds_heap_node_t *(*clone_node)(ds_heap_node_t *),
                             void (*destroy_clone)(ds_heap_node_t *),
                             ds_heap_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_HEAP_H */
