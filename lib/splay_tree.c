#include "splay_tree.h"
#include <stdlib.h>

ds_splay_tree_t *FUNC(splay_tree_create)(
    int (*compare)(ds_splay_tree_node_t *, ds_splay_tree_node_t *)) {
  return FUNC(splay_tree_create_alloc)(compare, malloc, free);
}

ds_splay_tree_t *FUNC(splay_tree_create_alloc)(
    int (*compare)(ds_splay_tree_node_t *, ds_splay_tree_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *)) {
  ds_splay_tree_t *tree = (ds_splay_tree_t *)allocator(sizeof(ds_splay_tree_t));
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->compare = compare;
  tree->nil = (ds_splay_tree_node_t *)allocator(sizeof(ds_splay_tree_node_t));
  tree->nil->left = tree->nil;
  tree->nil->right = tree->nil;
  tree->nil->parent = tree->nil;
  tree->root = tree->nil;
  tree->size = 0;
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(tree);
#endif
  return tree;
}

static void FUNC(splay_tree_left_rotate)(ds_splay_tree_t *tree,
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

static void FUNC(splay_tree_right_rotate)(ds_splay_tree_t *tree,
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

static void FUNC(splay_tree_splay)(ds_splay_tree_t *tree,
                                   ds_splay_tree_node_t *node) {
  while (node->parent != tree->nil) {
    ds_splay_tree_node_t *parent = node->parent;
    ds_splay_tree_node_t *grand = parent->parent;
    if (grand == tree->nil) {
      if (node == parent->left) {
        FUNC(splay_tree_right_rotate)(tree, parent);
      } else {
        FUNC(splay_tree_left_rotate)(tree, parent);
      }
    } else if (node == parent->left && parent == grand->left) {
      FUNC(splay_tree_right_rotate)(tree, grand);
      FUNC(splay_tree_right_rotate)(tree, parent);
    } else if (node == parent->right && parent == grand->right) {
      FUNC(splay_tree_left_rotate)(tree, grand);
      FUNC(splay_tree_left_rotate)(tree, parent);
    } else if (node == parent->right && parent == grand->left) {
      FUNC(splay_tree_left_rotate)(tree, parent);
      FUNC(splay_tree_right_rotate)(tree, grand);
    } else {
      FUNC(splay_tree_right_rotate)(tree, parent);
      FUNC(splay_tree_left_rotate)(tree, grand);
    }
  }
}

void FUNC(splay_tree_insert)(ds_splay_tree_t *tree,
                             ds_splay_tree_node_t *node) {
  ds_splay_tree_node_t *current;
  ds_splay_tree_node_t *parent;
  int cmp;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  if (node == tree->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(tree);
#endif
    return;
  }
  current = tree->root;
  parent = tree->nil;
  cmp = 0;
  while (current != tree->nil) {
    parent = current;
    cmp = tree->compare(node, current);
    if (cmp < 0) {
      current = current->left;
    } else if (cmp > 0) {
      current = current->right;
    } else {
      FUNC(splay_tree_splay)(tree, current);
#ifdef DS_THREAD_SAFE
      UNLOCK(tree);
#endif
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
  FUNC(splay_tree_splay)(tree, node);
  tree->size += 1;
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
}

ds_splay_tree_node_t *FUNC(splay_tree_search)(ds_splay_tree_t *tree,
                                              ds_splay_tree_node_t *node) {
  ds_splay_tree_node_t *current;
  ds_splay_tree_node_t *last;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  current = tree->root;
  last = tree->nil;
  while (current != tree->nil) {
    int cmp = tree->compare(node, current);
    last = current;
    if (cmp < 0) {
      current = current->left;
    } else if (cmp > 0) {
      current = current->right;
    } else {
      FUNC(splay_tree_splay)(tree, current);
#ifdef DS_THREAD_SAFE
      UNLOCK(tree);
#endif
      return current;
    }
  }
  if (last != tree->nil) {
    FUNC(splay_tree_splay)(tree, last);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return tree->nil;
}

ds_splay_tree_node_t *FUNC(splay_tree_minimum)(ds_splay_tree_t *tree,
                                               ds_splay_tree_node_t *node) {
  ds_splay_tree_node_t *current;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  current = node;
  if (current == tree->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(tree);
#endif
    return tree->nil;
  }
  while (current->left != tree->nil) {
    current = current->left;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return current;
}

ds_splay_tree_node_t *FUNC(splay_tree_maximum)(ds_splay_tree_t *tree,
                                               ds_splay_tree_node_t *node) {
  ds_splay_tree_node_t *current;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  current = node;
  if (current == tree->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(tree);
#endif
    return tree->nil;
  }
  while (current->right != tree->nil) {
    current = current->right;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return current;
}

ds_splay_tree_node_t *FUNC(splay_tree_remove)(ds_splay_tree_t *tree,
                                              ds_splay_tree_node_t *node) {
  ds_splay_tree_node_t *target;
  ds_splay_tree_node_t *left_subtree;
  ds_splay_tree_node_t *right_subtree;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  target = FUNC(splay_tree_search)(tree, node);
  if (target == tree->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(tree);
#endif
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
    FUNC(splay_tree_splay)(tree, max_left);
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
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return target;
}

bool FUNC(splay_tree_is_empty)(ds_splay_tree_t *tree) {
  bool result;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  result = tree->root == tree->nil;
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return result;
}

void FUNC(splay_tree_destroy_node)(ds_splay_tree_t *tree,
                                   ds_splay_tree_node_t *node,
                                   void (*destroy)(ds_splay_tree_node_t *)) {
  ds_splay_tree_node_t *removed;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  removed = FUNC(splay_tree_remove)(tree, node);
  if (removed != tree->nil && destroy != NULL) {
    destroy(removed);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
}

void FUNC(splay_tree_inorder_walk)(ds_splay_tree_t *tree,
                                   ds_splay_tree_node_t *node,
                                   void (*callback)(ds_splay_tree_node_t *)) {
  if (node == tree->nil) {
    return;
  }
  FUNC(splay_tree_inorder_walk)(tree, node->left, callback);
  callback(node);
  FUNC(splay_tree_inorder_walk)(tree, node->right, callback);
}

void FUNC(splay_tree_inorder_walk_tree)(
    ds_splay_tree_t *tree, void (*callback)(ds_splay_tree_node_t *)) {
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  FUNC(splay_tree_inorder_walk)(tree, tree->root, callback);
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
}

static void
FUNC(splay_tree_destroy_recursive)(ds_splay_tree_t *tree,
                                   ds_splay_tree_node_t *node,
                                   void (*destroy)(ds_splay_tree_node_t *)) {
  if (node == tree->nil) {
    return;
  }
  FUNC(splay_tree_destroy_recursive)(tree, node->left, destroy);
  FUNC(splay_tree_destroy_recursive)(tree, node->right, destroy);
  if (destroy != NULL) {
    destroy(node);
  }
}

void FUNC(splay_tree_destroy)(ds_splay_tree_t *tree,
                              void (*destroy)(ds_splay_tree_node_t *)) {
  FUNC(splay_tree_destroy_recursive)(tree, tree->root, destroy);
  tree->deallocator(tree->nil);
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(tree);
#endif
  tree->deallocator(tree);
}

static void FUNC(splay_tree_clone_recursive)(
    ds_splay_tree_t *tree, ds_splay_tree_t *new_tree,
    ds_splay_tree_node_t *node,
    ds_splay_tree_node_t *(*clone_node)(ds_splay_tree_node_t *)) {
  if (node == tree->nil) {
    return;
  }
  FUNC(splay_tree_clone_recursive)(tree, new_tree, node->left, clone_node);
  FUNC(splay_tree_insert)(new_tree, clone_node(node));
  FUNC(splay_tree_clone_recursive)(tree, new_tree, node->right, clone_node);
}

ds_splay_tree_t *FUNC(splay_tree_clone)(
    ds_splay_tree_t *tree,
    ds_splay_tree_node_t *(*clone_node)(ds_splay_tree_node_t *)) {
  ds_splay_tree_t *new_tree;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  new_tree = FUNC(splay_tree_create_alloc)(tree->compare, tree->allocator,
                                           tree->deallocator);
  FUNC(splay_tree_clone_recursive)(tree, new_tree, tree->root, clone_node);
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return new_tree;
}
