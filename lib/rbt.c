#include "rbt.h"
#include <stdio.h>
#include <stdlib.h>

void rbt_set_parent(rbt_node_t *node, rbt_node_t *parent) {
  node->parent_color = RBT_GET_COLOR_FROM_NODE(node) + (unsigned long)parent;
}

void rbt_set_parent_color(rbt_node_t *node, rbt_node_t *parent, int color) {
  node->parent_color = (unsigned long)parent + (long unsigned int)color;
}

rbt_t *rbt_create() {
  rbt_t *tree;
  tree = (rbt_t *)malloc(sizeof(rbt_t));
  tree->nil = (rbt_node_t *)malloc(sizeof(rbt_node_t));
  tree->nil->parent_color = RBT_BLACK;
  tree->root = tree->nil;
  rbt_set_parent_color(tree->root, tree->nil, RBT_BLACK);
  tree->root->left = tree->nil;
  tree->root->right = tree->nil;
  tree->size = 0;
#ifdef RBT_THREAD_SAFE
  LOCK_INIT_RECURSIVE(tree);
#endif

  return tree;
}

rbt_node_t *rbt_minimum(rbt_t *tree, rbt_node_t *node_x) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  while (node_x->left != tree->nil) {
    node_x = node_x->left;
  }
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
  return node_x;
}

rbt_node_t *rbt_maximum(rbt_t *tree, rbt_node_t *node_x) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  while (node_x->right != tree->nil) {
    node_x = node_x->right;
  }
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
  return node_x;
}

rbt_node_t *rbt_successor(rbt_t *tree, rbt_node_t *node_x) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_node_t *node_y;
  if (node_x->right != tree->nil) {
#ifdef RBT_THREAD_SAFE
    UNLOCK(tree)
#endif
    return rbt_minimum(tree, node_x->right);
  }
  node_y = RBT_GET_PARENT_FROM_NODE(node_x);
  while (node_y != tree->nil && node_x == node_y->right) {
    node_x = node_y;
    node_y = RBT_GET_PARENT_FROM_NODE(node_y);
  }
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
  return node_y;
}

rbt_node_t *rbt_predecessor(rbt_t *tree, rbt_node_t *node_x) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_node_t *node_y;
  if (node_x->left != tree->nil) {
#ifdef RBT_THREAD_SAFE
    UNLOCK(tree)
#endif
    return rbt_maximum(tree, node_x->left);
  }
  node_y = RBT_GET_PARENT_FROM_NODE(node_x);
  while (node_y != tree->nil && node_x == node_y->left) {
    node_x = node_y;
    node_y = RBT_GET_PARENT_FROM_NODE(node_y);
  }
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
  return node_y;
}

rbt_node_t *rbt_search(rbt_t *tree, rbt_node_t *node_x, rbt_node_t *data,
                       int (*compare)(rbt_node_t *, rbt_node_t *)) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  int cmp;
  while (node_x != tree->nil) {
    cmp = compare(data, node_x);
    if (cmp == 0) {
#ifdef RBT_THREAD_SAFE
      UNLOCK(tree)
#endif
      return node_x;
    } else if (cmp < 0) {
      node_x = node_x->left;
    } else {
      node_x = node_x->right;
    }
  }
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
  return tree->nil;
}

rbt_node_t *rbt_bigger_than(rbt_t *tree, rbt_node_t *node_x, rbt_node_t *key,
                            int (*compare)(rbt_node_t *, rbt_node_t *)) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_node_t *node_y = tree->nil;
  int cmp;
  while (node_x != tree->nil) {
    cmp = compare(key, node_x);
    if (cmp < 0) {
      node_y = node_x;
      node_x = node_x->left;
    } else {
      node_x = node_x->right;
    }
  }
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
  return node_y;
}

rbt_node_t *rbt_smaller_than(rbt_t *tree, rbt_node_t *node_x, rbt_node_t *key,
                             int (*compare)(rbt_node_t *, rbt_node_t *)) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_node_t *node_y = tree->nil;
  int cmp;
  while (node_x != tree->nil) {
    cmp = compare(key, node_x);
    if (cmp > 0) {
      node_y = node_x;
      node_x = node_x->right;
    } else {
      node_x = node_x->left;
    }
  }
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
  return node_y;
}

