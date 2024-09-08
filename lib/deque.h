#ifndef DS_DEQUE_H
#define DS_DEQUE_H

#ifndef CAST
#define CAST(node, type) ((type *)(node))
#endif

#ifdef THREAD_SAFE
#include <pthread.h>
#endif

typedef struct deque_node {
  struct deque_node *next;
  struct deque_node *prev;
} deque_node_t;

typedef struct deque {
  deque_node_t *head;
  deque_node_t *tail;
  deque_node_t *nil;

#ifdef THREAD_SAFE
  pthread_mutex_t lock;
#endif
} deque_t;

deque_t *deque_create();
void deque_push_front(deque_t *deque, deque_node_t *node);
void deque_push_back(deque_t *deque, deque_node_t *node);
deque_node_t *deque_pop_front(deque_t *deque);
deque_node_t *deque_pop_back(deque_t *deque);
deque_node_t *deque_peek_front(deque_t *deque);
deque_node_t *deque_peek_back(deque_t *deque);
int deque_is_empty(deque_t *deque);
void deque_destroy(deque_t *deque, void (*destroy_node)(deque_node_t *));

#endif // DS_DEQUE_H
