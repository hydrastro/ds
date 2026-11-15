#include "rbt.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

void ds_rbt_set_parent(ds_rbt_node_t *node, ds_rbt_node_t *parent) {
  node->parent_color = RBT_GET_COLOR_FROM_NODE(node) + (unsigned long)parent;
}

void ds_rbt_set_parent_color(ds_rbt_node_t *node, ds_rbt_node_t *parent,
                             int color) {
  node->parent_color = (unsigned long)parent + (long unsigned int)color;
}

ds_rbt_t *ds_rbt_create(void) { return ds_rbt_create_alloc(malloc, free); }

ds_rbt_t *ds_rbt_create_alloc(void *(*allocator)(size_t),
                              void (*deallocator)(void *)) {
  ds_rbt_t *tree;
  if (allocator == NULL || deallocator == NULL) {
    return NULL;
  }
  tree = (ds_rbt_t *)allocator(sizeof(ds_rbt_t));
  if (tree == NULL) {
    return NULL;
  }
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->nil = (ds_rbt_node_t *)allocator(sizeof(ds_rbt_node_t));
  if (tree->nil == NULL) {
    deallocator(tree);
    return NULL;
  }
  tree->nil->parent_color = RBT_BLACK;
  tree->root = tree->nil;
  ds_rbt_set_parent_color(tree->root, tree->nil, RBT_BLACK);
  tree->root->left = tree->nil;
  tree->root->right = tree->nil;
  tree->size = 0;

  return tree;
}

ds_rbt_node_t *ds_rbt_minimum(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  while (node_x->left != tree->nil) {
    node_x = node_x->left;
  }
  return node_x;
}

ds_rbt_node_t *ds_rbt_maximum(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  while (node_x->right != tree->nil) {
    node_x = node_x->right;
  }
  return node_x;
}

ds_rbt_node_t *ds_rbt_successor(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  ds_rbt_node_t *node_y;
  if (node_x->right != tree->nil) {
    return ds_rbt_minimum(tree, node_x->right);
  }
  node_y = RBT_GET_PARENT_FROM_NODE(node_x);
  while (node_y != tree->nil && node_x == node_y->right) {
    node_x = node_y;
    node_y = RBT_GET_PARENT_FROM_NODE(node_y);
  }
  return node_y;
}

ds_rbt_node_t *ds_rbt_predecessor(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  ds_rbt_node_t *node_y;
  if (node_x->left != tree->nil) {
    return ds_rbt_maximum(tree, node_x->left);
  }
  node_y = RBT_GET_PARENT_FROM_NODE(node_x);
  while (node_y != tree->nil && node_x == node_y->left) {
    node_x = node_y;
    node_y = RBT_GET_PARENT_FROM_NODE(node_y);
  }
  return node_y;
}

ds_rbt_node_t *ds_rbt_search(ds_rbt_t *tree, ds_rbt_node_t *node_x,
                             ds_rbt_node_t *data,
                             int (*compare)(ds_rbt_node_t *, ds_rbt_node_t *)) {
  int cmp;
  while (node_x != tree->nil) {
    cmp = compare(data, node_x);
    if (cmp == 0) {
      return node_x;
    } else if (cmp < 0) {
      node_x = node_x->left;
    } else {
      node_x = node_x->right;
    }
  }
  return tree->nil;
}

ds_rbt_node_t *
ds_rbt_bigger_than(ds_rbt_t *tree, ds_rbt_node_t *node_x, ds_rbt_node_t *key,
                   int (*compare)(ds_rbt_node_t *, ds_rbt_node_t *)) {
  int cmp;
  ds_rbt_node_t *node_y;
  node_y = tree->nil;
  while (node_x != tree->nil) {
    cmp = compare(key, node_x);
    if (cmp < 0) {
      node_y = node_x;
      node_x = node_x->left;
    } else {
      node_x = node_x->right;
    }
  }
  return node_y;
}

ds_rbt_node_t *
ds_rbt_smaller_than(ds_rbt_t *tree, ds_rbt_node_t *node_x, ds_rbt_node_t *key,
                    int (*compare)(ds_rbt_node_t *, ds_rbt_node_t *)) {
  int cmp;
  ds_rbt_node_t *node_y;
  node_y = tree->nil;
  while (node_x != tree->nil) {
    cmp = compare(key, node_x);
    if (cmp > 0) {
      node_y = node_x;
      node_x = node_x->right;
    } else {
      node_x = node_x->left;
    }
  }
  return node_y;
}

