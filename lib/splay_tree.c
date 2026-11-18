#include "splay_tree.h"
#include "legacy_alloc.h"
#include <stdlib.h>

static int splay_tree_compare(ds_splay_tree_t *owner, ds_splay_tree_node_t *a,
                              ds_splay_tree_node_t *b) {
  return owner->compare_with != NULL
             ? owner->compare_with(a, b, owner->compare_user)
             : owner->compare(a, b);
}

ds_splay_tree_t *ds_splay_tree_create(int (*compare)(ds_splay_tree_node_t *,
                                                     ds_splay_tree_node_t *)) {
  return ds_splay_tree_create_alloc(compare, malloc, free);
}

static ds_splay_tree_t *splay_tree_create_internal(
    int (*compare)(ds_splay_tree_node_t *, ds_splay_tree_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *),
    ds_context_t *context, ds_splay_tree_compare_fn compare_with,
    void *compare_user) {
  ds_splay_tree_t *tree;
  if (allocator == NULL || deallocator == NULL ||
      (compare == NULL && compare_with == NULL)) {
    return NULL;
  }
  tree = (ds_splay_tree_t *)DS_LEGACY_ALLOCATE(context, allocator,
                                               sizeof(ds_splay_tree_t));
  if (tree == NULL) {
    return NULL;
  }
  tree->context = context;
  tree->compare_with = compare_with;
  tree->compare_user = compare_user;
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->compare = compare;
  tree->nil = (ds_splay_tree_node_t *)DS_LEGACY_ALLOCATE(
      context, allocator, sizeof(ds_splay_tree_node_t));
  if (tree->nil == NULL) {
    DS_LEGACY_RELEASE(context, deallocator, tree);
    return NULL;
  }
  tree->nil->left = tree->nil;
  tree->nil->right = tree->nil;
  tree->nil->parent = tree->nil;
  tree->root = tree->nil;
  tree->size = 0;
  return tree;
}
ds_splay_tree_t *ds_splay_tree_create_alloc(
    int (*compare)(ds_splay_tree_node_t *, ds_splay_tree_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *)) {
  return splay_tree_create_internal(compare, allocator, deallocator, NULL, NULL,
                                    NULL);
}
ds_status_t ds_splay_tree_create_context(ds_splay_tree_compare_fn compare,
                                         void *user, ds_context_t *context,
                                         ds_splay_tree_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (compare == NULL)
    return DS_ERR_NULL;

  *out = splay_tree_create_internal(
      NULL, malloc, free, context != NULL ? context : ds_default_context(),
      compare, user);
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

static void splay_tree_left_rotate(ds_splay_tree_t *tree,
                                   ds_splay_tree_node_t *node) {
  ds_splay_tree_node_t *right = node->right;
  node->right = right->left;
  if (right->left != tree->nil) {
    right->left->parent = node;
  }
  right->parent = node->parent;
  if (node->parent == tree->nil) {
    tree->root = right;
  } else if (node == node->parent->left) {
    node->parent->left = right;
  } else {
    node->parent->right = right;
  }
  right->left = node;
  node->parent = right;
}

static void splay_tree_right_rotate(ds_splay_tree_t *tree,
                                    ds_splay_tree_node_t *node) {
  ds_splay_tree_node_t *left = node->left;
  node->left = left->right;
  if (left->right != tree->nil) {
    left->right->parent = node;
  }
  left->parent = node->parent;
  if (node->parent == tree->nil) {
    tree->root = left;
  } else if (node == node->parent->right) {
    node->parent->right = left;
  } else {
    node->parent->left = left;
  }
  left->right = node;
  node->parent = left;
}

static void splay_tree_splay(ds_splay_tree_t *tree,
                             ds_splay_tree_node_t *node) {
  while (node->parent != tree->nil) {
    ds_splay_tree_node_t *parent = node->parent;
    ds_splay_tree_node_t *grand = parent->parent;
    if (grand == tree->nil) {
      if (node == parent->left) {
        splay_tree_right_rotate(tree, parent);
      } else {
        splay_tree_left_rotate(tree, parent);
      }
    } else if (node == parent->left && parent == grand->left) {
      splay_tree_right_rotate(tree, grand);
      splay_tree_right_rotate(tree, parent);
    } else if (node == parent->right && parent == grand->right) {
      splay_tree_left_rotate(tree, grand);
      splay_tree_left_rotate(tree, parent);
    } else if (node == parent->right && parent == grand->left) {
      splay_tree_left_rotate(tree, parent);
      splay_tree_right_rotate(tree, grand);
    } else {
      splay_tree_right_rotate(tree, parent);
      splay_tree_left_rotate(tree, grand);
    }
  }
}

void ds_splay_tree_insert(ds_splay_tree_t *tree, ds_splay_tree_node_t *node) {
  ds_splay_tree_node_t *current;
  ds_splay_tree_node_t *parent;
  int cmp;
  if (node == tree->nil) {
    return;
  }
  current = tree->root;
  parent = tree->nil;
  cmp = 0;
  while (current != tree->nil) {
    parent = current;
    cmp = splay_tree_compare(tree, node, current);
    if (cmp < 0) {
      current = current->left;
    } else if (cmp > 0) {
      current = current->right;
    } else {
      splay_tree_splay(tree, current);
      return;
    }
  }
  node->parent = parent;
  node->left = tree->nil;
  node->right = tree->nil;
  if (parent == tree->nil) {
    tree->root = node;
  } else if (cmp < 0) {
    parent->left = node;
  } else {
    parent->right = node;
  }
  splay_tree_splay(tree, node);
  tree->size += 1;
}

ds_splay_tree_node_t *ds_splay_tree_search(ds_splay_tree_t *tree,
                                           ds_splay_tree_node_t *node) {
  ds_splay_tree_node_t *current;
  ds_splay_tree_node_t *last;
  current = tree->root;
  last = tree->nil;
  while (current != tree->nil) {
    int cmp = splay_tree_compare(tree, node, current);
    last = current;
    if (cmp < 0) {
      current = current->left;
    } else if (cmp > 0) {
      current = current->right;
    } else {
      splay_tree_splay(tree, current);
      return current;
    }
  }
  if (last != tree->nil) {
    splay_tree_splay(tree, last);
  }
  return tree->nil;
}

ds_splay_tree_node_t *ds_splay_tree_minimum(ds_splay_tree_t *tree,
                                            ds_splay_tree_node_t *node) {
  ds_splay_tree_node_t *current;
  current = node;
  if (current == tree->nil) {
    return tree->nil;
  }
  while (current->left != tree->nil) {
    current = current->left;
  }
  return current;
}

ds_splay_tree_node_t *ds_splay_tree_maximum(ds_splay_tree_t *tree,
                                            ds_splay_tree_node_t *node) {
  ds_splay_tree_node_t *current;
  current = node;
  if (current == tree->nil) {
    return tree->nil;
  }
  while (current->right != tree->nil) {
    current = current->right;
  }
  return current;
}

ds_splay_tree_node_t *ds_splay_tree_remove(ds_splay_tree_t *tree,
                                           ds_splay_tree_node_t *node) {
  ds_splay_tree_node_t *target;
  ds_splay_tree_node_t *left_subtree;
  ds_splay_tree_node_t *right_subtree;
  target = ds_splay_tree_search(tree, node);
  if (target == tree->nil) {
    return tree->nil;
  }
  /* target is now the root after the search splay */
  left_subtree = tree->root->left;
  right_subtree = tree->root->right;
  if (left_subtree != tree->nil) {
    ds_splay_tree_node_t *max_left;
    left_subtree->parent = tree->nil;
    max_left = left_subtree;
    while (max_left->right != tree->nil) {
      max_left = max_left->right;
    }
    tree->root = left_subtree;
    splay_tree_splay(tree, max_left);
    tree->root->right = right_subtree;
    if (right_subtree != tree->nil) {
      right_subtree->parent = tree->root;
    }
  } else {
    tree->root = right_subtree;
    if (right_subtree != tree->nil) {
      right_subtree->parent = tree->nil;
    }
  }
  target->left = tree->nil;
  target->right = tree->nil;
  target->parent = tree->nil;
  tree->size -= 1;
  return target;
}

bool ds_splay_tree_is_empty(ds_splay_tree_t *tree) {
  bool result;
  result = tree->root == tree->nil;
  return result;
}

void ds_splay_tree_destroy_node(ds_splay_tree_t *tree,
                                ds_splay_tree_node_t *node,
                                void (*destroy)(ds_splay_tree_node_t *)) {
  ds_splay_tree_node_t *removed;
  removed = ds_splay_tree_remove(tree, node);
  if (removed != tree->nil && destroy != NULL) {
    destroy(removed);
  }
}

/* Walks follow the parent links in O(1) space: a splay tree can be as
 * deep as it is large (after a sequential scan, say). */
#define SPLAY_FROM_ABOVE 0
#define SPLAY_FROM_LEFT 1
#define SPLAY_FROM_RIGHT 2

/* where a walk goes after leaving x: its parent and the side x is on */
static int splay_leave(ds_splay_tree_node_t *x, ds_splay_tree_node_t *root,
                       ds_splay_tree_node_t **parent) {
  *parent = x->parent;
  if (x == root) {
    return SPLAY_FROM_ABOVE;
  }
  return x == x->parent->left ? SPLAY_FROM_LEFT : SPLAY_FROM_RIGHT;
}

void ds_splay_tree_inorder_walk(ds_splay_tree_t *tree,
                                ds_splay_tree_node_t *node,
                                void (*callback)(ds_splay_tree_node_t *)) {
  ds_splay_tree_node_t *x = node;
  int from = SPLAY_FROM_ABOVE;
  if (node == tree->nil) {
    return;
  }
  for (;;) {
    ds_splay_tree_node_t *p;
    if (from == SPLAY_FROM_ABOVE && x->left != tree->nil) {
      x = x->left;
      continue;
    }
    if (from != SPLAY_FROM_RIGHT) {
      callback(x);
      if (x->right != tree->nil) {
        x = x->right;
        from = SPLAY_FROM_ABOVE;
        continue;
      }
    }
    from = splay_leave(x, node, &p);
    if (from == SPLAY_FROM_ABOVE) {
      return;
    }
    x = p;
  }
}

void ds_splay_tree_inorder_walk_tree(ds_splay_tree_t *tree,
                                     void (*callback)(ds_splay_tree_node_t *)) {
  ds_splay_tree_inorder_walk(tree, tree->root, callback);
}

/* post-order: each node is released after both of its subtrees */
static void splay_tree_destroy_all(ds_splay_tree_t *tree,
                                   ds_splay_tree_node_t *node,
                                   ds_splay_tree_destroy_fn destroy,
                                   void *user) {
  ds_splay_tree_node_t *x = node;
  int from = SPLAY_FROM_ABOVE;
  if (node == tree->nil || destroy == NULL) {
    return;
  }
  for (;;) {
    ds_splay_tree_node_t *p;
    if (from == SPLAY_FROM_ABOVE && x->left != tree->nil) {
      x = x->left;
      continue;
    }
    if (from != SPLAY_FROM_RIGHT && x->right != tree->nil) {
      x = x->right;
      from = SPLAY_FROM_ABOVE;
      continue;
    }
    from = splay_leave(x, node, &p);
    destroy(x, user);
    if (from == SPLAY_FROM_ABOVE) {
      return;
    }
    x = p;
  }
}

static void splay_tree_destroy_internal(ds_splay_tree_t *tree,
                                        ds_splay_tree_destroy_fn destroy,
                                        void *user) {
  splay_tree_destroy_all(tree, tree->root, destroy, user);
  DS_LEGACY_FREE(tree, tree->nil);
  DS_LEGACY_FREE(tree, tree);
}
void ds_splay_tree_destroy_with(ds_splay_tree_t *container,
                                ds_splay_tree_destroy_fn destroy, void *user) {
  if (container != NULL)
    splay_tree_destroy_internal(container, destroy, user);
}
static void splay_tree_destroy_legacy(ds_splay_tree_node_t *node, void *user) {
  void (**destroy)(ds_splay_tree_node_t *) =
      (void (**)(ds_splay_tree_node_t *))user;
  (*destroy)(node);
}
void ds_splay_tree_destroy(ds_splay_tree_t *container,
                           void (*destroy)(ds_splay_tree_node_t *)) {
  ds_splay_tree_destroy_with(
      container, destroy != NULL ? splay_tree_destroy_legacy : NULL, &destroy);
}

/* clones in key order: each insertion into the new tree is a splay at an
 * end, O(1) amortized. false when clone_node returns NULL; the clones
 * made so far are in new_tree. */
static bool splay_tree_clone_all(
    const ds_splay_tree_t *tree, ds_splay_tree_t *new_tree,
    ds_splay_tree_node_t *node,
    ds_splay_tree_node_t *(*clone_node)(ds_splay_tree_node_t *)) {
  ds_splay_tree_node_t *x = node;
  int from = SPLAY_FROM_ABOVE;
  if (node == tree->nil) {
    return true;
  }
  for (;;) {
    ds_splay_tree_node_t *p;
    if (from == SPLAY_FROM_ABOVE && x->left != tree->nil) {
      x = x->left;
      continue;
    }
    if (from != SPLAY_FROM_RIGHT) {
      ds_splay_tree_node_t *copy = clone_node(x);
      if (copy == NULL) {
        return false;
      }
      ds_splay_tree_insert(new_tree, copy);
      if (x->right != tree->nil) {
        x = x->right;
        from = SPLAY_FROM_ABOVE;
        continue;
      }
    }
    from = splay_leave(x, node, &p);
    if (from == SPLAY_FROM_ABOVE) {
      return true;
    }
    x = p;
  }
}

ds_splay_tree_t *ds_splay_tree_clone(
    ds_splay_tree_t *tree,
    ds_splay_tree_node_t *(*clone_node)(ds_splay_tree_node_t *)) {
  ds_splay_tree_t *new_tree;
  if (tree == NULL || clone_node == NULL) {
    return NULL;
  }
  new_tree = splay_tree_create_internal(tree->compare, tree->allocator,
                                        tree->deallocator, tree->context,
                                        tree->compare_with, tree->compare_user);
  if (new_tree != NULL &&
      !splay_tree_clone_all(tree, new_tree, tree->root, clone_node)) {
    ds_splay_tree_destroy(new_tree, NULL); /* the clones are not released */
    new_tree = NULL;
  }
  return new_tree;
}

ds_status_t ds_splay_tree_clone_ex(
    const ds_splay_tree_t *tree,
    ds_splay_tree_node_t *(*clone_node)(ds_splay_tree_node_t *),
    void (*destroy_clone)(ds_splay_tree_node_t *), ds_splay_tree_t **out) {
  ds_splay_tree_t *new_tree;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (tree == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  new_tree = splay_tree_create_internal(tree->compare, tree->allocator,
                                        tree->deallocator, tree->context,
                                        tree->compare_with, tree->compare_user);
  if (new_tree == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!splay_tree_clone_all(tree, new_tree, tree->root, clone_node)) {
    ds_splay_tree_destroy(new_tree, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  *out = new_tree;
  return DS_OK;
}

ds_status_t ds_splay_tree_walk_with(const ds_splay_tree_t *container,
                                    ds_splay_tree_visit_fn visit, void *user) {
  ds_splay_tree_node_t *node;
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

ds_status_t ds_splay_tree_insert_ex(ds_splay_tree_t *container,
                                    ds_splay_tree_node_t *node) {
  size_t size;
  if (container == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == container->nil)
    return DS_ERR_INVALID;
  if (container->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  size = container->size;
  ds_splay_tree_insert(container, node);
  return container->size == size ? DS_EXISTS : DS_OK;
}

ds_status_t ds_splay_tree_find_ex(ds_splay_tree_t *container,
                                  ds_splay_tree_node_t *key,
                                  ds_splay_tree_node_t **out) {
  ds_splay_tree_node_t *node;
  if (container == NULL || key == NULL || out == NULL)
    return DS_ERR_NULL;
  if (key == container->nil)
    return DS_ERR_INVALID;
  node = ds_splay_tree_search(container, key);
  if (node == container->nil)
    return DS_NOT_FOUND;
  *out = node;
  return DS_OK;
}

ds_status_t ds_splay_tree_remove_ex(ds_splay_tree_t *container,
                                    ds_splay_tree_node_t *key,
                                    ds_splay_tree_node_t **out) {
  ds_splay_tree_node_t *node;
  if (container == NULL || key == NULL || out == NULL)
    return DS_ERR_NULL;
  if (key == container->nil)
    return DS_ERR_INVALID;
  node = ds_splay_tree_remove(container, key);
  if (node == container->nil)
    return DS_NOT_FOUND;
  *out = node;
  return DS_OK;
}

typedef struct splay_tree_clone_policy {
  ds_splay_tree_t *copy;
  ds_splay_tree_clone_fn clone;
  ds_splay_tree_destroy_fn destroy;
  void *user;
} splay_tree_clone_policy_t;
static ds_status_t splay_tree_clone_visit(ds_splay_tree_node_t *source,
                                          void *user) {
  splay_tree_clone_policy_t *policy = (splay_tree_clone_policy_t *)user;
  ds_splay_tree_node_t *node = NULL;
  ds_status_t st = policy->clone(source, &node, policy->user);
  if (st != DS_OK || node == NULL)
    return st < 0 ? st : DS_ERR_CALLBACK;
  st = ds_splay_tree_insert_ex(policy->copy, node);
  if (st != DS_OK) {
    policy->destroy(node, policy->user);
    return st < 0 ? st : DS_ERR_CALLBACK;
  }
  return DS_OK;
}
ds_status_t ds_splay_tree_clone_with(const ds_splay_tree_t *container,
                                     ds_splay_tree_clone_fn clone,
                                     ds_splay_tree_destroy_fn destroy,
                                     void *user, ds_context_t *context,
                                     ds_splay_tree_t **out) {
  splay_tree_clone_policy_t policy;
  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (container == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  policy.copy = splay_tree_create_internal(
      container->compare, container->allocator, container->deallocator,
      context != NULL ? context : container->context, container->compare_with,
      container->compare_user);
  if (policy.copy == NULL)
    return DS_ERR_ALLOC;
  policy.clone = clone;
  policy.destroy = destroy;
  policy.user = user;
  st = ds_splay_tree_walk_with(container, splay_tree_clone_visit, &policy);
  if (st != DS_OK) {
    ds_splay_tree_destroy_with(policy.copy, destroy, user);
    return st;
  }
  *out = policy.copy;
  return DS_OK;
}
