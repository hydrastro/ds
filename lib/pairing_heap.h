#ifndef DS_PAIRING_HEAP_H
#define DS_PAIRING_HEAP_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pairing_heap_node {
  struct pairing_heap_node *child;
  struct pairing_heap_node *sibling;
  struct pairing_heap_node *prev;
} ds_pairing_heap_node_t;

typedef struct pairing_heap {
  ds_pairing_heap_node_t *root;
  ds_pairing_heap_node_t *nil;
  size_t size;
  int (*compare)(ds_pairing_heap_node_t *, ds_pairing_heap_node_t *);
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_pairing_heap_t;

ds_pairing_heap_t *FUNC(ds_pairing_heap_create)(
    int (*compare)(ds_pairing_heap_node_t *, ds_pairing_heap_node_t *));
ds_pairing_heap_t *FUNC(ds_pairing_heap_create_alloc)(
    int (*compare)(ds_pairing_heap_node_t *, ds_pairing_heap_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *));
void FUNC(ds_pairing_heap_node_init)(ds_pairing_heap_node_t *node);
void FUNC(ds_pairing_heap_insert)(ds_pairing_heap_t *heap,
                                  ds_pairing_heap_node_t *node);
ds_pairing_heap_node_t *FUNC(ds_pairing_heap_peek)(ds_pairing_heap_t *heap);
ds_pairing_heap_node_t *FUNC(ds_pairing_heap_extract)(ds_pairing_heap_t *heap);
void FUNC(ds_pairing_heap_decrease_key)(ds_pairing_heap_t *heap,
                                        ds_pairing_heap_node_t *node);
bool FUNC(ds_pairing_heap_is_empty)(ds_pairing_heap_t *heap);
size_t FUNC(ds_pairing_heap_size)(ds_pairing_heap_t *heap);
void FUNC(ds_pairing_heap_merge)(ds_pairing_heap_t *heap,
                                 ds_pairing_heap_t *other);
void FUNC(ds_pairing_heap_walk)(ds_pairing_heap_t *heap,
                                void (*callback)(ds_pairing_heap_node_t *));
void FUNC(ds_pairing_heap_destroy)(ds_pairing_heap_t *heap,
                                   void (*destroy)(ds_pairing_heap_node_t *));

#ifdef __cplusplus
}
#endif

#endif /* DS_PAIRING_HEAP_H */
