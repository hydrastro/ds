#include <dsa/structures/dlist.h>
#include <stdio.h>
#include <stdlib.h>

static ds_dlist_t *dlist_create_internal(ds_context_t *context) {
  ds_dlist_t *list;

  list = (ds_dlist_t *)ds_context_alloc(context, sizeof(ds_dlist_t));
  if (list == NULL) {
    return NULL;
  }
  list->context = context != NULL ? context : ds_default_context();

  list->nil =
      (ds_dlist_node_t *)ds_context_alloc(context, sizeof(ds_dlist_node_t));
  if (list->nil == NULL) {
    ds_context_free(context, list);
    return NULL;
  }
  list->head = list->nil;
  list->tail = list->nil;
  list->nil->prev = list->nil;
  list->nil->next = list->nil;
  list->size = 0;
  return list;
}

ds_dlist_t *ds_dlist_create(ds_context_t *context) {
  return dlist_create_internal(context);
}
ds_status_t ds_dlist_create_context(ds_context_t *context, ds_dlist_t **out) {
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out =
      dlist_create_internal(context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

void ds_dlist_append(ds_dlist_t *list, ds_dlist_node_t *node) {
  ds_dlist_insert_after(list, node, list->tail);
}

void ds_dlist_prepend(ds_dlist_t *list, ds_dlist_node_t *node) {
  ds_dlist_insert_before(list, node, list->head);
}

ds_dlist_node_t *ds_dlist_search(ds_dlist_t *list, ds_dlist_node_t *node,
                                 int (*compare)(ds_dlist_node_t *,
                                                ds_dlist_node_t *)) {
  ds_dlist_node_t *head;
  head = list->head;
  while (head != list->nil && compare(head, node) != 0) {
    head = head->next;
  }
  if (head == list->nil) {
    return list->nil;
  }
  return head;
}

void ds_dlist_insert_before(ds_dlist_t *list, ds_dlist_node_t *node,
                            ds_dlist_node_t *next) {
  node->next = next;
  node->prev = next->prev;
  next->prev->next = node;
  next->prev = node;

  list->head = list->nil->next;
  list->tail = list->nil->prev;
  list->size += 1;
}

void ds_dlist_insert_after(ds_dlist_t *list, ds_dlist_node_t *node,
                           ds_dlist_node_t *prev) {
  node->next = prev->next;
  node->prev = prev;
  prev->next->prev = node;
  prev->next = node;

  list->head = list->nil->next;
  list->tail = list->nil->prev;
  list->size += 1;
}

void ds_dlist_delete_node(ds_dlist_t *list, ds_dlist_node_t *node) {
  node->prev->next = node->next;
  node->next->prev = node->prev;

  if (node == list->head) {
    list->head = node->next;
  }

  if (node == list->tail) {
    list->tail = node->prev;
  }
  list->size -= 1;
}

void ds_dlist_delete(ds_dlist_t *list) {
  list->head = list->tail = list->nil;
  list->nil->next = list->nil;
  list->nil->prev = list->nil;
  list->size = 0;
}

void ds_dlist_destroy_node(ds_dlist_t *list, ds_dlist_node_t *node,
                           void (*destroy)(ds_dlist_node_t *)) {
  ds_dlist_delete_node(list, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

static void dlist_destroy_internal(ds_dlist_t *list,
                                   ds_dlist_destroy_fn destroy, void *user) {
  ds_dlist_node_t *node = list->head;
  while (node != list->nil) {
    ds_dlist_node_t *next = node->next;
    if (destroy != NULL) {
      destroy(node, user);
    }
    node = next;
  }
  ds_context_free(list->context, list->nil);
  ds_context_free(list->context, list);
}
void ds_dlist_destroy_with(ds_dlist_t *container, ds_dlist_destroy_fn destroy,
                           void *user) {
  if (container != NULL) {
    dlist_destroy_internal(container, destroy, user);
  }
}
static void dlist_destroy_direct(ds_dlist_node_t *node, void *user) {
  void (**destroy)(ds_dlist_node_t *) = (void (**)(ds_dlist_node_t *))user;
  (*destroy)(node);
}
void ds_dlist_destroy(ds_dlist_t *container,
                      void (*destroy)(ds_dlist_node_t *)) {
  ds_dlist_destroy_with(
      container, destroy != NULL ? dlist_destroy_direct : NULL, &destroy);
}

void ds_dlist_walk_forward(ds_dlist_t *list, ds_dlist_node_t *node,
                           void (*callback)(ds_dlist_node_t *)) {
  while (node != list->nil) {
    callback(CAST(node, void));
    node = node->next;
  }
}

void ds_dlist_walk_backwards(ds_dlist_t *list, ds_dlist_node_t *node,
                             void (*callback)(ds_dlist_node_t *)) {
  while (node != list->nil) {
    callback(CAST(node, void));
    node = node->prev;
  }
}

bool ds_dlist_is_empty(ds_dlist_t *list) {
  bool result;
  result = list->size == 0;
  return result;
}

/* clones of the source's nodes, in order, added to copy; false when
 * clone_node returns NULL (the clones made so far are in copy) */
static bool
dlist_clone_nodes(const ds_dlist_t *list, ds_dlist_t *copy,
                  ds_dlist_node_t *(*clone_node)(ds_dlist_node_t *)) {
  ds_dlist_node_t *current;
  for (current = list->head; current != list->nil; current = current->next) {
    ds_dlist_node_t *node = clone_node(current);
    if (node == NULL) {
      return false;
    }
    ds_dlist_append(copy, node);
  }
  return true;
}

ds_dlist_t *ds_dlist_clone(ds_dlist_t *list,
                           ds_dlist_node_t *(*clone_node)(ds_dlist_node_t *)) {
  ds_dlist_t *copy;
  if (list == NULL || clone_node == NULL) {
    return NULL;
  }
  copy = dlist_create_internal(list->context);
  if (copy != NULL && !dlist_clone_nodes(list, copy, clone_node)) {
    /* the clones made so far are not released */
    ds_dlist_destroy(copy, NULL);
    copy = NULL;
  }
  return copy;
}

ds_status_t
ds_dlist_clone_ex(const ds_dlist_t *list,
                  ds_dlist_node_t *(*clone_node)(ds_dlist_node_t *),
                  void (*destroy_clone)(ds_dlist_node_t *), ds_dlist_t **out) {
  ds_dlist_t *copy;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (list == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  copy = dlist_create_internal(list->context);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!dlist_clone_nodes(list, copy, clone_node)) {
    ds_dlist_destroy(copy, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  *out = copy;
  return DS_OK;
}

ds_status_t ds_dlist_walk_with(ds_dlist_t *container, ds_dlist_visit_fn visit,
                               void *user) {
  size_t i;
  ds_dlist_node_t *node;
  ds_status_t st;
  if (container == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  node = container->head;
  for (i = 0; i < container->size; ++i) {
    st = visit(node, user);
    if (st != DS_OK) {
      return st;
    }
    node = node->next;
  }
  return DS_OK;
}
ds_status_t ds_dlist_find_with(ds_dlist_t *container, ds_dlist_node_t *key,
                               ds_dlist_compare_fn compare, void *user,
                               ds_dlist_node_t **out) {
  size_t i;
  ds_dlist_node_t *node;
  if (container == NULL || key == NULL || compare == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
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

ds_status_t ds_dlist_clone_with(const ds_dlist_t *container,
                                ds_dlist_clone_fn clone,
                                ds_dlist_destroy_fn destroy, void *user,
                                ds_context_t *context, ds_dlist_t **out) {
  ds_dlist_t *copy;
  size_t i;
  ds_dlist_node_t *node;
  ds_dlist_node_t *new_node;

  ds_status_t st;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (container == NULL || clone == NULL || destroy == NULL) {
    return DS_ERR_NULL;
  }
  copy = dlist_create_internal(context != NULL ? context : container->context);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }

  node = container->head;
  for (i = 0; i < container->size; ++i) {
    new_node = NULL;
    st = clone(node, &new_node, user);
    if (st != DS_OK || new_node == NULL) {
      ds_dlist_destroy_with(copy, destroy, user);
      return st < 0 ? st : DS_ERR_CALLBACK;
    }
    ds_dlist_append(copy, new_node);
    node = node->next;
  }
  *out = copy;
  return DS_OK;
}

ds_status_t ds_dlist_append_ex(ds_dlist_t *container, ds_dlist_node_t *node) {
  if (container == NULL || node == NULL) {
    return DS_ERR_NULL;
  }
  if (node == container->nil) {
    return DS_ERR_INVALID;
  }
  if (container->size == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  ds_dlist_append(container, node);
  return DS_OK;
}

ds_status_t ds_dlist_prepend_ex(ds_dlist_t *container, ds_dlist_node_t *node) {
  if (container == NULL || node == NULL) {
    return DS_ERR_NULL;
  }
  if (node == container->nil) {
    return DS_ERR_INVALID;
  }
  if (container->size == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  ds_dlist_prepend(container, node);
  return DS_OK;
}

ds_status_t ds_dlist_remove_ex(ds_dlist_t *container,
                               ds_dlist_node_t *target) {
  size_t i;
  ds_dlist_node_t *node;
  if (container == NULL || target == NULL) {
    return DS_ERR_NULL;
  }
  if (target == container->nil) {
    return DS_ERR_INVALID;
  }
  node = container->head;
  for (i = 0; i < container->size; ++i) {
    if (node == target) {
      ds_dlist_delete_node(container, node);
      return DS_OK;
    }
    node = node->next;
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_dlist_walk_reverse_with(ds_dlist_t *container,
                                       ds_dlist_visit_fn visit, void *user) {
  ds_dlist_node_t *node;
  ds_status_t st;
  if (container == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  for (node = container->tail; node != container->nil; node = node->prev) {
    st = visit(node, user);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}
