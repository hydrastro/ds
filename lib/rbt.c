#include "rbt.h"
#include "legacy_alloc.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

void ds_rbt_set_parent(ds_rbt_node_t *node, ds_rbt_node_t *parent) {
  node->parent_color = RBT_GET_COLOR_FROM_NODE(node) + (unsigned long)parent;
}

void ds_rbt_set_parent_color(ds_rbt_node_t *node, ds_rbt_node_t *parent,
                             int color) {
  node->parent_color = (unsigned long)parent + (long unsigned int)color;
}

ds_rbt_t *ds_rbt_create(void) { return ds_rbt_create_alloc(malloc, free); }

static ds_rbt_t *rbt_create_internal(void *(*allocator)(size_t),
                                     void (*deallocator)(void *),
                                     ds_context_t *context) {
  ds_rbt_t *tree;
  if (allocator == NULL || deallocator == NULL) {
    return NULL;
  }
  tree = (ds_rbt_t *)DS_LEGACY_ALLOCATE(context, allocator, sizeof(ds_rbt_t));
  if (tree == NULL) {
    return NULL;
  }
  tree->context = context;
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->nil = (ds_rbt_node_t *)DS_LEGACY_ALLOCATE(context, allocator,
                                                  sizeof(ds_rbt_node_t));
  if (tree->nil == NULL) {
    DS_LEGACY_RELEASE(context, deallocator, tree);
    return NULL;
  }
  tree->nil->parent_color = RBT_BLACK;
  tree->root = tree->nil;
  ds_rbt_set_parent_color(tree->root, tree->nil, RBT_BLACK);
  tree->root->left = tree->nil;
  tree->root->right = tree->nil;
  tree->size = 0;

  return tree;
}

