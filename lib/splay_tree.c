#include "splay_tree.h"
#include <stdlib.h>

ds_splay_tree_t *ds_splay_tree_create(int (*compare)(ds_splay_tree_node_t *,
                                                     ds_splay_tree_node_t *)) {
  return ds_splay_tree_create_alloc(compare, malloc, free);
}

ds_splay_tree_t *ds_splay_tree_create_alloc(
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
  return tree;
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
    cmp = tree->compare(node, current);
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
    int cmp = tree->compare(node, current);
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

void ds_splay_tree_inorder_walk(ds_splay_tree_t *tree,
                                ds_splay_tree_node_t *node,
                                void (*callback)(ds_splay_tree_node_t *)) {
  if (node == tree->nil) {
    return;
  }
  ds_splay_tree_inorder_walk(tree, node->left, callback);
  callback(node);
  ds_splay_tree_inorder_walk(tree, node->right, callback);
}

void ds_splay_tree_inorder_walk_tree(ds_splay_tree_t *tree,
                                     void (*callback)(ds_splay_tree_node_t *)) {
  ds_splay_tree_inorder_walk(tree, tree->root, callback);
}

static void
splay_tree_destroy_recursive(ds_splay_tree_t *tree, ds_splay_tree_node_t *node,
                             void (*destroy)(ds_splay_tree_node_t *)) {
  if (node == tree->nil) {
    return;
  }
  splay_tree_destroy_recursive(tree, node->left, destroy);
  splay_tree_destroy_recursive(tree, node->right, destroy);
  if (destroy != NULL) {
    destroy(node);
  }
}

void ds_splay_tree_destroy(ds_splay_tree_t *tree,
                           void (*destroy)(ds_splay_tree_node_t *)) {
  splay_tree_destroy_recursive(tree, tree->root, destroy);
  tree->deallocator(tree->nil);
  tree->deallocator(tree);
}

static void splay_tree_clone_recursive(
    ds_splay_tree_t *tree, ds_splay_tree_t *new_tree,
    ds_splay_tree_node_t *node,
    ds_splay_tree_node_t *(*clone_node)(ds_splay_tree_node_t *)) {
  if (node == tree->nil) {
    return;
  }
  splay_tree_clone_recursive(tree, new_tree, node->left, clone_node);
  ds_splay_tree_insert(new_tree, clone_node(node));
  splay_tree_clone_recursive(tree, new_tree, node->right, clone_node);
}

ds_splay_tree_t *ds_splay_tree_clone(
    ds_splay_tree_t *tree,
    ds_splay_tree_node_t *(*clone_node)(ds_splay_tree_node_t *)) {
  ds_splay_tree_t *new_tree;
  new_tree = ds_splay_tree_create_alloc(tree->compare, tree->allocator,
                                        tree->deallocator);
  splay_tree_clone_recursive(tree, new_tree, tree->root, clone_node);
  return new_tree;
}