void ds_rbt_left_rotate(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  ds_rbt_node_t *node_y;
  node_y = node_x->right;
  node_x->right = node_y->left;
  if (node_y->left != tree->nil) {
    ds_rbt_set_parent(node_y->left, node_x);
  }
  ds_rbt_set_parent(node_y, RBT_GET_PARENT_FROM_NODE(node_x));
  if (RBT_GET_PARENT_FROM_NODE(node_x) == tree->nil) {
    tree->root = node_y;
  } else if (node_x == RBT_GET_PARENT_FROM_NODE(node_x)->left) {
    RBT_GET_PARENT_FROM_NODE(node_x)->left = node_y;
  } else {
    RBT_GET_PARENT_FROM_NODE(node_x)->right = node_y;
  }
  node_y->left = node_x;
  ds_rbt_set_parent(node_x, node_y);
}

void ds_rbt_right_rotate(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  ds_rbt_node_t *node_y;
  node_y = node_x->left;
  node_x->left = node_y->right;
  if (node_y->right != tree->nil) {
    ds_rbt_set_parent(node_y->right, node_x);
  }
  ds_rbt_set_parent(node_y, RBT_GET_PARENT_FROM_NODE(node_x));
  if (RBT_GET_PARENT_FROM_NODE(node_x) == tree->nil) {
    tree->root = node_y;
  } else if (node_x == RBT_GET_PARENT_FROM_NODE(node_x)->right) {
    RBT_GET_PARENT_FROM_NODE(node_x)->right = node_y;
  } else {
    RBT_GET_PARENT_FROM_NODE(node_x)->left = node_y;
  }
  node_y->right = node_x;
  ds_rbt_set_parent(node_x, node_y);
}

