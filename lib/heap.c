#include "heap.h"
#include <stdio.h>
#include <stdlib.h>

void swap(ds_heap_node_t **a, ds_heap_node_t **b) {
  ds_heap_node_t *temp;
  size_t index;
  index = (size_t)(*a)->index;
  (*a)->index = (size_t)(*b)->index;
  (*b)->index = index;
  temp = *a;
  *a = *b;
  *b = temp;
}

void heapify_up(ds_heap_t *heap, size_t index,
                int (*compare)(ds_heap_node_t *, ds_heap_node_t *)) {
  size_t parent;
  parent = (index - 1) / 2;
  while (index > 0 && compare(heap->data[index], heap->data[parent]) < 0) {
    swap(&heap->data[index], &heap->data[parent]);
    index = parent;
    parent = (index - 1) / 2;
  }
}

void heapify_down(ds_heap_t *heap, size_t index,
                  int (*compare)(ds_heap_node_t *, ds_heap_node_t *)) {
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
}

ds_heap_t *ds_heap_create(size_t capacity) {
  return ds_heap_create_alloc(capacity, malloc, free);
}

ds_heap_t *ds_heap_create_alloc(size_t capacity, void *(*allocator)(size_t),
                                void (*deallocator)(void *)) {
  ds_heap_t *heap;
  if (allocator == NULL || deallocator == NULL ||
      capacity > (size_t)-1 / sizeof(ds_heap_node_t *)) {
    return NULL;
  }
  heap = (ds_heap_t *)allocator(sizeof(ds_heap_t));
  if (heap == NULL) {
    return NULL;
  }
  heap->allocator = allocator;
  heap->deallocator = deallocator;
  heap->data =
      (capacity == 0U)
          ? NULL
          : (ds_heap_node_t **)allocator(capacity * sizeof(ds_heap_node_t *));
  if (capacity != 0U && heap->data == NULL) {
    deallocator(heap);
    return NULL;
  }
  heap->size = 0;
  heap->capacity = capacity;
  heap->nil = (ds_heap_node_t *)allocator(sizeof(ds_heap_node_t));
  if (heap->nil == NULL) {
    if (heap->data != NULL) {
      deallocator(heap->data);
    }
    deallocator(heap);
    return NULL;
  }
  heap->nil->index = (size_t)-1;
  return heap;
}

ds_status_t ds_heap_insert_ex(ds_heap_t *heap, ds_heap_node_t *node,
                              int (*compare)(ds_heap_node_t *,
                                             ds_heap_node_t *)) {
  if (heap == NULL || node == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (heap->size >= heap->capacity) {
    size_t new_capacity;
    ds_heap_node_t **new_data;
    size_t i;
    if (heap->capacity > (size_t)-1 / 2U / sizeof(*new_data)) {
      return DS_ERR_OVERFLOW;
    }
    new_capacity = heap->capacity == 0U ? 1U : heap->capacity * 2U;
    new_data =
        (ds_heap_node_t **)heap->allocator(new_capacity * sizeof(*new_data));
    if (new_data == NULL) {
      return DS_ERR_ALLOC;
    }
    for (i = 0U; i < heap->size; i++) {
      new_data[i] = heap->data[i];
    }
    if (heap->data != NULL) {
      heap->deallocator(heap->data);
    }
    heap->data = new_data;
    heap->capacity = new_capacity;
  }
  node->index = heap->size;
  heap->data[heap->size] = node;
  heapify_up(heap, heap->size, compare);
  heap->size++;
  return DS_OK;
}

void ds_heap_insert(ds_heap_t *heap, ds_heap_node_t *node,
                    int (*compare)(ds_heap_node_t *, ds_heap_node_t *)) {
  (void)ds_heap_insert_ex(heap, node, compare);
}

void *ds_heap_extract_root(ds_heap_t *heap,
                           int (*compare)(ds_heap_node_t *, ds_heap_node_t *)) {
  void *root;
  if (ds_heap_is_empty(heap)) {
    return heap->nil;
  }
  root = heap->data[0];
  heap->data[0] = heap->data[heap->size - 1];
  heap->size--;
  if (heap->size != 0U) {
    heap->data[0]->index = 0U;
    heapify_down(heap, (size_t)0, compare);
  }
  return root;
}

void *ds_heap_peek_root(ds_heap_t *heap) {
  if (ds_heap_is_empty(heap)) {
    return heap->nil;
  }
  return heap->data[0];
}

bool ds_heap_is_empty(ds_heap_t *heap) { return heap->size == 0; }

void ds_heap_destroy(ds_heap_t *heap, void (*destroy)(ds_heap_node_t *)) {
  size_t i;
  if (destroy != NULL) {
    for (i = 0; i < heap->size; ++i) {
      destroy(heap->data[i]);
    }
  }
  heap->deallocator(heap->nil);
  if (heap->data != NULL) {
    heap->deallocator(heap->data);
  }
  heap->deallocator(heap);
}

/* clones of the source's nodes at the same positions in copy (as large);
 * false when clone_node returns NULL (the clones made so far are in copy) */
static bool heap_clone_nodes(const ds_heap_t *heap, ds_heap_t *copy,
                             ds_heap_node_t *(*clone_node)(ds_heap_node_t *)) {
  size_t i;
  for (i = 0; i < heap->size; ++i) {
    ds_heap_node_t *node = clone_node(heap->data[i]);
    if (node == NULL) {
      return false;
    }
    node->index = i;
    copy->data[i] = node;
    copy->size = i + 1;
  }
  return true;
}

ds_heap_t *ds_heap_clone(ds_heap_t *heap,
                         ds_heap_node_t *(*clone_node)(ds_heap_node_t *)) {
  ds_heap_t *copy;
  if (heap == NULL || clone_node == NULL) {
    return NULL;
  }
  copy =
      ds_heap_create_alloc(heap->capacity, heap->allocator, heap->deallocator);
  if (copy != NULL && !heap_clone_nodes(heap, copy, clone_node)) {
    /* the clones made so far are not released */
    ds_heap_destroy(copy, NULL);
    copy = NULL;
  }
  return copy;
}

ds_status_t ds_heap_clone_ex(const ds_heap_t *heap,
                             ds_heap_node_t *(*clone_node)(ds_heap_node_t *),
                             void (*destroy_clone)(ds_heap_node_t *),
                             ds_heap_t **out) {
  ds_heap_t *copy;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (heap == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  copy =
      ds_heap_create_alloc(heap->capacity, heap->allocator, heap->deallocator);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!heap_clone_nodes(heap, copy, clone_node)) {
    ds_heap_destroy(copy, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  *out = copy;
  return DS_OK;
}
