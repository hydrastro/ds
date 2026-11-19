#include <dsa/structures/skip_list.h>
#include <stdlib.h>

static int skip_list_compare(ds_skip_list_t *owner, ds_skip_list_node_t *a,
                             ds_skip_list_node_t *b) {
  return owner->compare_with != NULL
             ? owner->compare_with(a, b, owner->compare_user)
             : owner->compare(a, b);
}

static size_t skip_list_random_level(ds_skip_list_t *list) {
  size_t level = 1;
  list->seed = list->seed * 1103515245UL + 12345UL;
  while (((list->seed >> 16) & 1UL) != 0UL && level < list->max_level) {
    list->seed = list->seed * 1103515245UL + 12345UL;
    level += 1;
  }
  return level;
}

static ds_skip_list_node_t *skip_list_make_header(ds_skip_list_t *list,
                                                  size_t level) {
  ds_skip_list_node_t *header;
  size_t i;
  header = (ds_skip_list_node_t *)ds_context_alloc(
      list->context, sizeof(ds_skip_list_node_t));
  if (header == NULL) {
    return NULL;
  }
  header->forward = (ds_skip_list_node_t **)ds_context_alloc(
      list->context, level * sizeof(ds_skip_list_node_t *));
  if (header->forward == NULL) {
    ds_context_free(list->context, header);
    return NULL;
  }
  header->level = level;
  for (i = 0; i < level; i++) {
    header->forward[i] = list->nil;
  }
  return header;
}

