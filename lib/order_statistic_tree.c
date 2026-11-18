#include "order_statistic_tree.h"
#include "legacy_alloc.h"
#include <stdlib.h>

static int ost_compare(ds_ost_t *owner, ds_ost_node_t *a, ds_ost_node_t *b) {
  return owner->compare_with != NULL
             ? owner->compare_with(a, b, owner->compare_user)
             : owner->compare(a, b);
}

static int ost_height(ds_ost_t *tree, ds_ost_node_t *node) {
  return node == tree->nil ? 0 : node->height;
}

static size_t ost_node_size(ds_ost_t *tree, ds_ost_node_t *node) {
  return node == tree->nil ? 0 : node->size;
}

static int ost_max_int(int a, int b) { return a > b ? a : b; }

static void ost_update(ds_ost_t *tree, ds_ost_node_t *node) {
  if (node == tree->nil) {
    return;
  }
  node->size =
      1 + ost_node_size(tree, node->left) + ost_node_size(tree, node->right);
  node->height = 1 + ost_max_int(ost_height(tree, node->left),
                                 ost_height(tree, node->right));
}

static int ost_balance_factor(ds_ost_t *tree, ds_ost_node_t *node) {
  if (node == tree->nil) {
    return 0;
  }
  return ost_height(tree, node->left) - ost_height(tree, node->right);
}

ds_ost_t *ds_ost_create(int (*compare)(ds_ost_node_t *, ds_ost_node_t *)) {
  return ds_ost_create_alloc(compare, malloc, free);
}

