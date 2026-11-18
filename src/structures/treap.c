#include <dsa/structures/treap.h>
#include <stdlib.h>
#include <string.h>

static void *tr_alloc(ds_treap_t *t, size_t n) {
  return t->use_context ? ds_context_alloc(t->context, n) : t->allocator(n);
}
static void tr_free(ds_treap_t *t, void *p) {
  if (t->use_context)
    ds_context_free(t->context, p);
  else if (p != NULL)
    t->deallocator(p);
}
static int tr_compare(ds_treap_t *t, ds_treap_node_t *a, ds_treap_node_t *b) {
  return t->compare_with != NULL ? t->compare_with(a, b, t->user)
                                 : t->compare(a, b);
}

static ds_treap_t *
tr_create(int (*compare)(ds_treap_node_t *, ds_treap_node_t *),
          unsigned long seed, void *(*allocator)(size_t),
          void (*deallocator)(void *), const ds_treap_config_t *config) {
  ds_treap_t *t;
  if (allocator == NULL || deallocator == NULL ||
      (compare == NULL && config == NULL))
    return NULL;
  t = (ds_treap_t *)(config == NULL
                         ? allocator(sizeof(*t))
                         : ds_context_alloc(config->context, sizeof(*t)));
  if (t == NULL)
    return NULL;
  memset(t, 0, sizeof(*t));
  t->allocator = allocator;
  t->deallocator = deallocator;
  t->compare = compare;
  t->seed = seed & 0xffffffffUL;
  if (t->seed == 0)
    t->seed = 0x9e3779b9UL;
  if (config != NULL) {
    t->use_context = true;
    t->context = config->context;
    t->compare_with = config->compare;
    t->user = config->user;
    t->priority = config->priority;
    t->priority_user = config->priority_user;
  }
  t->nil = (ds_treap_node_t *)tr_alloc(t, sizeof(*t->nil));
  if (t->nil == NULL) {
    tr_free(t, t);
    return NULL;
  }
  t->nil->left = t->nil;
  t->nil->right = t->nil;
  t->nil->parent = t->nil;
  t->nil->priority = 0;
  t->root = t->nil;
  return t;
}

void ds_treap_config_init(ds_treap_config_t *config) {
  if (config != NULL)
    memset(config, 0, sizeof(*config));
}

ds_status_t ds_treap_create_with(const ds_treap_config_t *config,
                                 ds_treap_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (config == NULL || config->compare == NULL)
    return DS_ERR_NULL;
  *out = tr_create(NULL, config->seed, malloc, free, config);
  return *out == NULL ? DS_ERR_ALLOC : DS_OK;
}

ds_treap_t *ds_treap_create(int (*compare)(ds_treap_node_t *,
                                           ds_treap_node_t *)) {
  return ds_treap_create_alloc(compare, 0UL, malloc, free);
}

ds_treap_t *
ds_treap_create_alloc(int (*compare)(ds_treap_node_t *, ds_treap_node_t *),
                      unsigned long seed, void *(*allocator)(size_t),
                      void (*deallocator)(void *)) {
  return tr_create(compare, seed, allocator, deallocator, NULL);
}

static unsigned long treap_random(ds_treap_t *t) {
  unsigned long x;
  if (t->priority != NULL)
    return t->priority(t->priority_user);
  x = t->seed;
  x ^= (x << 13) & 0xffffffffUL;
  x ^= x >> 17;
  x ^= (x << 5) & 0xffffffffUL;
  t->seed = x & 0xffffffffUL;
  return t->seed;
}

void ds_treap_node_init(ds_treap_node_t *node) {
  node->left = NULL;
  node->right = NULL;
  node->parent = NULL;
  node->priority = 0UL;
}

static void treap_left_rotate(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *right = node->right;
  node->right = right->left;
  if (right->left != treap->nil) {
    right->left->parent = node;
  }
  right->parent = node->parent;
  if (node->parent == treap->nil) {
    treap->root = right;
  } else if (node == node->parent->left) {
    node->parent->left = right;
  } else {
    node->parent->right = right;
  }
  right->left = node;
  node->parent = right;
}

