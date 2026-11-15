#include "bst.h"
#include <stdlib.h>

ds_bst_t *ds_bst_create(void) { return ds_bst_create_alloc(malloc, free); }

ds_bst_t *ds_bst_create_alloc(void *(*allocator)(size_t),
                              void (*deallocator)(void *)) {
  ds_bst_t *tree;
  if (allocator == NULL || deallocator == NULL) {
    return NULL;
  }
  tree = (ds_bst_t *)allocator(sizeof(ds_bst_t));
  if (tree == NULL) {
    return NULL;
  }
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->nil = (ds_bst_node_t *)allocator(sizeof(ds_bst_node_t));
  if (tree->nil == NULL) {
    deallocator(tree);
    return NULL;
  }
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
/* Walks follow the parent links, in O(1) space whatever the height (an
 * unbalanced tree can be as deep as it is large). The state says where
 * the walk came from: from above, from the left subtree, from the right. */
#define BST_FROM_ABOVE 0
#define BST_FROM_LEFT 1
#define BST_FROM_RIGHT 2
#define BST_PRE 0
#define BST_IN 1
#define BST_POST 2

static void bst_walk(ds_bst_t *tree, ds_bst_node_t *root, int order,
                     void (*callback)(void *)) {
  ds_bst_node_t *x = root;
  int from = BST_FROM_ABOVE;
  if (root == tree->nil) {
    return;
  }
  for (;;) {
    ds_bst_node_t *p;
    int side;
    if (from == BST_FROM_ABOVE) {
      if (order == BST_PRE) {
        callback(x);
      }
      if (x->left != tree->nil) {
        x = x->left;
        continue;
      }
      from = BST_FROM_LEFT;
    }
    if (from == BST_FROM_LEFT) {
      if (order == BST_IN) {
        callback(x);
      }
      if (x->right != tree->nil) {
        x = x->right;
        from = BST_FROM_ABOVE;
        continue;
      }
    }
    /* leaving x: read its links before a post-order visit */
    p = x->parent;
    side = x == root ? BST_FROM_ABOVE
                     : (x == p->left ? BST_FROM_LEFT : BST_FROM_RIGHT);
    if (order == BST_POST) {
      callback(x);
    }
    if (side == BST_FROM_ABOVE) {
      return;
    }
    x = p;
    from = side;
  }
}

void ds_bst_destroy_recursive(ds_bst_t *tree, ds_bst_node_t *node,
                              void (*destroy)(ds_bst_node_t *)) {
  ds_bst_node_t *x = node;
  int from = BST_FROM_ABOVE;
  if (node == tree->nil || destroy == NULL) {
    return;
  }
  for (;;) {
    ds_bst_node_t *p;
    int side;
    if (from == BST_FROM_ABOVE && x->left != tree->nil) {
      x = x->left;
      continue;
    }
    if (from != BST_FROM_RIGHT && x->right != tree->nil) {
      x = x->right;
      from = BST_FROM_ABOVE;
      continue;
    }
    p = x->parent;
    side = x == node ? BST_FROM_ABOVE
                     : (x == p->left ? BST_FROM_LEFT : BST_FROM_RIGHT);
    destroy(x);
    if (side == BST_FROM_ABOVE) {
      return;
    }
    x = p;
    from = side;
  }
}

void ds_bst_destroy_tree(ds_bst_t *tree, void (*destroy)(ds_bst_node_t *)) {
  ds_bst_destroy_recursive(tree, tree->root, destroy);

  tree->deallocator(tree->nil);

  tree->deallocator(tree);
}

void ds_bst_inorder_walk_helper(ds_bst_t *tree, ds_bst_node_t *node,
                                void (*callback)(void *)) {
  bst_walk(tree, node, BST_IN, callback);
}

void ds_bst_inorder_walk(ds_bst_t *tree, ds_bst_node_t *node,
                         void (*callback)(void *)) {
  bst_walk(tree, node, BST_IN, callback);
}

void ds_bst_inorder_walk_tree(ds_bst_t *tree, void (*callback)(void *)) {
  bst_walk(tree, tree->root, BST_IN, callback);
}

void ds_bst_preorder_walk_helper(ds_bst_t *tree, ds_bst_node_t *node,
                                 void (*callback)(void *)) {
  bst_walk(tree, node, BST_PRE, callback);
}

void ds_bst_preorder_walk(ds_bst_t *tree, ds_bst_node_t *node,
                          void (*callback)(void *)) {
  bst_walk(tree, node, BST_PRE, callback);
}

void ds_bst_preorder_walk_tree(ds_bst_t *tree, void (*callback)(void *)) {
  bst_walk(tree, tree->root, BST_PRE, callback);
}

void ds_bst_postorder_walk_helper(ds_bst_t *tree, ds_bst_node_t *node,
                                  void (*callback)(void *)) {
  bst_walk(tree, node, BST_POST, callback);
}
void ds_bst_postorder_walk(ds_bst_t *tree, ds_bst_node_t *node,
                           void (*callback)(void *)) {
  bst_walk(tree, node, BST_POST, callback);
}
void ds_bst_postorder_walk_tree(ds_bst_t *tree, void (*callback)(void *)) {
  bst_walk(tree, tree->root, BST_POST, callback);
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

/* a copy of the subtree at node, built in pre-order along the parent
 * links of both trees; the copy's root has new_tree->nil as its parent.
 * *out gets the copy's root (new_tree->nil for an empty subtree). Every
 * clone is linked in before the next is made, so when clone_node returns
 * NULL (false) *out is still a valid tree holding every clone made. */
static bool bst_clone_subtree(const ds_bst_t *tree, ds_bst_t *new_tree,
                              ds_bst_node_t *node,
                              ds_bst_node_t *(*clone_node)(ds_bst_node_t *),
                              ds_bst_node_t **out) {
  ds_bst_node_t *x = node;
  ds_bst_node_t *copy;
  ds_bst_node_t *cx;
  int from = BST_FROM_ABOVE;
  *out = new_tree->nil;
  if (node == tree->nil) {
    return true;
  }
  copy = clone_node(node);
  if (copy == NULL) {
    return false;
  }
  copy->left = new_tree->nil;
  copy->right = new_tree->nil;
  copy->parent = new_tree->nil;
  *out = copy;
  cx = copy;
  for (;;) {
    ds_bst_node_t *c;
    if (from == BST_FROM_ABOVE && x->left != tree->nil) {
      c = clone_node(x->left);
      if (c == NULL) {
        return false;
      }
      c->left = new_tree->nil;
      c->right = new_tree->nil;
      c->parent = cx;
      cx->left = c;
      x = x->left;
      cx = c;
      continue;
    }
    if (from != BST_FROM_RIGHT && x->right != tree->nil) {
      c = clone_node(x->right);
      if (c == NULL) {
        return false;
      }
      c->left = new_tree->nil;
      c->right = new_tree->nil;
      c->parent = cx;
      cx->right = c;
      x = x->right;
      cx = c;
      from = BST_FROM_ABOVE;
      continue;
    }
    if (x == node) {
      return true;
    }
    from = x == x->parent->left ? BST_FROM_LEFT : BST_FROM_RIGHT;
    x = x->parent;
    cx = cx->parent;
  }
}

ds_bst_node_t *
ds_bst_clone_recursive(ds_bst_t *tree, ds_bst_t *new_tree, ds_bst_node_t *node,
                       ds_bst_node_t *(*clone_node)(ds_bst_node_t *)) {
  ds_bst_node_t *copy;
  (void)bst_clone_subtree(tree, new_tree, node, clone_node, &copy);
  return copy;
}

ds_bst_t *ds_bst_clone(ds_bst_t *tree,
                       ds_bst_node_t *(*clone_node)(ds_bst_node_t *)) {
  ds_bst_t *new_tree;
  if (tree == NULL || clone_node == NULL) {
    return NULL;
  }
  new_tree = ds_bst_create_alloc(tree->allocator, tree->deallocator);
  if (new_tree == NULL) {
    return NULL;
  }
  if (!bst_clone_subtree(tree, new_tree, tree->root, clone_node,
                         &new_tree->root)) {
    ds_bst_destroy_tree(new_tree, NULL); /* the clones are not released */
    return NULL;
  }
  new_tree->size = tree->size;

  return new_tree;
}

ds_status_t ds_bst_clone_ex(const ds_bst_t *tree,
                            ds_bst_node_t *(*clone_node)(ds_bst_node_t *),
                            void (*destroy_clone)(ds_bst_node_t *),
                            ds_bst_t **out) {
  ds_bst_t *new_tree;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (tree == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  new_tree = ds_bst_create_alloc(tree->allocator, tree->deallocator);
  if (new_tree == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!bst_clone_subtree(tree, new_tree, tree->root, clone_node,
                         &new_tree->root)) {
    ds_bst_destroy_tree(new_tree, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  new_tree->size = tree->size;
  *out = new_tree;
  return DS_OK;
}
