#include "core/owner_alloc.h"
#include <dsa/structures/ring_list.h>
#include <stdlib.h>

ds_ring_list_t *ds_ring_list_create(void) {
  return ds_ring_list_create_alloc(malloc, free);
}

static ds_ring_list_t *ring_list_create_internal(void *(*allocator)(size_t),
                                                 void (*deallocator)(void *),
                                                 ds_context_t *context) {
  ds_ring_list_t *list;
  if (allocator == NULL || deallocator == NULL) {
    return NULL;
  }
  list = (ds_ring_list_t *)DS_OWNER_ALLOCATE(context, allocator,
                                             sizeof(ds_ring_list_t));
  if (list == NULL) {
    return NULL;
  }
  list->context = context;
  list->allocator = allocator;
  list->deallocator = deallocator;
  list->nil = (ds_ring_list_node_t *)DS_OWNER_ALLOCATE(
      context, allocator, sizeof(ds_ring_list_node_t));
  if (list->nil == NULL) {
    DS_OWNER_RELEASE(context, deallocator, list);
    return NULL;
  }
  list->nil->next = list->nil;
  list->tail = list->nil;
  list->size = 0;
  return list;
}

ds_ring_list_t *ds_ring_list_create_alloc(void *(*allocator)(size_t),
                                          void (*deallocator)(void *)) {
  return ring_list_create_internal(allocator, deallocator, NULL);
}
ds_status_t ds_ring_list_create_context(ds_context_t *context,
                                        ds_ring_list_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = ring_list_create_internal(
      malloc, free, context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

void ds_ring_list_push_front(ds_ring_list_t *list, ds_ring_list_node_t *node) {
  if (node == list->nil) {
    return;
  }
  if (list->tail == list->nil) {
    node->next = node;
    list->tail = node;
  } else {
    node->next = list->tail->next;
    list->tail->next = node;
  }
  list->size += 1;
}

void ds_ring_list_push_back(ds_ring_list_t *list, ds_ring_list_node_t *node) {
  if (node == list->nil) {
    return;
  }
  if (list->tail == list->nil) {
    node->next = node;
    list->tail = node;
  } else {
    node->next = list->tail->next;
    list->tail->next = node;
    list->tail = node;
  }
  list->size += 1;
}

ds_ring_list_node_t *ds_ring_list_pop_front(ds_ring_list_t *list) {
  ds_ring_list_node_t *head;
  if (list->tail == list->nil) {
    return list->nil;
  }
  head = list->tail->next;
  if (head == list->tail) {
    list->tail = list->nil;
  } else {
    list->tail->next = head->next;
  }
  head->next = list->nil;
  list->size -= 1;
  return head;
}

ds_ring_list_node_t *ds_ring_list_front(ds_ring_list_t *list) {
  ds_ring_list_node_t *result;
  result = list->tail == list->nil ? list->nil : list->tail->next;
  return result;
}

ds_ring_list_node_t *ds_ring_list_back(ds_ring_list_t *list) {
  ds_ring_list_node_t *result;
  result = list->tail;
  return result;
}

void ds_ring_list_rotate(ds_ring_list_t *list) {
  if (list->tail != list->nil) {
    list->tail = list->tail->next;
  }
}

bool ds_ring_list_is_empty(ds_ring_list_t *list) {
  bool result;
  result = list->tail == list->nil;
  return result;
}

ds_ring_list_node_t *ds_ring_list_search(
    ds_ring_list_t *list, ds_ring_list_node_t *node,
    int (*compare)(ds_ring_list_node_t *, ds_ring_list_node_t *)) {
  ds_ring_list_node_t *current;
  size_t i;
  if (list->tail == list->nil) {
    return list->nil;
  }
  current = list->tail->next;
  for (i = 0; i < list->size; i++) {
    if (compare(current, node) == 0) {
      return current;
    }
    current = current->next;
  }
  return list->nil;
}

void ds_ring_list_delete_node(ds_ring_list_t *list, ds_ring_list_node_t *node) {
  ds_ring_list_node_t *current;
  ds_ring_list_node_t *previous;
  size_t i;
  if (list->tail == list->nil) {
    return;
  }
  previous = list->tail;
  current = list->tail->next;
  for (i = 0; i < list->size; i++) {
    if (current == node) {
      if (current == current->next) {
        list->tail = list->nil;
      } else {
        previous->next = current->next;
        if (current == list->tail) {
          list->tail = previous;
        }
      }
      current->next = list->nil;
      list->size -= 1;
      return;
    }
    previous = current;
    current = current->next;
  }
}

void ds_ring_list_destroy_node(ds_ring_list_t *list, ds_ring_list_node_t *node,
                               void (*destroy)(ds_ring_list_node_t *)) {
  ds_ring_list_delete_node(list, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

void ds_ring_list_walk_forward(ds_ring_list_t *list,
                               ds_ring_list_node_t *start_node,
                               void (*callback)(ds_ring_list_node_t *)) {
  ds_ring_list_node_t *current;
  size_t i;
  if (list->tail == list->nil) {
    return;
  }
  current = start_node != list->nil ? start_node : list->tail->next;
  for (i = 0; i < list->size; i++) {
    callback(current);
    current = current->next;
  }
}

static void ring_list_destroy_internal(ds_ring_list_t *list,
                                       ds_ring_list_destroy_fn destroy,
                                       void *user) {
  ds_ring_list_node_t *current;
  size_t remaining = list->size;
  if (list->tail != list->nil) {
    current = list->tail->next;
    while (remaining > 0) {
      ds_ring_list_node_t *next = current->next;
      if (destroy != NULL) {
        destroy(current, user);
      }
      current = next;
      remaining -= 1;
    }
  }
  DS_OWNER_FREE(list, list->nil);
  DS_OWNER_FREE(list, list);
}
void ds_ring_list_destroy_with(ds_ring_list_t *container,
                               ds_ring_list_destroy_fn destroy, void *user) {
  if (container != NULL)
    ring_list_destroy_internal(container, destroy, user);
}
static void ring_list_destroy_direct(ds_ring_list_node_t *node, void *user) {
  void (**destroy)(ds_ring_list_node_t *) =
      (void (**)(ds_ring_list_node_t *))user;
  (*destroy)(node);
}
void ds_ring_list_destroy(ds_ring_list_t *container,
                          void (*destroy)(ds_ring_list_node_t *)) {
  ds_ring_list_destroy_with(
      container, destroy != NULL ? ring_list_destroy_direct : NULL, &destroy);
}

/* clones of the source's nodes, in order, added to copy; false when
 * clone_node returns NULL (the clones made so far are in copy) */
static bool ring_list_clone_nodes(
    const ds_ring_list_t *list, ds_ring_list_t *copy,
    ds_ring_list_node_t *(*clone_node)(ds_ring_list_node_t *)) {
  ds_ring_list_node_t *current;
  size_t i;
  if (list->tail == list->nil) {
    return true;
  }
  current = list->tail->next;
  for (i = 0; i < list->size; i++) {
    ds_ring_list_node_t *node = clone_node(current);
    if (node == NULL) {
      return false;
    }
    ds_ring_list_push_back(copy, node);
    current = current->next;
  }
  return true;
}

ds_ring_list_t *
ds_ring_list_clone(ds_ring_list_t *list,
                   ds_ring_list_node_t *(*clone_node)(ds_ring_list_node_t *)) {
  ds_ring_list_t *copy;
  if (list == NULL || clone_node == NULL) {
    return NULL;
  }
  copy = ring_list_create_internal(list->allocator, list->deallocator,
                                   list->context);
  if (copy != NULL && !ring_list_clone_nodes(list, copy, clone_node)) {
    /* the clones made so far are not released */
    ds_ring_list_destroy(copy, NULL);
    copy = NULL;
  }
  return copy;
}

ds_status_t
ds_ring_list_clone_ex(const ds_ring_list_t *list,
                      ds_ring_list_node_t *(*clone_node)(ds_ring_list_node_t *),
                      void (*destroy_clone)(ds_ring_list_node_t *),
                      ds_ring_list_t **out) {
  ds_ring_list_t *copy;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (list == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  copy = ring_list_create_internal(list->allocator, list->deallocator,
                                   list->context);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!ring_list_clone_nodes(list, copy, clone_node)) {
    ds_ring_list_destroy(copy, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  *out = copy;
  return DS_OK;
}

ds_status_t ds_ring_list_walk_with(ds_ring_list_t *container,
                                   ds_ring_list_visit_fn visit, void *user) {
  size_t i;
  ds_ring_list_node_t *node;
  ds_status_t st;
  if (container == NULL || visit == NULL)
    return DS_ERR_NULL;
  node = container->tail->next;
  for (i = 0; i < container->size; ++i) {

    st = visit(node, user);
    if (st != DS_OK)
      return st;
    node = node->next;
  }
  return DS_OK;
}
ds_status_t ds_ring_list_find_with(ds_ring_list_t *container,
                                   ds_ring_list_node_t *key,
                                   ds_ring_list_compare_fn compare, void *user,
                                   ds_ring_list_node_t **out) {
  size_t i;
  ds_ring_list_node_t *node;
  if (container == NULL || key == NULL || compare == NULL || out == NULL)
    return DS_ERR_NULL;
  node = container->tail->next;
  for (i = 0; i < container->size; ++i) {

    if (compare(node, key, user) == 0) {
      *out = node;
      return DS_OK;
    }
    node = node->next;
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_ring_list_clone_with(const ds_ring_list_t *container,
                                    ds_ring_list_clone_fn clone,
                                    ds_ring_list_destroy_fn destroy, void *user,
                                    ds_context_t *context,
                                    ds_ring_list_t **out) {
  ds_ring_list_t *copy;
  size_t i;
  ds_ring_list_node_t *node;
  ds_ring_list_node_t *new_node;

  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (container == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  copy =
      ring_list_create_internal(container->allocator, container->deallocator,
                                context != NULL ? context : container->context);
  if (copy == NULL)
    return DS_ERR_ALLOC;

  node = container->tail->next;
  for (i = 0; i < container->size; ++i) {

    new_node = NULL;
    st = clone(node, &new_node, user);
    if (st != DS_OK || new_node == NULL) {
      ds_ring_list_destroy_with(copy, destroy, user);
      return st < 0 ? st : DS_ERR_CALLBACK;
    }
    ds_ring_list_push_back(copy, new_node);
    node = node->next;
  }
  *out = copy;
  return DS_OK;
}

ds_status_t ds_ring_list_push_front_ex(ds_ring_list_t *container,
                                       ds_ring_list_node_t *node) {
  if (container == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == container->nil)
    return DS_ERR_INVALID;
  if (container->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  ds_ring_list_push_front(container, node);
  return DS_OK;
}

ds_status_t ds_ring_list_push_back_ex(ds_ring_list_t *container,
                                      ds_ring_list_node_t *node) {
  if (container == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == container->nil)
    return DS_ERR_INVALID;
  if (container->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  ds_ring_list_push_back(container, node);
  return DS_OK;
}

ds_status_t ds_ring_list_pop_front_ex(ds_ring_list_t *container,
                                      ds_ring_list_node_t **out) {
  if (container == NULL || out == NULL)
    return DS_ERR_NULL;
  if (container->size == 0)
    return DS_EMPTY;
  *out = ds_ring_list_pop_front(container);
  return DS_OK;
}

ds_status_t ds_ring_list_remove_ex(ds_ring_list_t *container,
                                   ds_ring_list_node_t *target) {
  size_t i;
  ds_ring_list_node_t *node;
  if (container == NULL || target == NULL)
    return DS_ERR_NULL;
  if (target == container->nil)
    return DS_ERR_INVALID;
  node = container->tail->next;
  for (i = 0; i < container->size; ++i) {

    if (node == target) {
      ds_ring_list_delete_node(container, node);
      return DS_OK;
    }
    node = node->next;
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_ring_list_walk_reverse_with(ds_ring_list_t *container,
                                           ds_ring_list_visit_fn visit,
                                           void *user) {
  ds_ring_list_node_t *node, *next, *rev, *first;
  size_t i;
  ds_status_t st = DS_OK;
  if (container == NULL || visit == NULL)
    return DS_ERR_NULL;
  if (container->size == 0)
    return DS_OK;
  first = container->tail->next;
  node = first;
  rev = container->tail;
  for (i = 0; i < container->size; ++i) {
    next = node->next;
    node->next = rev;
    rev = node;
    node = next;
  }
  node = rev;
  rev = first;
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
