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
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} deque_t;

deque_t *FUNC(deque_create)(void);
void FUNC(deque_push_front)(deque_t *deque, deque_node_t *node);
void FUNC(deque_push_back)(deque_t *deque, deque_node_t *node);
deque_node_t *FUNC(deque_pop_front)(deque_t *deque);
deque_node_t *FUNC(deque_pop_back)(deque_t *deque);
deque_node_t *FUNC(deque_peek_front)(deque_t *deque);
deque_node_t *FUNC(deque_peek_back)(deque_t *deque);
bool FUNC(deque_is_empty)(deque_t *deque);
void FUNC(deque_destroy)(deque_t *deque, void (*destroy)(deque_node_t *));

void FUNC(deque_delete)(deque_t *deque);
void FUNC(deque_delete_node)(deque_t *deque, deque_node_t *node);
void FUNC(deque_destroy_node)(deque_t *deque, deque_node_t *node,
                              void (*destroy)(deque_node_t *));
void FUNC(deque_pop_front_destroy)(deque_t *deque,
                                   void (*destroy)(deque_node_t *));
void FUNC(deque_pop_back_destroy)(deque_t *deque,
                                  void (*destroy)(deque_node_t *));
deque_node_t *FUNC(deque_search)(deque_t *deque, deque_node_t *node,
                                 int (*compare)(deque_node_t *,
                                                deque_node_t *));
void FUNC(deque_walk_forward)(deque_t *deque, deque_node_t *node,
                              void (*callback)(deque_node_t *));
void FUNC(deque_walk_backwards)(deque_t *deque, deque_node_t *node,
                                void (*callback)(deque_node_t *));
deque_t *FUNC(deque_clone)(deque_t *deque,
                           deque_node_t *(*clone_node)(deque_node_t *));

#endif /* DS_DEQUE_H */
