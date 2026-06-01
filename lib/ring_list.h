#ifndef DS_RING_LIST_H
#define DS_RING_LIST_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ring_list_node {
  struct ring_list_node *next;
} ds_ring_list_node_t;

typedef struct ring_list {
  ds_ring_list_node_t *tail;
  ds_ring_list_node_t *nil;
  size_t size;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} ds_ring_list_t;

ds_ring_list_t *FUNC(ring_list_create)(void);
ds_ring_list_t *FUNC(ring_list_create_alloc)(void *(*allocator)(size_t),
                                             void (*deallocator)(void *));
void FUNC(ring_list_push_front)(ds_ring_list_t *list,
                                ds_ring_list_node_t *node);
void FUNC(ring_list_push_back)(ds_ring_list_t *list, ds_ring_list_node_t *node);
ds_ring_list_node_t *FUNC(ring_list_pop_front)(ds_ring_list_t *list);
ds_ring_list_node_t *FUNC(ring_list_front)(ds_ring_list_t *list);
ds_ring_list_node_t *FUNC(ring_list_back)(ds_ring_list_t *list);
void FUNC(ring_list_rotate)(ds_ring_list_t *list);
bool FUNC(ring_list_is_empty)(ds_ring_list_t *list);
ds_ring_list_node_t *FUNC(ring_list_search)(
    ds_ring_list_t *list, ds_ring_list_node_t *node,
    int (*compare)(ds_ring_list_node_t *, ds_ring_list_node_t *));
void FUNC(ring_list_delete_node)(ds_ring_list_t *list,
                                 ds_ring_list_node_t *node);
void FUNC(ring_list_destroy_node)(ds_ring_list_t *list,
                                  ds_ring_list_node_t *node,
                                  void (*destroy)(ds_ring_list_node_t *));
void FUNC(ring_list_walk_forward)(ds_ring_list_t *list,
                                  ds_ring_list_node_t *node,
                                  void (*callback)(ds_ring_list_node_t *));
void FUNC(ring_list_destroy)(ds_ring_list_t *list,
                             void (*destroy)(ds_ring_list_node_t *));
ds_ring_list_t *FUNC(ring_list_clone)(
    ds_ring_list_t *list,
    ds_ring_list_node_t *(*clone_node)(ds_ring_list_node_t *));

#ifdef __cplusplus
}
#endif

#endif /* DS_RING_LIST_H */