ds_rbt_t *ds_rbt_create_alloc(void *(*allocator)(size_t),
                              void (*deallocator)(void *)) {
  return rbt_create_internal(allocator, deallocator, NULL);
}
ds_status_t ds_rbt_create_context(ds_context_t *context, ds_rbt_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = rbt_create_internal(malloc, free,
                             context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

ds_rbt_node_t *ds_rbt_minimum(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  while (node_x->left != tree->nil) {
    node_x = node_x->left;
  }
  return node_x;
}

ds_rbt_node_t *ds_rbt_maximum(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  while (node_x->right != tree->nil) {
    node_x = node_x->right;
  }
  return node_x;
}

ds_rbt_node_t *ds_rbt_successor(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  ds_rbt_node_t *node_y;
  if (node_x->right != tree->nil) {
    return ds_rbt_minimum(tree, node_x->right);
  }
  node_y = RBT_GET_PARENT_FROM_NODE(node_x);
  while (node_y != tree->nil && node_x == node_y->right) {
    node_x = node_y;
    node_y = RBT_GET_PARENT_FROM_NODE(node_y);
  }
  return node_y;
}

ds_rbt_node_t *ds_rbt_predecessor(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  ds_rbt_node_t *node_y;
  if (node_x->left != tree->nil) {
    return ds_rbt_maximum(tree, node_x->left);
  }
  node_y = RBT_GET_PARENT_FROM_NODE(node_x);
  while (node_y != tree->nil && node_x == node_y->left) {
    node_x = node_y;
    node_y = RBT_GET_PARENT_FROM_NODE(node_y);
  }
  return node_y;
}

ds_rbt_node_t *ds_rbt_search(ds_rbt_t *tree, ds_rbt_node_t *node_x,
                             ds_rbt_node_t *data,
                             int (*compare)(ds_rbt_node_t *, ds_rbt_node_t *)) {
  int cmp;
  while (node_x != tree->nil) {
    cmp = compare(data, node_x);
    if (cmp == 0) {
      return node_x;
    } else if (cmp < 0) {
      node_x = node_x->left;
    } else {
      node_x = node_x->right;
    }
  }
  return tree->nil;
}

ds_rbt_node_t *
ds_rbt_bigger_than(ds_rbt_t *tree, ds_rbt_node_t *node_x, ds_rbt_node_t *key,
                   int (*compare)(ds_rbt_node_t *, ds_rbt_node_t *)) {
  int cmp;
  ds_rbt_node_t *node_y;
  node_y = tree->nil;
  while (node_x != tree->nil) {
    cmp = compare(key, node_x);
    if (cmp < 0) {
      node_y = node_x;
      node_x = node_x->left;
    } else {
      node_x = node_x->right;
    }
  }
  return node_y;
}

ds_rbt_node_t *
ds_rbt_smaller_than(ds_rbt_t *tree, ds_rbt_node_t *node_x, ds_rbt_node_t *key,
                    int (*compare)(ds_rbt_node_t *, ds_rbt_node_t *)) {
  int cmp;
  ds_rbt_node_t *node_y;
  node_y = tree->nil;
  while (node_x != tree->nil) {
    cmp = compare(key, node_x);
    if (cmp > 0) {
      node_y = node_x;
      node_x = node_x->right;
    } else {
      node_x = node_x->left;
    }
  }
  return node_y;
}

void ds_rbt_left_rotate(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  ds_rbt_node_t *node_y;
  node_y = node_x->right;
  node_x->right = node_y->left;
  if (node_y->left != tree->nil) {
    ds_rbt_set_parent(node_y->left, node_x);
  }
  ds_rbt_set_parent(node_y, RBT_GET_PARENT_FROM_NODE(node_x));
  if (RBT_GET_PARENT_FROM_NODE(node_x) == tree->nil) {
    tree->root = node_y;
  } else if (node_x == RBT_GET_PARENT_FROM_NODE(node_x)->left) {
    RBT_GET_PARENT_FROM_NODE(node_x)->left = node_y;
  } else {
    RBT_GET_PARENT_FROM_NODE(node_x)->right = node_y;
  }
  node_y->left = node_x;
  ds_rbt_set_parent(node_x, node_y);
}

void ds_rbt_right_rotate(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  ds_rbt_node_t *node_y;
  node_y = node_x->left;
  node_x->left = node_y->right;
  if (node_y->right != tree->nil) {
    ds_rbt_set_parent(node_y->right, node_x);
  }
  ds_rbt_set_parent(node_y, RBT_GET_PARENT_FROM_NODE(node_x));
  if (RBT_GET_PARENT_FROM_NODE(node_x) == tree->nil) {
    tree->root = node_y;
  } else if (node_x == RBT_GET_PARENT_FROM_NODE(node_x)->right) {
    RBT_GET_PARENT_FROM_NODE(node_x)->right = node_y;
  } else {
    RBT_GET_PARENT_FROM_NODE(node_x)->left = node_y;
  }
  node_y->right = node_x;
  ds_rbt_set_parent(node_x, node_y);
}

void ds_rbt_insert_fixup(ds_rbt_t *tree, ds_rbt_node_t *node_z) {
  while (RBT_IS_RED_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z))) {
    if (RBT_GET_PARENT_FROM_NODE(node_z) ==
        RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z))->left) {
      ds_rbt_node_t *node_y;
      node_y =
          RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z))->right;
      if (RBT_IS_RED_FROM_NODE(node_y)

      ) {
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
        RBT_SET_BLACK_FROM_NODE(node_y);
        RBT_SET_RED_FROM_NODE(
            RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
        node_z = RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
      } else {
        if (node_z == RBT_GET_PARENT_FROM_NODE(node_z)->right) {
          node_z = RBT_GET_PARENT_FROM_NODE(node_z);
          ds_rbt_left_rotate(tree, node_z);
        }
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
        RBT_SET_RED_FROM_NODE(
            RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
        ds_rbt_right_rotate(
            tree, RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
      }
    } else {
      ds_rbt_node_t *node_y;
      node_y = RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z))->left;
      if (RBT_IS_RED_FROM_NODE(node_y)) {
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
        RBT_SET_BLACK_FROM_NODE(node_y);
        RBT_SET_RED_FROM_NODE(
            RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
        node_z = RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
      } else {
        if (node_z == RBT_GET_PARENT_FROM_NODE(node_z)->left) {
          node_z = RBT_GET_PARENT_FROM_NODE(node_z);
          ds_rbt_right_rotate(tree, node_z);
        }
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z));
        RBT_SET_RED_FROM_NODE(
            RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
        ds_rbt_left_rotate(
            tree, RBT_GET_PARENT_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_z)));
      }
    }
  }
  RBT_SET_BLACK_FROM_NODE(tree->root);
}