void rbt_left_rotate(rbt_t *tree, rbt_node_t *node_x) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_node_t *node_y;
  node_y = node_x->right;
  node_x->right = node_y->left;
  if (node_y->left != tree->nil) {
    rbt_set_parent(node_y->left, node_x);
  }
  rbt_set_parent(node_y, RBT_GET_PARENT_FROM_NODE(node_x));
  if (RBT_GET_PARENT_FROM_NODE(node_x) == tree->nil) {
    tree->root = node_y;
  } else if (node_x == RBT_GET_PARENT_FROM_NODE(node_x)->left) {
    RBT_GET_PARENT_FROM_NODE(node_x)->left = node_y;
  } else {
    RBT_GET_PARENT_FROM_NODE(node_x)->right = node_y;
  }
  node_y->left = node_x;
  rbt_set_parent(node_x, node_y);
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_right_rotate(rbt_t *tree, rbt_node_t *node_x) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_node_t *node_y;
  node_y = node_x->left;
  node_x->left = node_y->right;
  if (node_y->right != tree->nil) {
    rbt_set_parent(node_y->right, node_x);
  }
  rbt_set_parent(node_y, RBT_GET_PARENT_FROM_NODE(node_x));
  if (RBT_GET_PARENT_FROM_NODE(node_x) == tree->nil) {
    tree->root = node_y;
  } else if (node_x == RBT_GET_PARENT_FROM_NODE(node_x)->right) {
    RBT_GET_PARENT_FROM_NODE(node_x)->right = node_y;
  } else {
    RBT_GET_PARENT_FROM_NODE(node_x)->left = node_y;
  }
  node_y->right = node_x;
  rbt_set_parent(node_x, node_y);
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_insert_fixup(rbt_t *tree, rbt_node_t *node_z) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  while (RBT_IS_RED_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z))) {

    if (RBT_GET_PARENT_FROM_NODE(node_z) ==
        RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z))->left) {
      rbt_node_t *node_y;
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
          rbt_left_rotate(tree, node_z);
        }
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
        RBT_SET_RED_FROM_NODE(
            RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
        rbt_right_rotate(
            tree, RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
      }
    } else {
      rbt_node_t *node_y;
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
          rbt_right_rotate(tree, node_z);
        }
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
        RBT_SET_RED_FROM_NODE(
            RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
        rbt_left_rotate(
            tree, RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
      }
    }
  }
  RBT_SET_BLACK_FROM_NODE(tree->root);
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_insert(rbt_t *tree, rbt_node_t *node_z,
                int (*compare)(rbt_node_t *, rbt_node_t *)) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_node_t *node_y;
  rbt_node_t *node_x;
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
  rbt_set_parent(node_z, node_y);
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
  rbt_insert_fixup(tree, node_z);
  tree->size += 1;
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_inorder_walk_helper(rbt_t *tree, rbt_node_t *node,
                             void (*callback)(void *)) {
  if (node != tree->nil) {
    rbt_inorder_walk_helper(tree, node->left, callback);
    callback(node);
    rbt_inorder_walk_helper(tree, node->right, callback);
  }
}

void rbt_inorder_walk(rbt_t *tree, rbt_node_t *node, void (*callback)(void *)) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_inorder_walk_helper(tree, node, callback);
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_inorder_walk_tree(rbt_t *tree, void (*callback)(void *)) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_inorder_walk_helper(tree, tree->root, callback);
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_preorder_walk_helper(rbt_t *tree, rbt_node_t *node,
                              void (*callback)(void *)) {
  if (node != tree->nil) {
    callback(node);
    rbt_preorder_walk_helper(tree, node->left, callback);
    rbt_preorder_walk_helper(tree, node->right, callback);
  }
}

void rbt_preorder_walk(rbt_t *tree, rbt_node_t *node,
                       void (*callback)(void *)) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_preorder_walk_helper(tree, node, callback);
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_preorder_walk_tree(rbt_t *tree, void (*callback)(void *)) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_preorder_walk_helper(tree, tree->root, callback);
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_postorder_walk_helper(rbt_t *tree, rbt_node_t *node,
                               void (*callback)(void *)) {
  if (node != tree->nil) {
    rbt_postorder_walk_helper(tree, node->left, callback);
    rbt_postorder_walk_helper(tree, node->right, callback);
    callback(node);
  }
}

