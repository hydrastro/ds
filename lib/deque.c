#include "deque.h"
#include <stdlib.h>

ds_deque_t *ds_deque_create(void) {
  return ds_deque_create_alloc(malloc, free);
}

ds_deque_t *ds_deque_create_alloc(void *(*allocator)(size_t),
                                  void (*deallocator)(void *)) {
  ds_deque_t *deque;
  if (allocator == NULL || deallocator == NULL) {
    return NULL;
  }
  deque = (ds_deque_t *)allocator(sizeof(ds_deque_t));
  if (deque == NULL) {
    return NULL;
  }
  deque->allocator = allocator;
  deque->deallocator = deallocator;
  deque->nil = (ds_deque_node_t *)allocator(sizeof(ds_deque_node_t));
  if (deque->nil == NULL) {
    deallocator(deque);
    return NULL;
  }
  deque->nil->next = deque->nil;
  deque->nil->prev = deque->nil;
  deque->head = deque->nil;
  deque->tail = deque->nil;
  deque->size = 0;

  return deque;
}

void ds_deque_push_front(ds_deque_t *deque, ds_deque_node_t *node) {
  node->next = deque->head;
  node->prev = deque->nil;
  if (deque->head != deque->nil) {
    deque->head->prev = node;
  }
  deque->head = node;
  if (deque->tail == deque->nil) {
    deque->tail = node;
  }
  deque->size += 1;
}

void ds_deque_push_back(ds_deque_t *deque, ds_deque_node_t *node) {
  node->next = deque->nil;
  node->prev = deque->tail;
  if (deque->tail != deque->nil) {
    deque->tail->next = node;
  }
  deque->tail = node;
  if (deque->head == deque->nil) {
    deque->head = node;
  }
  deque->size += 1;
}

ds_deque_node_t *ds_deque_pop_front(ds_deque_t *deque) {
  ds_deque_node_t *node;

  if (ds_deque_is_empty(deque)) {
    return deque->nil;
  }
  node = deque->head;
  deque->head = node->next;
  if (deque->head != deque->nil) {
    deque->head->prev = deque->nil;
  } else {
    deque->tail = deque->nil;
  }
  deque->size -= 1;

  return node;
}

ds_deque_node_t *ds_deque_pop_back(ds_deque_t *deque) {
  ds_deque_node_t *node;

  if (ds_deque_is_empty(deque)) {
    return deque->nil;
  }
  node = deque->tail;
  deque->tail = node->prev;
  if (deque->tail != deque->nil) {
    deque->tail->next = deque->nil;
  } else {
    deque->head = deque->nil;
  }
  deque->size -= 1;

  return node;
}

ds_deque_node_t *ds_deque_peek_front(ds_deque_t *deque) {
  ds_deque_node_t *result;
  result = deque->head;
  return result;
}

ds_deque_node_t *ds_deque_peek_back(ds_deque_t *deque) {
  ds_deque_node_t *result;
  result = deque->tail;
  return result;
}

bool ds_deque_is_empty(ds_deque_t *deque) {
  bool empty;

  empty = deque->head == deque->nil;

  return empty;
}

void ds_deque_destroy(ds_deque_t *deque, void (*destroy)(ds_deque_node_t *)) {
  ds_deque_node_t *node = deque->head;
  while (node != deque->nil) {
    ds_deque_node_t *next = node->next;
    if (destroy != NULL) {
      destroy(node);
    }
    node = next;
  }
  deque->deallocator(deque->nil);

  deque->deallocator(deque);
}

void ds_deque_delete(ds_deque_t *deque) {
  deque->head = deque->tail = deque->nil;
  deque->size = 0;
}

