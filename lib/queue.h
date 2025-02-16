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
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} queue_t;

queue_t *FUNC(queue_create)(void);
queue_t *FUNC(queue_create_alloc)(void *(*allocator)(size_t),
                                  void (*deallocator)(void *));
void FUNC(queue_enqueue)(queue_t *queue, queue_node_t *node);
queue_node_t *FUNC(queue_dequeue)(queue_t *queue);
queue_node_t *FUNC(queue_peek)(queue_t *queue);
queue_node_t *FUNC(queue_peek_tail)(queue_t *queue);
bool FUNC(queue_is_empty)(queue_t *queue);
void FUNC(queue_destroy)(queue_t *queue, void (*destroy)(queue_node_t *));

void FUNC(queue_delete)(queue_t *queue);
void FUNC(queue_delete_node)(queue_t *queue, queue_node_t *node);
void FUNC(queue_destroy_node)(queue_t *queue, queue_node_t *node,
                              void (*destroy)(queue_node_t *));
void FUNC(queue_pop_destroy)(queue_t *queue,
                             void (*destroy_node)(queue_node_t *));
queue_node_t *FUNC(queue_search)(queue_t *queue, queue_node_t *node,
                                 int (*compare)(queue_node_t *,
                                                queue_node_t *));
void FUNC(queue_walk_forward)(queue_t *queue, queue_node_t *node,
                              void (*callback)(queue_node_t *));
void FUNC(queue_walk_backwards)(queue_t *queue, queue_node_t *node,
                                void (*callback)(queue_node_t *));
queue_t *FUNC(queue_clone)(queue_t *queue,
                           queue_node_t *(*clone_node)(queue_node_t *));

#endif /* DS_QUEUE_H */