static ds_status_t rbt_insert_core(ds_rbt_t *tree, ds_rbt_node_t *node_z,
                                   ds_rbt_compare_fn compare, void *user) {
  ds_rbt_node_t *node_y;
  ds_rbt_node_t *node_x;
  node_y = tree->nil;
  node_x = tree->root;
  while (node_x != tree->nil) {
    node_y = node_x;
    if (compare(node_z, node_x, user) < 0) {
      node_x = node_x->left;
    } else {
      node_x = node_x->right;
    }
  }
  ds_rbt_set_parent(node_z, node_y);
  if (node_y == tree->nil) {
    tree->root = node_z;
  } else if (compare(node_z, node_y, user) < 0) {
    node_y->left = node_z;
  } else {
    node_y->right = node_z;
  }
  node_z->left = tree->nil;
  node_z->right = tree->nil;
  RBT_SET_RED_FROM_NODE(node_z);
  ds_rbt_insert_fixup(tree, node_z);
  tree->size += 1;
  return DS_OK;
}
static int rbt_compare_legacy(ds_rbt_node_t *a, ds_rbt_node_t *b, void *user) {
  int (**compare)(ds_rbt_node_t *, ds_rbt_node_t *) =
      (int (**)(ds_rbt_node_t *, ds_rbt_node_t *))user;
  return (*compare)(a, b);
}
ds_status_t ds_rbt_insert_with(ds_rbt_t *tree, ds_rbt_node_t *node,
                               ds_rbt_compare_fn compare, void *user) {
  if (tree == NULL || node == NULL || compare == NULL)
    return DS_ERR_NULL;
  if (node == tree->nil)
    return DS_ERR_INVALID;
  if (tree->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  return rbt_insert_core(tree, node, compare, user);
}
void ds_rbt_insert(ds_rbt_t *tree, ds_rbt_node_t *node_z,
                   int (*compare)(ds_rbt_node_t *, ds_rbt_node_t *)) {
  (void)ds_rbt_insert_with(tree, node_z, rbt_compare_legacy, &compare);
}

void ds_rbt_inorder_walk_helper(ds_rbt_t *tree, ds_rbt_node_t *node,
                                void (*callback)(void *)) {
  if (node != tree->nil) {
    ds_rbt_inorder_walk_helper(tree, node->left, callback);
    callback(node);
    ds_rbt_inorder_walk_helper(tree, node->right, callback);
  }
}

void ds_rbt_inorder_walk(ds_rbt_t *tree, ds_rbt_node_t *node,
                         void (*callback)(void *)) {
  ds_rbt_inorder_walk_helper(tree, node, callback);
}

void ds_rbt_inorder_walk_tree(ds_rbt_t *tree, void (*callback)(void *)) {
  ds_rbt_inorder_walk_helper(tree, tree->root, callback);
}

void ds_rbt_preorder_walk_helper(ds_rbt_t *tree, ds_rbt_node_t *node,
                                 void (*callback)(void *)) {
  if (node != tree->nil) {
    callback(node);
    ds_rbt_preorder_walk_helper(tree, node->left, callback);
    ds_rbt_preorder_walk_helper(tree, node->right, callback);
  }
}

void ds_rbt_preorder_walk(ds_rbt_t *tree, ds_rbt_node_t *node,
                          void (*callback)(void *)) {
  ds_rbt_preorder_walk_helper(tree, node, callback);
}

void ds_rbt_preorder_walk_tree(ds_rbt_t *tree, void (*callback)(void *)) {
  ds_rbt_preorder_walk_helper(tree, tree->root, callback);
}

void ds_rbt_postorder_walk_helper(ds_rbt_t *tree, ds_rbt_node_t *node,
                                  void (*callback)(void *)) {
  if (node != tree->nil) {
    ds_rbt_postorder_walk_helper(tree, node->left, callback);
    ds_rbt_postorder_walk_helper(tree, node->right, callback);
    callback(node);
  }
}

void ds_rbt_postorder_walk(ds_rbt_t *tree, ds_rbt_node_t *node,
                           void (*callback)(void *)) {
  ds_rbt_postorder_walk_helper(tree, node, callback);
}

void ds_rbt_postorder_walk_tree(ds_rbt_t *tree, void (*callback)(void *)) {
  ds_rbt_postorder_walk_helper(tree, tree->root, callback);
}

void ds_rbt_transplant(ds_rbt_t *tree, ds_rbt_node_t *node_u,
                       ds_rbt_node_t *node_v) {
  if (RBT_GET_PARENT_FROM_NODE(node_u) == tree->nil) {
    tree->root = node_v;
  } else if (node_u == RBT_GET_PARENT_FROM_NODE(node_u)->left) {
    RBT_GET_PARENT_FROM_NODE(node_u)->left = node_v;
  } else {
    RBT_GET_PARENT_FROM_NODE(node_u)->right = node_v;
  }
  ds_rbt_set_parent(node_v, RBT_GET_PARENT_FROM_NODE(node_u));
}

void ds_rbt_delete_fixup(ds_rbt_t *tree, ds_rbt_node_t *node_x) {
  while (node_x != tree->root && RBT_IS_BLACK_FROM_NODE(node_x)) {
    if (node_x == RBT_GET_PARENT_FROM_NODE(node_x)->left) {
      ds_rbt_node_t *node_w;
      node_w = RBT_GET_PARENT_FROM_NODE(node_x)->right;
      if (RBT_IS_RED_FROM_NODE(node_w)) {
        RBT_SET_BLACK_FROM_NODE(node_w);
        RBT_SET_RED_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x));
        ds_rbt_left_rotate(tree, RBT_GET_PARENT_FROM_NODE(node_x));
        node_w = RBT_GET_PARENT_FROM_NODE(node_x)->right;
      }
      if (RBT_IS_BLACK_FROM_NODE(node_w->left) &&
          RBT_IS_BLACK_FROM_NODE(node_w->right)) {
        RBT_SET_RED_FROM_NODE(node_w);
        node_x = RBT_GET_PARENT_FROM_NODE(node_x);
      } else {
        if (RBT_IS_BLACK_FROM_NODE(node_w->right)) {
          RBT_SET_BLACK_FROM_NODE(node_w->left);
          RBT_SET_RED_FROM_NODE(node_w);
          ds_rbt_right_rotate(tree, node_w);
          node_w = RBT_GET_PARENT_FROM_NODE(node_x)->right;
        }
        RBT_SET_COLOR_FROM_NODE(
            node_w, RBT_GET_COLOR_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x)));
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x));
        RBT_SET_BLACK_FROM_NODE(node_w->right);
        ds_rbt_left_rotate(tree, RBT_GET_PARENT_FROM_NODE(node_x));
        node_x = tree->root;
      }
    } else {
      ds_rbt_node_t *node_w;
      node_w = RBT_GET_PARENT_FROM_NODE(node_x)->left;
      if (RBT_IS_RED_FROM_NODE(node_w)) {
        RBT_SET_BLACK_FROM_NODE(node_w);
        RBT_SET_RED_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x));
        ds_rbt_right_rotate(tree, RBT_GET_PARENT_FROM_NODE(node_x));
        node_w = RBT_GET_PARENT_FROM_NODE(node_x)->left;
      }
      if (RBT_IS_BLACK_FROM_NODE(node_w->right) &&
          RBT_IS_BLACK_FROM_NODE(node_w->left)) {
        RBT_SET_RED_FROM_NODE(node_w);
        node_x = RBT_GET_PARENT_FROM_NODE(node_x);
      } else {
        if (RBT_IS_BLACK_FROM_NODE(node_w->left)) {
          RBT_SET_BLACK_FROM_NODE(node_w->right);
          RBT_SET_RED_FROM_NODE(node_w);
          ds_rbt_left_rotate(tree, node_w);
          node_w = RBT_GET_PARENT_FROM_NODE(node_x)->left;
        }
        RBT_SET_COLOR_FROM_NODE(
            node_w, RBT_GET_COLOR_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x)));
        RBT_SET_BLACK_FROM_NODE(RBT_GET_PARENT_FROM_NODE(node_x));
        RBT_SET_BLACK_FROM_NODE(node_w->left);
        ds_rbt_right_rotate(tree, RBT_GET_PARENT_FROM_NODE(node_x));
        node_x = tree->root;
      }
    }
  }
  RBT_SET_BLACK_FROM_NODE(node_x);
}

