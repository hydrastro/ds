#ifndef DS_LIST_H
#define DS_LIST_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct list_node {
  struct list_node *next;
} list_node_t;

typedef struct linked_list {
  list_node_t *head;
  list_node_t *tail;
  list_node_t *nil;
  size_t size;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} list_t;

list_t *FUNC(list_create)(void);
list_t *FUNC(list_create_alloc)(void *(*allocator)(size_t),
                                void (*deallocator)(void *));
void FUNC(list_append)(list_t *list, list_node_t *node);
void FUNC(list_prepend)(list_t *list, list_node_t *node);
list_node_t *FUNC(list_search)(list_t *list, list_node_t *node,
                               int (*compare)(list_node_t *, list_node_t *));
void FUNC(list_insert_before)(list_t *list, list_node_t *node,
                              list_node_t *next);
void FUNC(list_insert_after)(list_t *list, list_node_t *node,
                             list_node_t *prev);
void FUNC(list_delete_node)(list_t *list, list_node_t *node);
void FUNC(list_delete)(list_t *list);
void FUNC(list_destroy_node)(list_t *list, list_node_t *node,
                             void (*destroy)(list_node_t *));
void FUNC(list_destroy)(list_t *list, void (*destroy)(list_node_t *));
void FUNC(list_walk_forward)(list_t *list, list_node_t *node,
                             void (*callback)(list_node_t *));
void FUNC(list_walk_backwards)(list_t *list, list_node_t *node,
                               void (*callback)(list_node_t *));
bool FUNC(list_is_empty)(list_t *list);
list_t *FUNC(list_clone)(list_t *list,
                         list_node_t *(*clone_node)(list_node_t *));

#endif /* DS_LIST_H */
