#include "order_statistic_tree.h"
#include <stdlib.h>

static int FUNC(ost_height)(ds_ost_t *tree, ds_ost_node_t *node) {
  return node == tree->nil ? 0 : node->height;
}

static size_t FUNC(ost_node_size)(ds_ost_t *tree, ds_ost_node_t *node) {
  return node == tree->nil ? 0 : node->size;
}

static int FUNC(ost_max_int)(int a, int b) {
  return a > b ? a : b;
}

static void FUNC(ost_update)(ds_ost_t *tree, ds_ost_node_t *node) {
  if (node == tree->nil) {
    return;
  }
  node->size = 1 + FUNC(ost_node_size)(tree, node->left) +
               FUNC(ost_node_size)(tree, node->right);
  node->height = 1 + FUNC(ost_max_int)(FUNC(ost_height)(tree, node->left),
                                       FUNC(ost_height)(tree, node->right));
}

static int FUNC(ost_balance_factor)(ds_ost_t *tree, ds_ost_node_t *node) {
  if (node == tree->nil) {
    return 0;
  }
  return FUNC(ost_height)(tree, node->left) -
         FUNC(ost_height)(tree, node->right);
}

ds_ost_t *FUNC(ost_create)(int (*compare)(ds_ost_node_t *, ds_ost_node_t *)) {
  return FUNC(ost_create_alloc)(compare, malloc, free);
}

ds_ost_t *FUNC(ost_create_alloc)(int (*compare)(ds_ost_node_t *,
                                                ds_ost_node_t *),
                                 void *(*allocator)(size_t),
                                 void (*deallocator)(void *)) {
  ds_ost_t *tree = (ds_ost_t *)allocator(sizeof(ds_ost_t));
  tree->compare = compare;
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->size = 0;
  tree->nil = (ds_ost_node_t *)allocator(sizeof(ds_ost_node_t));
  tree->nil->left = tree->nil;
  tree->nil->right = tree->nil;
  tree->nil->parent = tree->nil;
  tree->nil->size = 0;
  tree->nil->height = 0;
  tree->root = tree->nil;
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(tree);
#endif
  return tree;
}

static void FUNC(ost_rotate_left)(ds_ost_t *tree, ds_ost_node_t *x) {
  ds_ost_node_t *y = x->right;
  x->right = y->left;
  if (y->left != tree->nil) {
    y->left->parent = x;
  }
  y->parent = x->parent;
  if (x->parent == tree->nil) {
    tree->root = y;
  } else if (x == x->parent->left) {
    x->parent->left = y;
  } else {
    x->parent->right = y;
  }
  y->left = x;
  x->parent = y;
  FUNC(ost_update)(tree, x);
  FUNC(ost_update)(tree, y);
}

static void FUNC(ost_rotate_right)(ds_ost_t *tree, ds_ost_node_t *x) {
  ds_ost_node_t *y = x->left;
  x->left = y->right;
  if (y->right != tree->nil) {
    y->right->parent = x;
  }
  y->parent = x->parent;
  if (x->parent == tree->nil) {
    tree->root = y;
  } else if (x == x->parent->right) {
    x->parent->right = y;
  } else {
    x->parent->left = y;
  }
  y->right = x;
  x->parent = y;
  FUNC(ost_update)(tree, x);
  FUNC(ost_update)(tree, y);
}

/* rebalance from node up to the root, updating sizes/heights along the way */
static void FUNC(ost_rebalance)(ds_ost_t *tree, ds_ost_node_t *node) {
  while (node != tree->nil) {
    int balance;
    FUNC(ost_update)(tree, node);
    balance = FUNC(ost_balance_factor)(tree, node);
    if (balance > 1) {
      if (FUNC(ost_balance_factor)(tree, node->left) < 0) {
        FUNC(ost_rotate_left)(tree, node->left);
      }
      FUNC(ost_rotate_right)(tree, node);
      node = node->parent; /* the new subtree root */
    } else if (balance < -1) {
      if (FUNC(ost_balance_factor)(tree, node->right) > 0) {
        FUNC(ost_rotate_right)(tree, node->right);
      }
      FUNC(ost_rotate_left)(tree, node);
      node = node->parent;
    }
    node = node->parent;
  }
}

bool FUNC(ost_insert)(ds_ost_t *tree, ds_ost_node_t *node) {
  ds_ost_node_t *current;
  ds_ost_node_t *parent;
  int cmp = 0;
  bool result = true;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  current = tree->root;
  parent = tree->nil;
  while (current != tree->nil) {
    parent = current;
    cmp = tree->compare(node, current);
    if (cmp == 0) {
#ifdef DS_THREAD_SAFE
      UNLOCK(tree);
#endif
      return false;
    }
    current = cmp < 0 ? current->left : current->right;
  }
  node->left = tree->nil;
  node->right = tree->nil;
  node->parent = parent;
  node->size = 1;
  node->height = 1;
  if (parent == tree->nil) {
    tree->root = node;
  } else if (cmp < 0) {
    parent->left = node;
  } else {
    parent->right = node;
  }
  FUNC(ost_rebalance)(tree, parent);
  tree->size += 1;
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return result;
}