static void treap_right_rotate(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *left = node->left;
  node->left = left->right;
  if (left->right != treap->nil) {
    left->right->parent = node;
  }
  left->parent = node->parent;
  if (node->parent == treap->nil) {
    treap->root = left;
  } else if (node == node->parent->right) {
    node->parent->right = left;
  } else {
    node->parent->left = left;
  }
  left->right = node;
  node->parent = left;
}

ds_status_t ds_treap_insert_ex(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *current;
  ds_treap_node_t *parent;
  int cmp;
  if (treap == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == treap->nil)
    return DS_ERR_INVALID;
  current = treap->root;
  parent = treap->nil;
  cmp = 0;
  while (current != treap->nil) {
    parent = current;
    cmp = tr_compare(treap, node, current);
    if (cmp < 0) {
      current = current->left;
    } else if (cmp > 0) {
      current = current->right;
    } else {
      return DS_EXISTS;
    }
  }
  if (treap->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  node->parent = parent;
  node->left = treap->nil;
  node->right = treap->nil;
  node->priority = treap_random(treap);
  if (parent == treap->nil) {
    treap->root = node;
  } else if (cmp < 0) {
    parent->left = node;
  } else {
    parent->right = node;
  }
  while (node->parent != treap->nil &&
         node->parent->priority < node->priority) {
    if (node == node->parent->left) {
      treap_right_rotate(treap, node->parent);
    } else {
      treap_left_rotate(treap, node->parent);
    }
  }
  treap->size += 1;
  return DS_OK;
}

void ds_treap_insert(ds_treap_t *treap, ds_treap_node_t *node) {
  (void)ds_treap_insert_ex(treap, node);
}

ds_treap_node_t *ds_treap_search(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *current;
  current = treap->root;
  while (current != treap->nil) {
    int cmp = tr_compare(treap, node, current);
    if (cmp < 0) {
      current = current->left;
    } else if (cmp > 0) {
      current = current->right;
    } else {
      return current;
    }
  }
  return treap->nil;
}

ds_treap_node_t *ds_treap_minimum(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *current;
  current = node;
  if (current == treap->nil) {
    return treap->nil;
  }
  while (current->left != treap->nil) {
    current = current->left;
  }
  return current;
}

ds_treap_node_t *ds_treap_maximum(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *current;
  current = node;
  if (current == treap->nil) {
    return treap->nil;
  }
  while (current->right != treap->nil) {
    current = current->right;
  }
  return current;
}

ds_treap_node_t *ds_treap_remove(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *target;
  target = ds_treap_search(treap, node);
  if (target == treap->nil) {
    return treap->nil;
  }
  while (target->left != treap->nil || target->right != treap->nil) {
    if (target->left == treap->nil) {
      treap_left_rotate(treap, target);
    } else if (target->right == treap->nil) {
      treap_right_rotate(treap, target);
    } else if (target->left->priority > target->right->priority) {
      treap_right_rotate(treap, target);
    } else {
      treap_left_rotate(treap, target);
    }
  }
  if (target->parent == treap->nil) {
    treap->root = treap->nil;
  } else if (target == target->parent->left) {
    target->parent->left = treap->nil;
  } else {
    target->parent->right = treap->nil;
  }
  target->left = treap->nil;
  target->right = treap->nil;
  target->parent = treap->nil;
  treap->size -= 1;
  return target;
}

bool ds_treap_is_empty(ds_treap_t *treap) {
  bool result;
  result = treap->root == treap->nil;
  return result;
}

void ds_treap_destroy_node(ds_treap_t *treap, ds_treap_node_t *node,
                           void (*destroy)(ds_treap_node_t *)) {
  ds_treap_node_t *removed;
  removed = ds_treap_remove(treap, node);
  if (removed != treap->nil && destroy != NULL) {
    destroy(removed);
  }
}

/* In-order successor constrained to the starting subtree's parent. */
static ds_treap_node_t *tr_next(const ds_treap_t *t, ds_treap_node_t *n,
                                ds_treap_node_t *stop) {
  if (n->right != t->nil) {
    n = n->right;
    while (n->left != t->nil)
      n = n->left;
    return n;
  }
  while (n->parent != stop && n == n->parent->right)
    n = n->parent;
  return n->parent == stop ? t->nil : n->parent;
}

void ds_treap_inorder_walk(ds_treap_t *t, ds_treap_node_t *node,
                           void (*callback)(ds_treap_node_t *)) {
  ds_treap_node_t *stop;
  if (t == NULL || node == NULL || node == t->nil || callback == NULL)
    return;
  stop = node->parent;
  while (node->left != t->nil)
    node = node->left;
  while (node != t->nil) {
    callback(node);
    node = tr_next(t, node, stop);
  }
}

void ds_treap_inorder_walk_tree(ds_treap_t *t,
                                void (*callback)(ds_treap_node_t *)) {
  if (t != NULL)
    ds_treap_inorder_walk(t, t->root, callback);
}

ds_status_t ds_treap_walk_with(ds_treap_t *t, ds_treap_visit_fn visit,
                               void *user) {
  ds_treap_node_t *node;
  if (t == NULL || visit == NULL)
    return DS_ERR_NULL;
  node = t->root;
  if (node != t->nil)
    while (node->left != t->nil)
      node = node->left;
  while (node != t->nil) {
    ds_status_t st = visit(node, user);
    if (st != DS_OK)
      return st;
    node = tr_next(t, node, t->nil);
  }
  return DS_OK;
}

static void tr_destroy(ds_treap_t *t, void (*direct)(ds_treap_node_t *),
                       ds_treap_destroy_fn destroy, void *user) {
  ds_treap_node_t *node, *parent;
  if (t == NULL)
    return;
  node = t->root;
  while (node != t->nil) {
    if (node->left != t->nil) {
      node = node->left;
      continue;
    }
    if (node->right != t->nil) {
      node = node->right;
      continue;
    }
    parent = node->parent;
    if (parent != t->nil) {
      if (parent->left == node)
        parent->left = t->nil;
      else
        parent->right = t->nil;
    }
    node->parent = node->left = node->right = NULL;
    if (destroy != NULL)
      destroy(node, user);
    else if (direct != NULL)
      direct(node);
    node = parent;
  }
  tr_free(t, t->nil);
  tr_free(t, t);
}

void ds_treap_destroy(ds_treap_t *t, void (*destroy)(ds_treap_node_t *)) {
  tr_destroy(t, destroy, NULL, NULL);
}
void ds_treap_destroy_with(ds_treap_t *t, ds_treap_destroy_fn destroy,
                           void *user) {
  tr_destroy(t, NULL, destroy, user);
}

typedef struct tr_clone_callbacks {
  ds_treap_node_t *(*direct)(ds_treap_node_t *);
  void (*release)(ds_treap_node_t *);
  ds_treap_clone_fn clone;
  ds_treap_destroy_fn destroy;
  void *user;
} tr_clone_callbacks_t;

static ds_status_t tr_copy_node(const ds_treap_t *source, ds_treap_t *dest,
                                ds_treap_node_t *node, ds_treap_node_t *parent,
                                const tr_clone_callbacks_t *callbacks,
                                ds_treap_node_t **out) {
  ds_treap_node_t *copy = NULL;
  ds_status_t st = DS_OK;
  if (callbacks->clone != NULL)
    st = callbacks->clone(node, callbacks->user, &copy);
  else
    copy = callbacks->direct(node);
  if (st != DS_OK)
    return st < DS_OK ? st : DS_ERR_CALLBACK;
  if (copy == NULL || copy == node || copy == source->nil || copy == dest->nil)
    return DS_ERR_CALLBACK;
  copy->priority = node->priority;
  copy->left = copy->right = dest->nil;
  copy->parent = parent;
  *out = copy;
  dest->size++;
  return DS_OK;
}

static ds_status_t tr_clone(const ds_treap_t *t,
                            const tr_clone_callbacks_t *callbacks,
                            ds_treap_t **out) {
  ds_treap_t *copy;
  ds_treap_node_t *src, *dst;
  ds_treap_config_t config;
  ds_status_t st;
  ds_treap_config_init(&config);
  config.compare = t->compare_with;
  config.user = t->user;
  config.context = t->context;
  config.priority = t->priority;
  config.priority_user = t->priority_user;
  copy = tr_create(t->compare, t->seed, t->allocator, t->deallocator,
                   t->use_context ? &config : NULL);
  if (copy == NULL)
    return DS_ERR_ALLOC;
  if (t->root == t->nil) {
    *out = copy;
    return DS_OK;
  }
  st = tr_copy_node(t, copy, t->root, copy->nil, callbacks, &copy->root);
  src = t->root;
  dst = copy->root;
  while (st == DS_OK) {
    if (src->left != t->nil && dst->left == copy->nil) {
      st = tr_copy_node(t, copy, src->left, dst, callbacks, &dst->left);
      if (st != DS_OK)
        break;
      src = src->left;
      dst = dst->left;
    } else if (src->right != t->nil && dst->right == copy->nil) {
      st = tr_copy_node(t, copy, src->right, dst, callbacks, &dst->right);
      if (st != DS_OK)
        break;
      src = src->right;
      dst = dst->right;
    } else {
      src = src->parent;
      dst = dst->parent;
      if (src == t->nil)
        break;
    }
  }
  if (st != DS_OK) {
    tr_destroy(copy, callbacks->release, callbacks->destroy, callbacks->user);
    return st;
  }
  *out = copy;
  return DS_OK;
}

ds_status_t ds_treap_clone_with(const ds_treap_t *t, ds_treap_clone_fn clone,
                                ds_treap_destroy_fn destroy, void *user,
                                ds_treap_t **out) {
  tr_clone_callbacks_t callbacks;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (t == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.clone = clone;
  callbacks.destroy = destroy;
  callbacks.user = user;
  return tr_clone(t, &callbacks, out);
}

ds_treap_t *ds_treap_clone(ds_treap_t *t,
                           ds_treap_node_t *(*clone)(ds_treap_node_t *)) {
  ds_treap_t *out = NULL;
  tr_clone_callbacks_t callbacks;
  if (t == NULL || clone == NULL)
    return NULL;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.direct = clone;
  (void)tr_clone(t, &callbacks, &out);
  return out;
}

ds_status_t ds_treap_clone_ex(const ds_treap_t *t,
                              ds_treap_node_t *(*clone)(ds_treap_node_t *),
                              void (*destroy)(ds_treap_node_t *),
                              ds_treap_t **out) {
  tr_clone_callbacks_t callbacks;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (t == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.direct = clone;
  callbacks.release = destroy;
  return tr_clone(t, &callbacks, out);
}

ds_status_t ds_treap_find_ex(ds_treap_t *t, ds_treap_node_t *key,
                             ds_treap_node_t **out) {
  ds_treap_node_t *node;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (t == NULL || key == NULL)
    return DS_ERR_NULL;
  if (key == t->nil)
    return DS_ERR_INVALID;
  node = ds_treap_search(t, key);
  if (node == t->nil)
    return DS_NOT_FOUND;
  *out = node;
  return DS_OK;
}

ds_status_t ds_treap_remove_ex(ds_treap_t *t, ds_treap_node_t *key,
                               ds_treap_node_t **out) {
  ds_treap_node_t *node;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (t == NULL || key == NULL)
    return DS_ERR_NULL;
  if (key == t->nil)
    return DS_ERR_INVALID;
  node = ds_treap_remove(t, key);
  if (node == t->nil)
    return DS_NOT_FOUND;
  *out = node;
  return DS_OK;
}