void ds_rbt_insert_fixup(ds_rbt_t *tree, ds_rbt_node_t *node_z) {
  while (RBT_IS_RED_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z))) {
    if (RBT_GET_PARENT_FROM_NODE(node_z) ==
        RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z))->left) {
      ds_rbt_node_t *node_y;
      node_y =
          RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z))->right;
      if (RBT_IS_RED_FROM_NODE(node_y)

      ) {
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
        RBT_SET_BLACK_FROM_NODE(node_y);
        RBT_SET_RED_FROM_NODE(
            RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
        node_z = RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
      } else {
        if (node_z == RBT_GET_PARENT_FROM_NODE(node_z)->right) {
          node_z = RBT_GET_PARENT_FROM_NODE(node_z);
          ds_rbt_left_rotate(tree, node_z);
        }
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
        RBT_SET_RED_FROM_NODE(
            RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
        ds_rbt_right_rotate(
            tree, RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
      }
    } else {
      ds_rbt_node_t *node_y;
      node_y = RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z))->left;
      if (RBT_IS_RED_FROM_NODE(node_y)) {
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
        RBT_SET_BLACK_FROM_NODE(node_y);
        RBT_SET_RED_FROM_NODE(
            RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
        node_z = RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
      } else {
        if (node_z == RBT_GET_PARENT_FROM_NODE(node_z)->left) {
          node_z = RBT_GET_PARENT_FROM_NODE(node_z);
          ds_rbt_right_rotate(tree, node_z);
        }
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
        RBT_SET_RED_FROM_NODE(
            RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
        ds_rbt_left_rotate(
            tree, RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
      }
    }
  }
  RBT_SET_BLACK_FROM_NODE(tree->root);
}

void ds_rbt_insert(ds_rbt_t *tree, ds_rbt_node_t *node_z,
                   int (*compare)(ds_rbt_node_t *, ds_rbt_node_t *)) {
  ds_rbt_node_t *node_y;
  ds_rbt_node_t *node_x;
  node_y = tree->nil;
  node_x = tree->root;
  while (node_x != tree->nil) {
    node_y = node_x;
    if (compare(node_z, node_x) < 0) {
      node_x = node_x->left;
    } else {
      node_x = node_x->right;
    }
  }
  ds_rbt_set_parent(node_z, node_y);
  if (node_y == tree->nil) {
    tree->root = node_z;
  } else if (compare(node_z, node_y) < 0) {
    node_y->left = node_z;
  } else {
    node_y->right = node_z;
  }
  node_z->left = tree->nil;
  node_z->right = tree->nil;
  RBT_SET_RED_FROM_NODE(node_z);
  ds_rbt_insert_fixup(tree, node_z);
  tree->size += 1;
}

void ds_rbt_inorder_walk_helper(ds_rbt_t *tree, ds_rbt_node_t *node,
                                void (*callback)(void *)) {
  if (node != tree->nil) {
    ds_rbt_inorder_walk_helper(tree, node->left, callback);
    callback(node);
    ds_rbt_inorder_walk_helper(tree, node->right, callback);
  }
}

void ds_rbt_inorder_walk(ds_rbt_t *tree, ds_rbt_node_t *node,
                         void (*callback)(void *)) {
  ds_rbt_inorder_walk_helper(tree, node, callback);
}

void ds_rbt_inorder_walk_tree(ds_rbt_t *tree, void (*callback)(void *)) {
  ds_rbt_inorder_walk_helper(tree, tree->root, callback);
}

void ds_rbt_preorder_walk_helper(ds_rbt_t *tree, ds_rbt_node_t *node,
                                 void (*callback)(void *)) {
  if (node != tree->nil) {
    callback(node);
    ds_rbt_preorder_walk_helper(tree, node->left, callback);
    ds_rbt_preorder_walk_helper(tree, node->right, callback);
  }
}

void ds_rbt_preorder_walk(ds_rbt_t *tree, ds_rbt_node_t *node,
                          void (*callback)(void *)) {
  ds_rbt_preorder_walk_helper(tree, node, callback);
}

void ds_rbt_preorder_walk_tree(ds_rbt_t *tree, void (*callback)(void *)) {
  ds_rbt_preorder_walk_helper(tree, tree->root, callback);
}

void ds_rbt_postorder_walk_helper(ds_rbt_t *tree, ds_rbt_node_t *node,
                                  void (*callback)(void *)) {
  if (node != tree->nil) {
    ds_rbt_postorder_walk_helper(tree, node->left, callback);
    ds_rbt_postorder_walk_helper(tree, node->right, callback);
    callback(node);
  }
}

void ds_rbt_postorder_walk(ds_rbt_t *tree, ds_rbt_node_t *node,
                           void (*callback)(void *)) {
  ds_rbt_postorder_walk_helper(tree, node, callback);
}

void ds_rbt_postorder_walk_tree(ds_rbt_t *tree, void (*callback)(void *)) {
  ds_rbt_postorder_walk_helper(tree, tree->root, callback);
}

void ds_rbt_transplant(ds_rbt_t *tree, ds_rbt_node_t *node_u,
                       ds_rbt_node_t *node_v) {
  if (RBT_GET_PARENT_FROM_NODE(node_u) == tree->nil) {
    tree->root = node_v;
  } else if (node_u == RBT_GET_PARENT_FROM_NODE(node_u)->left) {
    RBT_GET_PARENT_FROM_NODE(node_u)->left = node_v;
  } else {
    RBT_GET_PARENT_FROM_NODE(node_u)->right = node_v;
  }
  ds_rbt_set_parent(node_v, RBT_GET_PARENT_FROM_NODE(node_u));
}

void ds_rbt_delete_fixup(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  while (node_x != tree->root && RBT_IS_BLACK_FROM_NODE(node_x)) {
    if (node_x == RBT_GET_PARENT_FROM_NODE(node_x)->left) {
      ds_rbt_node_t *node_w;
      node_w = RBT_GET_PARENT_FROM_NODE(node_x)->right;
      if (RBT_IS_RED_FROM_NODE(node_w)) {
        RBT_SET_BLACK_FROM_NODE(node_w);
        RBT_SET_RED_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x));
        ds_rbt_left_rotate(tree, RBT_GET_PARENT_FROM_NODE(node_x));
        node_w = RBT_GET_PARENT_FROM_NODE(node_x)->right;
      }
      if (RBT_IS_BLACK_FROM_NODE(node_w->left) &&
          RBT_IS_BLACK_FROM_NODE(node_w->right)) {
        RBT_SET_RED_FROM_NODE(node_w);
        node_x = RBT_GET_PARENT_FROM_NODE(node_x);
      } else {
        if (RBT_IS_BLACK_FROM_NODE(node_w->right)) {
          RBT_SET_BLACK_FROM_NODE(node_w->left);
          RBT_SET_RED_FROM_NODE(node_w);
          ds_rbt_right_rotate(tree, node_w);
          node_w = RBT_GET_PARENT_FROM_NODE(node_x)->right;
        }
        RBT_SET_COLOR_FROM_NODE(
            node_w, RBT_GET_COLOR_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x)));
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x));
        RBT_SET_BLACK_FROM_NODE(node_w->right);
        ds_rbt_left_rotate(tree, RBT_GET_PARENT_FROM_NODE(node_x));
        node_x = tree->root;
      }
    } else {
      ds_rbt_node_t *node_w;
      node_w = RBT_GET_PARENT_FROM_NODE(node_x)->left;
      if (RBT_IS_RED_FROM_NODE(node_w)) {
        RBT_SET_BLACK_FROM_NODE(node_w);
        RBT_SET_RED_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x));
        ds_rbt_right_rotate(tree, RBT_GET_PARENT_FROM_NODE(node_x));
        node_w = RBT_GET_PARENT_FROM_NODE(node_x)->left;
      }
      if (RBT_IS_BLACK_FROM_NODE(node_w->right) &&
          RBT_IS_BLACK_FROM_NODE(node_w->left)) {
        RBT_SET_RED_FROM_NODE(node_w);
        node_x = RBT_GET_PARENT_FROM_NODE(node_x);
      } else {
        if (RBT_IS_BLACK_FROM_NODE(node_w->left)) {
          RBT_SET_BLACK_FROM_NODE(node_w->right);
          RBT_SET_RED_FROM_NODE(node_w);
          ds_rbt_left_rotate(tree, node_w);
          node_w = RBT_GET_PARENT_FROM_NODE(node_x)->left;
        }
        RBT_SET_COLOR_FROM_NODE(
            node_w, RBT_GET_COLOR_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x)));
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x));
        RBT_SET_BLACK_FROM_NODE(node_w->left);
        ds_rbt_right_rotate(tree, RBT_GET_PARENT_FROM_NODE(node_x));
        node_x = tree->root;
      }
    }
  }
  RBT_SET_BLACK_FROM_NODE(node_x);
}