static ds_ost_t *
ost_create_internal(int (*compare)(ds_ost_node_t *, ds_ost_node_t *),
                    void *(*allocator)(size_t), void (*deallocator)(void *),
                    ds_context_t *context, ds_ost_compare_fn compare_with,
                    void *compare_user) {
  ds_ost_t *tree;
  if (allocator == NULL || deallocator == NULL ||
      (compare == NULL && compare_with == NULL)) {
    return NULL;
  }
  tree = (ds_ost_t *)DS_LEGACY_ALLOCATE(context, allocator, sizeof(ds_ost_t));
  if (tree == NULL) {
    return NULL;
  }
  tree->compare = compare;
  tree->context = context;
  tree->compare_with = compare_with;
  tree->compare_user = compare_user;
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->size = 0;
  tree->nil = (ds_ost_node_t *)DS_LEGACY_ALLOCATE(context, allocator,
                                                  sizeof(ds_ost_node_t));
  if (tree->nil == NULL) {
    DS_LEGACY_RELEASE(context, deallocator, tree);
    return NULL;
  }
  tree->nil->left = tree->nil;
  tree->nil->right = tree->nil;
  tree->nil->parent = tree->nil;
  tree->nil->size = 0;
  tree->nil->height = 0;
  tree->root = tree->nil;
  return tree;
}
ds_ost_t *ds_ost_create_alloc(int (*compare)(ds_ost_node_t *, ds_ost_node_t *),
                              void *(*allocator)(size_t),
                              void (*deallocator)(void *)) {
  return ost_create_internal(compare, allocator, deallocator, NULL, NULL, NULL);
}
ds_status_t ds_ost_create_context(ds_ost_compare_fn compare, void *user,
                                  ds_context_t *context, ds_ost_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (compare == NULL)
    return DS_ERR_NULL;

  *out = ost_create_internal(NULL, malloc, free,
                             context != NULL ? context : ds_default_context(),
                             compare, user);
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

static void ost_rotate_left(ds_ost_t *tree, ds_ost_node_t *x) {
  ds_ost_node_t *y = x->right;
  x->right = y->left;
  if (y->left != tree->nil) {
    y->left->parent = x;
  }
  y->parent = x->parent;
  if (x->parent == tree->nil) {
    tree->root = y;
  } else if (x == x->parent->left) {
    x->parent->left = y;
  } else {
    x->parent->right = y;
  }
  y->left = x;
  x->parent = y;
  ost_update(tree, x);
  ost_update(tree, y);
}

static void ost_rotate_right(ds_ost_t *tree, ds_ost_node_t *x) {
  ds_ost_node_t *y = x->left;
  x->left = y->right;
  if (y->right != tree->nil) {
    y->right->parent = x;
  }
  y->parent = x->parent;
  if (x->parent == tree->nil) {
    tree->root = y;
  } else if (x == x->parent->right) {
    x->parent->right = y;
  } else {
    x->parent->left = y;
  }
  y->right = x;
  x->parent = y;
  ost_update(tree, x);
  ost_update(tree, y);
}

/* rebalance from node up to the root, updating sizes/heights along the way */
static void ost_rebalance(ds_ost_t *tree, ds_ost_node_t *node) {
  while (node != tree->nil) {
    int balance;
    ost_update(tree, node);
    balance = ost_balance_factor(tree, node);
    if (balance > 1) {
      if (ost_balance_factor(tree, node->left) < 0) {
        ost_rotate_left(tree, node->left);
      }
      ost_rotate_right(tree, node);
      node = node->parent; /* the new subtree root */
    } else if (balance < -1) {
      if (ost_balance_factor(tree, node->right) > 0) {
        ost_rotate_right(tree, node->right);
      }
      ost_rotate_left(tree, node);
      node = node->parent;
    }
    node = node->parent;
  }
}

bool ds_ost_insert(ds_ost_t *tree, ds_ost_node_t *node) {
  ds_ost_node_t *current;
  ds_ost_node_t *parent;
  int cmp = 0;
  bool result = true;
  current = tree->root;
  parent = tree->nil;
  while (current != tree->nil) {
    parent = current;
    cmp = ost_compare(tree, node, current);
    if (cmp == 0) {
      return false;
    }
    current = cmp < 0 ? current->left : current->right;
  }
  node->left = tree->nil;
  node->right = tree->nil;
  node->parent = parent;
  node->size = 1;
  node->height = 1;
  if (parent == tree->nil) {
    tree->root = node;
  } else if (cmp < 0) {
    parent->left = node;
  } else {
    parent->right = node;
  }
  ost_rebalance(tree, parent);
  tree->size += 1;
  return result;
}

static ds_ost_node_t *ost_min_node(ds_ost_t *tree, ds_ost_node_t *node) {
  while (node->left != tree->nil) {
    node = node->left;
  }
  return node;
}

ds_ost_node_t *ds_ost_search(ds_ost_t *tree, ds_ost_node_t *key) {
  ds_ost_node_t *current;
  current = tree->root;
  while (current != tree->nil) {
    int cmp = ost_compare(tree, key, current);
    if (cmp == 0) {
      break;
    }
    current = cmp < 0 ? current->left : current->right;
  }
  return current;
}

/* transplant: replace subtree u with subtree v */
static void ost_transplant(ds_ost_t *tree, ds_ost_node_t *u, ds_ost_node_t *v) {
  if (u->parent == tree->nil) {
    tree->root = v;
  } else if (u == u->parent->left) {
    u->parent->left = v;
  } else {
    u->parent->right = v;
  }
  if (v != tree->nil) {
    v->parent = u->parent;
  }
}

bool ds_ost_remove(ds_ost_t *tree, ds_ost_node_t *key,
                   ds_ost_node_t **out_node) {
  ds_ost_node_t *z;
  ds_ost_node_t *fixup_from;
  bool result = false;
  z = tree->root;
  while (z != tree->nil) {
    int cmp = ost_compare(tree, key, z);
    if (cmp == 0) {
      break;
    }
    z = cmp < 0 ? z->left : z->right;
  }
  if (z == tree->nil) {
    return false;
  }
  if (out_node != NULL) {
    *out_node = z;
  }
  if (z->left == tree->nil) {
    fixup_from = z->parent;
    ost_transplant(tree, z, z->right);
  } else if (z->right == tree->nil) {
    fixup_from = z->parent;
    ost_transplant(tree, z, z->left);
  } else {
    ds_ost_node_t *y = ost_min_node(tree, z->right);
    if (y->parent == z) {
      fixup_from = y;
    } else {
      fixup_from = y->parent;
      ost_transplant(tree, y, y->right);
      y->right = z->right;
      y->right->parent = y;
    }
    ost_transplant(tree, z, y);
    y->left = z->left;
    y->left->parent = y;
  }
  ost_rebalance(tree, fixup_from);
  tree->size -= 1;
  result = true;
  return result;
}

ds_ost_node_t *ds_ost_select(ds_ost_t *tree, size_t k) {
  ds_ost_node_t *current;
  ds_ost_node_t *result;
  result = tree->nil;
  current = tree->root;
  if (k < tree->size) {
    while (current != tree->nil) {
      size_t left_size = ost_node_size(tree, current->left);
      if (k < left_size) {
        current = current->left;
      } else if (k > left_size) {
        k -= left_size + 1;
        current = current->right;
      } else {
        result = current;
        break;
      }
    }
  }
  return result;
}

size_t ds_ost_rank(ds_ost_t *tree, ds_ost_node_t *key) {
  ds_ost_node_t *current;
  size_t rank = 0;
  current = tree->root;
  while (current != tree->nil) {
    int cmp = ost_compare(tree, key, current);
    if (cmp <= 0) {
      current = current->left;
    } else {
      rank += ost_node_size(tree, current->left) + 1;
      current = current->right;
    }
  }
  return rank;
}

ds_ost_node_t *ds_ost_minimum(ds_ost_t *tree) {
  ds_ost_node_t *result;
  result = tree->root == tree->nil ? tree->nil : ost_min_node(tree, tree->root);
  return result;
}

ds_ost_node_t *ds_ost_maximum(ds_ost_t *tree) {
  ds_ost_node_t *current;
  current = tree->root;
  if (current != tree->nil) {
    while (current->right != tree->nil) {
      current = current->right;
    }
  }
  return current;
}

size_t ds_ost_size(ds_ost_t *tree) {
  size_t result;
  result = tree->size;
  return result;
}

bool ds_ost_is_empty(ds_ost_t *tree) {
  bool result;
  result = tree->size == 0;
  return result;
}

static void ost_walk_recursive(ds_ost_t *tree, ds_ost_node_t *node,
                               void (*callback)(ds_ost_node_t *)) {
  if (node == tree->nil) {
    return;
  }
  ost_walk_recursive(tree, node->left, callback);
  callback(node);
  ost_walk_recursive(tree, node->right, callback);
}

void ds_ost_inorder_walk(ds_ost_t *tree, void (*callback)(ds_ost_node_t *)) {
  ost_walk_recursive(tree, tree->root, callback);
}

static void ost_destroy_recursive(ds_ost_t *tree, ds_ost_node_t *node,
                                  ds_ost_destroy_fn destroy, void *user) {
  if (node == tree->nil) {
    return;
  }
  ost_destroy_recursive(tree, node->left, destroy, user);
  ost_destroy_recursive(tree, node->right, destroy, user);
  if (destroy != NULL) {
    destroy(node, user);
  }
}

static void ost_destroy_internal(ds_ost_t *tree, ds_ost_destroy_fn destroy,
                                 void *user) {
  ost_destroy_recursive(tree, tree->root, destroy, user);
  DS_LEGACY_FREE(tree, tree->nil);
  DS_LEGACY_FREE(tree, tree);
}
void ds_ost_destroy_with(ds_ost_t *container, ds_ost_destroy_fn destroy,
                         void *user) {
  if (container != NULL)
    ost_destroy_internal(container, destroy, user);
}
static void ost_destroy_legacy(ds_ost_node_t *node, void *user) {
  void (**destroy)(ds_ost_node_t *) = (void (**)(ds_ost_node_t *))user;
  (*destroy)(node);
}
void ds_ost_destroy(ds_ost_t *container, void (*destroy)(ds_ost_node_t *)) {
  ds_ost_destroy_with(container, destroy != NULL ? ost_destroy_legacy : NULL,
                      &destroy);
}

ds_status_t ds_ost_walk_with(const ds_ost_t *container, ds_ost_visit_fn visit,
                             void *user) {
  ds_ost_node_t *node;
  ds_status_t st;
  if (container == NULL || visit == NULL)
    return DS_ERR_NULL;
  node = container->root;
  if (node == container->nil)
    return DS_OK;
  while (node->left != container->nil)
    node = node->left;
  while (node != container->nil) {
    st = visit(node, user);
    if (st != DS_OK)
      return st;
    if (node->right != container->nil) {
      node = node->right;
      while (node->left != container->nil)
        node = node->left;
    } else {
      while (node->parent != container->nil && node == node->parent->right)
        node = node->parent;
      node = node->parent;
    }
  }
  return DS_OK;
}

ds_status_t ds_ost_insert_ex(ds_ost_t *container, ds_ost_node_t *node) {

  if (container == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == container->nil)
    return DS_ERR_INVALID;
  if (container->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  return ds_ost_insert(container, node) ? DS_OK : DS_EXISTS;
}

ds_status_t ds_ost_find_ex(ds_ost_t *container, ds_ost_node_t *key,
                           ds_ost_node_t **out) {
  ds_ost_node_t *node;
  if (container == NULL || key == NULL || out == NULL)
    return DS_ERR_NULL;
  if (key == container->nil)
    return DS_ERR_INVALID;
  node = ds_ost_search(container, key);
  if (node == container->nil)
    return DS_NOT_FOUND;
  *out = node;
  return DS_OK;
}

ds_status_t ds_ost_remove_ex(ds_ost_t *container, ds_ost_node_t *key,
                             ds_ost_node_t **out) {
  ds_ost_node_t *node;
  if (container == NULL || key == NULL || out == NULL)
    return DS_ERR_NULL;
  if (key == container->nil)
    return DS_ERR_INVALID;
  if (!ds_ost_remove(container, key, &node))
    return DS_NOT_FOUND;
  *out = node;
  return DS_OK;
}

typedef struct ost_clone_policy {
  ds_ost_t *copy;
  ds_ost_clone_fn clone;
  ds_ost_destroy_fn destroy;
  void *user;
} ost_clone_policy_t;
static ds_status_t ost_clone_visit(ds_ost_node_t *source, void *user) {
  ost_clone_policy_t *policy = (ost_clone_policy_t *)user;
  ds_ost_node_t *node = NULL;
  ds_status_t st = policy->clone(source, &node, policy->user);
  if (st != DS_OK || node == NULL)
    return st < 0 ? st : DS_ERR_CALLBACK;
  st = ds_ost_insert_ex(policy->copy, node);
  if (st != DS_OK) {
    policy->destroy(node, policy->user);
    return st < 0 ? st : DS_ERR_CALLBACK;
  }
  return DS_OK;
}
ds_status_t ds_ost_clone_with(const ds_ost_t *container, ds_ost_clone_fn clone,
                              ds_ost_destroy_fn destroy, void *user,
                              ds_context_t *context, ds_ost_t **out) {
  ost_clone_policy_t policy;
  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (container == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  policy.copy = ost_create_internal(
      container->compare, container->allocator, container->deallocator,
      context != NULL ? context : container->context, container->compare_with,
      container->compare_user);
  if (policy.copy == NULL)
    return DS_ERR_ALLOC;
  policy.clone = clone;
  policy.destroy = destroy;
  policy.user = user;
  st = ds_ost_walk_with(container, ost_clone_visit, &policy);
  if (st != DS_OK) {
    ds_ost_destroy_with(policy.copy, destroy, user);
    return st;
  }
  *out = policy.copy;
  return DS_OK;
}

ds_status_t ds_ost_select_ex(ds_ost_t *tree, size_t rank, ds_ost_node_t **out) {
  if (tree == NULL || out == NULL)
    return DS_ERR_NULL;
  if (rank >= tree->size)
    return DS_ERR_RANGE;
  *out = ds_ost_select(tree, rank);
  return DS_OK;
}
ds_status_t ds_ost_rank_ex(ds_ost_t *tree, ds_ost_node_t *key, size_t *out) {
  if (tree == NULL || key == NULL || out == NULL)
    return DS_ERR_NULL;
  if (key == tree->nil)
    return DS_ERR_INVALID;
  *out = ds_ost_rank(tree, key);
  return DS_OK;
}
