#ifndef DS_HEAP_H
#define DS_HEAP_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct heap_node {
  size_t index;
} heap_node_t;

typedef struct heap {
  heap_node_t **data;
  size_t size;
  size_t capacity;
  heap_node_t *nil;
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} heap_t;

void swap(heap_node_t **a, heap_node_t **b);

void heapify_up(heap_t *heap, size_t index,
                int (*compare)(heap_node_t *, heap_node_t *));

void heapify_down(heap_t *heap, size_t index,
                  int (*compare)(heap_node_t *, heap_node_t *));

heap_t *heap_create(size_t capacity);

void heap_insert(heap_t *heap, heap_node_t *node,
                 int (*compare)(heap_node_t *, heap_node_t *));

void *heap_extract_root(heap_t *heap,
                        int (*compare)(heap_node_t *, heap_node_t *));

void *heap_peek_root(heap_t *heap);

bool heap_is_empty(heap_t *heap);

void heap_destroy(heap_t *heap, void (*destroy)(heap_node_t *));

heap_t *heap_clone(heap_t *heap, heap_node_t *(*clone_node)(heap_node_t *));

#endif /* DS_HEAP_H */
