#ifndef DS_QUEUE_H
#define DS_QUEUE_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct queue_node {
  struct queue_node *next;
} queue_node_t;

typedef struct queue {
  queue_node_t *head;
  queue_node_t *tail;
  queue_node_t *nil;
  size_t size;
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} queue_t;

queue_t *queue_create(void);
void queue_enqueue(queue_t *queue, queue_node_t *node);
queue_node_t *queue_dequeue(queue_t *queue);
queue_node_t *queue_peek(queue_t *queue);
queue_node_t *queue_peek_tail(queue_t *queue);
bool queue_is_empty(queue_t *queue);
void queue_destroy(queue_t *queue, void (*destroy)(queue_node_t *));

void queue_delete(queue_t *queue);
void queue_delete_node(queue_t *queue, queue_node_t *node);
void queue_destroy_node(queue_t *queue, queue_node_t *node,
                        void (*destroy)(queue_node_t *));
void queue_pop_destroy(queue_t *queue, void (*destroy_node)(queue_node_t *));
queue_node_t *queue_search(queue_t *queue, queue_node_t *node,
                           int (*compare)(queue_node_t *, queue_node_t *));
void queue_walk_forward(queue_t *queue, queue_node_t *node,
                        void (*callback)(queue_node_t *));
void queue_walk_backwards(queue_t *queue, queue_node_t *node,
                          void (*callback)(queue_node_t *));
queue_t *queue_clone(queue_t *queue,
                     queue_node_t *(*clone_node)(queue_node_t *));

#endif /* DS_QUEUE_H */