void ds_rbt_delete_node(ds_rbt_t *tree, ds_rbt_node_t *node_z) {
  ds_rbt_node_t *node_y;
  ds_rbt_node_t *node_x;
  unsigned int color;
  node_y = node_z;
  color = RBT_GET_COLOR_FROM_NODE(node_y);
  if (node_z->left == tree->nil) {
    node_x = node_z->right;
    ds_rbt_transplant(tree, node_z, node_z->right);
  } else if (node_z->right == tree->nil) {
    node_x = node_z->left;
    ds_rbt_transplant(tree, node_z, node_z->left);
  } else {
    node_y = ds_rbt_minimum(tree, node_z->right);
    color = RBT_GET_COLOR_FROM_NODE(node_y);
    node_x = node_y->right;
    if (RBT_GET_PARENT_FROM_NODE(node_y) == node_z) {
      ds_rbt_set_parent(node_x, node_y);
    } else {
      ds_rbt_transplant(tree, node_y, node_y->right);
      node_y->right = node_z->right;
      ds_rbt_set_parent(node_y->right, node_y);
    }
    ds_rbt_transplant(tree, node_z, node_y);
    node_y->left = node_z->left;
    ds_rbt_set_parent(node_y->left, node_y);
    RBT_SET_COLOR_FROM_NODE(node_y, RBT_GET_COLOR_FROM_NODE(node_z));
  }
  if (color == RBT_BLACK) {
    ds_rbt_delete_fixup(tree, node_x);
  }
  tree->size -= 1;
}

void ds_rbt_delete_tree(ds_rbt_t *tree) {
  tree->root = tree->nil;
  tree->size = 0;
}

