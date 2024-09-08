#ifndef DS_DEQUE_H
#define DS_DEQUE_H

#include "common.h"

#ifdef DEQUE_THREAD_SAFE
#include <pthread.h>
#include <stdbool.h>
#endif

typedef struct deque_node {
  struct deque_node *next;
  struct deque_node *prev;
} deque_node_t;

typedef struct deque {
  deque_node_t *head;
  deque_node_t *tail;
  deque_node_t *nil;
#ifdef DEQUE_THREAD_SAFE
  pthread_mutex_t lock;
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
int deque_is_empty(deque_t *deque);
void deque_destroy(deque_t *deque, void (*destroy_node)(deque_node_t *));

#endif // DS_DEQUE_H
