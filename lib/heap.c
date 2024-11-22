#include "heap.h"
#include <stdio.h>
#include <stdlib.h>

void swap(heap_node_t **a, heap_node_t **b) {
  heap_node_t *temp;
  size_t index;
  index = (size_t)(*a)->index;
  (*a)->index = (size_t)(*b)->index;
  (*b)->index = index;
  temp = *a;
  *a = *b;
  *b = temp;
}

void heapify_up(heap_t *heap, size_t index,
                int (*compare)(heap_node_t *, heap_node_t *)) {
#ifdef HEAP_THREAD_SAFE
  LOCK(heap)
#endif
  size_t parent = (index - 1) / 2;
  while (index > 0 && compare(heap->data[index], heap->data[parent]) < 0) {
    swap(&heap->data[index], &heap->data[parent]);
    index = parent;
    parent = (index - 1) / 2;
  }
#ifdef HEAP_THREAD_SAFE
  UNLOCK(heap)
#endif
}

void heapify_down(heap_t *heap, size_t index,
                  int (*compare)(heap_node_t *, heap_node_t *)) {
#ifdef HEAP_THREAD_SAFE
  LOCK(heap)
#endif
  size_t left, right, smallest;
  while (1) {
    left = 2 * index + 1;
    right = 2 * index + 2;
    smallest = index;

    if (left < heap->size &&
        compare(heap->data[left], heap->data[smallest]) < 0) {
      smallest = left;
    }
    if (right < heap->size &&
        compare(heap->data[right], heap->data[smallest]) < 0) {
      smallest = right;
    }
    if (smallest != index) {
      swap(&heap->data[index], &heap->data[smallest]);
      index = smallest;
    } else {
      break;
    }
  }
#ifdef HEAP_THREAD_SAFE
  UNLOCK(heap)
#endif
}

heap_t *heap_create(size_t capacity) {
  heap_t *heap = (heap_t *)malloc(sizeof(heap_t));
  heap->data = (heap_node_t **)malloc(capacity * sizeof(heap_node_t *));
  heap->size = 0;
  heap->capacity = capacity;
  heap->nil = (heap_node_t *)malloc(sizeof(heap_node_t));
  heap->nil->index = (size_t)-1;
#ifdef HEAP_THREAD_SAFE
  LOCK_INIT_RECURSIVE(heap)
#endif
  return heap;
}

void heap_insert(heap_t *heap, heap_node_t *node,
                 int (*compare)(heap_node_t *, heap_node_t *)) {
#ifdef HEAP_THREAD_SAFE
  LOCK(heap)
#endif
  if (heap->size >= heap->capacity) {
    heap->capacity *= 2;
    heap->data = (heap_node_t **)realloc(heap->data, heap->capacity *
                                                         sizeof(heap_node_t *));
  }
  node->index = heap->size;
  heap->data[heap->size] = node;
  heapify_up(heap, heap->size, compare);
  heap->size++;
#ifdef HEAP_THREAD_SAFE
  UNLOCK(heap)
#endif
}

void *heap_extract_root(heap_t *heap,
                        int (*compare)(heap_node_t *, heap_node_t *)) {
#ifdef HEAP_THREAD_SAFE
  LOCK(heap)
#endif
  if (heap_is_empty(heap)) {
#ifdef HEAP_THREAD_SAFE
    UNLOCK(heap)
#endif
    return heap->nil;
  }
  void *root = heap->data[0];
  heap->data[0] = heap->data[heap->size - 1];
  heap->size--;
  heapify_down(heap, (size_t)0, compare);
#ifdef HEAP_THREAD_SAFE
  UNLOCK(heap)
#endif
  return root;
}

void *heap_peek_root(heap_t *heap) {
#ifdef HEAP_THREAD_SAFE
  LOCK(heap)
#endif
  if (heap_is_empty(heap)) {
#ifdef HEAP_THREAD_SAFE
    UNLOCK(heap)
#endif
    return heap->nil;
  }
#ifdef HEAP_THREAD_SAFE
  UNLOCK(heap)
#endif
  return heap->data[0];
}

bool heap_is_empty(heap_t *heap) { return heap->size == 0; }

void heap_destroy(heap_t *heap, void (*destroy)(heap_node_t *)) {
  size_t i;
  if (destroy != NULL) {
    for (i = 0; i < heap->size; ++i) {
      destroy(heap->data[i]);
    }
  }
#ifdef HEAP_THREAD_SAFE
  LOCK_DESTROY(heap)
#endif
  free(heap->nil);
  free(heap->data);
  free(heap);
}
