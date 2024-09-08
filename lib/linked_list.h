#ifndef DS_LINKED_LIST_H
#define DS_LINKED_LIST_H

#include "common.h"

#ifdef LINKED_LIST_THREAD_SAFE
#include <pthread.h>
#include <stdbool.h>
#endif

typedef struct linked_list_node {
  struct linked_list_node *next;
} linked_list_node_t;

typedef struct linked_list {
  linked_list_node_t *head;
  linked_list_node_t *tail;
  linked_list_node_t *nil;
#ifdef LINKED_LIST_THREAD_SAFE
  pthread_mutex_t lock;
  bool is_thread_safe;
#endif
} linked_list_t;

linked_list_t *linked_list_create();

void linked_list_append(linked_list_t *list, linked_list_node_t *node);

void linked_list_prepend(linked_list_t *list, linked_list_node_t *node);

linked_list_node_t *linked_list_search(linked_list_t *list, void *data,
                                       int (*compare)(linked_list_node_t *,
                                                      void *));

void linked_list_insert_before(linked_list_t *list, linked_list_node_t *node,
                               linked_list_node_t *next);

void linked_list_insert_after(linked_list_t *list, linked_list_node_t *node,
                              linked_list_node_t *prev);

void linked_list_delete_node(linked_list_t *list, linked_list_node_t *node);

void linked_list_destroy_node(linked_list_t *list, linked_list_node_t *node,
                              void (*destroy_node)(void *));

void linked_list_destroy(linked_list_t *list, void (*destroy_node)(void *));

void linked_list_walk_forward(linked_list_t *list, linked_list_node_t *node,
                              void (*callback)(void *));

void linked_list_walk_backwards(linked_list_t *list, linked_list_node_t *node,
                                void (*callback)(void *));

#endif // DS_LINKED_LIST_H
