#include "bst.h"
#include <stdio.h>
#include <stdlib.h>

bst_t *bst_create() {
  bst_t *tree = (bst_t *)malloc(sizeof(bst_t));
  tree->nil = (bst_node_t *)malloc(sizeof(bst_node_t));
  tree->root = tree->nil;
  tree->size = 0;

#ifdef BST_THREAD_SAFE
  LOCK_INIT_RECURSIVE(tree)
#endif

  return tree;
}

void bst_insert(bst_t *tree, bst_node_t *node,
                int (*compare)(bst_node_t *, bst_node_t *)) {
#ifdef BST_THREAD_SAFE
  LOCK(tree);
#endif

  if (tree->root == tree->nil) {
    tree->root = node;
    node->parent = node->left = node->right = tree->nil;
  } else {
    bst_node_t *current = tree->root;
    bst_node_t *parent = tree->nil;
    while (current != tree->nil) {
      parent = current;
      if (compare(node, current) < 0) {
        current = current->left;
      } else {
        current = current->right;
      }
    }
    if (compare(node, parent) < 0) {
      parent->left = node;
    } else {
      parent->right = node;
    }
    node->parent = parent;
    node->left = node->right = tree->nil;
  }
  tree->size += 1;

#ifdef BST_THREAD_SAFE
  UNLOCK(tree);
#endif
}

bst_node_t *bst_search(bst_t *tree, bst_node_t *data,
                       int (*compare)(bst_node_t *, bst_node_t *)) {
#ifdef BST_THREAD_SAFE
  LOCK(tree)
#endif

  bst_node_t *current = tree->root;
  while (current != tree->nil && compare(data, current) != 0) {
    if (compare(data, current) < 0) {
      current = current->left;
    } else {
      current = current->right;
    }
  }

#ifdef BST_THREAD_SAFE
  UNLOCK(tree)
#endif

  return current;
}

bst_node_t *bst_minimum(bst_t *tree, bst_node_t *node) {
#ifdef BST_THREAD_SAFE
  LOCK(tree)
#endif
  while (node != tree->nil && node->left != tree->nil) {
    node = node->left;
  }
#ifdef BST_THREAD_SAFE
  UNLOCK(tree)
#endif
  return node;
}

bst_node_t *bst_maximum(bst_t *tree, bst_node_t *node) {
#ifdef BST_THREAD_SAFE
  LOCK(tree)
#endif
  while (node != tree->nil && node->right != tree->nil) {
    node = node->right;
  }
#ifdef BST_THREAD_SAFE
  UNLOCK(tree)
#endif
  return node;
}

void bst_transplant(bst_t *tree, bst_node_t *u, bst_node_t *v) {
#ifdef BST_THREAD_SAFE
  LOCK(tree);
#endif
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
#ifdef BST_THREAD_SAFE
  UNLOCK(tree);
#endif
}
void bst_delete_node(bst_t *tree, bst_node_t *node) {
#ifdef BST_THREAD_SAFE
  LOCK(tree);
#endif
  if (node->left == tree->nil) {
    bst_transplant(tree, node, node->right);
  } else if (node->right == tree->nil) {
    bst_transplant(tree, node, node->left);
  } else {
    bst_node_t *successor = bst_minimum(tree, node->right);
    if (successor->parent != node) {
      bst_transplant(tree, successor, successor->right);
      successor->right = node->right;
      successor->right->parent = successor;
    }
    bst_transplant(tree, node, successor);
    successor->left = node->left;
    successor->left->parent = successor;
  }
  tree->size -= 1;
#ifdef BST_THREAD_SAFE
  UNLOCK(tree);
#endif
}

void bst_delete(bst_t *tree) {
#ifdef BST_THREAD_SAFE
  LOCK(tree);
#endif
  tree->root = tree->nil;
  tree->size = 0;
#ifdef BST_THREAD_SAFE
  UNLOCK(tree);
#endif
}

