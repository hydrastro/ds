#ifndef DS_QUEUE_H
#define DS_QUEUE_H

#include "common.h"

#ifdef QUEUE_THREAD_SAFE
#include <pthread.h>
#include <stdbool.h>
#endif

typedef struct queue_node {
  struct queue_node *next;
} queue_node_t;

typedef struct queue {
  queue_node_t *head;
  queue_node_t *tail;
  queue_node_t *nil;
#ifdef QUEUE_THREAD_SAFE
  pthread_mutex_t lock;
  bool is_thread_safe;
#endif
} queue_t;

queue_t *queue_create();
void queue_enqueue(queue_t *queue, queue_node_t *node);
queue_node_t *queue_dequeue(queue_t *queue);
queue_node_t *queue_peek(queue_t *queue);
queue_node_t *queue_peek_tail(queue_t *queue);
int queue_is_empty(queue_t *queue);
void queue_destroy(queue_t *queue, void (*destroy_node)(queue_node_t *));

#endif // DS_QUEUE_H
