#include "queue.h"
#include <stdlib.h>

ds_queue_t *ds_queue_create(void) {
  return ds_queue_create_alloc(malloc, free);
}

ds_queue_t *ds_queue_create_alloc(void *(*allocator)(size_t),
                                  void (*deallocator)(void *)) {
  ds_queue_t *queue;
  if (allocator == NULL || deallocator == NULL) {
    return NULL;
  }
  queue = (ds_queue_t *)allocator(sizeof(ds_queue_t));
  if (queue == NULL) {
    return NULL;
  }
  queue->allocator = allocator;
  queue->deallocator = deallocator;
  queue->nil = (ds_queue_node_t *)allocator(sizeof(ds_queue_node_t));
  if (queue->nil == NULL) {
    deallocator(queue);
    return NULL;
  }
  queue->nil->next = queue->nil;
  queue->head = queue->nil;
  queue->tail = queue->nil;
  queue->size = 0;

  return queue;
}

void ds_queue_enqueue(ds_queue_t *queue, ds_queue_node_t *node) {
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

ds_queue_node_t *ds_queue_dequeue(ds_queue_t *queue) {
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

ds_queue_node_t *ds_queue_peek(ds_queue_t *queue) {
  ds_queue_node_t *result;
  result = queue->head;

  return result;
}

ds_queue_node_t *ds_queue_peek_tail(ds_queue_t *queue) {
  ds_queue_node_t *result;
  result = queue->tail;
  return result;
}

bool ds_queue_is_empty(ds_queue_t *queue) {
  bool empty;

  empty = queue->head == queue->nil;

  return empty;
}

void ds_queue_destroy(ds_queue_t *queue, void (*destroy)(ds_queue_node_t *)) {
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

void ds_queue_delete(ds_queue_t *queue) {
  queue->head = queue->tail = queue->nil;
  queue->size = 0;
}

void ds_queue_delete_node(ds_queue_t *queue, ds_queue_node_t *node) {
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

void ds_queue_destroy_node(ds_queue_t *queue, ds_queue_node_t *node,
                           void (*destroy)(ds_queue_node_t *)) {
  ds_queue_delete_node(queue, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

void ds_queue_pop_destroy(ds_queue_t *queue,
                          void (*destroy)(ds_queue_node_t *)) {
  if (queue->head != queue->nil) {
    ds_queue_node_t *node = ds_queue_dequeue(queue);
    if (destroy != NULL) {
      destroy(node);
    }
  }
}

ds_queue_node_t *ds_queue_search(ds_queue_t *queue, ds_queue_node_t *node,
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

void ds_queue_walk_forward(ds_queue_t *queue, ds_queue_node_t *start_node,
                           void (*callback)(ds_queue_node_t *)) {
  ds_queue_node_t *current;

  current = start_node != NULL ? start_node : queue->head;

  while (current != queue->nil) {
    callback(current);
    current = current->next;
  }
}

/* reverses the links from current to the end, then walks them back,
 * restoring each link before its node is visited: O(1) space, where
 * recursion would go as deep as the queue is long */
void ds_queue_walk_backwards(ds_queue_t *queue, ds_queue_node_t *current,
                             void (*callback)(ds_queue_node_t *)) {
  ds_queue_node_t *rev = queue->nil;
  ds_queue_node_t *next;
  while (current != queue->nil) {
    next = current->next;
    current->next = rev;
    rev = current;
    current = next;
  }
  current = rev;
  rev = queue->nil;
  while (current != queue->nil) {
    next = current->next;
    current->next = rev;
    rev = current;
    callback(current);
    current = next;
  }
}

/* clones of the source's nodes, in order, added to copy; false when
 * clone_node returns NULL (the clones made so far are in copy) */
static bool
queue_clone_nodes(const ds_queue_t *queue, ds_queue_t *copy,
                  ds_queue_node_t *(*clone_node)(ds_queue_node_t *)) {
  ds_queue_node_t *current;
  for (current = queue->head; current != queue->nil; current = current->next) {
    ds_queue_node_t *node = clone_node(current);
    if (node == NULL) {
      return false;
    }
    ds_queue_enqueue(copy, node);
  }
  return true;
}

ds_queue_t *ds_queue_clone(ds_queue_t *queue,
                           ds_queue_node_t *(*clone_node)(ds_queue_node_t *)) {
  ds_queue_t *copy;
  if (queue == NULL || clone_node == NULL) {
    return NULL;
  }
  copy = ds_queue_create_alloc(queue->allocator, queue->deallocator);
  if (copy != NULL && !queue_clone_nodes(queue, copy, clone_node)) {
    /* the clones made so far are not released */
    ds_queue_destroy(copy, NULL);
    copy = NULL;
  }
  return copy;
}

ds_status_t ds_queue_clone_ex(const ds_queue_t *queue,
                              ds_queue_node_t *(*clone_node)(ds_queue_node_t *),
                              void (*destroy_clone)(ds_queue_node_t *),
                              ds_queue_t **out) {
  ds_queue_t *copy;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (queue == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  copy = ds_queue_create_alloc(queue->allocator, queue->deallocator);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!queue_clone_nodes(queue, copy, clone_node)) {
    ds_queue_destroy(copy, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  *out = copy;
  return DS_OK;
}