void ds_rbt_delete_node(ds_rbt_t *tree, ds_rbt_node_t *node_z) {
  ds_rbt_node_t *node_y;
  ds_rbt_node_t *node_x;
  unsigned int color;
  node_y = node_z;
  color = RBT_GET_COLOR_FROM_NODE(node_y);
  if (node_z->left == tree->nil) {
    node_x = node_z->right;
    ds_rbt_transplant(tree, node_z, node_z->right);
  } else if (node_z->right == tree->nil) {
    node_x = node_z->left;
    ds_rbt_transplant(tree, node_z, node_z->left);
  } else {
    node_y = ds_rbt_minimum(tree, node_z->right);
    color = RBT_GET_COLOR_FROM_NODE(node_y);
    node_x = node_y->right;
    if (RBT_GET_PARENT_FROM_NODE(node_y) == node_z) {
      ds_rbt_set_parent(node_x, node_y);
    } else {
      ds_rbt_transplant(tree, node_y, node_y->right);
      node_y->right = node_z->right;
      ds_rbt_set_parent(node_y->right, node_y);
    }
    ds_rbt_transplant(tree, node_z, node_y);
    node_y->left = node_z->left;
    ds_rbt_set_parent(node_y->left, node_y);
    RBT_SET_COLOR_FROM_NODE(node_y, RBT_GET_COLOR_FROM_NODE(node_z));
  }
  if (color == RBT_BLACK) {
    ds_rbt_delete_fixup(tree, node_x);
  }
  tree->size -= 1;
}

