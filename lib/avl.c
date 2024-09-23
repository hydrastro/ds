#include "avl.h"
#include <stdio.h>
#include <stdlib.h>

avl_t *avl_create() {
  avl_t *tree = (avl_t *)malloc(sizeof(avl_t));
  tree->nil = (avl_node_t *)malloc(sizeof(avl_node_t));
  tree->nil->height = 0;
  tree->nil->left = tree->nil;
  tree->nil->right = tree->nil;
  tree->nil->parent = tree->nil;
  tree->root = tree->nil;
  tree->root->left = tree->nil;
  tree->root->right = tree->nil;
  tree->root->parent = tree->nil;
#ifdef AVL_THREAD_SAFE
  LOCK_INIT_RECURSIVE(tree);
#endif
  return tree;
}
void avl_inorder_walk(avl_t *tree, avl_node_t *node,
                      void (*callback)(avl_node_t *)) {
  if (node != tree->nil) {
    avl_inorder_walk(tree, node->left, callback);
    callback(node);
    avl_inorder_walk(tree, node->right, callback);
  }
}

void avl_inorder_walk_tree(avl_t *tree, void (*callback)(avl_node_t *)) {
#ifdef AVL_THREAD_SAFE
  LOCK(tree)
#endif
  avl_inorder_walk(tree, tree->root, callback);
#ifdef AVL_THREAD_SAFE
  UNLOCK(tree)
#endif
}
avl_node_t *avl_search(avl_t *tree, avl_node_t *data,
                       int (*compare)(avl_node_t *, avl_node_t *)) {
#ifdef AVL_THREAD_SAFE
  LOCK(tree)
#endif
  avl_node_t *node = tree->root;
  while (node != tree->nil) {
    int cmp = compare(data, node);
    if (cmp == 0) {
#ifdef AVL_THREAD_SAFE
      UNLOCK(tree)
#endif
      return node;
    } else if (cmp < 0) {
      node = node->left;
    } else {
      node = node->right;
    }
  }
#ifdef AVL_THREAD_SAFE
  UNLOCK(tree)
#endif
  return tree->nil;
}

static void avl_destroy_node(avl_t *tree, avl_node_t *node,
                             void (*destroy)(avl_node_t *)) {
  if (node != tree->nil) {
    avl_destroy_node(tree, node->left, destroy);
    avl_destroy_node(tree, node->right, destroy);
    destroy(node);
  }
}

void avl_destroy_tree(avl_t *tree, void (*destroy)(avl_node_t *)) {
#ifdef AVL_THREAD_SAFE
  LOCK(tree)
#endif
  avl_destroy_node(tree, tree->root, destroy);
  tree->root = tree->nil;
#ifdef AVL_THREAD_SAFE
  if (tree->is_thread_safe) {
    pthread_mutex_unlock(&tree->lock);
    pthread_mutex_destroy(&tree->lock);
  }
#endif
  free(tree->nil);
  free(tree);
}

static int avl_get_height(avl_node_t *node) { return node ? node->height : 0; }

static int avl_get_balance(avl_node_t *node) {
  return node ? avl_get_height(node->left) - avl_get_height(node->right) : 0;
}

static void avl_update_height(avl_node_t *node) {
  if (node) {
    int left_height = avl_get_height(node->left);
    int right_height = avl_get_height(node->right);
    node->height =
        1 + (left_height > right_height ? left_height : right_height);
  }
}

static avl_node_t *avl_left_rotate(avl_node_t *node) {
  avl_node_t *right = node->right;
  node->right = right->left;
  if (right->left) {
    right->left->parent = node;
  }
  right->parent = node->parent;
  right->left = node;
  node->parent = right;
  avl_update_height(node);
  avl_update_height(right);
  return right;
}

static avl_node_t *avl_right_rotate(avl_node_t *node) {
  avl_node_t *left = node->left;
  node->left = left->right;
  if (left->right) {
    left->right->parent = node;
  }
  left->parent = node->parent;
  left->right = node;
  node->parent = left;
  avl_update_height(node);
  avl_update_height(left);
  return left;
}

static avl_node_t *avl_balance(avl_node_t *node) {
  avl_update_height(node);
  int balance = avl_get_balance(node);

  if (balance > 1) {
    if (avl_get_balance(node->left) < 0) {
      node->left = avl_left_rotate(node->left);
    }
    return avl_right_rotate(node);
  } else if (balance < -1) {
    if (avl_get_balance(node->right) > 0) {
      node->right = avl_right_rotate(node->right);
    }
    return avl_left_rotate(node);
  }
  return node;
}

void avl_insert(avl_t *tree, avl_node_t *data,
                int (*compare)(avl_node_t *, avl_node_t *)) {
#ifdef AVL_THREAD_SAFE
  LOCK(tree);
#endif

  avl_node_t *current = tree->root;
  avl_node_t *parent = tree->nil;

  while (current != tree->nil) {
    parent = current;

    int cmp = compare(data, current);
    if (cmp < 0) {
      current = current->left;
    } else if (cmp > 0) {
      current = current->right;
    } else {
#ifdef AVL_THREAD_SAFE
      UNLOCK(tree);
#endif
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

  for (avl_node_t *n = parent; n != tree->nil; n = n->parent) {
    n = avl_balance(n);
    if (n->parent == tree->nil) {
      tree->root = n;
    } else if (n == n->parent->left) {
      n->parent->left = n;
    } else {
      n->parent->right = n;
    }
  }

#ifdef AVL_THREAD_SAFE
  UNLOCK(tree);
#endif
}

static avl_node_t *avl_min_node(avl_t *tree, avl_node_t *node) {
  while (node->left != tree->nil) {
    node = node->left;
  }
  return node;
}

static void avl_transplant(avl_t *tree, avl_node_t *u, avl_node_t *v) {
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

void avl_delete_node(avl_t *tree, avl_node_t *root, avl_node_t *data) {
#ifdef AVL_THREAD_SAFE
  LOCK(tree);
#endif
  avl_node_t *y = data;
  avl_node_t *x;
  if (data->left == tree->nil) {
    x = data->right;
    avl_transplant(tree, data, data->right);
  } else if (data->right == tree->nil) {
    x = data->left;
    avl_transplant(tree, data, data->left);
  } else {
    y = avl_min_node(tree, data->right);
    if (y->parent != data) {
      avl_transplant(tree, y, y->right);
      y->right = data->right;
      y->right->parent = y;
    }
    avl_transplant(tree, data, y);
    y->left = data->left;
    y->left->parent = y;
    x = y;
  }

  while (x != tree->nil) {
    x = avl_balance(x);
    x = x->parent;
  }

#ifdef AVL_THREAD_SAFE
  UNLOCK(tree);
#endif
}
