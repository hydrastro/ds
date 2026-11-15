#include "avl.h"
#include <stdio.h>
#include <stdlib.h>

ds_avl_t *ds_avl_create(void) { return ds_avl_create_alloc(malloc, free); }

ds_avl_t *ds_avl_create_alloc(void *(*allocator)(size_t),
                              void (*deallocator)(void *)) {
  ds_avl_t *tree;
  if (allocator == NULL || deallocator == NULL) {
    return NULL;
  }
  tree = (ds_avl_t *)allocator(sizeof(ds_avl_t));
  if (tree == NULL) {
    return NULL;
  }
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->nil = (ds_avl_node_t *)allocator(sizeof(ds_avl_node_t));
  if (tree->nil == NULL) {
    deallocator(tree);
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
  tree->deallocator(tree->nil);
  tree->deallocator(tree);
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

void ds_avl_insert(ds_avl_t *tree, ds_avl_node_t *data,
                   int (*compare)(ds_avl_node_t *, ds_avl_node_t *)) {
  int cmp;
  ds_avl_node_t *n, *current, *parent;
  current = tree->root;
  parent = tree->nil;

  while (current != tree->nil) {
    parent = current;

    cmp = compare(data, current);
    if (cmp < 0) {
      current = current->left;
    } else if (cmp > 0) {
      current = current->right;
    } else {
      return;
    }
  }

  data->parent = parent;
  data->left = tree->nil;
  data->right = tree->nil;
  data->height = 1;

  if (parent == tree->nil) {
    tree->root = data;
  } else if (compare(data, parent) < 0) {
    parent->left = data;
  } else {
    parent->right = data;
  }
  for (n = parent; n != tree->nil;) {
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
  tree->size += 1;
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

void ds_avl_delete_node(ds_avl_t *tree, ds_avl_node_t *data) {
  ds_avl_node_t *x, *y;
  y = data;
  if (data->left == tree->nil) {
    x = data->right;
    ds_avl_transplant(tree, data, data->right);
  } else if (data->right == tree->nil) {
    x = data->left;
    ds_avl_transplant(tree, data, data->left);
  } else {
    y = ds_avl_min_node(tree, data->right);
    if (y->parent != data) {
      ds_avl_transplant(tree, y, y->right);
      y->right = data->right;
      y->right->parent = y;
    }
    ds_avl_transplant(tree, data, y);
    y->left = data->left;
    y->left->parent = y;
    x = y;
  }

  while (x != tree->nil) {
    x = ds_avl_balance(tree, x);
    x = x->parent;
  }
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
static bool avl_clone_subtree(const ds_avl_t *tree, ds_avl_t *new_tree,
                              ds_avl_node_t *node,
                              ds_avl_node_t *(*clone_node)(ds_avl_node_t *),
                              ds_avl_node_t **out) {
  ds_avl_node_t *copy;
  bool ok;
  *out = new_tree->nil;
  if (node == tree->nil) {
    return true;
  }
  copy = clone_node(node);
  if (copy == NULL) {
    return false;
  }
  copy->height = node->height;
  copy->left = new_tree->nil;
  copy->right = new_tree->nil;
  copy->parent = new_tree->nil;
  *out = copy;
  ok = avl_clone_subtree(tree, new_tree, node->left, clone_node, &copy->left);
  if (copy->left != new_tree->nil) {
    copy->left->parent = copy;
  }
  if (ok) {
    ok = avl_clone_subtree(tree, new_tree, node->right, clone_node,
                           &copy->right);
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
  (void)avl_clone_subtree(tree, new_tree, node, clone_node, &copy);
  return copy;
}

ds_avl_t *ds_avl_clone(ds_avl_t *tree,
                       ds_avl_node_t *(*clone_node)(ds_avl_node_t *)) {
  ds_avl_t *new_tree;
  if (tree == NULL || clone_node == NULL) {
    return NULL;
  }
  new_tree = ds_avl_create_alloc(tree->allocator, tree->deallocator);
  if (new_tree == NULL) {
    return NULL;
  }
  if (!avl_clone_subtree(tree, new_tree, tree->root, clone_node,
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
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (tree == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  new_tree = ds_avl_create_alloc(tree->allocator, tree->deallocator);
  if (new_tree == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!avl_clone_subtree(tree, new_tree, tree->root, clone_node,
                         &new_tree->root)) {
    ds_avl_destroy_tree(new_tree, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  new_tree->size = tree->size;
  *out = new_tree;
  return DS_OK;
}
