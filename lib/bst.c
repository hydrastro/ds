#include "bst.h"
#include <stdlib.h>

bst_t *bst_create() {
  bst_t *tree = (bst_t *)malloc(sizeof(bst_t));
  tree->nil = (bst_node_t *)malloc(sizeof(bst_node_t));
  tree->root = tree->nil;

#ifdef BST_THREAD_SAFE
  LOCK_INIT(tree)
#endif

  return tree;
}

static bst_node_t *bst_create_node(bst_t *tree, int key) {
  bst_node_t *node = (bst_node_t *)malloc(sizeof(bst_node_t));
  node->key = key;
  node->left = tree->nil;
  node->right = tree->nil;
  return node;
}

void bst_insert(bst_t *tree, int key) {
#ifdef BST_THREAD_SAFE
  LOCK(tree)
#endif

  bst_node_t *node = bst_create_node(tree, key);
  if (tree->root == tree->nil) {
    tree->root = node;
  } else {
    bst_node_t *current = tree->root;
    bst_node_t *parent = tree->nil;
    while (current != tree->nil) {
      parent = current;
      if (key < current->key) {
        current = current->left;
      } else {
        current = current->right;
      }
    }
    if (key < parent->key) {
      parent->left = node;
    } else {
      parent->right = node;
    }
  }

#ifdef BST_THREAD_SAFE
  UNLOCK(tree)
#endif
}

bst_node_t *bst_search(bst_t *tree, int key) {
#ifdef BST_THREAD_SAFE
  LOCK(tree)
#endif

  bst_node_t *current = tree->root;
  while (current != tree->nil && current->key != key) {
    if (key < current->key) {
      current = current->left;
    } else {
      current = current->right;
    }
  }

#ifdef BST_THREAD_SAFE
  UNLOCK(tree)
#endif

  return current == tree->nil ? tree->nil : current;
}

static bst_node_t *bst_minimum_node(bst_t *tree, bst_node_t *node) {
  while (node != tree->nil && node->left != tree->nil) {
    node = node->left;
  }
  return node;
}

static bst_node_t *bst_maximum_node(bst_t *tree, bst_node_t *node) {
  while (node != tree->nil && node->right != tree->nil) {
    node = node->right;
  }
  return node;
}

static bst_node_t *bst_delete_node(bst_t *tree, bst_node_t *node, int key) {
  if (node == tree->nil)
    return node;

  if (key < node->key) {
    node->left = bst_delete_node(tree, node->left, key);
  } else if (key > node->key) {
    node->right = bst_delete_node(tree, node->right, key);
  } else {
    if (node->left == tree->nil) {
      bst_node_t *temp = node->right;
      free(node);
      return temp;
    } else if (node->right == tree->nil) {
      bst_node_t *temp = node->left;
      free(node);
      return temp;
    }

    bst_node_t *temp = bst_minimum_node(tree, node->right);
    node->key = temp->key;
    node->right = bst_delete_node(tree, node->right, temp->key);
  }
  return node;
}

void bst_delete(bst_t *tree, int key) {
#ifdef BST_THREAD_SAFE
  LOCK(tree)
#endif

  tree->root = bst_delete_node(tree, tree->root, key);

#ifdef BST_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void bst_destroy(bst_t *tree, void (*destroy_node)(bst_node_t *)) {
  while (tree->root != tree->nil) {
    bst_delete(tree, tree->root->key);
  }
  free(tree->nil);

#ifdef BST_THREAD_SAFE
  LOCK_DESTROY(tree)
#endif

  free(tree);
}

bst_node_t *bst_minimum(bst_t *tree) {
#ifdef BST_THREAD_SAFE
  LOCK(tree)
#endif

  bst_node_t *node = bst_minimum_node(tree, tree->root);

#ifdef BST_THREAD_SAFE
  UNLOCK(tree)
#endif

  return node == tree->nil ? tree->nil : node;
}

bst_node_t *bst_maximum(bst_t *tree) {
#ifdef BST_THREAD_SAFE
  LOCK(tree)
#endif

  bst_node_t *node = bst_maximum_node(tree, tree->root);

#ifdef BST_THREAD_SAFE
  UNLOCK(tree)
#endif

  return node == tree->nil ? tree->nil : node;
}
