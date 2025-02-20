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
  size_t parent;
#ifdef DS_THREAD_SAFE
  LOCK(heap)
#endif
  parent = (index - 1) / 2;
  while (index > 0 && compare(heap->data[index], heap->data[parent]) < 0) {
    swap(&heap->data[index], &heap->data[parent]);
    index = parent;
    parent = (index - 1) / 2;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(heap)
#endif
}

void heapify_down(heap_t *heap, size_t index,
                  int (*compare)(heap_node_t *, heap_node_t *)) {
  size_t left, right, smallest;
#ifdef DS_THREAD_SAFE
  LOCK(heap)
#endif
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
#ifdef DS_THREAD_SAFE
  UNLOCK(heap)
#endif
}

heap_t *FUNC(heap_create)(size_t capacity) {
  return FUNC(heap_create_alloc)(capacity, malloc, free);
}

heap_t *FUNC(heap_create_alloc)(size_t capacity, void *(*allocator)(size_t),
                                void (*deallocator)(void *)) {
  heap_t *heap = (heap_t *)allocator(sizeof(heap_t));
  heap->allocator = allocator;
  heap->deallocator = deallocator;
  heap->data = (heap_node_t **)allocator(capacity * sizeof(heap_node_t *));
  heap->size = 0;
  heap->capacity = capacity;
  heap->nil = (heap_node_t *)allocator(sizeof(heap_node_t));
  heap->nil->index = (size_t)-1;
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(heap)
#endif
  return heap;
}

void FUNC(heap_insert)(heap_t *heap, heap_node_t *node,
                       int (*compare)(heap_node_t *, heap_node_t *)) {
#ifdef DS_THREAD_SAFE
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
#ifdef DS_THREAD_SAFE
  UNLOCK(heap)
#endif
}

void *FUNC(heap_extract_root)(heap_t *heap,
                              int (*compare)(heap_node_t *, heap_node_t *)) {
  void *root;
#ifdef DS_THREAD_SAFE
  LOCK(heap)
#endif
  if (FUNC(heap_is_empty)(heap)) {
#ifdef DS_THREAD_SAFE
    UNLOCK(heap)
#endif
    return heap->nil;
  }
  root = heap->data[0];
  heap->data[0] = heap->data[heap->size - 1];
  heap->size--;
  heapify_down(heap, (size_t)0, compare);
#ifdef DS_THREAD_SAFE
  UNLOCK(heap)
#endif
  return root;
}

void *FUNC(heap_peek_root)(heap_t *heap) {
#ifdef DS_THREAD_SAFE
  LOCK(heap)
#endif
  if (FUNC(heap_is_empty)(heap)) {
#ifdef DS_THREAD_SAFE
    UNLOCK(heap)
#endif
    return heap->nil;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(heap)
#endif
  return heap->data[0];
}

bool FUNC(heap_is_empty)(heap_t *heap) { return heap->size == 0; }

void FUNC(heap_destroy)(heap_t *heap, void (*destroy)(heap_node_t *)) {
  size_t i;
  if (destroy != NULL) {
    for (i = 0; i < heap->size; ++i) {
      destroy(heap->data[i]);
    }
  }
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(heap)
#endif
  heap->deallocator(heap->nil);
  heap->deallocator(heap->data);
  heap->deallocator(heap);
}

heap_t *FUNC(heap_clone)(heap_t *heap,
                         heap_node_t *(*clone_node)(heap_node_t *)) {
  size_t i;
  heap_t *new_heap;
#ifdef DS_THREAD_SAFE
  LOCK(heap)
#endif
  new_heap = FUNC(heap_create_alloc)(heap->capacity, heap->allocator,
                                     heap->deallocator);
  new_heap->size = heap->size;
  for (i = 0; i < heap->size; ++i) {
    heap_node_t *original_node = heap->data[i];
    heap_node_t *cloned_node = clone_node(original_node);
    new_heap->data[i] = cloned_node;
    cloned_node->index = i;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(heap)
#endif

  return new_heap;
}
