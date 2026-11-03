#include "treap.h"
#include <stdlib.h>

ds_treap_t *ds_treap_create(int (*compare)(ds_treap_node_t *,
                                           ds_treap_node_t *)) {
  return ds_treap_create_alloc(compare, 0UL, malloc, free);
}

ds_treap_t *
ds_treap_create_alloc(int (*compare)(ds_treap_node_t *, ds_treap_node_t *),
                      unsigned long seed, void *(*allocator)(size_t),
                      void (*deallocator)(void *)) {
  ds_treap_t *treap = (ds_treap_t *)allocator(sizeof(ds_treap_t));
  treap->allocator = allocator;
  treap->deallocator = deallocator;
  treap->compare = compare;
  treap->seed = seed != 0UL ? seed : 0x9E3779B9UL;
  treap->nil = (ds_treap_node_t *)allocator(sizeof(ds_treap_node_t));
  treap->nil->left = treap->nil;
  treap->nil->right = treap->nil;
  treap->nil->parent = treap->nil;
  treap->nil->priority = 0UL;
  treap->root = treap->nil;
  treap->size = 0;
  return treap;
}

static unsigned long treap_random(ds_treap_t *treap) {
  treap->seed = treap->seed * 1103515245UL + 12345UL;
  return (treap->seed >> 16) & 0x7FFFUL;
}

void ds_treap_node_init(ds_treap_node_t *node) {
  node->left = NULL;
  node->right = NULL;
  node->parent = NULL;
  node->priority = 0UL;
}

static void treap_left_rotate(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *right = node->right;
  node->right = right->left;
  if (right->left != treap->nil) {
    right->left->parent = node;
  }
  right->parent = node->parent;
  if (node->parent == treap->nil) {
    treap->root = right;
  } else if (node == node->parent->left) {
    node->parent->left = right;
  } else {
    node->parent->right = right;
  }
  right->left = node;
  node->parent = right;
}

static void treap_right_rotate(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *left = node->left;
  node->left = left->right;
  if (left->right != treap->nil) {
    left->right->parent = node;
  }
  left->parent = node->parent;
  if (node->parent == treap->nil) {
    treap->root = left;
  } else if (node == node->parent->right) {
    node->parent->right = left;
  } else {
    node->parent->left = left;
  }
  left->right = node;
  node->parent = left;
}

void ds_treap_insert(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *current;
  ds_treap_node_t *parent;
  int cmp;
  if (node == treap->nil) {
    return;
  }
  current = treap->root;
  parent = treap->nil;
  cmp = 0;
  while (current != treap->nil) {
    parent = current;
    cmp = treap->compare(node, current);
    if (cmp < 0) {
      current = current->left;
    } else if (cmp > 0) {
      current = current->right;
    } else {
      return;
    }
  }
  node->parent = parent;
  node->left = treap->nil;
  node->right = treap->nil;
  node->priority = treap_random(treap);
  if (parent == treap->nil) {
    treap->root = node;
  } else if (cmp < 0) {
    parent->left = node;
  } else {
    parent->right = node;
  }
  while (node->parent != treap->nil &&
         node->parent->priority < node->priority) {
    if (node == node->parent->left) {
      treap_right_rotate(treap, node->parent);
    } else {
      treap_left_rotate(treap, node->parent);
    }
  }
  treap->size += 1;
}

ds_treap_node_t *ds_treap_search(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *current;
  current = treap->root;
  while (current != treap->nil) {
    int cmp = treap->compare(node, current);
    if (cmp < 0) {
      current = current->left;
    } else if (cmp > 0) {
      current = current->right;
    } else {
      return current;
    }
  }
  return treap->nil;
}

ds_treap_node_t *ds_treap_minimum(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *current;
  current = node;
  if (current == treap->nil) {
    return treap->nil;
  }
  while (current->left != treap->nil) {
    current = current->left;
  }
  return current;
}

ds_treap_node_t *ds_treap_maximum(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *current;
  current = node;
  if (current == treap->nil) {
    return treap->nil;
  }
  while (current->right != treap->nil) {
    current = current->right;
  }
  return current;
}

ds_treap_node_t *ds_treap_remove(ds_treap_t *treap, ds_treap_node_t *node) {
  ds_treap_node_t *target;
  target = ds_treap_search(treap, node);
  if (target == treap->nil) {
    return treap->nil;
  }
  while (target->left != treap->nil || target->right != treap->nil) {
    if (target->left == treap->nil) {
      treap_left_rotate(treap, target);
    } else if (target->right == treap->nil) {
      treap_right_rotate(treap, target);
    } else if (target->left->priority > target->right->priority) {
      treap_right_rotate(treap, target);
    } else {
      treap_left_rotate(treap, target);
    }
  }
  if (target->parent == treap->nil) {
    treap->root = treap->nil;
  } else if (target == target->parent->left) {
    target->parent->left = treap->nil;
  } else {
    target->parent->right = treap->nil;
  }
  target->left = treap->nil;
  target->right = treap->nil;
  target->parent = treap->nil;
  treap->size -= 1;
  return target;
}

bool ds_treap_is_empty(ds_treap_t *treap) {
  bool result;
  result = treap->root == treap->nil;
  return result;
}

void ds_treap_destroy_node(ds_treap_t *treap, ds_treap_node_t *node,
                           void (*destroy)(ds_treap_node_t *)) {
  ds_treap_node_t *removed;
  removed = ds_treap_remove(treap, node);
  if (removed != treap->nil && destroy != NULL) {
    destroy(removed);
  }
}

void ds_treap_inorder_walk(ds_treap_t *treap, ds_treap_node_t *node,
                           void (*callback)(ds_treap_node_t *)) {
  if (node == treap->nil) {
    return;
  }
  ds_treap_inorder_walk(treap, node->left, callback);
  callback(node);
  ds_treap_inorder_walk(treap, node->right, callback);
}

void ds_treap_inorder_walk_tree(ds_treap_t *treap,
                                void (*callback)(ds_treap_node_t *)) {
  ds_treap_inorder_walk(treap, treap->root, callback);
}

static void treap_destroy_recursive(ds_treap_t *treap, ds_treap_node_t *node,
                                    void (*destroy)(ds_treap_node_t *)) {
  if (node == treap->nil) {
    return;
  }
  treap_destroy_recursive(treap, node->left, destroy);
  treap_destroy_recursive(treap, node->right, destroy);
  if (destroy != NULL) {
    destroy(node);
  }
}

void ds_treap_destroy(ds_treap_t *treap, void (*destroy)(ds_treap_node_t *)) {
  treap_destroy_recursive(treap, treap->root, destroy);
  treap->deallocator(treap->nil);
  treap->deallocator(treap);
}

static void
treap_clone_recursive(ds_treap_t *treap, ds_treap_t *new_treap,
                      ds_treap_node_t *node,
                      ds_treap_node_t *(*clone_node)(ds_treap_node_t *)) {
  if (node == treap->nil) {
    return;
  }
  treap_clone_recursive(treap, new_treap, node->left, clone_node);
  ds_treap_insert(new_treap, clone_node(node));
  treap_clone_recursive(treap, new_treap, node->right, clone_node);
}

ds_treap_t *ds_treap_clone(ds_treap_t *treap,
                           ds_treap_node_t *(*clone_node)(ds_treap_node_t *)) {
  ds_treap_t *new_treap;
  new_treap = ds_treap_create_alloc(treap->compare, treap->seed,
                                    treap->allocator, treap->deallocator);
  treap_clone_recursive(treap, new_treap, treap->root, clone_node);
  return new_treap;
}
