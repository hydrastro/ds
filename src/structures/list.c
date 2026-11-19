#include <dsa/structures/list.h>
#include <stdlib.h>

static ds_list_t *list_create_internal(ds_context_t *context) {
  ds_list_t *list;

  list = (ds_list_t *)ds_context_alloc(context, sizeof(ds_list_t));
  if (list == NULL) {
    return NULL;
  }
  list->context = context != NULL ? context : ds_default_context();

  list->nil =
      (ds_list_node_t *)ds_context_alloc(context, sizeof(ds_list_node_t));
  if (list->nil == NULL) {
    ds_context_free(context, list);
    return NULL;
  }
  list->head = list->nil;
  list->tail = list->nil;
  list->nil->next = list->nil;
  list->size = 0;
  return list;
}

ds_list_t *ds_list_create(ds_context_t *context) {
  return list_create_internal(context);
}
ds_status_t ds_list_create_context(ds_context_t *context, ds_list_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out =
      list_create_internal(context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

void ds_list_append(ds_list_t *list, ds_list_node_t *node) {
  node->next = list->nil;
  list->tail->next = node;
  list->tail = node;
  if (list->head == list->nil) {
    list->head = node;
  }
  list->size += 1;
}

void ds_list_prepend(ds_list_t *list, ds_list_node_t *node) {
  node->next = list->head;
  list->head = node;
  list->nil->next = node; /* the sentinel always leads to the head */
  if (list->tail == list->nil) {
    list->tail = node;
  }
  list->size += 1;
}

ds_list_node_t *ds_list_search(ds_list_t *list, ds_list_node_t *node,
                               int (*compare)(ds_list_node_t *,
                                              ds_list_node_t *)) {
  ds_list_node_t *head;
  head = list->head;
  while (head != list->nil && compare(head, node) != 0) {
    head = head->next;
  }
  if (head == list->nil) {
    return list->nil;
  }
  return head;
}

void ds_list_insert_before(ds_list_t *list, ds_list_node_t *node,
                           ds_list_node_t *next) {
  ds_list_node_t *prev;
  /* from the sentinel, whose next is the head, so that inserting before
   * the head (or into an empty list) finds its predecessor too */
  prev = list->nil;
  while (prev->next != next) {
    prev = prev->next;
  }
  node->next = next;
  prev->next = node;
  if (prev == list->nil) {
    list->head = node;
  }
  if (next == list->nil) {
    list->tail = node;
  }
  list->size += 1;
}

void ds_list_insert_after(ds_list_t *list, ds_list_node_t *node,
                          ds_list_node_t *prev) {
  node->next = prev->next;
  prev->next = node;

  if (prev == list->tail) {
    list->tail = node;
  }

  if (prev == list->nil) {
    list->head = node; /* prev->next above was the sentinel's: the head */
  }
  list->size += 1;
}

void ds_list_delete_node(ds_list_t *list, ds_list_node_t *node) {
  ds_list_node_t *prev;
  prev = list->nil;
  while (prev->next != node) {
    prev = prev->next;
  }
  prev->next = node->next; /* from the sentinel: it leads to the head */
  if (node == list->head) {
    list->head = node->next;
  }
  if (node == list->tail) {
    list->tail = prev;
  }
  list->size -= 1;
}

void ds_list_delete(ds_list_t *list) {
  list->head = list->tail = list->nil;
  list->nil->next = list->nil;
  list->size = 0;
}

void ds_list_destroy_node(ds_list_t *list, ds_list_node_t *node,
                          void (*destroy)(ds_list_node_t *)) {
  ds_list_delete_node(list, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

static void list_destroy_internal(ds_list_t *list, ds_list_destroy_fn destroy,
                                  void *user) {
  ds_list_node_t *node, *next;
  node = list->head;
  while (node != list->nil) {
    next = node->next;
    if (destroy != NULL) {
      destroy(node, user);
    }
    node = next;
  }
  ds_context_free(list->context, list->nil);
  ds_context_free(list->context, list);
}
void ds_list_destroy_with(ds_list_t *container, ds_list_destroy_fn destroy,
                          void *user) {
  if (container != NULL)
    list_destroy_internal(container, destroy, user);
}
static void list_destroy_direct(ds_list_node_t *node, void *user) {
  void (**destroy)(ds_list_node_t *) = (void (**)(ds_list_node_t *))user;
  (*destroy)(node);
}
void ds_list_destroy(ds_list_t *container, void (*destroy)(ds_list_node_t *)) {
  ds_list_destroy_with(container, destroy != NULL ? list_destroy_direct : NULL,
                       &destroy);
}

void ds_list_walk_forward(ds_list_t *list, ds_list_node_t *node,
                          void (*callback)(ds_list_node_t *)) {
  ds_list_node_t *cur;
  cur = node;
  while (cur != list->nil) {
    callback(CAST(cur, void));
    cur = cur->next;
  }
}

/* without recursion (it went as deep as the list was long): the nodes
 * from `node` on are reversed in place, visited, and put back */
void ds_list_walk_backwards(ds_list_t *list, ds_list_node_t *node,
                            void (*callback)(ds_list_node_t *)) {
  ds_list_node_t *rev = list->nil;
  ds_list_node_t *cur = node;
  ds_list_node_t *next;
  while (cur != list->nil) {
    next = cur->next;
    cur->next = rev;
    rev = cur;
    cur = next;
  }
  cur = rev;
  rev = list->nil;
  while (cur != list->nil) {
    next = cur->next;
    cur->next = rev; /* restored before the callback sees the node */
    rev = cur;
    callback(CAST(cur, void));
    cur = next;
  }
}

bool ds_list_is_empty(ds_list_t *list) {
  bool result;
  result = list->size == 0;
  return result;
}

ds_list_t *ds_list_clone(ds_list_t *list,
                         ds_list_node_t *(*clone_node)(ds_list_node_t *)) {
  ds_list_t *new_list;
  ds_list_node_t *current;
  if (list == NULL || clone_node == NULL) {
    return NULL;
  }
  new_list = list_create_internal(list->context);
  if (new_list == NULL) {
    return NULL;
  }
  current = list->head;
  while (current != list->nil) {
    ds_list_node_t *cloned_node = clone_node(current);
    if (cloned_node == NULL) { /* the clones made so far are not released */
      ds_list_destroy(new_list, NULL);
      return NULL;
    }
    ds_list_append(new_list, cloned_node);
    current = current->next;
  }

  return new_list;
}

ds_status_t ds_list_clone_ex(const ds_list_t *list,
                             ds_list_node_t *(*clone_node)(ds_list_node_t *),
                             void (*destroy_clone)(ds_list_node_t *),
                             ds_list_t **out) {
  ds_list_t *copy;
  ds_list_node_t *current;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (list == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  copy = list_create_internal(list->context);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  for (current = list->head; current != list->nil; current = current->next) {
    ds_list_node_t *node = clone_node(current);
    if (node == NULL) {
      ds_list_destroy(copy, destroy_clone);
      return DS_ERR_CALLBACK;
    }
    ds_list_append(copy, node);
  }
  *out = copy;
  return DS_OK;
}

ds_status_t ds_list_walk_with(ds_list_t *container, ds_list_visit_fn visit,
                              void *user) {
  size_t i;
  ds_list_node_t *node;
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
ds_status_t ds_list_find_with(ds_list_t *container, ds_list_node_t *key,
                              ds_list_compare_fn compare, void *user,
                              ds_list_node_t **out) {
  size_t i;
  ds_list_node_t *node;
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

ds_status_t ds_list_clone_with(const ds_list_t *container,
                               ds_list_clone_fn clone,
                               ds_list_destroy_fn destroy, void *user,
                               ds_context_t *context, ds_list_t **out) {
  ds_list_t *copy;
  size_t i;
  ds_list_node_t *node;
  ds_list_node_t *new_node;

  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (container == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  copy = list_create_internal(context != NULL ? context : container->context);
  if (copy == NULL)
    return DS_ERR_ALLOC;

  node = container->head;
  for (i = 0; i < container->size; ++i) {
    new_node = NULL;
    st = clone(node, &new_node, user);
    if (st != DS_OK || new_node == NULL) {
      ds_list_destroy_with(copy, destroy, user);
      return st < 0 ? st : DS_ERR_CALLBACK;
    }
    ds_list_append(copy, new_node);
    node = node->next;
  }
  *out = copy;
  return DS_OK;
}

ds_status_t ds_list_append_ex(ds_list_t *container, ds_list_node_t *node) {
  if (container == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == container->nil)
    return DS_ERR_INVALID;
  if (container->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  ds_list_append(container, node);
  return DS_OK;
}

ds_status_t ds_list_prepend_ex(ds_list_t *container, ds_list_node_t *node) {
  if (container == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == container->nil)
    return DS_ERR_INVALID;
  if (container->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  ds_list_prepend(container, node);
  return DS_OK;
}

ds_status_t ds_list_remove_ex(ds_list_t *container, ds_list_node_t *target) {
  size_t i;
  ds_list_node_t *node;
  if (container == NULL || target == NULL)
    return DS_ERR_NULL;
  if (target == container->nil)
    return DS_ERR_INVALID;
  node = container->head;
  for (i = 0; i < container->size; ++i) {
    if (node == target) {
      ds_list_delete_node(container, node);
      return DS_OK;
    }
    node = node->next;
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_list_walk_reverse_with(ds_list_t *container,
                                      ds_list_visit_fn visit, void *user) {
  ds_list_node_t *node, *next, *rev, *first;
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
