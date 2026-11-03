#include "queue.h"
#include <stdlib.h>

ds_queue_t *FUNC(ds_queue_create)(void) {
  return FUNC(ds_queue_create_alloc)(malloc, free);
}

ds_queue_t *FUNC(ds_queue_create_alloc)(void *(*allocator)(size_t),
                                        void (*deallocator)(void *)) {
  ds_queue_t *queue = (ds_queue_t *)allocator(sizeof(ds_queue_t));
  queue->allocator = allocator;
  queue->deallocator = deallocator;
  queue->nil = (ds_queue_node_t *)allocator(sizeof(ds_queue_node_t));
  queue->nil->next = queue->nil;
  queue->head = queue->nil;
  queue->tail = queue->nil;
  queue->size = 0;

  return queue;
}

void FUNC(ds_queue_enqueue)(ds_queue_t *queue, ds_queue_node_t *node) {
  node->next = queue->nil;
  if (queue->head == queue->nil) {
    queue->head = node;
    queue->tail = node;
  } else {
    queue->tail->next = node;
    queue->tail = node;
  }
  queue->size += 1;
}

ds_queue_node_t *FUNC(ds_queue_dequeue)(ds_queue_t *queue) {
  ds_queue_node_t *node;

  if (queue->head == queue->nil) {
    return queue->nil;
  }
  node = queue->head;
  queue->head = node->next;
  if (queue->head == queue->nil) {
    queue->tail = queue->nil;
  }
  queue->size -= 1;

  return node;
}

ds_queue_node_t *FUNC(ds_queue_peek)(ds_queue_t *queue) {
  ds_queue_node_t *result;
  result = queue->head;

  return result;
}

ds_queue_node_t *FUNC(ds_queue_peek_tail)(ds_queue_t *queue) {
  ds_queue_node_t *result;
  result = queue->tail;
  return result;
}

bool FUNC(ds_queue_is_empty)(ds_queue_t *queue) {
  bool empty;

  empty = queue->head == queue->nil;

  return empty;
}

void FUNC(ds_queue_destroy)(ds_queue_t *queue,
                            void (*destroy)(ds_queue_node_t *)) {
  ds_queue_node_t *node = queue->head;
  while (node != queue->nil) {
    ds_queue_node_t *next = node->next;
    if (destroy != NULL) {
      destroy(node);
    }
    node = next;
  }
  queue->deallocator(queue->nil);

  queue->deallocator(queue);
}

void FUNC(ds_queue_delete)(ds_queue_t *queue) {
  queue->head = queue->tail = queue->nil;
  queue->size = 0;
}

void FUNC(ds_queue_delete_node)(ds_queue_t *queue, ds_queue_node_t *node) {
  bool removed;

  removed = false;
  if (queue->head == node) {
    queue->head = node->next;

    if (queue->head == queue->nil) {
      queue->tail = queue->nil;
    }
    removed = true;
  } else {
    ds_queue_node_t *prev = queue->head;
    while (prev->next != node && prev->next != queue->nil) {
      prev = prev->next;
    }

    if (prev->next == node) {
      prev->next = node->next;

      if (node == queue->tail) {
        queue->tail = prev;
      }
      removed = true;
    }
  }

  if (removed) {
    queue->size--;
  }
}

void FUNC(ds_queue_destroy_node)(ds_queue_t *queue, ds_queue_node_t *node,
                                 void (*destroy)(ds_queue_node_t *)) {
  FUNC(ds_queue_delete_node)(queue, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

void FUNC(ds_queue_pop_destroy)(ds_queue_t *queue,
                                void (*destroy)(ds_queue_node_t *)) {
  if (queue->head != queue->nil) {
    ds_queue_node_t *node = FUNC(ds_queue_dequeue)(queue);
    if (destroy != NULL) {
      destroy(node);
    }
  }
}

ds_queue_node_t *FUNC(ds_queue_search)(ds_queue_t *queue, ds_queue_node_t *node,
                                       int (*compare)(ds_queue_node_t *,
                                                      ds_queue_node_t *)) {
  ds_queue_node_t *current;

  current = queue->head;

  while (current != queue->nil) {
    if (compare(current, node) == 0) {
      return current;
    }
    current = current->next;
  }

  return queue->nil;
}

void FUNC(ds_queue_walk_forward)(ds_queue_t *queue, ds_queue_node_t *start_node,
                                 void (*callback)(ds_queue_node_t *)) {
  ds_queue_node_t *current;

  current = start_node != NULL ? start_node : queue->head;

  while (current != queue->nil) {
    callback(current);
    current = current->next;
  }
}

void FUNC(ds_queue_walk_backwards)(ds_queue_t *queue, ds_queue_node_t *current,
                                   void (*callback)(ds_queue_node_t *)) {
  if (current == queue->nil) {
    return;
  }
  FUNC(ds_queue_walk_backwards)(queue, current->next, callback);
  callback(current);
}

ds_queue_t *
FUNC(ds_queue_clone)(ds_queue_t *queue,
                     ds_queue_node_t *(*clone_node)(ds_queue_node_t *)) {
  ds_queue_t *new_queue;
  ds_queue_node_t *current;
  new_queue = FUNC(ds_queue_create_alloc)(queue->allocator, queue->deallocator);
  current = queue->head;
  while (current != queue->nil) {
    ds_queue_node_t *cloned_node = clone_node(current);
    FUNC(ds_queue_enqueue)(new_queue, cloned_node);
    current = current->next;
  }

  return new_queue;
}
