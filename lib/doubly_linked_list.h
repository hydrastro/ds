#ifndef DS_DOUBLY_LINKED_LIST_H
#define DS_DOUBLY_LINKED_LIST_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct doubly_linked_list_node {
  struct doubly_linked_list_node *next;
  struct doubly_linked_list_node *prev;
} doubly_linked_list_node_t;

typedef struct doubly_linked_list {
  size_t size;
  doubly_linked_list_node_t *head;
  doubly_linked_list_node_t *tail;
  doubly_linked_list_node_t *nil;
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} doubly_linked_list_t;

doubly_linked_list_t *doubly_linked_list_create(void);

void doubly_linked_list_append(doubly_linked_list_t *list,
                               doubly_linked_list_node_t *node);

void doubly_linked_list_prepend(doubly_linked_list_t *list,
                                doubly_linked_list_node_t *node);

doubly_linked_list_node_t *doubly_linked_list_search(
    doubly_linked_list_t *list, doubly_linked_list_node_t *node,
    int (*compare)(doubly_linked_list_node_t *, doubly_linked_list_node_t *));

void doubly_linked_list_insert_before(doubly_linked_list_t *list,
                                      doubly_linked_list_node_t *node,
                                      doubly_linked_list_node_t *next);

void doubly_linked_list_insert_after(doubly_linked_list_t *list,
                                     doubly_linked_list_node_t *node,
                                     doubly_linked_list_node_t *prev);

void doubly_linked_list_delete_node(doubly_linked_list_t *list,
                                    doubly_linked_list_node_t *node);

void doubly_linked_list_delete(doubly_linked_list_t *list);

void doubly_linked_list_destroy_node(
    doubly_linked_list_t *list, doubly_linked_list_node_t *node,
    void (*destroy)(doubly_linked_list_node_t *));

void doubly_linked_list_destroy(doubly_linked_list_t *list,
                                void (*destroy)(doubly_linked_list_node_t *));

void doubly_linked_list_walk_forward(
    doubly_linked_list_t *list, doubly_linked_list_node_t *node,
    void (*callback)(doubly_linked_list_node_t *));

void doubly_linked_list_walk_backwards(
    doubly_linked_list_t *list, doubly_linked_list_node_t *node,
    void (*callback)(doubly_linked_list_node_t *));

bool doubly_linked_list_is_empty(doubly_linked_list_t *);
#endif // DS_DOUBLY_LINKED_LIST_H