void ds_rbt_delete_tree(ds_rbt_t *tree) {
  tree->root = tree->nil;
  tree->size = 0;
}

void ds_rbt_destroy_node(ds_rbt_t *tree, ds_rbt_node_t *node,
                         void (*destroy)(ds_rbt_node_t *)) {
  ds_rbt_delete_node(tree, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

void ds_rbt_destroy_recursive(ds_rbt_t *tree, ds_rbt_node_t *node,
                              void (*destroy)(ds_rbt_node_t *)) {
  if (node == tree->nil) {
    return;
  }
  ds_rbt_destroy_recursive(tree, node->left, destroy);
  ds_rbt_destroy_recursive(tree, node->right, destroy);
  if (destroy != NULL) {
    destroy(node);
  }
}

void ds_rbt_destroy_tree(ds_rbt_t *tree, void (*destroy)(ds_rbt_node_t *)) {
  ds_rbt_destroy_recursive(tree, tree->root, destroy);
  DS_LEGACY_FREE(tree, tree->nil);
  DS_LEGACY_FREE(tree, tree);
}

/* a copy of the subtree at node, colors included, whose root has
 * new_tree->nil as its parent; *out gets that root (new_tree->nil for an
 * empty subtree). Each clone is linked in before the next is made, so
 * when clone_node returns NULL (false) *out is still a tree holding every
 * clone made. The recursion is as deep as the tree: O(log n). */

typedef struct rbt_clone_policy {
  ds_rbt_node_t *(*legacy)(ds_rbt_node_t *);
  ds_rbt_clone_fn checked;
  void *user;
  ds_status_t status;
} rbt_clone_policy_t;
static ds_rbt_node_t *rbt_clone_one(rbt_clone_policy_t *p,
                                    ds_rbt_node_t *node) {
  ds_rbt_node_t *copy = NULL;
  if (p->checked != NULL) {
    p->status = p->checked(node, p->user, &copy);
    if (p->status != DS_OK) {
      if (p->status > DS_OK)
        p->status = DS_ERR_CALLBACK;
      return NULL;
    }
  } else
    copy = p->legacy(node);
  if (copy == NULL)
    p->status = DS_ERR_CALLBACK;
  return copy;
}
static bool rbt_clone_subtree(const ds_rbt_t *tree, ds_rbt_t *new_tree,
                              ds_rbt_node_t *node, rbt_clone_policy_t *policy,
                              ds_rbt_node_t **out) {
  ds_rbt_node_t *copy;
  bool ok;
  *out = new_tree->nil;
  if (node == tree->nil) {
    return true;
  }
  copy = rbt_clone_one(policy, node);
  if (copy == NULL) {
    return false;
  }
  ds_rbt_set_parent_color(copy, new_tree->nil,
                          (int)RBT_GET_COLOR_FROM_NODE(node));
  copy->left = new_tree->nil;
  copy->right = new_tree->nil;
  *out = copy;
  ok = rbt_clone_subtree(tree, new_tree, node->left, policy, &copy->left);
  if (copy->left != new_tree->nil) {
    ds_rbt_set_parent(copy->left, copy);
  }
  if (ok) {
    ok = rbt_clone_subtree(tree, new_tree, node->right, policy, &copy->right);
    if (copy->right != new_tree->nil) {
      ds_rbt_set_parent(copy->right, copy);
    }
  }
  return ok;
}

ds_rbt_node_t *
ds_rbt_clone_recursive(ds_rbt_t *tree, ds_rbt_t *new_tree, ds_rbt_node_t *node,
                       ds_rbt_node_t *(*clone_node)(ds_rbt_node_t *)) {
  ds_rbt_node_t *copy;
  rbt_clone_policy_t policy;
  policy.legacy = clone_node;
  policy.checked = NULL;
  policy.user = NULL;
  policy.status = DS_OK;
  (void)rbt_clone_subtree(tree, new_tree, node, &policy, &copy);
  return copy;
}

ds_rbt_t *ds_rbt_clone(ds_rbt_t *tree,
                       ds_rbt_node_t *(*clone_node)(ds_rbt_node_t *)) {
  ds_rbt_t *new_tree;
  rbt_clone_policy_t policy;
  policy.legacy = clone_node;
  policy.checked = NULL;
  policy.user = NULL;
  policy.status = DS_OK;
  if (tree == NULL || clone_node == NULL) {
    return NULL;
  }
  new_tree =
      rbt_create_internal(tree->allocator, tree->deallocator, tree->context);
  if (new_tree == NULL) {
    return NULL;
  }
  if (!rbt_clone_subtree(tree, new_tree, tree->root, &policy,
                         &new_tree->root)) {
    ds_rbt_destroy_tree(new_tree, NULL); /* the clones are not released */
    return NULL;
  }
  new_tree->size = tree->size;

  return new_tree;
}

ds_status_t ds_rbt_clone_ex(const ds_rbt_t *tree,
                            ds_rbt_node_t *(*clone_node)(ds_rbt_node_t *),
                            void (*destroy_clone)(ds_rbt_node_t *),
                            ds_rbt_t **out) {
  ds_rbt_t *new_tree;
  rbt_clone_policy_t policy;
  policy.legacy = clone_node;
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
  new_tree =
      rbt_create_internal(tree->allocator, tree->deallocator, tree->context);
  if (new_tree == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!rbt_clone_subtree(tree, new_tree, tree->root, &policy,
                         &new_tree->root)) {
    ds_rbt_destroy_tree(new_tree, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  new_tree->size = tree->size;
  *out = new_tree;
  return DS_OK;
}

ds_status_t ds_rbt_find_with(const ds_rbt_t *tree, ds_rbt_node_t *key,
                             ds_rbt_compare_fn compare, void *user,
                             ds_rbt_node_t **out) {
  ds_rbt_node_t *node;
  if (tree == NULL || key == NULL || compare == NULL || out == NULL)
    return DS_ERR_NULL;
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
ds_status_t ds_rbt_traverse_with(const ds_rbt_t *tree, ds_rbt_node_t *root,
                                 ds_tree_walk_order_t order,
                                 ds_rbt_visit_fn visit, void *user) {
  ds_rbt_node_t *node, *parent;
  int state = 0;
  ds_status_t st;
  if (tree == NULL || root == NULL || visit == NULL)
    return DS_ERR_NULL;
  if (order != DS_TREE_PREORDER && order != DS_TREE_INORDER &&
      order != DS_TREE_POSTORDER)
    return DS_ERR_INVALID;
  node = root;
  while (node != tree->nil) {
    if (state == 0) {
      if (order == DS_TREE_PREORDER) {
        st = visit(node, user);
        if (st != DS_OK)
          return st;
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
        if (st != DS_OK)
          return st;
      }
      if (node->right != tree->nil) {
        node = node->right;
        state = 0;
        continue;
      }
    }
    if (order == DS_TREE_POSTORDER) {
      st = visit(node, user);
      if (st != DS_OK)
        return st;
    }
    if (node == root)
      break;
    parent = RBT_GET_PARENT_FROM_NODE(node);
    state = node == parent->left ? 1 : 2;
    node = parent;
  }
  return DS_OK;
}
ds_status_t ds_rbt_walk_with(const ds_rbt_t *tree, ds_rbt_visit_fn visit,
                             void *user) {
  if (tree == NULL)
    return DS_ERR_NULL;
  return ds_rbt_traverse_with(tree, tree->root, DS_TREE_INORDER, visit, user);
}
void ds_rbt_destroy_with(ds_rbt_t *tree, ds_rbt_destroy_fn destroy,
                         void *user) {
  ds_rbt_node_t *node, *parent;
  if (tree == NULL)
    return;
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
    parent = RBT_GET_PARENT_FROM_NODE(node);
    if (parent != tree->nil) {
      if (parent->left == node)
        parent->left = tree->nil;
      else
        parent->right = tree->nil;
    }
    if (destroy != NULL)
      destroy(node, user);
    node = parent;
  }
  DS_LEGACY_FREE(tree, tree->nil);
  DS_LEGACY_FREE(tree, tree);
}
ds_status_t ds_rbt_clone_with(const ds_rbt_t *tree, ds_rbt_clone_fn clone,
                              ds_rbt_destroy_fn destroy, void *user,
                              ds_context_t *context, ds_rbt_t **out) {
  ds_rbt_t *copy;
  rbt_clone_policy_t policy;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (tree == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  copy = rbt_create_internal(tree->allocator, tree->deallocator,
                             context != NULL ? context : tree->context);
  if (copy == NULL)
    return DS_ERR_ALLOC;
  policy.legacy = NULL;
  policy.checked = clone;
  policy.user = user;
  policy.status = DS_OK;
  if (!rbt_clone_subtree(tree, copy, tree->root, &policy, &copy->root)) {
    ds_rbt_destroy_with(copy, destroy, user);
    return policy.status;
  }
  copy->size = tree->size;
  *out = copy;
  return DS_OK;
}