static ds_skip_list_t *skip_list_create_internal(
    int (*compare)(ds_skip_list_node_t *, ds_skip_list_node_t *),
    size_t max_level, unsigned long seed, ds_context_t *context,
    ds_skip_list_compare_fn compare_with, void *compare_user) {
  ds_skip_list_t *list;
  if (compare == NULL && compare_with == NULL) {
    return NULL;
  }
  list = (ds_skip_list_t *)ds_context_alloc(context, sizeof(ds_skip_list_t));
  if (list == NULL) {
    return NULL;
  }
  list->context = context != NULL ? context : ds_default_context();
  list->compare_with = compare_with;
  list->compare_user = compare_user;

  list->compare = compare;
  /* insert and remove keep one predecessor per level on the stack */
  list->max_level = max_level > 0 && max_level < DS_SKIP_LIST_MAX_LEVEL
                        ? max_level
                        : DS_SKIP_LIST_MAX_LEVEL;
  list->seed = seed != 0UL ? seed : 0x2545F491UL;
  list->level = 1;
  list->size = 0;
  list->nil = (ds_skip_list_node_t *)ds_context_alloc(
      context, sizeof(ds_skip_list_node_t));
  if (list->nil == NULL) {
    ds_context_free(context, list);
    return NULL;
  }
  list->nil->forward = NULL;
  list->nil->level = 0;
  list->header = skip_list_make_header(list, list->max_level);
  if (list->header == NULL) {
    ds_context_free(context, list->nil);
    ds_context_free(context, list);
    return NULL;
  }
  return list;
}
ds_skip_list_t *ds_skip_list_create(int (*compare)(ds_skip_list_node_t *,
                                                   ds_skip_list_node_t *),
                                    size_t max_level, unsigned long seed,
                                    ds_context_t *context) {
  return skip_list_create_internal(compare, max_level, seed, context, NULL,
                                   NULL);
}
ds_status_t ds_skip_list_create_context(size_t max_level, unsigned long seed,
                                        ds_skip_list_compare_fn compare,
                                        void *user, ds_context_t *context,
                                        ds_skip_list_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (compare == NULL)
    return DS_ERR_NULL;
  if (max_level > DS_SKIP_LIST_MAX_LEVEL)
    return DS_ERR_RANGE;
  *out = skip_list_create_internal(
      NULL, max_level, seed, context != NULL ? context : ds_default_context(),
      compare, user);
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

void ds_skip_list_node_init(ds_skip_list_node_t *node) {
  node->forward = NULL;
  node->level = 0;
}

ds_status_t ds_skip_list_insert_ex(ds_skip_list_t *list,
                                   ds_skip_list_node_t *node) {
  ds_skip_list_node_t *update[DS_SKIP_LIST_MAX_LEVEL];
  ds_skip_list_node_t **forward;
  ds_skip_list_node_t *current;
  size_t i;
  size_t new_level;
  unsigned long saved_seed;
  if (list == NULL || node == NULL) {
    return DS_ERR_NULL;
  }
  if (node == list->nil) {
    return DS_ERR_INVALID;
  }
  if (list->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  current = list->header;
  i = list->level;
  while (i > 0) {
    i -= 1;
    while (current->forward[i] != list->nil &&
           skip_list_compare(list, current->forward[i], node) < 0) {
      current = current->forward[i];
    }
    update[i] = current;
  }
  saved_seed = list->seed;
  new_level = skip_list_random_level(list);
  forward = (ds_skip_list_node_t **)ds_context_alloc(
      list->context, new_level * sizeof(ds_skip_list_node_t *));
  if (forward == NULL) {
    list->seed = saved_seed;
    return DS_ERR_ALLOC;
  }
  if (new_level > list->level) {
    for (i = list->level; i < new_level; i++) {
      update[i] = list->header;
    }
    list->level = new_level;
  }
  node->level = new_level;
  node->forward = forward;
  for (i = 0; i < new_level; i++) {
    node->forward[i] = update[i]->forward[i];
    update[i]->forward[i] = node;
  }
  list->size += 1;
  return DS_OK;
}

void ds_skip_list_insert(ds_skip_list_t *list, ds_skip_list_node_t *node) {
  (void)ds_skip_list_insert_ex(list, node);
}

ds_skip_list_node_t *ds_skip_list_search(ds_skip_list_t *list,
                                         ds_skip_list_node_t *node) {
  ds_skip_list_node_t *current;
  size_t i;
  current = list->header;
  i = list->level;
  while (i > 0) {
    i -= 1;
    while (current->forward[i] != list->nil &&
           skip_list_compare(list, current->forward[i], node) < 0) {
      current = current->forward[i];
    }
  }
  current = current->forward[0];
  if (current != list->nil && skip_list_compare(list, current, node) == 0) {
    return current;
  }
  return list->nil;
}

ds_skip_list_node_t *ds_skip_list_remove(ds_skip_list_t *list,
                                         ds_skip_list_node_t *node) {
  ds_skip_list_node_t *update[DS_SKIP_LIST_MAX_LEVEL];
  ds_skip_list_node_t *current;
  size_t i;
  current = list->header;
  i = list->level;
  while (i > 0) {
    i -= 1;
    while (current->forward[i] != list->nil &&
           skip_list_compare(list, current->forward[i], node) < 0) {
      current = current->forward[i];
    }
    update[i] = current;
  }
  current = current->forward[0];
  if (current == list->nil || skip_list_compare(list, current, node) != 0) {
    return list->nil;
  }
  for (i = 0; i < list->level; i++) {
    if (update[i]->forward[i] != current) {
      break;
    }
    update[i]->forward[i] = current->forward[i];
  }
  ds_context_free(list->context, current->forward);
  current->forward = NULL;
  current->level = 0;
  while (list->level > 1 &&
         list->header->forward[list->level - 1] == list->nil) {
    list->level -= 1;
  }
  list->size -= 1;
  return current;
}

ds_skip_list_node_t *ds_skip_list_first(ds_skip_list_t *list) {
  ds_skip_list_node_t *result;
  result = list->header->forward[0];
  return result;
}

ds_skip_list_node_t *ds_skip_list_next(ds_skip_list_t *list,
                                       ds_skip_list_node_t *node) {
  ds_skip_list_node_t *result;
  if (node == list->nil || node->forward == NULL) {
    return list->nil;
  }
  result = node->forward[0];
  return result;
}

bool ds_skip_list_is_empty(ds_skip_list_t *list) {
  bool result;
  result = list->header->forward[0] == list->nil;
  return result;
}

void ds_skip_list_delete_node(ds_skip_list_t *list,
                              ds_skip_list_node_t *node) {
  ds_skip_list_node_t *removed;
  removed = ds_skip_list_remove(list, node);
  (void)removed;
}

void ds_skip_list_destroy_node(ds_skip_list_t *list, ds_skip_list_node_t *node,
                               void (*destroy)(ds_skip_list_node_t *)) {
  ds_skip_list_node_t *removed;
  removed = ds_skip_list_remove(list, node);
  if (removed != list->nil && destroy != NULL) {
    destroy(removed);
  }
}

void ds_skip_list_walk_forward(ds_skip_list_t *list,
                               ds_skip_list_node_t *start_node,
                               void (*callback)(ds_skip_list_node_t *)) {
  ds_skip_list_node_t *current;
  current = start_node != list->nil ? start_node : list->header->forward[0];
  while (current != list->nil) {
    callback(current);
    current = current->forward[0];
  }
}

static void skip_list_destroy_internal(ds_skip_list_t *list,
                                       ds_skip_list_destroy_fn destroy,
                                       void *user) {
  ds_skip_list_node_t *current = list->header->forward[0];
  while (current != list->nil) {
    ds_skip_list_node_t *next = current->forward[0];
    ds_context_free(list->context, current->forward);
    current->forward = NULL;
    if (destroy != NULL) {
      destroy(current, user);
    }
    current = next;
  }
  ds_context_free(list->context, list->header->forward);
  ds_context_free(list->context, list->header);
  ds_context_free(list->context, list->nil);
  ds_context_free(list->context, list);
}
void ds_skip_list_destroy_with(ds_skip_list_t *container,
                               ds_skip_list_destroy_fn destroy, void *user) {
  if (container != NULL)
    skip_list_destroy_internal(container, destroy, user);
}
static void skip_list_destroy_direct(ds_skip_list_node_t *node, void *user) {
  void (**destroy)(ds_skip_list_node_t *) =
      (void (**)(ds_skip_list_node_t *))user;
  (*destroy)(node);
}
void ds_skip_list_destroy(ds_skip_list_t *container,
                          void (*destroy)(ds_skip_list_node_t *)) {
  ds_skip_list_destroy_with(
      container, destroy != NULL ? skip_list_destroy_direct : NULL, &destroy);
}

/* clones of the source's nodes, in order, inserted into copy: DS_OK,
 * DS_ERR_CALLBACK for a NULL from clone_node, or DS_ERR_ALLOC when a
 * clone cannot be linked in (that clone then goes to destroy_clone, when
 * there is one). The clones linked so far are in copy. */
static ds_status_t skip_list_clone_nodes(
    const ds_skip_list_t *list, ds_skip_list_t *copy,
    ds_skip_list_node_t *(*clone_node)(ds_skip_list_node_t *),
    void (*destroy_clone)(ds_skip_list_node_t *)) {
  ds_skip_list_node_t *current;
  for (current = list->header->forward[0]; current != list->nil;
       current = current->forward[0]) {
    ds_skip_list_node_t *node = clone_node(current);
    ds_status_t st;
    if (node == NULL) {
      return DS_ERR_CALLBACK;
    }
    st = ds_skip_list_insert_ex(copy, node);
    if (st != DS_OK) {
      if (destroy_clone != NULL) {
        destroy_clone(node);
      }
      return st;
    }
  }
  return DS_OK;
}

ds_skip_list_t *
ds_skip_list_clone(ds_skip_list_t *list,
                   ds_skip_list_node_t *(*clone_node)(ds_skip_list_node_t *)) {
  ds_skip_list_t *new_list;
  if (list == NULL || clone_node == NULL) {
    return NULL;
  }
  new_list = skip_list_create_internal(list->compare, list->max_level,
                                       list->seed, list->context,
                                       list->compare_with, list->compare_user);
  if (new_list != NULL &&
      skip_list_clone_nodes(list, new_list, clone_node, NULL) != DS_OK) {
    ds_skip_list_destroy(new_list, NULL); /* the clones are not released */
    new_list = NULL;
  }
  return new_list;
}

ds_status_t ds_skip_list_clone_ex(
    const ds_skip_list_t *list,
    ds_skip_list_node_t *(*clone_node)(ds_skip_list_node_t *),
    void (*destroy_clone)(ds_skip_list_node_t *), ds_skip_list_t **out) {
  ds_skip_list_t *new_list;
  ds_status_t st;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (list == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  new_list = skip_list_create_internal(list->compare, list->max_level,
                                       list->seed, list->context,
                                       list->compare_with, list->compare_user);
  if (new_list == NULL) {
    return DS_ERR_ALLOC;
  }
  st = skip_list_clone_nodes(list, new_list, clone_node, destroy_clone);
  if (st != DS_OK) {
    ds_skip_list_destroy(new_list, destroy_clone);
    return st;
  }
  *out = new_list;
  return DS_OK;
}

ds_status_t ds_skip_list_walk_with(const ds_skip_list_t *container,
                                   ds_skip_list_visit_fn visit, void *user) {
  ds_skip_list_node_t *node;
  ds_status_t st;
  if (container == NULL || visit == NULL)
    return DS_ERR_NULL;
  for (node = container->header->forward[0]; node != container->nil;
       node = node->forward[0]) {
    st = visit(node, user);
    if (st != DS_OK)
      return st;
  }
  return DS_OK;
}

ds_status_t ds_skip_list_find_ex(ds_skip_list_t *container,
                                 ds_skip_list_node_t *key,
                                 ds_skip_list_node_t **out) {
  ds_skip_list_node_t *node;
  if (container == NULL || key == NULL || out == NULL)
    return DS_ERR_NULL;
  if (key == container->nil)
    return DS_ERR_INVALID;
  node = ds_skip_list_search(container, key);
  if (node == container->nil)
    return DS_NOT_FOUND;
  *out = node;
  return DS_OK;
}

ds_status_t ds_skip_list_remove_ex(ds_skip_list_t *container,
                                   ds_skip_list_node_t *key,
                                   ds_skip_list_node_t **out) {
  ds_skip_list_node_t *node;
  if (container == NULL || key == NULL || out == NULL)
    return DS_ERR_NULL;
  if (key == container->nil)
    return DS_ERR_INVALID;
  node = ds_skip_list_remove(container, key);
  if (node == container->nil)
    return DS_NOT_FOUND;
  *out = node;
  return DS_OK;
}

typedef struct skip_list_clone_policy {
  ds_skip_list_t *copy;
  ds_skip_list_clone_fn clone;
  ds_skip_list_destroy_fn destroy;
  void *user;
} skip_list_clone_policy_t;
static ds_status_t skip_list_clone_visit(ds_skip_list_node_t *source,
                                         void *user) {
  skip_list_clone_policy_t *policy = (skip_list_clone_policy_t *)user;
  ds_skip_list_node_t *node = NULL;
  ds_status_t st = policy->clone(source, &node, policy->user);
  if (st != DS_OK || node == NULL)
    return st < 0 ? st : DS_ERR_CALLBACK;
  st = ds_skip_list_insert_ex(policy->copy, node);
  if (st != DS_OK) {
    policy->destroy(node, policy->user);
    return st < 0 ? st : DS_ERR_CALLBACK;
  }
  return DS_OK;
}
ds_status_t ds_skip_list_clone_with(const ds_skip_list_t *container,
                                    ds_skip_list_clone_fn clone,
                                    ds_skip_list_destroy_fn destroy,
                                    void *user, ds_context_t *context,
                                    ds_skip_list_t **out) {
  skip_list_clone_policy_t policy;
  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (container == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  policy.copy = skip_list_create_internal(
      container->compare, container->max_level, container->seed,
      context != NULL ? context : container->context, container->compare_with,
      container->compare_user);
  if (policy.copy == NULL)
    return DS_ERR_ALLOC;
  policy.clone = clone;
  policy.destroy = destroy;
  policy.user = user;
  st = ds_skip_list_walk_with(container, skip_list_clone_visit, &policy);
  if (st != DS_OK) {
    ds_skip_list_destroy_with(policy.copy, destroy, user);
    return st;
  }
  *out = policy.copy;
  return DS_OK;
}
