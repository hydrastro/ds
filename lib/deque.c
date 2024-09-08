#include "deque.h"
#include <stdlib.h>

deque_t *deque_create() {
  deque_t *deque = (deque_t *)malloc(sizeof(deque_t));
  deque->nil = (deque_node_t *)malloc(sizeof(deque_node_t));
  deque->nil->next = deque->nil;
  deque->nil->prev = deque->nil;
  deque->head = deque->nil;
  deque->tail = deque->nil;

#ifdef DEQUE_THREAD_SAFE
  deque->is_thread_safe = true;
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&deque->lock, &attr);
#endif

  return deque;
}

void deque_push_front(deque_t *deque, deque_node_t *node) {
#ifdef DEQUE_THREAD_SAFE
LOCK(deque)
#endif

  node->next = deque->head;
  node->prev = deque->nil;
  if (deque->head != deque->nil) {
    deque->head->prev = node;
  }
  deque->head = node;
  if (deque->tail == deque->nil) {
    deque->tail = node;
  }

#ifdef DEQUE_THREAD_SAFE
UNLOCK(deque)
#endif
}

void deque_push_back(deque_t *deque, deque_node_t *node) {
#ifdef DEQUE_THREAD_SAFE
LOCK(deque)
#endif

  node->next = deque->nil;
  node->prev = deque->tail;
  if (deque->tail != deque->nil) {
    deque->tail->next = node;
  }
  deque->tail = node;
  if (deque->head == deque->nil) {
    deque->head = node;
  }

#ifdef DEQUE_THREAD_SAFE
 UNLOCK(deque)
#endif
}

deque_node_t *deque_pop_front(deque_t *deque) {
#ifdef DEQUE_THREAD_SAFE
 LOCK(deque)
#endif

  if (deque_is_empty(deque)) {
#ifdef DEQUE_THREAD_SAFE
 UNLOCK(deque)
#endif
    return NULL;
  }
  deque_node_t *node = deque->head;
  deque->head = node->next;
  if (deque->head != deque->nil) {
    deque->head->prev = deque->nil;
  } else {
    deque->tail = deque->nil;
  }

#ifdef DEQUE_THREAD_SAFE
 UNLOCK(deque)
#endif

  return node;
}

deque_node_t *deque_pop_back(deque_t *deque) {
#ifdef DEQUE_THREAD_SAFE
LOCK(deque)
#endif

  if (deque_is_empty(deque)) {
#ifdef DEQUE_THREAD_SAFE
UNLOCK(deque)
#endif
    return NULL;
  }
  deque_node_t *node = deque->tail;
  deque->tail = node->prev;
  if (deque->tail != deque->nil) {
    deque->tail->next = deque->nil;
  } else {
    deque->head = deque->nil;
  }

#ifdef DEQUE_THREAD_SAFE
 UNLOCK(deque)
#endif

  return node;
}

deque_node_t *deque_peek_front(deque_t *deque) {
#ifdef DEQUE_THREAD_SAFE
 LOCK(deque)
#endif

  if (deque->head == deque->nil) {
#ifdef DEQUE_THREAD_SAFE
UNLOCK(deque)
#endif
    return NULL;
  }

#ifdef DEQUE_THREAD_SAFE
 UNLOCK(deque)
#endif

  return deque->head;
}

deque_node_t *deque_peek_back(deque_t *deque) {
#ifdef DEQUE_THREAD_SAFE
LOCK(deque)
#endif

  if (deque->tail == deque->nil) {
#ifdef DEQUE_THREAD_SAFE
 UNLOCK(deque)
#endif
    return NULL;
  }

#ifdef DEQUE_THREAD_SAFE
 UNLOCK(deque)
#endif

  return deque->tail;
}

int deque_is_empty(deque_t *deque) {
#ifdef DEQUE_THREAD_SAFE
 LOCK(deque)
#endif

  int empty = deque->head == deque->nil;

#ifdef DEQUE_THREAD_SAFE
 UNLOCK(deque)
#endif

  return empty;
}

void deque_destroy(deque_t *deque, void (*destroy_node)(deque_node_t *)) {
  deque_node_t *node = deque->head;
  while (node != deque->nil) {
    deque_node_t *next = node->next;
    destroy_node(node);
    node = next;
  }
  free(deque->nil);

#ifdef DEQUE_THREAD_SAFE
  pthread_mutex_destroy(&deque->lock);
#endif

  free(deque);
}
