#include <dsa/structures/bst.h>
#include <stdlib.h>

static ds_bst_t *bst_create_internal(ds_context_t *context) {
  ds_bst_t *tree;

  tree = (ds_bst_t *)ds_context_alloc(context, sizeof(ds_bst_t));
  if (tree == NULL) {
    return NULL;
  }
  tree->context = context != NULL ? context : ds_default_context();

  tree->nil =
      (ds_bst_node_t *)ds_context_alloc(context, sizeof(ds_bst_node_t));
  if (tree->nil == NULL) {
    ds_context_free(context, tree);
    return NULL;
  }
  tree->nil->left = tree->nil;
  tree->nil->right = tree->nil;
  tree->nil->parent = tree->nil;
  tree->root = tree->nil;
  tree->size = 0;

  return tree;
}

ds_bst_t *ds_bst_create(ds_context_t *context) {
  return bst_create_internal(context);
}
ds_status_t ds_bst_create_context(ds_context_t *context, ds_bst_t **out) {
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = bst_create_internal(context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

static ds_status_t bst_insert_core(ds_bst_t *tree, ds_bst_node_t *node,
                                   ds_bst_compare_fn compare, void *user) {
  if (tree->root == tree->nil) {
    tree->root = node;
    node->parent = node->left = node->right = tree->nil;
  } else {
    ds_bst_node_t *current = tree->root;
    ds_bst_node_t *parent = tree->nil;
    while (current != tree->nil) {
      parent = current;
      if (compare(node, current, user) < 0) {
        current = current->left;
      } else {
        current = current->right;
      }
    }
    if (compare(node, parent, user) < 0) {
      parent->left = node;
    } else {
      parent->right = node;
    }
    node->parent = parent;
    node->left = node->right = tree->nil;
  }
  tree->size += 1;
  return DS_OK;
}
static int bst_compare_direct(ds_bst_node_t *a, ds_bst_node_t *b, void *user) {
  int (**compare)(ds_bst_node_t *, ds_bst_node_t *) =
      (int (**)(ds_bst_node_t *, ds_bst_node_t *))user;
  return (*compare)(a, b);
}
ds_status_t ds_bst_insert_with(ds_bst_t *tree, ds_bst_node_t *node,
                               ds_bst_compare_fn compare, void *user) {
  if (tree == NULL || node == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (node == tree->nil) {
    return DS_ERR_INVALID;
  }
  if (tree->size == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  return bst_insert_core(tree, node, compare, user);
}
void ds_bst_insert(ds_bst_t *tree, ds_bst_node_t *node,
                   int (*compare)(ds_bst_node_t *, ds_bst_node_t *)) {
  (void)ds_bst_insert_with(tree, node, bst_compare_direct, &compare);
}

ds_bst_node_t *ds_bst_search(ds_bst_t *tree, ds_bst_node_t *data,
                             int (*compare)(ds_bst_node_t *,
                                            ds_bst_node_t *)) {
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

  ds_context_free(tree->context, tree->nil);

  ds_context_free(tree->context, tree);
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

typedef struct bst_clone_policy {
  ds_bst_node_t *(*direct)(ds_bst_node_t *);
  ds_bst_clone_fn checked;
  void *user;
  ds_status_t status;
} bst_clone_policy_t;
static ds_bst_node_t *bst_clone_one(bst_clone_policy_t *p,
                                    ds_bst_node_t *node) {
  ds_bst_node_t *copy = NULL;
  if (p->checked != NULL) {
    p->status = p->checked(node, p->user, &copy);
    if (p->status != DS_OK) {
      if (p->status > DS_OK) {
        p->status = DS_ERR_CALLBACK;
      }
      return NULL;
    }
  } else {
    copy = p->direct(node);
  }
  if (copy == NULL) {
    p->status = DS_ERR_CALLBACK;
  }
  return copy;
}
static bool bst_clone_subtree(const ds_bst_t *tree, ds_bst_t *new_tree,
                              ds_bst_node_t *node, bst_clone_policy_t *policy,
                              ds_bst_node_t **out) {
  ds_bst_node_t *x = node;
  ds_bst_node_t *copy;
  ds_bst_node_t *cx;
  int from = BST_FROM_ABOVE;
  *out = new_tree->nil;
  if (node == tree->nil) {
    return true;
  }
  copy = bst_clone_one(policy, node);
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
      c = bst_clone_one(policy, x->left);
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
      c = bst_clone_one(policy, x->right);
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
  bst_clone_policy_t policy;
  policy.direct = clone_node;
  policy.checked = NULL;
  policy.user = NULL;
  policy.status = DS_OK;
  (void)bst_clone_subtree(tree, new_tree, node, &policy, &copy);
  return copy;
}

ds_bst_t *ds_bst_clone(ds_bst_t *tree,
                       ds_bst_node_t *(*clone_node)(ds_bst_node_t *)) {
  ds_bst_t *new_tree;
  bst_clone_policy_t policy;
  policy.direct = clone_node;
  policy.checked = NULL;
  policy.user = NULL;
  policy.status = DS_OK;
  if (tree == NULL || clone_node == NULL) {
    return NULL;
  }
  new_tree = bst_create_internal(tree->context);
  if (new_tree == NULL) {
    return NULL;
  }
  if (!bst_clone_subtree(tree, new_tree, tree->root, &policy,
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
  bst_clone_policy_t policy;
  policy.direct = clone_node;
  policy.checked = NULL;
  policy.user = NULL;
  policy.status = DS_OK;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (tree == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  new_tree = bst_create_internal(tree->context);
  if (new_tree == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!bst_clone_subtree(tree, new_tree, tree->root, &policy,
                         &new_tree->root)) {
    ds_bst_destroy_tree(new_tree, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  new_tree->size = tree->size;
  *out = new_tree;
  return DS_OK;
}

ds_status_t ds_bst_find_with(const ds_bst_t *tree, ds_bst_node_t *key,
                             ds_bst_compare_fn compare, void *user,
                             ds_bst_node_t **out) {
  ds_bst_node_t *node;
  if (tree == NULL || key == NULL || compare == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  node = tree->root;
  while (node != tree->nil) {
    int cmp = compare(key, node, user);
    if (cmp == 0) {
      *out = node;
      return DS_OK;
    }
    node = cmp < 0 ? node->left : node->right;
  }
  return DS_NOT_FOUND;
}
ds_status_t ds_bst_traverse_with(const ds_bst_t *tree, ds_bst_node_t *root,
                                 ds_tree_walk_order_t order,
                                 ds_bst_visit_fn visit, void *user) {
  ds_bst_node_t *node, *parent;
  int state = 0;
  ds_status_t st;
  if (tree == NULL || root == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (order != DS_TREE_PREORDER && order != DS_TREE_INORDER &&
      order != DS_TREE_POSTORDER) {
    return DS_ERR_INVALID;
  }
  node = root;
  while (node != tree->nil) {
    if (state == 0) {
      if (order == DS_TREE_PREORDER) {
        st = visit(node, user);
        if (st != DS_OK) {
          return st;
        }
      }
      if (node->left != tree->nil) {
        node = node->left;
        continue;
      }
      state = 1;
    }
    if (state == 1) {
      if (order == DS_TREE_INORDER) {
        st = visit(node, user);
        if (st != DS_OK) {
          return st;
        }
      }
      if (node->right != tree->nil) {
        node = node->right;
        state = 0;
        continue;
      }
    }
    if (order == DS_TREE_POSTORDER) {
      st = visit(node, user);
      if (st != DS_OK) {
        return st;
      }
    }
    if (node == root) {
      break;
    }
    parent = node->parent;
    state = node == parent->left ? 1 : 2;
    node = parent;
  }
  return DS_OK;
}
ds_status_t ds_bst_walk_with(const ds_bst_t *tree, ds_bst_visit_fn visit,
                             void *user) {
  if (tree == NULL) {
    return DS_ERR_NULL;
  }
  return ds_bst_traverse_with(tree, tree->root, DS_TREE_INORDER, visit, user);
}
void ds_bst_destroy_with(ds_bst_t *tree, ds_bst_destroy_fn destroy,
                         void *user) {
  ds_bst_node_t *node, *parent;
  if (tree == NULL) {
    return;
  }
  node = tree->root;
  while (node != tree->nil) {
    if (node->left != tree->nil) {
      node = node->left;
      continue;
    }
    if (node->right != tree->nil) {
      node = node->right;
      continue;
    }
    parent = node->parent;
    if (parent != tree->nil) {
      if (parent->left == node) {
        parent->left = tree->nil;
      } else {
        parent->right = tree->nil;
      }
    }
    if (destroy != NULL) {
      destroy(node, user);
    }
    node = parent;
  }
  ds_context_free(tree->context, tree->nil);
  ds_context_free(tree->context, tree);
}
ds_status_t ds_bst_clone_with(const ds_bst_t *tree, ds_bst_clone_fn clone,
                              ds_bst_destroy_fn destroy, void *user,
                              ds_context_t *context, ds_bst_t **out) {
  ds_bst_t *copy;
  bst_clone_policy_t policy;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (tree == NULL || clone == NULL || destroy == NULL) {
    return DS_ERR_NULL;
  }
  copy = bst_create_internal(context != NULL ? context : tree->context);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  policy.direct = NULL;
  policy.checked = clone;
  policy.user = user;
  policy.status = DS_OK;
  if (!bst_clone_subtree(tree, copy, tree->root, &policy, &copy->root)) {
    ds_bst_destroy_with(copy, destroy, user);
    return policy.status;
  }
  copy->size = tree->size;
  *out = copy;
  return DS_OK;
}