void rbt_postorder_walk(rbt_t *tree, rbt_node_t *node,
                        void (*callback)(void *)) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_postorder_walk_helper(tree, node, callback);
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_postorder_walk_tree(rbt_t *tree, void (*callback)(void *)) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_postorder_walk_helper(tree, tree->root, callback);
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_transplant(rbt_t *tree, rbt_node_t *node_u, rbt_node_t *node_v) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  if (RBT_GET_PARENT_FROM_NODE(node_u) == tree->nil) {
    tree->root = node_v;
  } else if (node_u == RBT_GET_PARENT_FROM_NODE(node_u)->left) {
    RBT_GET_PARENT_FROM_NODE(node_u)->left = node_v;
  } else {
    RBT_GET_PARENT_FROM_NODE(node_u)->right = node_v;
  }
  rbt_set_parent(node_v, RBT_GET_PARENT_FROM_NODE(node_u));
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_delete_fixup(rbt_t *tree, rbt_node_t *node_x) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  while (node_x != tree->root && RBT_IS_BLACK_FROM_NODE(node_x)) {
    if (node_x == RBT_GET_PARENT_FROM_NODE(node_x)->left) {
      rbt_node_t *node_w;
      node_w = RBT_GET_PARENT_FROM_NODE(node_x)->right;
      if (RBT_IS_RED_FROM_NODE(node_w)) {
        RBT_SET_BLACK_FROM_NODE(node_w);
        RBT_SET_RED_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x));
        rbt_left_rotate(tree, RBT_GET_PARENT_FROM_NODE(node_x));
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
          rbt_right_rotate(tree, node_w);
          node_w = RBT_GET_PARENT_FROM_NODE(node_x)->right;
        }
        RBT_SET_COLOR_FROM_NODE(
            node_w, RBT_GET_COLOR_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x)));
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x));
        RBT_SET_BLACK_FROM_NODE(node_w->right);
        rbt_left_rotate(tree, RBT_GET_PARENT_FROM_NODE(node_x));
        node_x = tree->root;
      }
    } else {
      rbt_node_t *node_w;
      node_w = RBT_GET_PARENT_FROM_NODE(node_x)->left;
      if (RBT_IS_RED_FROM_NODE(node_w)) {
        RBT_SET_BLACK_FROM_NODE(node_w);
        RBT_SET_RED_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x));
        rbt_right_rotate(tree, RBT_GET_PARENT_FROM_NODE(node_x));
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
          rbt_left_rotate(tree, node_w);
          node_w = RBT_GET_PARENT_FROM_NODE(node_x)->left;
        }
        RBT_SET_COLOR_FROM_NODE(
            node_w, RBT_GET_COLOR_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x)));
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x));
        RBT_SET_BLACK_FROM_NODE(node_w->left);
        rbt_right_rotate(tree, RBT_GET_PARENT_FROM_NODE(node_x));
        node_x = tree->root;
      }
    }
  }
  RBT_SET_BLACK_FROM_NODE(node_x);
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_delete_node(rbt_t *tree, rbt_node_t *node_z) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_node_t *node_y;
  rbt_node_t *node_x;
  unsigned int color;
  node_y = node_z;
  color = RBT_GET_COLOR_FROM_NODE(node_y);
  if (node_z->left == tree->nil) {
    node_x = node_z->right;
    rbt_transplant(tree, node_z, node_z->right);
  } else if (node_z->right == tree->nil) {
    node_x = node_z->left;
    rbt_transplant(tree, node_z, node_z->left);
  } else {
    node_y = rbt_minimum(tree, node_z->right);
    color = RBT_GET_COLOR_FROM_NODE(node_y);
    node_x = node_y->right;
    if (RBT_GET_PARENT_FROM_NODE(node_y) == node_z) {
      rbt_set_parent(node_x, node_y);
    } else {
      rbt_transplant(tree, node_y, node_y->right);
      node_y->right = node_z->right;
      rbt_set_parent(node_y->right, node_y);
    }
    rbt_transplant(tree, node_z, node_y);
    node_y->left = node_z->left;
    rbt_set_parent(node_y->left, node_y);
    RBT_SET_COLOR_FROM_NODE(node_y, RBT_GET_COLOR_FROM_NODE(node_z));
  }
  if (color == RBT_BLACK) {
    rbt_delete_fixup(tree, node_x);
  }
  tree->size -= 1;
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_delete_tree(rbt_t *tree) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  tree->root = tree->nil;
  tree->size = 0;
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_destroy_node(rbt_t *tree, rbt_node_t *node,
                      void (*destroy)(rbt_node_t *)) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  rbt_delete_node(tree, node);
  if (destroy != NULL) {
    destroy(node);
  }
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_destroy_recursive(rbt_t *tree, rbt_node_t *node,
                           void (*destroy)(rbt_node_t *)) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree)
#endif
  if (node == tree->nil) {
#ifdef RBT_THREAD_SAFE
    UNLOCK(tree)
#endif
    return;
  }
  rbt_destroy_recursive(tree, node->left, destroy);
  rbt_destroy_recursive(tree, node->right, destroy);
  if (destroy != NULL) {
    destroy(node);
  }
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void rbt_destroy_tree(rbt_t *tree, void (*destroy)(rbt_node_t *)) {
  rbt_destroy_recursive(tree, tree->root, destroy);
#ifdef RBT_THREAD_SAFE
  LOCK_DESTROY(tree)
#endif
  free(tree->nil);
  free(tree);
}

rbt_node_t *rbt_clone_recursive(rbt_t *tree, rbt_t *new_tree, rbt_node_t *node,
                                rbt_node_t *(*clone_node)(rbt_node_t *)) {
  if (node == tree->nil) {
    return new_tree->nil;
  }

  rbt_node_t *new_node = clone_node(node);
  new_node->left = rbt_clone_recursive(tree, new_tree, node->left, clone_node);
  new_node->right =
      rbt_clone_recursive(tree, new_tree, node->right, clone_node);

  return new_node;
}

rbt_t *rbt_clone(rbt_t *tree, rbt_node_t *(*clone_node)(rbt_node_t *)) {
#ifdef RBT_THREAD_SAFE
  LOCK(tree);
#endif
  rbt_t *new_tree = rbt_create();
  new_tree->root = rbt_clone_recursive(tree, new_tree, tree->root, clone_node);
  new_tree->size = tree->size;
#ifdef RBT_THREAD_SAFE
  UNLOCK(tree);
#endif

  return new_tree;
}
