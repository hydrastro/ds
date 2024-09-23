#ifndef DS_DEQUE_H
#define DS_DEQUE_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct deque_node {
  struct deque_node *next;
  struct deque_node *prev;
} deque_node_t;

typedef struct deque {
  deque_node_t *head;
  deque_node_t *tail;
  deque_node_t *nil;
  size_t size;
#ifdef DEQUE_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} deque_t;

deque_t *deque_create(void);
void deque_push_front(deque_t *deque, deque_node_t *node);
void deque_push_back(deque_t *deque, deque_node_t *node);
deque_node_t *deque_pop_front(deque_t *deque);
deque_node_t *deque_pop_back(deque_t *deque);
deque_node_t *deque_peek_front(deque_t *deque);
deque_node_t *deque_peek_back(deque_t *deque);
bool deque_is_empty(deque_t *deque);
void deque_destroy(deque_t *deque, void (*destroy)(deque_node_t *));

void deque_delete(deque_t *deque);
void deque_delete_node(deque_t *deque, deque_node_t *node);
void deque_destroy_node(deque_t *deque, deque_node_t *node,
                        void (*destroy)(deque_node_t *));
void deque_pop_front_destroy(deque_t *deque, void (*destroy)(deque_node_t *));
void deque_pop_back_destroy(deque_t *deque, void (*destroy)(deque_node_t *));
deque_node_t *deque_search(deque_t *deque, deque_node_t *node,
                           int (*compare)(deque_node_t *, deque_node_t *));
void deque_walk_forward(deque_t *deque, deque_node_t *node,
                        void (*callback)(deque_node_t *));
void deque_walk_backwards(deque_t *deque, deque_node_t *node,
                          void (*callback)(deque_node_t *));

#endif // DS_DEQUE_H
