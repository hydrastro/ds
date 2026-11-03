#ifndef DS_CIRCULAR_BUFFER_H
#define DS_CIRCULAR_BUFFER_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct circular_buffer_node {
  struct circular_buffer_node *placeholder;
} ds_circular_buffer_node_t;

typedef struct circular_buffer {
  ds_circular_buffer_node_t **data;
  size_t head;
  size_t tail;
  size_t size;
  size_t capacity;
  bool overwrite;
  ds_circular_buffer_node_t *nil;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_circular_buffer_t;

ds_circular_buffer_t *FUNC(ds_circular_buffer_create)(size_t capacity);
ds_circular_buffer_t *
    FUNC(ds_circular_buffer_create_alloc)(size_t capacity, bool overwrite,
                                          void *(*allocator)(size_t),
                                          void (*deallocator)(void *));
bool FUNC(ds_circular_buffer_is_empty)(ds_circular_buffer_t *buffer);
bool FUNC(ds_circular_buffer_is_full)(ds_circular_buffer_t *buffer);
bool FUNC(ds_circular_buffer_push)(ds_circular_buffer_t *buffer,
                                   ds_circular_buffer_node_t *node);
ds_circular_buffer_node_t *
    FUNC(ds_circular_buffer_pop)(ds_circular_buffer_t *buffer);
ds_circular_buffer_node_t *
    FUNC(ds_circular_buffer_peek)(ds_circular_buffer_t *buffer);
ds_circular_buffer_node_t *
    FUNC(ds_circular_buffer_peek_tail)(ds_circular_buffer_t *buffer);
ds_circular_buffer_node_t *
    FUNC(ds_circular_buffer_at)(ds_circular_buffer_t *buffer, size_t index);
void FUNC(ds_circular_buffer_walk_forward)(
    ds_circular_buffer_t *buffer,
    void (*callback)(ds_circular_buffer_node_t *));
void FUNC(ds_circular_buffer_clear)(ds_circular_buffer_t *buffer);
void FUNC(ds_circular_buffer_destroy)(
    ds_circular_buffer_t *buffer, void (*destroy)(ds_circular_buffer_node_t *));
ds_circular_buffer_t *FUNC(ds_circular_buffer_clone)(
    ds_circular_buffer_t *buffer,
    ds_circular_buffer_node_t *(*clone_node)(ds_circular_buffer_node_t *));

#ifdef __cplusplus
}
#endif

#endif /* DS_CIRCULAR_BUFFER_H */
