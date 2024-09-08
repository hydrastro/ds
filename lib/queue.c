#include "queue.h"
#include <stdlib.h>

queue_t *queue_create() {
  queue_t *queue = (queue_t *)malloc(sizeof(queue_t));
  queue->nil = (queue_node_t *)malloc(sizeof(queue_node_t));
  queue->nil->next = NULL;
  queue->head = queue->nil;
  queue->tail = queue->nil;
  return queue;
}

void queue_enqueue(queue_t *queue, queue_node_t *node) {
  node->next = queue->nil;
  if (queue->head == queue->nil) {
    queue->head = node;
    queue->tail = node;
  } else {
    queue->tail->next = node;
    queue->tail = node;
  }
}

queue_node_t *queue_dequeue(queue_t *queue) {
  if (queue->head == queue->nil) {
    return NULL;
  }
  queue_node_t *node = queue->head;
  queue->head = node->next;
  if (queue->head == queue->nil) {
    queue->tail = queue->nil;
  }
  return node;
}

queue_node_t *queue_peek(queue_t *queue) {
  if (queue->head == queue->nil) {
    return NULL;
  }
  return queue->head;
}

queue_node_t *queue_peek_tail(queue_t *queue) {
  if (queue->head == queue->nil) {
    return NULL;
  }
  return queue->tail;
}

int queue_is_empty(queue_t *queue) { return queue->head == queue->nil; }

void queue_destroy(queue_t *queue, void (*destroy_node)(queue_node_t *)) {
  queue_node_t *node = queue->head;
  while (node != queue->nil) {
    queue_node_t *next = node->next;
    destroy_node(node);
    node = next;
  }
  free(queue->nil);
  free(queue);
}
