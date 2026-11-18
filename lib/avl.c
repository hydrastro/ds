#include "avl.h"
#include "legacy_alloc.h"
#include <stdio.h>
#include <stdlib.h>

ds_avl_t *ds_avl_create(void) { return ds_avl_create_alloc(malloc, free); }

static ds_avl_t *avl_create_internal(void *(*allocator)(size_t),
                                     void (*deallocator)(void *),
                                     ds_context_t *context) {
  ds_avl_t *tree;
  if (allocator == NULL || deallocator == NULL) {
    return NULL;
  }
  tree = (ds_avl_t *)DS_LEGACY_ALLOCATE(context, allocator, sizeof(ds_avl_t));
  if (tree == NULL) {
    return NULL;
  }
  tree->context = context;
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->nil = (ds_avl_node_t *)DS_LEGACY_ALLOCATE(context, allocator,
                                                  sizeof(ds_avl_node_t));
  if (tree->nil == NULL) {
    DS_LEGACY_RELEASE(context, deallocator, tree);
    return NULL;
  }
  tree->nil->height = 0;
  tree->nil->left = tree->nil;
  tree->nil->right = tree->nil;
  tree->nil->parent = tree->nil;
  tree->root = tree->nil;
  tree->root->left = tree->nil;
  tree->root->right = tree->nil;
  tree->root->parent = tree->nil;
  tree->size = 0;
  return tree;
}