void ds_rbt_destroy_node(ds_rbt_t *tree, ds_rbt_node_t *node,
                         void (*destroy)(ds_rbt_node_t *)) {
  ds_rbt_delete_node(tree, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

void ds_rbt_destroy_recursive(ds_rbt_t *tree, ds_rbt_node_t *node,
                              void (*destroy)(ds_rbt_node_t *)) {
  if (node == tree->nil) {
    return;
  }
  ds_rbt_destroy_recursive(tree, node->left, destroy);
  ds_rbt_destroy_recursive(tree, node->right, destroy);
  if (destroy != NULL) {
    destroy(node);
  }
}

void ds_rbt_destroy_tree(ds_rbt_t *tree, void (*destroy)(ds_rbt_node_t *)) {
  ds_rbt_destroy_recursive(tree, tree->root, destroy);
  tree->deallocator(tree->nil);
  tree->deallocator(tree);
}

/* a copy of the subtree at node, colors included, whose root has
 * new_tree->nil as its parent; *out gets that root (new_tree->nil for an
 * empty subtree). Each clone is linked in before the next is made, so
 * when clone_node returns NULL (false) *out is still a tree holding every
 * clone made. The recursion is as deep as the tree: O(log n). */
static bool rbt_clone_subtree(const ds_rbt_t *tree, ds_rbt_t *new_tree,
                              ds_rbt_node_t *node,
                              ds_rbt_node_t *(*clone_node)(ds_rbt_node_t *),
                              ds_rbt_node_t **out) {
  ds_rbt_node_t *copy;
  bool ok;
  *out = new_tree->nil;
  if (node == tree->nil) {
    return true;
  }
  copy = clone_node(node);
  if (copy == NULL) {
    return false;
  }
  ds_rbt_set_parent_color(copy, new_tree->nil,
                          (int)RBT_GET_COLOR_FROM_NODE(node));
  copy->left = new_tree->nil;
  copy->right = new_tree->nil;
  *out = copy;
  ok = rbt_clone_subtree(tree, new_tree, node->left, clone_node, &copy->left);
  if (copy->left != new_tree->nil) {
    ds_rbt_set_parent(copy->left, copy);
  }
  if (ok) {
    ok = rbt_clone_subtree(tree, new_tree, node->right, clone_node,
                           &copy->right);
    if (copy->right != new_tree->nil) {
      ds_rbt_set_parent(copy->right, copy);
    }
  }
  return ok;
}

ds_rbt_node_t *
ds_rbt_clone_recursive(ds_rbt_t *tree, ds_rbt_t *new_tree, ds_rbt_node_t *node,
                       ds_rbt_node_t *(*clone_node)(ds_rbt_node_t *)) {
  ds_rbt_node_t *copy;
  (void)rbt_clone_subtree(tree, new_tree, node, clone_node, &copy);
  return copy;
}

ds_rbt_t *ds_rbt_clone(ds_rbt_t *tree,
                       ds_rbt_node_t *(*clone_node)(ds_rbt_node_t *)) {
  ds_rbt_t *new_tree;
  if (tree == NULL || clone_node == NULL) {
    return NULL;
  }
  new_tree = ds_rbt_create_alloc(tree->allocator, tree->deallocator);
  if (new_tree == NULL) {
    return NULL;
  }
  if (!rbt_clone_subtree(tree, new_tree, tree->root, clone_node,
                         &new_tree->root)) {
    ds_rbt_destroy_tree(new_tree, NULL); /* the clones are not released */
    return NULL;
  }
  new_tree->size = tree->size;

  return new_tree;
}

ds_status_t ds_rbt_clone_ex(const ds_rbt_t *tree,
                            ds_rbt_node_t *(*clone_node)(ds_rbt_node_t *),
                            void (*destroy_clone)(ds_rbt_node_t *),
                            ds_rbt_t **out) {
  ds_rbt_t *new_tree;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (tree == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  new_tree = ds_rbt_create_alloc(tree->allocator, tree->deallocator);
  if (new_tree == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!rbt_clone_subtree(tree, new_tree, tree->root, clone_node,
                         &new_tree->root)) {
    ds_rbt_destroy_tree(new_tree, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  new_tree->size = tree->size;
  *out = new_tree;
  return DS_OK;
}
