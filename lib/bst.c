#include "bst.h"
#include <stdlib.h>

ds_bst_t *ds_bst_create(void) {
  return ds_bst_create_alloc(malloc, free);
}

ds_bst_t *ds_bst_create_alloc(void *(*allocator)(size_t),
                              void (*deallocator)(void *)) {
  ds_bst_t *tree = (ds_bst_t *)allocator(sizeof(ds_bst_t));
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->nil = (ds_bst_node_t *)allocator(sizeof(ds_bst_node_t));
  tree->nil->left = tree->nil;
  tree->nil->right = tree->nil;
  tree->nil->parent = tree->nil;
  tree->root = tree->nil;
  tree->size = 0;

  return tree;
}

void ds_bst_insert(ds_bst_t *tree, ds_bst_node_t *node,
                   int (*compare)(ds_bst_node_t *, ds_bst_node_t *)) {
  if (tree->root == tree->nil) {
    tree->root = node;
    node->parent = node->left = node->right = tree->nil;
  } else {
    ds_bst_node_t *current = tree->root;
    ds_bst_node_t *parent = tree->nil;
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
}

ds_bst_node_t *ds_bst_search(ds_bst_t *tree, ds_bst_node_t *data,
                             int (*compare)(ds_bst_node_t *, ds_bst_node_t *)) {
  ds_bst_node_t *current;
  current = tree->root;
  while (current != tree->nil && compare(data, current) != 0) {
    if (compare(data, current) < 0) {
      current = current->left;
    } else {
      current = current->right;
    }
  }

  return current;
}

ds_bst_node_t *ds_bst_minimum(ds_bst_t *tree, ds_bst_node_t *node) {
  while (node != tree->nil && node->left != tree->nil) {
    node = node->left;
  }
  return node;
}

ds_bst_node_t *ds_bst_maximum(ds_bst_t *tree, ds_bst_node_t *node) {
  while (node != tree->nil && node->right != tree->nil) {
    node = node->right;
  }
  return node;
}

void ds_bst_transplant(ds_bst_t *tree, ds_bst_node_t *u, ds_bst_node_t *v) {
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
void ds_bst_delete_node(ds_bst_t *tree, ds_bst_node_t *node) {
  if (node->left == tree->nil) {
    ds_bst_transplant(tree, node, node->right);
  } else if (node->right == tree->nil) {
    ds_bst_transplant(tree, node, node->left);
  } else {
    ds_bst_node_t *successor = ds_bst_minimum(tree, node->right);
    if (successor->parent != node) {
      ds_bst_transplant(tree, successor, successor->right);
      successor->right = node->right;
      successor->right->parent = successor;
    }
    ds_bst_transplant(tree, node, successor);
    successor->left = node->left;
    successor->left->parent = successor;
  }
  tree->size -= 1;
}

void ds_bst_delete_tree(ds_bst_t *tree) {
  tree->root = tree->nil;
  tree->size = 0;
}

void ds_bst_destroy_node(ds_bst_t *tree, ds_bst_node_t *node,
                         void (*destroy)(ds_bst_node_t *)) {
  if (node == tree->nil) {
    return;
  }

  ds_bst_delete_node(tree, node);
  if (destroy != NULL) {
    destroy(node);
  }
}
void ds_bst_destroy_recursive(ds_bst_t *tree, ds_bst_node_t *node,
                              void (*destroy)(ds_bst_node_t *)) {
  if (node == tree->nil) {
    return;
  }

  ds_bst_destroy_recursive(tree, node->left, destroy);
  ds_bst_destroy_recursive(tree, node->right, destroy);

  if (destroy != NULL) {
    destroy(node);
  }
}
void ds_bst_destroy_tree(ds_bst_t *tree, void (*destroy)(ds_bst_node_t *)) {
  ds_bst_destroy_recursive(tree, tree->root, destroy);

  tree->deallocator(tree->nil);

  tree->deallocator(tree);
}

void ds_bst_inorder_walk_helper(ds_bst_t *tree, ds_bst_node_t *node,
                                void (*callback)(void *)) {
  if (node != tree->nil) {
    ds_bst_inorder_walk_helper(tree, node->left, callback);
    callback(node);
    ds_bst_inorder_walk_helper(tree, node->right, callback);
  }
}

void ds_bst_inorder_walk(ds_bst_t *tree, ds_bst_node_t *node,
                         void (*callback)(void *)) {
  ds_bst_inorder_walk_helper(tree, node, callback);
}

void ds_bst_inorder_walk_tree(ds_bst_t *tree, void (*callback)(void *)) {
  ds_bst_inorder_walk_helper(tree, tree->root, callback);
}

void ds_bst_preorder_walk_helper(ds_bst_t *tree, ds_bst_node_t *node,
                                 void (*callback)(void *)) {
  if (node != tree->nil) {
    callback(node);
    ds_bst_preorder_walk_helper(tree, node->left, callback);
    ds_bst_preorder_walk_helper(tree, node->right, callback);
  }
}

void ds_bst_preorder_walk(ds_bst_t *tree, ds_bst_node_t *node,
                          void (*callback)(void *)) {
  ds_bst_preorder_walk_helper(tree, node, callback);
}

void ds_bst_preorder_walk_tree(ds_bst_t *tree, void (*callback)(void *)) {
  ds_bst_preorder_walk_helper(tree, tree->root, callback);
}

void ds_bst_postorder_walk_helper(ds_bst_t *tree, ds_bst_node_t *node,
                                  void (*callback)(void *)) {
  if (node != tree->nil) {
    ds_bst_postorder_walk_helper(tree, node->left, callback);
    ds_bst_postorder_walk_helper(tree, node->right, callback);
    callback(node);
  }
}
void ds_bst_postorder_walk(ds_bst_t *tree, ds_bst_node_t *node,
                           void (*callback)(void *)) {
  ds_bst_postorder_walk_helper(tree, node, callback);
}
void ds_bst_postorder_walk_tree(ds_bst_t *tree, void (*callback)(void *)) {
  ds_bst_postorder_walk_helper(tree, tree->root, callback);
}

ds_bst_node_t *ds_bst_successor(ds_bst_t *tree, ds_bst_node_t *node_x) {
  ds_bst_node_t *node_y;
  if (node_x->right != tree->nil) {
    return ds_bst_minimum(tree, node_x->right);
  }
  node_y = node_x->parent;
  while (node_y != tree->nil && node_x == node_y->right) {
    node_x = node_y;
    node_y = node_y->parent;
  }
  return node_y;
}

ds_bst_node_t *ds_bst_predecessor(ds_bst_t *tree, ds_bst_node_t *node_x) {
  ds_bst_node_t *node_y;
  if (node_x->left != tree->nil) {
    return ds_bst_maximum(tree, node_x->left);
  }
  node_y = node_x->parent;
  while (node_y != tree->nil && node_x == node_y->left) {
    node_x = node_y;
    node_y = node_y->parent;
  }
  return node_y;
}

ds_bst_node_t *
ds_bst_clone_recursive(ds_bst_t *tree, ds_bst_t *new_tree, ds_bst_node_t *node,
                       ds_bst_node_t *(*clone_node)(ds_bst_node_t *)) {
  ds_bst_node_t *new_node;
  if (node == tree->nil) {
    return new_tree->nil;
  }

  new_node = clone_node(node);
  new_node->left =
      ds_bst_clone_recursive(tree, new_tree, node->left, clone_node);
  new_node->right =
      ds_bst_clone_recursive(tree, new_tree, node->right, clone_node);
  if (new_node->left != new_tree->nil) {
    new_node->left->parent = new_node;
  }
  if (new_node->right != new_tree->nil) {
    new_node->right->parent = new_node;
  }

  return new_node;
}

ds_bst_t *ds_bst_clone(ds_bst_t *tree,
                       ds_bst_node_t *(*clone_node)(ds_bst_node_t *)) {
  ds_bst_t *new_tree;
  new_tree = ds_bst_create_alloc(tree->allocator, tree->deallocator);
  new_tree->root =
      ds_bst_clone_recursive(tree, new_tree, tree->root, clone_node);
  new_tree->size = tree->size;

  return new_tree;
}