static ds_ost_node_t *FUNC(ost_min_node)(ds_ost_t *tree, ds_ost_node_t *node) {
  while (node->left != tree->nil) {
    node = node->left;
  }
  return node;
}

ds_ost_node_t *FUNC(ost_search)(ds_ost_t *tree, ds_ost_node_t *key) {
  ds_ost_node_t *current;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  current = tree->root;
  while (current != tree->nil) {
    int cmp = tree->compare(key, current);
    if (cmp == 0) {
      break;
    }
    current = cmp < 0 ? current->left : current->right;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return current;
}

/* transplant: replace subtree u with subtree v */
static void FUNC(ost_transplant)(ds_ost_t *tree, ds_ost_node_t *u,
                                 ds_ost_node_t *v) {
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

bool FUNC(ost_remove)(ds_ost_t *tree, ds_ost_node_t *key,
                      ds_ost_node_t **out_node) {
  ds_ost_node_t *z;
  ds_ost_node_t *fixup_from;
  bool result = false;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  z = tree->root;
  while (z != tree->nil) {
    int cmp = tree->compare(key, z);
    if (cmp == 0) {
      break;
    }
    z = cmp < 0 ? z->left : z->right;
  }
  if (z == tree->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(tree);
#endif
    return false;
  }
  if (out_node != NULL) {
    *out_node = z;
  }
  if (z->left == tree->nil) {
    fixup_from = z->parent;
    FUNC(ost_transplant)(tree, z, z->right);
  } else if (z->right == tree->nil) {
    fixup_from = z->parent;
    FUNC(ost_transplant)(tree, z, z->left);
  } else {
    ds_ost_node_t *y = FUNC(ost_min_node)(tree, z->right);
    if (y->parent == z) {
      fixup_from = y;
    } else {
      fixup_from = y->parent;
      FUNC(ost_transplant)(tree, y, y->right);
      y->right = z->right;
      y->right->parent = y;
    }
    FUNC(ost_transplant)(tree, z, y);
    y->left = z->left;
    y->left->parent = y;
  }
  FUNC(ost_rebalance)(tree, fixup_from);
  tree->size -= 1;
  result = true;
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return result;
}

ds_ost_node_t *FUNC(ost_select)(ds_ost_t *tree, size_t k) {
  ds_ost_node_t *current;
  ds_ost_node_t *result;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  result = tree->nil;
  current = tree->root;
  if (k < tree->size) {
    while (current != tree->nil) {
      size_t left_size = FUNC(ost_node_size)(tree, current->left);
      if (k < left_size) {
        current = current->left;
      } else if (k > left_size) {
        k -= left_size + 1;
        current = current->right;
      } else {
        result = current;
        break;
      }
    }
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return result;
}

size_t FUNC(ost_rank)(ds_ost_t *tree, ds_ost_node_t *key) {
  ds_ost_node_t *current;
  size_t rank = 0;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  current = tree->root;
  while (current != tree->nil) {
    int cmp = tree->compare(key, current);
    if (cmp <= 0) {
      current = current->left;
    } else {
      rank += FUNC(ost_node_size)(tree, current->left) + 1;
      current = current->right;
    }
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return rank;
}

ds_ost_node_t *FUNC(ost_minimum)(ds_ost_t *tree) {
  ds_ost_node_t *result;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  result = tree->root == tree->nil ? tree->nil
                                   : FUNC(ost_min_node)(tree, tree->root);
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return result;
}

ds_ost_node_t *FUNC(ost_maximum)(ds_ost_t *tree) {
  ds_ost_node_t *current;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  current = tree->root;
  if (current != tree->nil) {
    while (current->right != tree->nil) {
      current = current->right;
    }
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return current;
}

size_t FUNC(ost_size)(ds_ost_t *tree) {
  size_t result;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  result = tree->size;
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return result;
}

bool FUNC(ost_is_empty)(ds_ost_t *tree) {
  bool result;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  result = tree->size == 0;
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return result;
}

static void FUNC(ost_walk_recursive)(ds_ost_t *tree, ds_ost_node_t *node,
                                     void (*callback)(ds_ost_node_t *)) {
  if (node == tree->nil) {
    return;
  }
  FUNC(ost_walk_recursive)(tree, node->left, callback);
  callback(node);
  FUNC(ost_walk_recursive)(tree, node->right, callback);
}

void FUNC(ost_inorder_walk)(ds_ost_t *tree, void (*callback)(ds_ost_node_t *)) {
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  FUNC(ost_walk_recursive)(tree, tree->root, callback);
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
}

static void FUNC(ost_destroy_recursive)(ds_ost_t *tree, ds_ost_node_t *node,
                                        void (*destroy)(ds_ost_node_t *)) {
  if (node == tree->nil) {
    return;
  }
  FUNC(ost_destroy_recursive)(tree, node->left, destroy);
  FUNC(ost_destroy_recursive)(tree, node->right, destroy);
  if (destroy != NULL) {
    destroy(node);
  }
}

void FUNC(ost_destroy)(ds_ost_t *tree, void (*destroy)(ds_ost_node_t *)) {
  FUNC(ost_destroy_recursive)(tree, tree->root, destroy);
  tree->deallocator(tree->nil);
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(tree);
#endif
  tree->deallocator(tree);
}
