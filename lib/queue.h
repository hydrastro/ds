#ifndef DS_QUEUE_H
#define DS_QUEUE_H

#ifndef CAST
#define CAST(node, type) ((type *)(node))
#endif

typedef struct queue_node {
  struct queue_node *next;
} queue_node_t;

typedef struct queue {
  queue_node_t *head;
  queue_node_t *tail;
  queue_node_t *nil;
} queue_t;

queue_t *queue_create();

void queue_enqueue(queue_t *queue, queue_node_t *node);

queue_node_t *queue_dequeue(queue_t *queue);

queue_node_t *queue_peek(queue_t *queue);

queue_node_t *queue_peek_tail(queue_t *queue);

int queue_is_empty(queue_t *queue);

void queue_destroy(queue_t *queue, void (*destroy_node)(queue_node_t *));

#endif // DS_QUEUE_H
