#include <dsa/structures/deque.h>
#include <stdlib.h>

static ds_deque_t *deque_create_internal(ds_context_t *context) {
  ds_deque_t *deque;

  deque = (ds_deque_t *)ds_context_alloc(context, sizeof(ds_deque_t));
  if (deque == NULL) {
    return NULL;
  }
  deque->context = context != NULL ? context : ds_default_context();

  deque->nil =
      (ds_deque_node_t *)ds_context_alloc(context, sizeof(ds_deque_node_t));
  if (deque->nil == NULL) {
    ds_context_free(context, deque);
    return NULL;
  }
  deque->nil->next = deque->nil;
  deque->nil->prev = deque->nil;
  deque->head = deque->nil;
  deque->tail = deque->nil;
  deque->size = 0;

  return deque;
}

ds_deque_t *ds_deque_create(ds_context_t *context) {
  return deque_create_internal(context);
}
ds_status_t ds_deque_create_context(ds_context_t *context, ds_deque_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out =
      deque_create_internal(context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
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

static void deque_destroy_internal(ds_deque_t *deque,
                                   ds_deque_destroy_fn destroy, void *user) {
  ds_deque_node_t *node = deque->head;
  while (node != deque->nil) {
    ds_deque_node_t *next = node->next;
    if (destroy != NULL) {
      destroy(node, user);
    }
    node = next;
  }
  ds_context_free(deque->context, deque->nil);

  ds_context_free(deque->context, deque);
}
void ds_deque_destroy_with(ds_deque_t *container, ds_deque_destroy_fn destroy,
                           void *user) {
  if (container != NULL)
    deque_destroy_internal(container, destroy, user);
}
static void deque_destroy_direct(ds_deque_node_t *node, void *user) {
  void (**destroy)(ds_deque_node_t *) = (void (**)(ds_deque_node_t *))user;
  (*destroy)(node);
}
void ds_deque_destroy(ds_deque_t *container,
                      void (*destroy)(ds_deque_node_t *)) {
  ds_deque_destroy_with(
      container, destroy != NULL ? deque_destroy_direct : NULL, &destroy);
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
  copy = deque_create_internal(deque->context);
  if (copy != NULL && !deque_clone_nodes(deque, copy, clone_node)) {
    /* the clones made so far are not released */
    ds_deque_destroy(copy, NULL);
    copy = NULL;
  }
  return copy;
}

ds_status_t
ds_deque_clone_ex(const ds_deque_t *deque,
                  ds_deque_node_t *(*clone_node)(ds_deque_node_t *),
                  void (*destroy_clone)(ds_deque_node_t *), ds_deque_t **out) {
  ds_deque_t *copy;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (deque == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  copy = deque_create_internal(deque->context);
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

ds_status_t ds_deque_walk_with(ds_deque_t *container, ds_deque_visit_fn visit,
                               void *user) {
  size_t i;
  ds_deque_node_t *node;
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
ds_status_t ds_deque_find_with(ds_deque_t *container, ds_deque_node_t *key,
                               ds_deque_compare_fn compare, void *user,
                               ds_deque_node_t **out) {
  size_t i;
  ds_deque_node_t *node;
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

ds_status_t ds_deque_clone_with(const ds_deque_t *container,
                                ds_deque_clone_fn clone,
                                ds_deque_destroy_fn destroy, void *user,
                                ds_context_t *context, ds_deque_t **out) {
  ds_deque_t *copy;
  size_t i;
  ds_deque_node_t *node;
  ds_deque_node_t *new_node;

  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (container == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  copy = deque_create_internal(context != NULL ? context : container->context);
  if (copy == NULL)
    return DS_ERR_ALLOC;

  node = container->head;
  for (i = 0; i < container->size; ++i) {
    new_node = NULL;
    st = clone(node, &new_node, user);
    if (st != DS_OK || new_node == NULL) {
      ds_deque_destroy_with(copy, destroy, user);
      return st < 0 ? st : DS_ERR_CALLBACK;
    }
    ds_deque_push_back(copy, new_node);
    node = node->next;
  }
  *out = copy;
  return DS_OK;
}

ds_status_t ds_deque_push_front_ex(ds_deque_t *container,
                                   ds_deque_node_t *node) {
  if (container == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == container->nil)
    return DS_ERR_INVALID;
  if (container->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  ds_deque_push_front(container, node);
  return DS_OK;
}

ds_status_t ds_deque_push_back_ex(ds_deque_t *container,
                                  ds_deque_node_t *node) {
  if (container == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == container->nil)
    return DS_ERR_INVALID;
  if (container->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  ds_deque_push_back(container, node);
  return DS_OK;
}

ds_status_t ds_deque_pop_front_ex(ds_deque_t *container,
                                  ds_deque_node_t **out) {
  if (container == NULL || out == NULL)
    return DS_ERR_NULL;
  if (container->size == 0)
    return DS_EMPTY;
  *out = ds_deque_pop_front(container);
  return DS_OK;
}

ds_status_t ds_deque_pop_back_ex(ds_deque_t *container,
                                 ds_deque_node_t **out) {
  if (container == NULL || out == NULL)
    return DS_ERR_NULL;
  if (container->size == 0)
    return DS_EMPTY;
  *out = ds_deque_pop_back(container);
  return DS_OK;
}

ds_status_t ds_deque_remove_ex(ds_deque_t *container,
                               ds_deque_node_t *target) {
  size_t i;
  ds_deque_node_t *node;
  if (container == NULL || target == NULL)
    return DS_ERR_NULL;
  if (target == container->nil)
    return DS_ERR_INVALID;
  node = container->head;
  for (i = 0; i < container->size; ++i) {
    if (node == target) {
      ds_deque_delete_node(container, node);
      return DS_OK;
    }
    node = node->next;
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_deque_walk_reverse_with(ds_deque_t *container,
                                       ds_deque_visit_fn visit, void *user) {
  ds_deque_node_t *node;
  ds_status_t st;
  if (container == NULL || visit == NULL)
    return DS_ERR_NULL;
  for (node = container->tail; node != container->nil; node = node->prev) {
    st = visit(node, user);
    if (st != DS_OK)
      return st;
  }
  return DS_OK;
}
