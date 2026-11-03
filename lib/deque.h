#ifndef DS_DEQUE_H
#define DS_DEQUE_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct deque_node {
  struct deque_node *next;
  struct deque_node *prev;
} ds_deque_node_t;

typedef struct deque {
  ds_deque_node_t *head;
  ds_deque_node_t *tail;
  ds_deque_node_t *nil;
  size_t size;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_deque_t;

ds_deque_t *ds_deque_create(void);
ds_deque_t *ds_deque_create_alloc(void *(*allocator)(size_t),
                                  void (*deallocator)(void *));
void ds_deque_push_front(ds_deque_t *deque, ds_deque_node_t *node);
void ds_deque_push_back(ds_deque_t *deque, ds_deque_node_t *node);
ds_deque_node_t *ds_deque_pop_front(ds_deque_t *deque);
ds_deque_node_t *ds_deque_pop_back(ds_deque_t *deque);
ds_deque_node_t *ds_deque_peek_front(ds_deque_t *deque);
ds_deque_node_t *ds_deque_peek_back(ds_deque_t *deque);
bool ds_deque_is_empty(ds_deque_t *deque);
void ds_deque_destroy(ds_deque_t *deque, void (*destroy)(ds_deque_node_t *));

void ds_deque_delete(ds_deque_t *deque);
void ds_deque_delete_node(ds_deque_t *deque, ds_deque_node_t *node);
void ds_deque_destroy_node(ds_deque_t *deque, ds_deque_node_t *node,
                           void (*destroy)(ds_deque_node_t *));
void ds_deque_pop_front_destroy(ds_deque_t *deque,
                                void (*destroy)(ds_deque_node_t *));
void ds_deque_pop_back_destroy(ds_deque_t *deque,
                               void (*destroy)(ds_deque_node_t *));
ds_deque_node_t *ds_deque_search(ds_deque_t *deque, ds_deque_node_t *node,
                                 int (*compare)(ds_deque_node_t *,
                                                ds_deque_node_t *));
void ds_deque_walk_forward(ds_deque_t *deque, ds_deque_node_t *node,
                           void (*callback)(ds_deque_node_t *));
void ds_deque_walk_backwards(ds_deque_t *deque, ds_deque_node_t *node,
                             void (*callback)(ds_deque_node_t *));
ds_deque_t *ds_deque_clone(ds_deque_t *deque,
                           ds_deque_node_t *(*clone_node)(ds_deque_node_t *));

#ifdef __cplusplus
}
#endif

#endif /* DS_DEQUE_H */
