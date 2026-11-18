#include "queue.h"
#include "legacy_alloc.h"
#include <stdlib.h>

ds_queue_t *ds_queue_create(void) {
  return ds_queue_create_alloc(malloc, free);
}

static ds_queue_t *queue_create_internal(void *(*allocator)(size_t),
                                         void (*deallocator)(void *),
                                         ds_context_t *context) {
  ds_queue_t *queue;
  if (allocator == NULL || deallocator == NULL) {
    return NULL;
  }
  queue =
      (ds_queue_t *)DS_LEGACY_ALLOCATE(context, allocator, sizeof(ds_queue_t));
  if (queue == NULL) {
    return NULL;
  }
  queue->context = context;
  queue->allocator = allocator;
  queue->deallocator = deallocator;
  queue->nil = (ds_queue_node_t *)DS_LEGACY_ALLOCATE(context, allocator,
                                                     sizeof(ds_queue_node_t));
  if (queue->nil == NULL) {
    DS_LEGACY_RELEASE(context, deallocator, queue);
    return NULL;
  }
  queue->nil->next = queue->nil;
  queue->head = queue->nil;
  queue->tail = queue->nil;
  queue->size = 0;

  return queue;
}

ds_queue_t *ds_queue_create_alloc(void *(*allocator)(size_t),
                                  void (*deallocator)(void *)) {
  return queue_create_internal(allocator, deallocator, NULL);
}
ds_status_t ds_queue_create_context(ds_context_t *context, ds_queue_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = queue_create_internal(
      malloc, free, context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
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

static void queue_destroy_internal(ds_queue_t *queue,
                                   ds_queue_destroy_fn destroy, void *user) {
  ds_queue_node_t *node = queue->head;
  while (node != queue->nil) {
    ds_queue_node_t *next = node->next;
    if (destroy != NULL) {
      destroy(node, user);
    }
    node = next;
  }
  DS_LEGACY_FREE(queue, queue->nil);

  DS_LEGACY_FREE(queue, queue);
}
void ds_queue_destroy_with(ds_queue_t *container, ds_queue_destroy_fn destroy,
                           void *user) {
  if (container != NULL)
    queue_destroy_internal(container, destroy, user);
}
static void queue_destroy_legacy(ds_queue_node_t *node, void *user) {
  void (**destroy)(ds_queue_node_t *) = (void (**)(ds_queue_node_t *))user;
  (*destroy)(node);
}
void ds_queue_destroy(ds_queue_t *container,
                      void (*destroy)(ds_queue_node_t *)) {
  ds_queue_destroy_with(
      container, destroy != NULL ? queue_destroy_legacy : NULL, &destroy);
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
  copy = queue_create_internal(queue->allocator, queue->deallocator,
                               queue->context);
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
  copy = queue_create_internal(queue->allocator, queue->deallocator,
                               queue->context);
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

ds_status_t ds_queue_walk_with(ds_queue_t *container, ds_queue_visit_fn visit,
                               void *user) {
  size_t i;
  ds_queue_node_t *node;
  ds_status_t st;
  if (container == NULL || visit == NULL)
    return DS_ERR_NULL;
  node = container->head;
  for (i = 0; i < container->size; ++i) {

    st = visit(node, user);
    if (st != DS_OK)
      return st;
    node = node->next;
  }
  return DS_OK;
}
ds_status_t ds_queue_find_with(ds_queue_t *container, ds_queue_node_t *key,
                               ds_queue_compare_fn compare, void *user,
                               ds_queue_node_t **out) {
  size_t i;
  ds_queue_node_t *node;
  if (container == NULL || key == NULL || compare == NULL || out == NULL)
    return DS_ERR_NULL;
  node = container->head;
  for (i = 0; i < container->size; ++i) {

    if (compare(node, key, user) == 0) {
      *out = node;
      return DS_OK;
    }
    node = node->next;
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_queue_clone_with(const ds_queue_t *container,
                                ds_queue_clone_fn clone,
                                ds_queue_destroy_fn destroy, void *user,
                                ds_context_t *context, ds_queue_t **out) {
  ds_queue_t *copy;
  size_t i;
  ds_queue_node_t *node;
  ds_queue_node_t *new_node;

  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (container == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  copy = queue_create_internal(container->allocator, container->deallocator,
                               context != NULL ? context : container->context);
  if (copy == NULL)
    return DS_ERR_ALLOC;

  node = container->head;
  for (i = 0; i < container->size; ++i) {

    new_node = NULL;
    st = clone(node, &new_node, user);
    if (st != DS_OK || new_node == NULL) {
      ds_queue_destroy_with(copy, destroy, user);
      return st < 0 ? st : DS_ERR_CALLBACK;
    }
    ds_queue_enqueue(copy, new_node);
    node = node->next;
  }
  *out = copy;
  return DS_OK;
}

ds_status_t ds_queue_enqueue_ex(ds_queue_t *container, ds_queue_node_t *node) {
  if (container == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == container->nil)
    return DS_ERR_INVALID;
  if (container->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  ds_queue_enqueue(container, node);
  return DS_OK;
}

ds_status_t ds_queue_dequeue_ex(ds_queue_t *container, ds_queue_node_t **out) {
  if (container == NULL || out == NULL)
    return DS_ERR_NULL;
  if (container->size == 0)
    return DS_EMPTY;
  *out = ds_queue_dequeue(container);
  return DS_OK;
}

ds_status_t ds_queue_remove_ex(ds_queue_t *container, ds_queue_node_t *target) {
  size_t i;
  ds_queue_node_t *node;
  if (container == NULL || target == NULL)
    return DS_ERR_NULL;
  if (target == container->nil)
    return DS_ERR_INVALID;
  node = container->head;
  for (i = 0; i < container->size; ++i) {

    if (node == target) {
      ds_queue_delete_node(container, node);
      return DS_OK;
    }
    node = node->next;
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_queue_walk_reverse_with(ds_queue_t *container,
                                       ds_queue_visit_fn visit, void *user) {
  ds_queue_node_t *node, *next, *rev, *first;
  size_t i;
  ds_status_t st = DS_OK;
  if (container == NULL || visit == NULL)
    return DS_ERR_NULL;
  if (container->size == 0)
    return DS_OK;
  first = container->head;
  node = first;
  rev = container->nil;
  for (i = 0; i < container->size; ++i) {
    next = node->next;
    node->next = rev;
    rev = node;
    node = next;
  }
  node = rev;
  rev = container->nil;
  for (i = 0; i < container->size; ++i) {
    next = node->next;
    node->next = rev;
    rev = node;
    if (st == DS_OK)
      st = visit(node, user);
    node = next;
  }
  return st;
}
