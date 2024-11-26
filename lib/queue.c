#include "queue.h"
#include <stdlib.h>

queue_t *queue_create() {
  queue_t *queue = (queue_t *)malloc(sizeof(queue_t));
  queue->nil = (queue_node_t *)malloc(sizeof(queue_node_t));
  queue->nil->next = queue->nil;
  queue->head = queue->nil;
  queue->tail = queue->nil;
  queue->size = 0;

#ifdef QUEUE_THREAD_SAFE
  LOCK_INIT(queue)
#endif

  return queue;
}

void queue_enqueue(queue_t *queue, queue_node_t *node) {
#ifdef QUEUE_THREAD_SAFE
  LOCK(queue)
#endif

  node->next = queue->nil;
  if (queue->head == queue->nil) {
    queue->head = node;
    queue->tail = node;
  } else {
    queue->tail->next = node;
    queue->tail = node;
  }
  queue->size += 1;

#ifdef QUEUE_THREAD_SAFE
  UNLOCK(queue)
#endif
}

queue_node_t *queue_dequeue(queue_t *queue) {
#ifdef QUEUE_THREAD_SAFE
  LOCK(queue)
#endif

  if (queue->head == queue->nil) {
#ifdef QUEUE_THREAD_SAFE
    UNLOCK(queue)
#endif
    return queue->nil;
  }
  queue_node_t *node = queue->head;
  queue->head = node->next;
  if (queue->head == queue->nil) {
    queue->tail = queue->nil;
  }
  queue->size -= 1;

#ifdef QUEUE_THREAD_SAFE
  UNLOCK(queue)

#endif

  return node;
}

queue_node_t *queue_peek(queue_t *queue) {
#ifdef QUEUE_THREAD_SAFE
  LOCK(queue)
#endif
  queue_node_t *result = queue->head;

#ifdef QUEUE_THREAD_SAFE
  UNLOCK(queue)
#endif
  return result;
}

queue_node_t *queue_peek_tail(queue_t *queue) {
#ifdef QUEUE_THREAD_SAFE
  LOCK(queue)
#endif
  queue_node_t *result = queue->tail;
#ifdef QUEUE_THREAD_SAFE
  UNLOCK(queue)
#endif
  return result;
}

bool queue_is_empty(queue_t *queue) {
#ifdef QUEUE_THREAD_SAFE
  LOCK(queue)
#endif

  bool empty = queue->head == queue->nil;

#ifdef QUEUE_THREAD_SAFE
  UNLOCK(queue)
#endif

  return empty;
}

void queue_destroy(queue_t *queue, void (*destroy)(queue_node_t *)) {
  queue_node_t *node = queue->head;
  while (node != queue->nil) {
    queue_node_t *next = node->next;
    if (destroy != NULL) {
      destroy(node);
    }
    node = next;
  }
  free(queue->nil);

#ifdef QUEUE_THREAD_SAFE
  LOCK_DESTROY(queue)
#endif

  free(queue);
}

void queue_delete(queue_t *queue) {
#ifdef QUEUE_THREAD_SAFE
  LOCK(queue)
#endif
  queue->head = queue->tail = queue->nil;
  queue->size = 0;
#ifdef QUEUE_THREAD_SAFE
  UNLOCK(queue)
#endif
}

void queue_delete_node(queue_t *queue, queue_node_t *node) {
#ifdef QUEUE_THREAD_SAFE
  LOCK(queue)
#endif

  if (queue->head == node) {
    queue->head = node->next;

    if (queue->head == queue->nil) {
      queue->tail = queue->nil;
    }
  } else {
    queue_node_t *prev = queue->head;
    while (prev->next != node && prev->next != queue->nil) {
      prev = prev->next;
    }

    if (prev->next == node) {
      prev->next = node->next;

      if (node == queue->tail) {
        queue->tail = prev;
      }
    }
  }

  queue->size--;

#ifdef QUEUE_THREAD_SAFE
  UNLOCK(queue)
#endif
}

void queue_destroy_node(queue_t *queue, queue_node_t *node,
                        void (*destroy)(queue_node_t *)) {
#ifdef QUEUE_THREAD_SAFE
  LOCK(queue)
#endif

  queue_delete_node(queue, node);
  if (destroy != NULL) {
    destroy(node);
  }

#ifdef QUEUE_THREAD_SAFE
  UNLOCK(queue)
#endif
}

void queue_pop_destroy(queue_t *queue, void (*destroy)(queue_node_t *)) {
#ifdef QUEUE_THREAD_SAFE
  LOCK(queue)
#endif

  if (queue->head != queue->nil) {
    queue_node_t *node = queue_dequeue(queue);
    if (destroy != NULL) {
      destroy(node);
    }
  }

#ifdef QUEUE_THREAD_SAFE
  UNLOCK(queue)
#endif
}

queue_node_t *queue_search(queue_t *queue, queue_node_t *node,
                           int (*compare)(queue_node_t *, queue_node_t *)) {
#ifdef QUEUE_THREAD_SAFE
  LOCK(queue)
#endif

  queue_node_t *current = queue->head;

  while (current != queue->nil) {
    if (compare(current, node) == 0) {
#ifdef QUEUE_THREAD_SAFE
      UNLOCK(queue)
#endif
      return current;
    }
    current = current->next;
  }

#ifdef QUEUE_THREAD_SAFE
  UNLOCK(queue)
#endif

  return queue->nil;
}

void queue_walk_forward(queue_t *queue, queue_node_t *start_node,
                        void (*callback)(queue_node_t *)) {
#ifdef QUEUE_THREAD_SAFE
  LOCK(queue)
#endif

  queue_node_t *current = start_node != NULL ? start_node : queue->head;

  while (current != queue->nil) {
    callback(current);
    current = current->next;
  }

#ifdef QUEUE_THREAD_SAFE
  UNLOCK(queue)
#endif
}

void queue_walk_backwards(queue_t *queue, queue_node_t *current,
                          void (*callback)(queue_node_t *)) {
#ifdef QUEUE_THREAD_SAFE
  LOCK(queue)
#endif
  if (current == queue->nil) {
    return;
  }
  queue_walk_backwards(queue, current->next, callback);
  callback(current);
#ifdef QUEUE_THREAD_SAFE
  UNLOCK(queue)
#endif
}

queue_t *queue_clone(queue_t *queue,
                     queue_node_t *(*clone_node)(queue_node_t *)) {
#ifdef QUEUE_THREAD_SAFE
  LOCK(queue)
#endif
  queue_t *new_queue = queue_create();
  queue_node_t *current = queue->head;
  while (current != queue->nil) {
    queue_node_t *cloned_node = clone_node(current);
    queue_enqueue(new_queue, cloned_node);
    current = current->next;
  }
#ifdef QUEUE_THREAD_SAFE
  UNLOCK(queue)
#endif

  return new_queue;
}