ds_avl_t *ds_avl_create_alloc(void *(*allocator)(size_t),
                              void (*deallocator)(void *)) {
  return avl_create_internal(allocator, deallocator, NULL);
}
ds_status_t ds_avl_create_context(ds_context_t *context, ds_avl_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = avl_create_internal(malloc, free,
                             context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

void ds_avl_inorder_walk_helper(ds_avl_t *tree, ds_avl_node_t *node,
                                void (*callback)(ds_avl_node_t *)) {
  if (node != tree->nil) {
    ds_avl_inorder_walk(tree, node->left, callback);
    callback(node);
    ds_avl_inorder_walk(tree, node->right, callback);
  }
}

void ds_avl_inorder_walk(ds_avl_t *tree, ds_avl_node_t *node,
                         void (*callback)(ds_avl_node_t *)) {
  ds_avl_inorder_walk_helper(tree, node, callback);
}

void ds_avl_inorder_walk_tree(ds_avl_t *tree,
                              void (*callback)(ds_avl_node_t *)) {
  ds_avl_inorder_walk_helper(tree, tree->root, callback);
}
void ds_avl_preorder_walk_helper(ds_avl_t *tree, ds_avl_node_t *node,
                                 void (*callback)(ds_avl_node_t *)) {
  if (node != tree->nil) {
    callback(node);
    ds_avl_preorder_walk(tree, node->left, callback);
    ds_avl_preorder_walk(tree, node->right, callback);
  }
}
void ds_avl_preorder_walk(ds_avl_t *tree, ds_avl_node_t *node,
                          void (*callback)(ds_avl_node_t *)) {
  ds_avl_preorder_walk_helper(tree, node, callback);
}

void ds_avl_preorder_walk_tree(ds_avl_t *tree,
                               void (*callback)(ds_avl_node_t *)) {
  ds_avl_preorder_walk_helper(tree, tree->root, callback);
}

void ds_avl_postorder_walk_helper(ds_avl_t *tree, ds_avl_node_t *node,
                                  void (*callback)(ds_avl_node_t *)) {
  if (node != tree->nil) {
    ds_avl_postorder_walk(tree, node->left, callback);
    ds_avl_postorder_walk(tree, node->right, callback);
    callback(node);
  }
}

void ds_avl_postorder_walk(ds_avl_t *tree, ds_avl_node_t *node,
                           void (*callback)(ds_avl_node_t *)) {
  ds_avl_postorder_walk_helper(tree, node, callback);
}

void ds_avl_postorder_walk_tree(ds_avl_t *tree,
                                void (*callback)(ds_avl_node_t *)) {
  ds_avl_postorder_walk_helper(tree, tree->root, callback);
}

ds_avl_node_t *ds_avl_search(ds_avl_t *tree, ds_avl_node_t *data,
                             int (*compare)(ds_avl_node_t *, ds_avl_node_t *)) {
  ds_avl_node_t *node;
  node = tree->root;
  while (node != tree->nil) {
    int cmp = compare(data, node);
    if (cmp == 0) {
      return node;
    } else if (cmp < 0) {
      node = node->left;
    } else {
      node = node->right;
    }
  }
  return tree->nil;
}

bool ds_avl_is_empty(ds_avl_t *tree) {
  return tree == NULL || tree->root == tree->nil;
}

void ds_avl_destroy_node(ds_avl_t *tree, ds_avl_node_t *node,
                         void (*destroy)(ds_avl_node_t *)) {
  ds_avl_delete_node(tree, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

void ds_avl_destroy_recursive(ds_avl_t *tree, ds_avl_node_t *node,
                              void (*destroy)(ds_avl_node_t *)) {
  if (node != tree->nil) {
    ds_avl_destroy_recursive(tree, node->left, destroy);
    ds_avl_destroy_recursive(tree, node->right, destroy);
    tree->size -= 1;
    if (destroy != NULL) { /* NULL owns nothing, as in the other trees */
      destroy(node);
    }
  }
}

void ds_avl_destroy_tree(ds_avl_t *tree, void (*destroy)(ds_avl_node_t *)) {
  ds_avl_destroy_recursive(tree, tree->root, destroy);
  tree->root = tree->nil;
  DS_LEGACY_FREE(tree, tree->nil);
  DS_LEGACY_FREE(tree, tree);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
int ds_avl_get_height(ds_avl_t *tree, ds_avl_node_t *node) {
  int result;
  result = node ? node->height : 0;
  return result;
}
#pragma GCC diagnostic pop

int ds_avl_get_balance(ds_avl_t *tree, ds_avl_node_t *node) {
  int result;
  result = node ? ds_avl_get_height(tree, node->left) -
                      ds_avl_get_height(tree, node->right)
                : 0;
  return result;
}

void ds_avl_update_height(ds_avl_t *tree, ds_avl_node_t *node) {
  int left_height, right_height;
  left_height = ds_avl_get_height(tree, node->left);
  right_height = ds_avl_get_height(tree, node->right);
  node->height = 1 + (left_height > right_height ? left_height : right_height);
}

ds_avl_node_t *ds_avl_left_rotate(ds_avl_t *tree, ds_avl_node_t *node) {
  ds_avl_node_t *right;
  right = node->right;
  node->right = right->left;
  if (right->left != tree->nil) {
    right->left->parent = node;
  }
  right->parent = node->parent;
  right->left = node;
  node->parent = right;
  ds_avl_update_height(tree, node);
  ds_avl_update_height(tree, right);
  return right;
}

ds_avl_node_t *ds_avl_right_rotate(ds_avl_t *tree, ds_avl_node_t *node) {
  ds_avl_node_t *left;
  left = node->left;
  node->left = left->right;
  if (left->right != tree->nil) {
    left->right->parent = node;
  }
  left->parent = node->parent;
  left->right = node;
  node->parent = left;
  ds_avl_update_height(tree, node);
  ds_avl_update_height(tree, left);
  return left;
}

ds_avl_node_t *ds_avl_balance(ds_avl_t *tree, ds_avl_node_t *node) {
  int balance;
  ds_avl_update_height(tree, node);
  balance = ds_avl_get_balance(tree, node);

  if (balance > 1) {
    if (ds_avl_get_balance(tree, node->left) < 0) {
      node->left = ds_avl_left_rotate(tree, node->left);
    }
    return ds_avl_right_rotate(tree, node);
  } else if (balance < -1) {
    if (ds_avl_get_balance(tree, node->right) > 0) {
      node->right = ds_avl_right_rotate(tree, node->right);
    }
    return ds_avl_left_rotate(tree, node);
  }
  return node;
}

/* heights and balance restored from n up to the root; a subtree that a
 * rotation gives a new root is linked back into its parent (or becomes
 * the tree's root) */
static void avl_rebalance_up(ds_avl_t *tree, ds_avl_node_t *n) {
  while (n != tree->nil) {
    ds_avl_node_t *old_parent;
    bool was_left;
    old_parent = n->parent;
    was_left = old_parent != tree->nil && n == old_parent->left;
    n = ds_avl_balance(tree, n);
    if (old_parent == tree->nil) {
      tree->root = n;
    } else if (was_left) {
      old_parent->left = n;
    } else {
      old_parent->right = n;
    }
    n->parent = old_parent;
    n = old_parent;
  }
}

static ds_status_t avl_insert_core(ds_avl_t *tree, ds_avl_node_t *data,
                                   ds_avl_compare_fn compare, void *user) {
  int cmp;
  ds_avl_node_t *current, *parent;
  current = tree->root;
  parent = tree->nil;

  while (current != tree->nil) {
    parent = current;

    cmp = compare(data, current, user);
    if (cmp < 0) {
      current = current->left;
    } else if (cmp > 0) {
      current = current->right;
    } else {
      return DS_EXISTS;
    }
  }

  data->parent = parent;
  data->left = tree->nil;
  data->right = tree->nil;
  data->height = 1;

  if (parent == tree->nil) {
    tree->root = data;
  } else if (compare(data, parent, user) < 0) {
    parent->left = data;
  } else {
    parent->right = data;
  }
  avl_rebalance_up(tree, parent);
  tree->size += 1;
  return DS_OK;
}
static int avl_compare_legacy(ds_avl_node_t *a, ds_avl_node_t *b, void *user) {
  int (**compare)(ds_avl_node_t *, ds_avl_node_t *) =
      (int (**)(ds_avl_node_t *, ds_avl_node_t *))user;
  return (*compare)(a, b);
}
ds_status_t ds_avl_insert_with(ds_avl_t *tree, ds_avl_node_t *node,
                               ds_avl_compare_fn compare, void *user) {
  if (tree == NULL || node == NULL || compare == NULL)
    return DS_ERR_NULL;
  if (node == tree->nil)
    return DS_ERR_INVALID;
  if (tree->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  return avl_insert_core(tree, node, compare, user);
}
void ds_avl_insert(ds_avl_t *tree, ds_avl_node_t *data,
                   int (*compare)(ds_avl_node_t *, ds_avl_node_t *)) {
  (void)ds_avl_insert_with(tree, data, avl_compare_legacy, &compare);
}

ds_avl_node_t *ds_avl_min_node(ds_avl_t *tree, ds_avl_node_t *node) {
  while (node->left != tree->nil) {
    node = node->left;
  }
  return node;
}

void ds_avl_transplant(ds_avl_t *tree, ds_avl_node_t *u, ds_avl_node_t *v) {
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

/* rebalancing starts at the lowest node whose subtree lost a node: the
 * removed node's parent, or the successor's old parent when the successor
 * moves up from below its right child */
void ds_avl_delete_node(ds_avl_t *tree, ds_avl_node_t *data) {
  ds_avl_node_t *x, *y;
  if (data->left == tree->nil) {
    x = data->parent;
    ds_avl_transplant(tree, data, data->right);
  } else if (data->right == tree->nil) {
    x = data->parent;
    ds_avl_transplant(tree, data, data->left);
  } else {
    y = ds_avl_min_node(tree, data->right);
    if (y->parent != data) {
      x = y->parent;
      ds_avl_transplant(tree, y, y->right);
      y->right = data->right;
      y->right->parent = y;
    } else {
      x = y;
    }
    ds_avl_transplant(tree, data, y);
    y->left = data->left;
    y->left->parent = y;
  }
  avl_rebalance_up(tree, x);
  tree->size -= 1;
}

void ds_avl_delete_tree(ds_avl_t *tree) {
  tree->root = tree->nil;
  tree->size = 0;
}

/* a copy of the subtree at node, heights included, whose root has
 * new_tree->nil as its parent; *out gets that root (new_tree->nil for an
 * empty subtree). Each clone is linked in before the next is made, so
 * when clone_node returns NULL (false) *out is still a tree holding every
 * clone made. The recursion is as deep as the tree: O(log n). */

typedef struct avl_clone_policy {
  ds_avl_node_t *(*legacy)(ds_avl_node_t *);
  ds_avl_clone_fn checked;
  void *user;
  ds_status_t status;
} avl_clone_policy_t;
static ds_avl_node_t *avl_clone_one(avl_clone_policy_t *p,
                                    ds_avl_node_t *node) {
  ds_avl_node_t *copy = NULL;
  if (p->checked != NULL) {
    p->status = p->checked(node, p->user, &copy);
    if (p->status != DS_OK) {
      if (p->status > DS_OK)
        p->status = DS_ERR_CALLBACK;
      return NULL;
    }
  } else
    copy = p->legacy(node);
  if (copy == NULL)
    p->status = DS_ERR_CALLBACK;
  return copy;
}
static bool avl_clone_subtree(const ds_avl_t *tree, ds_avl_t *new_tree,
                              ds_avl_node_t *node, avl_clone_policy_t *policy,
                              ds_avl_node_t **out) {
  ds_avl_node_t *copy;
  bool ok;
  *out = new_tree->nil;
  if (node == tree->nil) {
    return true;
  }
  copy = avl_clone_one(policy, node);
  if (copy == NULL) {
    return false;
  }
  copy->height = node->height;
  copy->left = new_tree->nil;
  copy->right = new_tree->nil;
  copy->parent = new_tree->nil;
  *out = copy;
  ok = avl_clone_subtree(tree, new_tree, node->left, policy, &copy->left);
  if (copy->left != new_tree->nil) {
    copy->left->parent = copy;
  }
  if (ok) {
    ok = avl_clone_subtree(tree, new_tree, node->right, policy, &copy->right);
    if (copy->right != new_tree->nil) {
      copy->right->parent = copy;
    }
  }
  return ok;
}

ds_avl_node_t *
ds_avl_clone_recursive(ds_avl_t *tree, ds_avl_t *new_tree, ds_avl_node_t *node,
                       ds_avl_node_t *(*clone_node)(ds_avl_node_t *)) {
  ds_avl_node_t *copy;
  avl_clone_policy_t policy;
  policy.legacy = clone_node;
  policy.checked = NULL;
  policy.user = NULL;
  policy.status = DS_OK;
  (void)avl_clone_subtree(tree, new_tree, node, &policy, &copy);
  return copy;
}

ds_avl_t *ds_avl_clone(ds_avl_t *tree,
                       ds_avl_node_t *(*clone_node)(ds_avl_node_t *)) {
  ds_avl_t *new_tree;
  avl_clone_policy_t policy;
  policy.legacy = clone_node;
  policy.checked = NULL;
  policy.user = NULL;
  policy.status = DS_OK;
  if (tree == NULL || clone_node == NULL) {
    return NULL;
  }
  new_tree =
      avl_create_internal(tree->allocator, tree->deallocator, tree->context);
  if (new_tree == NULL) {
    return NULL;
  }
  if (!avl_clone_subtree(tree, new_tree, tree->root, &policy,
                         &new_tree->root)) {
    ds_avl_destroy_tree(new_tree, NULL); /* the clones are not released */
    return NULL;
  }
  new_tree->size = tree->size;

  return new_tree;
}

ds_status_t ds_avl_clone_ex(const ds_avl_t *tree,
                            ds_avl_node_t *(*clone_node)(ds_avl_node_t *),
                            void (*destroy_clone)(ds_avl_node_t *),
                            ds_avl_t **out) {
  ds_avl_t *new_tree;
  avl_clone_policy_t policy;
  policy.legacy = clone_node;
  policy.checked = NULL;
  policy.user = NULL;
  policy.status = DS_OK;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (tree == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  new_tree =
      avl_create_internal(tree->allocator, tree->deallocator, tree->context);
  if (new_tree == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!avl_clone_subtree(tree, new_tree, tree->root, &policy,
                         &new_tree->root)) {
    ds_avl_destroy_tree(new_tree, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  new_tree->size = tree->size;
  *out = new_tree;
  return DS_OK;
}

ds_status_t ds_avl_find_with(const ds_avl_t *tree, ds_avl_node_t *key,
                             ds_avl_compare_fn compare, void *user,
                             ds_avl_node_t **out) {
  ds_avl_node_t *node;
  if (tree == NULL || key == NULL || compare == NULL || out == NULL)
    return DS_ERR_NULL;
  node = tree->root;
  while (node != tree->nil) {
    int cmp = compare(key, node, user);
    if (cmp == 0) {
      *out = node;
      return DS_OK;
    }
    node = cmp < 0 ? node->left : node->right;
  }
  return DS_NOT_FOUND;
}
ds_status_t ds_avl_traverse_with(const ds_avl_t *tree, ds_avl_node_t *root,
                                 ds_tree_walk_order_t order,
                                 ds_avl_visit_fn visit, void *user) {
  ds_avl_node_t *node, *parent;
  int state = 0;
  ds_status_t st;
  if (tree == NULL || root == NULL || visit == NULL)
    return DS_ERR_NULL;
  if (order != DS_TREE_PREORDER && order != DS_TREE_INORDER &&
      order != DS_TREE_POSTORDER)
    return DS_ERR_INVALID;
  node = root;
  while (node != tree->nil) {
    if (state == 0) {
      if (order == DS_TREE_PREORDER) {
        st = visit(node, user);
        if (st != DS_OK)
          return st;
      }
      if (node->left != tree->nil) {
        node = node->left;
        continue;
      }
      state = 1;
    }
    if (state == 1) {
      if (order == DS_TREE_INORDER) {
        st = visit(node, user);
        if (st != DS_OK)
          return st;
      }
      if (node->right != tree->nil) {
        node = node->right;
        state = 0;
        continue;
      }
    }
    if (order == DS_TREE_POSTORDER) {
      st = visit(node, user);
      if (st != DS_OK)
        return st;
    }
    if (node == root)
      break;
    parent = node->parent;
    state = node == parent->left ? 1 : 2;
    node = parent;
  }
  return DS_OK;
}
ds_status_t ds_avl_walk_with(const ds_avl_t *tree, ds_avl_visit_fn visit,
                             void *user) {
  if (tree == NULL)
    return DS_ERR_NULL;
  return ds_avl_traverse_with(tree, tree->root, DS_TREE_INORDER, visit, user);
}
void ds_avl_destroy_with(ds_avl_t *tree, ds_avl_destroy_fn destroy,
                         void *user) {
  ds_avl_node_t *node, *parent;
  if (tree == NULL)
    return;
  node = tree->root;
  while (node != tree->nil) {
    if (node->left != tree->nil) {
      node = node->left;
      continue;
    }
    if (node->right != tree->nil) {
      node = node->right;
      continue;
    }
    parent = node->parent;
    if (parent != tree->nil) {
      if (parent->left == node)
        parent->left = tree->nil;
      else
        parent->right = tree->nil;
    }
    if (destroy != NULL)
      destroy(node, user);
    node = parent;
  }
  DS_LEGACY_FREE(tree, tree->nil);
  DS_LEGACY_FREE(tree, tree);
}
ds_status_t ds_avl_clone_with(const ds_avl_t *tree, ds_avl_clone_fn clone,
                              ds_avl_destroy_fn destroy, void *user,
                              ds_context_t *context, ds_avl_t **out) {
  ds_avl_t *copy;
  avl_clone_policy_t policy;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (tree == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  copy = avl_create_internal(tree->allocator, tree->deallocator,
                             context != NULL ? context : tree->context);
  if (copy == NULL)
    return DS_ERR_ALLOC;
  policy.legacy = NULL;
  policy.checked = clone;
  policy.user = user;
  policy.status = DS_OK;
  if (!avl_clone_subtree(tree, copy, tree->root, &policy, &copy->root)) {
    ds_avl_destroy_with(copy, destroy, user);
    return policy.status;
  }
  copy->size = tree->size;
  *out = copy;
  return DS_OK;
}
