#include <stdlib.h>
#include <stdio.h>
#include "heap.h"

void swap(void **a, void **b) {
  void *temp = *a;
  *a = *b;
  *b = temp;
}

void heapify_up(heap_t *heap, size_t index, int (*cmp)(void *, void *)) {
  size_t parent = (index - 1) / 2;
  while (index > 0 && cmp(heap->data[index], heap->data[parent]) < 0) {
    swap(&heap->data[index], &heap->data[parent]);
    index = parent;
    parent = (index - 1) / 2;
  }
}

void heapify_down(heap_t *heap, size_t index, int (*cmp)(void *, void *)) {
  size_t left, right, smallest;
  while (1) {
    left = 2 * index + 1;
    right = 2 * index + 2;
    smallest = index;

    if (left < heap->size && cmp(heap->data[left], heap->data[smallest]) < 0) {
      smallest = left;
    }
    if (right < heap->size && cmp(heap->data[right], heap->data[smallest]) < 0) {
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

heap_t *heap_create(size_t capacity) {
  heap_t *heap = (heap_t *)malloc(sizeof(heap_t));
  heap->data = (void **)malloc(capacity * sizeof(void *));
  heap->size = 0;
  heap->capacity = capacity;
  heap->nil = (void *)malloc(sizeof(void));
  return heap;
}

void heap_insert(heap_t *heap, void *node, int (*cmp)(void *, void *)) {
  if (heap->size >= heap->capacity) {
    heap->capacity *= 2;
    heap->data = (void **)realloc(heap->data, heap->capacity * sizeof(void *));
  }
  heap->data[heap->size] = node;
  heapify_up(heap, heap->size, cmp);
  heap->size++;
}

void *heap_extract_root(heap_t *heap, int (*cmp)(void *, void*)) {
  if (heap_is_empty(heap)) {
    return heap->nil;
  }
  void *root = heap->data[0];
  heap->data[0] = heap->data[heap->size - 1];
  heap->size--;
  heapify_down(heap, 0, cmp);
  return root;
}

void *heap_peek_root(heap_t *heap) {
  if (heap_is_empty(heap)) {
    return heap->nil;
  }
  return heap->data[0];
}

int heap_is_empty(heap_t *heap) {
  return heap->size == 0;
}

void heap_destroy(heap_t *heap, void (*destroy_node)(void *)) {
  if (destroy_node) {
    for (size_t i = 0; i < heap->size; ++i) {
      destroy_node(heap->data[i]);
    }
  }
  free(heap->nil);
  free(heap->data);
  free(heap);
}
