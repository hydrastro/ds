#ifndef DS_LINKED_LIST_H
#define DS_LINKED_LIST_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef LINKED_LIST_THREAD_SAFE
#include <pthread.h>
#endif

typedef struct linked_list_node {
  struct linked_list_node *next;
} linked_list_node_t;

typedef struct linked_list {
  linked_list_node_t *head;
  linked_list_node_t *tail;
  linked_list_node_t *nil;
  size_t size;
#ifdef LINKED_LIST_THREAD_SAFE
  pthread_mutex_t lock;
  bool is_thread_safe;
#endif
} linked_list_t;

linked_list_t *linked_list_create(void);

void linked_list_append(linked_list_t *list, linked_list_node_t *node);

void linked_list_prepend(linked_list_t *list, linked_list_node_t *node);

linked_list_node_t *
linked_list_search(linked_list_t *list, linked_list_node_t *node,
                   int (*compare)(linked_list_node_t *, linked_list_node_t *));

void linked_list_insert_before(linked_list_t *list, linked_list_node_t *node,
                               linked_list_node_t *next);

void linked_list_insert_after(linked_list_t *list, linked_list_node_t *node,
                              linked_list_node_t *prev);

void linked_list_delete_node(linked_list_t *list, linked_list_node_t *node);
void linked_list_delete(linked_list_t *list);
void linked_list_destroy_node(linked_list_t *list, linked_list_node_t *node,
                              void (*destroy)(linked_list_node_t *));

void linked_list_destroy(linked_list_t *list,
                         void (*destroy)(linked_list_node_t *));

void linked_list_walk_forward(linked_list_t *list, linked_list_node_t *node,
                              void (*callback)(linked_list_node_t *));

void linked_list_walk_backwards(linked_list_t *list, linked_list_node_t *node,
                                void (*callback)(linked_list_node_t *));
bool linked_list_is_empty(linked_list_t *list);

#endif // DS_LINKED_LIST_H