void bst_destroy_node(bst_t *tree, bst_node_t *node,
                      void (*destroy)(bst_node_t *)) {
#ifdef BST_THREAD_SAFE
  LOCK(tree);
#endif
  if (node == tree->nil) {
#ifdef BST_THREAD_SAFE
    UNLOCK(tree);
#endif
    return;
  }

  if (destroy != NULL) {
    destroy(node);
  }
  tree->size -= 1;
#ifdef BST_THREAD_SAFE
  UNLOCK(tree);
#endif
}
void bst_destroy(bst_t *tree, bst_node_t *node, void (*destroy)(bst_node_t *)) {
#ifdef BST_THREAD_SAFE
  LOCK(tree);
#endif
  if (node == tree->nil) {
#ifdef BST_THREAD_SAFE
    UNLOCK(tree);
#endif

    return;
  }

  bst_destroy(tree, node->left, destroy);
  bst_destroy(tree, node->right, destroy);
  bst_delete_node(tree, node);

  if (destroy != NULL) {
    destroy(node);
  }
#ifdef BST_THREAD_SAFE
  UNLOCK(tree);
#endif
}
void bst_destroy_tree(bst_t *tree, void (*destroy)(bst_node_t *)) {
#ifdef BST_THREAD_SAFE
  LOCK(tree);
#endif

  bst_destroy(tree, tree->root, destroy);

  free(tree->nil);

#ifdef BST_THREAD_SAFE
  LOCK_DESTROY(tree);
#endif

  free(tree);
}

void bst_inorder_walk(bst_t *tree, bst_node_t *node, void (*callback)(void *)) {
#ifdef BST_THREAD_SAFE
  LOCK(tree)
#endif
  if (node != tree->nil) {
    bst_inorder_walk(tree, node->left, callback);
    callback(node);
    bst_inorder_walk(tree, node->right, callback);
  }
#ifdef BST_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void bst_preorder_walk(bst_t *tree, bst_node_t *node,
                       void (*callback)(void *)) {
#ifdef bst_THREAD_SAFE
  LOCK(tree)
#endif
  if (node != tree->nil) {
    callback(node);
    bst_preorder_walk(tree, node->left, callback);
    bst_preorder_walk(tree, node->right, callback);
  }
#ifdef BST_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void bst_postorder_walk(bst_t *tree, bst_node_t *node,
                        void (*callback)(void *)) {
  if (node != tree->nil) {
#ifdef BST_THREAD_SAFE
    LOCK(tree)
#endif
    bst_postorder_walk(tree, node->left, callback);
    bst_postorder_walk(tree, node->right, callback);
    callback(node);
  }
#ifdef BST_THREAD_SAFE
  UNLOCK(tree)
#endif
}

bst_node_t *bst_successor(bst_t *tree, bst_node_t *node_x) {
#ifdef BST_THREAD_SAFE
  LOCK(tree)
#endif
  bst_node_t *node_y;
  if (node_x->left != tree->nil) {
#ifdef BST_THREAD_SAFE
    UNLOCK(tree)
#endif
    return bst_minimum(tree, node_x->left);
  }
  node_y = node_x->parent;
  while (node_y != tree->nil && node_x == node_y->left) {
    node_x = node_y;
    node_y = node_y->parent;
  }
#ifdef BST_THREAD_SAFE
  UNLOCK(tree)
#endif
}
bst_node_t *bst_predecessor(bst_t *tree, bst_node_t *node_x) {
#ifdef BST_THREAD_SAFE
  LOCK(tree)
#endif
  bst_node_t *node_y;
  if (node_x->left != tree->nil) {
#ifdef BST_THREAD_SAFE
    UNLOCK(tree)
#endif
    return bst_maximum(tree, node_x->left);
  }
  node_y = node_x->parent;
  while (node_y != tree->nil && node_x == node_y->left) {
    node_x = node_y;
    node_y = node_y->parent;
  }
#ifdef BST_THREAD_SAFE
  UNLOCK(tree)
#endif
}
