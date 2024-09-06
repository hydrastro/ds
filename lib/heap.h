#ifndef DS_HEAP_H
#define DS_HEAP_H

#ifndef CAST
#define CAST(node, type) ((type *)(node))
#endif

#include<stddef.h>

typedef struct heap {
  void **data;
  size_t size;
  size_t capacity;
  void *nil;
} heap_t;

heap_t *heap_create(size_t capacity);

void heap_insert(heap_t *heap, void *node, int (*cmp)(void *, void *));

void *heap_extract_root(heap_t *heap, int (*cmp)(void *, void *));

void *heap_peek_root(heap_t *heap);

int heap_is_empty(heap_t *heap);

void heap_destroy(heap_t *heap, void (*destroy_node)(void *));

#endif // DS_HEAP_H