void ds_deque_delete_node(ds_deque_t *deque, ds_deque_node_t *node) {
  if (node == deque->head) {
    deque->head = node->next;
    if (deque->head != deque->nil) {
      deque->head->prev = deque->nil;
    } else {
      deque->tail = deque->nil;
    }
  } else if (node == deque->tail) {
    deque->tail = node->prev;
    if (deque->tail != deque->nil) {
      deque->tail->next = deque->nil;
    } else {
      deque->head = deque->nil;
    }
  } else {
    node->prev->next = node->next;
    node->next->prev = node->prev;
  }

  deque->size--;
}

void ds_deque_destroy_node(ds_deque_t *deque, ds_deque_node_t *node,
                           void (*destroy)(ds_deque_node_t *)) {
  ds_deque_delete_node(deque, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

void ds_deque_pop_front_destroy(ds_deque_t *deque,
                                void (*destroy)(ds_deque_node_t *)) {
  if (deque->head != deque->nil) {
    ds_deque_node_t *node = ds_deque_pop_front(deque);
    if (destroy != NULL) {
      destroy(node);
    }
  }
}

void ds_deque_pop_back_destroy(ds_deque_t *deque,
                               void (*destroy)(ds_deque_node_t *)) {
  if (deque->tail != deque->nil) {
    ds_deque_node_t *node = ds_deque_pop_back(deque);
    if (destroy != NULL) {
      destroy(node);
    }
  }
}

ds_deque_node_t *ds_deque_search(ds_deque_t *deque, ds_deque_node_t *node,
                                 int (*compare)(ds_deque_node_t *,
                                                ds_deque_node_t *)) {
  ds_deque_node_t *current;

  current = deque->head;

  while (current != deque->nil) {
    if (compare(current, node) == 0) {
      return current;
    }
    current = current->next;
  }

  return deque->nil;
}

void ds_deque_walk_forward(ds_deque_t *deque, ds_deque_node_t *start_node,
                           void (*callback)(ds_deque_node_t *)) {
  ds_deque_node_t *current;

  current = (start_node != NULL) ? start_node : deque->head;

  while (current != deque->nil) {
    callback(current);
    current = current->next;
  }
}

void ds_deque_walk_backwards(ds_deque_t *deque, ds_deque_node_t *current,
                             void (*callback)(ds_deque_node_t *)) {
  while (current != deque->nil) {
    callback(current);
    current = current->prev;
  }
}

/* clones of the source's nodes, in order, added to copy; false when
 * clone_node returns NULL (the clones made so far are in copy) */
static bool
deque_clone_nodes(const ds_deque_t *deque, ds_deque_t *copy,
                  ds_deque_node_t *(*clone_node)(ds_deque_node_t *)) {
  ds_deque_node_t *current;
  for (current = deque->head; current != deque->nil; current = current->next) {
    ds_deque_node_t *node = clone_node(current);
    if (node == NULL) {
      return false;
    }
    ds_deque_push_back(copy, node);
  }
  return true;
}

ds_deque_t *ds_deque_clone(ds_deque_t *deque,
                           ds_deque_node_t *(*clone_node)(ds_deque_node_t *)) {
  ds_deque_t *copy;
  if (deque == NULL || clone_node == NULL) {
    return NULL;
  }
  copy = ds_deque_create_alloc(deque->allocator, deque->deallocator);
  if (copy != NULL && !deque_clone_nodes(deque, copy, clone_node)) {
    /* the clones made so far are not released */
    ds_deque_destroy(copy, NULL);
    copy = NULL;
  }
  return copy;
}

ds_status_t ds_deque_clone_ex(const ds_deque_t *deque,
                              ds_deque_node_t *(*clone_node)(ds_deque_node_t *),
                              void (*destroy_clone)(ds_deque_node_t *),
                              ds_deque_t **out) {
  ds_deque_t *copy;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (deque == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  copy = ds_deque_create_alloc(deque->allocator, deque->deallocator);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!deque_clone_nodes(deque, copy, clone_node)) {
    ds_deque_destroy(copy, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  *out = copy;
  return DS_OK;
}
