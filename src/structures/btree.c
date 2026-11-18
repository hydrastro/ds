#include <dsa/structures/btree.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static void *bt_alloc(ds_btree_t *t, size_t n) {
  return t->use_context ? ds_context_alloc(t->context, n) : t->allocator(n);
}
static void bt_free(ds_btree_t *t, void *p) {
  if (t->use_context)
    ds_context_free(t->context, p);
  else if (p != NULL)
    t->deallocator(p);
}

ds_btree_internal_node_t *ds_btree_create_node(ds_btree_t *tree, bool is_leaf) {
  size_t keys = 2 * (size_t)tree->degree - 1;
  ds_btree_internal_node_t *node = (ds_btree_internal_node_t *)bt_alloc(
      tree, sizeof(ds_btree_internal_node_t));
  if (node == NULL) {
    return NULL;
  }
  node->num_keys = 0;
  node->is_leaf = is_leaf;
  node->parent = NULL;
  node->data =
      (ds_btree_node_t **)bt_alloc(tree, keys * sizeof(ds_btree_node_t *));
  node->children = (ds_btree_internal_node_t **)bt_alloc(
      tree, (keys + 1) * sizeof(ds_btree_internal_node_t *));
  if (node->data == NULL || node->children == NULL) {
    if (node->data != NULL) {
      bt_free(tree, node->data);
    }
    if (node->children != NULL) {
      bt_free(tree, node->children);
    }
    bt_free(tree, node);
    return NULL;
  }
  memset(node->children, 0, (keys + 1) * sizeof(ds_btree_internal_node_t *));
  return node;
}

ds_btree_t *ds_btree_create(int degree) {
  return ds_btree_create_alloc(degree, malloc, free);
}

/* the tree without its root: NULL for a degree below 2 (a node must hold
 * at least three keys), one whose node arrays cannot be sized, or when
 * memory runs out */
static ds_btree_t *btree_shell(int degree, void *(*allocator)(size_t),
                               void (*deallocator)(void *),
                               ds_context_t *context, bool use_context) {
  ds_btree_t *tree;
  if (allocator == NULL || deallocator == NULL || degree < 2 ||
      degree > INT_MAX / 2 ||
      (size_t)degree > ((size_t)-1 / sizeof(void *) - 1) / 2) {
    return NULL;
  }
  tree = (ds_btree_t *)(use_context ? ds_context_alloc(context, sizeof(*tree))
                                    : allocator(sizeof(*tree)));
  if (tree == NULL) {
    return NULL;
  }
  tree->context = context;
  tree->use_context = use_context;
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->degree = degree;
  tree->root = NULL;
  tree->size = 0;
  tree->nil = (ds_btree_node_t *)bt_alloc(tree, sizeof(ds_btree_node_t));
  if (tree->nil == NULL) {
    bt_free(tree, tree);
    return NULL;
  }
  tree->nil->internal = NULL;
  return tree;
}

ds_btree_t *ds_btree_create_alloc(int degree, void *(*allocator)(size_t),
                                  void (*deallocator)(void *)) {
  ds_btree_t *tree = btree_shell(degree, allocator, deallocator, NULL, false);
  if (tree == NULL) {
    return NULL;
  }
  tree->root = ds_btree_create_node(tree, true);
  if (tree->root == NULL) {
    bt_free(tree, tree->nil);
    bt_free(tree, tree);
    return NULL;
  }
  tree->root->parent = NULL;

  return tree;
}

static void bt_split(ds_btree_t *tree, ds_btree_internal_node_t *parent,
                     int index, ds_btree_internal_node_t *new_node) {
  int i;
  ds_btree_internal_node_t *child;
  child = parent->children[index];
  new_node->is_leaf = child->is_leaf;
  new_node->num_keys = tree->degree - 1;
  new_node->parent = parent;

  for (i = 0; i < tree->degree - 1; i++) {
    new_node->data[i] = child->data[i + tree->degree];
    new_node->data[i]->internal = new_node;
  }
  if (!child->is_leaf) {
    for (i = 0; i < tree->degree; i++) {
      new_node->children[i] = child->children[i + tree->degree];
      new_node->children[i]->parent = new_node;
    }
  }
  child->num_keys = tree->degree - 1;
  for (i = parent->num_keys; i >= index + 1; i--) {
    parent->children[i + 1] = parent->children[i];
  }
  parent->children[index + 1] = new_node;

  for (i = parent->num_keys - 1; i >= index; i--) {
    parent->data[i + 1] = parent->data[i];
    parent->data[i + 1]->internal = parent;
  }
  parent->data[index] = child->data[tree->degree - 1];
  parent->data[index]->internal = parent;
  parent->num_keys++;
}

static void bt_node_free(ds_btree_t *tree, ds_btree_internal_node_t *node) {
  bt_free(tree, node->children);
  bt_free(tree, node->data);
  bt_free(tree, node);
}

ds_status_t ds_btree_create_context(int degree, ds_context_t *context,
                                    ds_btree_t **out) {
  ds_btree_t *tree;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (degree < 2 || degree > INT_MAX / 2 ||
      (size_t)degree > ((size_t)-1 / sizeof(void *) - 1) / 2)
    return DS_ERR_RANGE;
  tree = btree_shell(degree, malloc, free, context, true);
  if (tree == NULL)
    return DS_ERR_ALLOC;
  tree->root = ds_btree_create_node(tree, true);
  if (tree->root == NULL) {
    bt_free(tree, tree->nil);
    bt_free(tree, tree);
    return DS_ERR_ALLOC;
  }
  *out = tree;
  return DS_OK;
}

ds_status_t ds_btree_split_child_ex(ds_btree_t *tree,
                                    ds_btree_internal_node_t *parent,
                                    int index) {
  ds_btree_internal_node_t *sibling;
  if (tree == NULL || parent == NULL)
    return DS_ERR_NULL;
  if (index < 0 || index > parent->num_keys || parent->is_leaf ||
      parent->num_keys >= 2 * tree->degree - 1 ||
      parent->children[index] == NULL ||
      parent->children[index]->num_keys != 2 * tree->degree - 1)
    return DS_ERR_INVALID;
  sibling = ds_btree_create_node(tree, true);
  if (sibling == NULL)
    return DS_ERR_ALLOC;
  bt_split(tree, parent, index, sibling);
  return DS_OK;
}

void ds_btree_split_child(ds_btree_t *tree, ds_btree_internal_node_t *parent,
                          int index) {
  (void)ds_btree_split_child_ex(tree, parent, index);
}

/* Count exactly the splits on the hypothetical top-down descent, including
 * the equality convention used after promotion (equal goes to the left half).
 * Staged nodes are linked through their otherwise unused parent fields. */
static ds_status_t bt_insert(ds_btree_t *tree, ds_btree_internal_node_t *root,
                             ds_btree_node_t *data, ds_btree_compare_fn compare,
                             void *user, bool whole_tree) {
  ds_btree_internal_node_t *node, *staged = NULL, *next;
  size_t need = 0;
  int i, lo, hi;
  if (tree == NULL || root == NULL || data == NULL || compare == NULL)
    return DS_ERR_NULL;
  if (data == tree->nil ||
      (!whole_tree && root->num_keys == 2 * tree->degree - 1))
    return DS_ERR_INVALID;
  if (whole_tree && tree->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  if (root->num_keys == 2 * tree->degree - 1)
    need++; /* new root, in addition to its sibling below */
  for (node = root;;) {
    lo = 0;
    hi = node->num_keys - 1;
    if (node->num_keys == 2 * tree->degree - 1) {
      need++;
      if (compare(data, node->data[tree->degree - 1], user) <= 0)
        hi = tree->degree - 2;
      else
        lo = tree->degree;
    }
    if (node->is_leaf)
      break;
    i = hi;
    while (i >= lo && compare(data, node->data[i], user) < 0)
      i--;
    node = node->children[i + 1];
  }
  while (need != 0) {
    node = ds_btree_create_node(tree, true);
    if (node == NULL) {
      while (staged != NULL) {
        next = staged->parent;
        bt_node_free(tree, staged);
        staged = next;
      }
      return DS_ERR_ALLOC;
    }
    node->parent = staged;
    staged = node;
    need--;
  }
  node = root;
  if (root->num_keys == 2 * tree->degree - 1) {
    node = staged;
    staged = staged->parent;
    node->parent = NULL;
    node->is_leaf = false;
    node->children[0] = root;
    root->parent = node;
    tree->root = node;
    next = staged;
    staged = staged->parent;
    bt_split(tree, node, 0, next);
    /* Root promotion must use the same equal-goes-left rule as planning. */
    node = node->children[compare(data, node->data[0], user) > 0 ? 1 : 0];
  }
  while (!node->is_leaf) {
    i = node->num_keys - 1;
    while (i >= 0 && compare(data, node->data[i], user) < 0)
      i--;
    i++;
    if (node->children[i]->num_keys == 2 * tree->degree - 1) {
      next = staged;
      staged = staged->parent;
      bt_split(tree, node, i, next);
      if (compare(data, node->data[i], user) > 0)
        i++;
    }
    node = node->children[i];
  }
  i = node->num_keys - 1;
  while (i >= 0 && compare(data, node->data[i], user) < 0) {
    node->data[i + 1] = node->data[i];
    i--;
  }
  node->data[i + 1] = data;
  data->internal = node;
  node->num_keys++;
  if (whole_tree)
    tree->size++;
  return DS_OK;
}

typedef struct bt_direct_compare {
  int (*compare)(ds_btree_node_t *, ds_btree_node_t *);
} bt_direct_compare_t;
static int bt_compare_direct(const ds_btree_node_t *a, const ds_btree_node_t *b,
                             void *user) {
  bt_direct_compare_t *c = (bt_direct_compare_t *)user;
  /* Direct comparators are observational despite their non-const signature. */
  ds_btree_node_t *x, *y;
  memcpy(&x, &a, sizeof(x));
  memcpy(&y, &b, sizeof(y));
  return c->compare(x, y);
}

ds_status_t ds_btree_insert_with(ds_btree_t *tree, ds_btree_node_t *node,
                                 ds_btree_compare_fn compare, void *user) {
  return tree == NULL ? DS_ERR_NULL
                      : bt_insert(tree, tree->root, node, compare, user, true);
}

ds_status_t ds_btree_insert_ex(ds_btree_t *tree, ds_btree_node_t *data,
                               int (*compare)(ds_btree_node_t *,
                                              ds_btree_node_t *)) {
  bt_direct_compare_t c;
  if (compare == NULL)
    return DS_ERR_NULL;
  c.compare = compare;
  return ds_btree_insert_with(tree, data, bt_compare_direct, &c);
}

void ds_btree_insert_non_full(ds_btree_t *tree, ds_btree_internal_node_t *node,
                              ds_btree_node_t *data,
                              int (*compare)(ds_btree_node_t *,
                                             ds_btree_node_t *)) {
  bt_direct_compare_t c;
  if (compare == NULL)
    return;
  c.compare = compare;
  (void)bt_insert(tree, node, data, bt_compare_direct, &c, false);
}

void ds_btree_insert(ds_btree_t *tree, ds_btree_node_t *data,
                     int (*compare)(ds_btree_node_t *, ds_btree_node_t *)) {
  (void)ds_btree_insert_ex(tree, data, compare);
}

ds_status_t ds_btree_find_with(ds_btree_t *tree, ds_btree_node_t *key,
                               ds_btree_compare_fn compare, void *user,
                               ds_btree_node_t **out) {
  ds_btree_internal_node_t *node;
  int i, cmp;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (tree == NULL || key == NULL || compare == NULL)
    return DS_ERR_NULL;
  node = tree->root;
  for (;;) {
    for (i = 0; i < node->num_keys; i++) {
      cmp = compare(key, node->data[i], user);
      if (cmp == 0) {
        *out = node->data[i];
        return DS_OK;
      }
      if (cmp < 0)
        break;
    }
    if (node->is_leaf)
      return DS_NOT_FOUND;
    node = node->children[i];
  }
}

ds_btree_node_t *
ds_btree_search_node(ds_btree_t *tree, ds_btree_internal_node_t *node,
                     ds_btree_node_t *key,
                     int (*compare)(ds_btree_node_t *, ds_btree_node_t *)) {
  int i;
  ds_btree_node_t *result;
  i = 0;
  while (i < node->num_keys && compare(key, node->data[i]) > 0) {
    i++;
  }

  if (i < node->num_keys && compare(key, node->data[i]) == 0) {
    return node->data[i];
  }

  if (node->is_leaf) {
    return tree->nil;
  }
  result = ds_btree_search_node(tree, node->children[i], key, compare);
  return result;
}

ds_btree_node_t *ds_btree_search(ds_btree_t *tree, ds_btree_node_t *key,
                                 int (*compare)(ds_btree_node_t *,
                                                ds_btree_node_t *)) {
  ds_btree_node_t *result;

  result = ds_btree_search_node(tree, tree->root, key, compare);

  return result;
}

void *ds_btree_local_minimum(ds_btree_t *tree, ds_btree_internal_node_t *node) {
  if (tree == NULL) {
    return NULL;
  }
  if (node == NULL) {
    return tree->nil;
  }
  while (!node->is_leaf) {
    node = node->children[0];
  }

  return node->num_keys == 0 ? tree->nil : node->data[0];
}

void *ds_btree_local_maximum(ds_btree_t *tree, ds_btree_internal_node_t *node) {
  if (tree == NULL) {
    return NULL;
  }
  if (node == NULL) {
    return tree->nil;
  }
  while (!node->is_leaf) {
    node = node->children[node->num_keys];
  }

  return node->num_keys == 0 ? tree->nil : node->data[node->num_keys - 1];
}

void *ds_btree_minimum(ds_btree_t *tree) {
  return tree == NULL ? NULL : ds_btree_local_minimum(tree, tree->root);
}

void *ds_btree_maximum(ds_btree_t *tree) {
  return tree == NULL ? NULL : ds_btree_local_maximum(tree, tree->root);
}

/* node, not the root, has one key fewer than the minimum (degree - 1):
 * borrow through the parent from a sibling that can spare a key, moving
 * the matching child along when the nodes are internal, or merge with a
 * sibling around their separator and fix the parent the same way. Keys
 * keep their owner (internal) and children their parent. */
void ds_btree_rebalance(ds_btree_t *tree, ds_btree_internal_node_t *node) {
  int i, j;
  int t = tree->degree;
  ds_btree_internal_node_t *parent;
  ds_btree_internal_node_t *left;
  ds_btree_internal_node_t *right;
  parent = node->parent;
  if (parent == NULL) {
    return; /* the root may hold fewer keys */
  }
  i = 0;
  while (i <= parent->num_keys && parent->children[i] != node) {
    i++;
  }
  left = i > 0 ? parent->children[i - 1] : NULL;
  right = i < parent->num_keys ? parent->children[i + 1] : NULL;
  if (right != NULL && right->num_keys > t - 1) {
    /* the separator comes down, right's first key goes up */
    node->data[node->num_keys] = parent->data[i];
    node->data[node->num_keys]->internal = node;
    if (!node->is_leaf) {
      node->children[node->num_keys + 1] = right->children[0];
      node->children[node->num_keys + 1]->parent = node;
    }
    node->num_keys++;
    parent->data[i] = right->data[0];
    parent->data[i]->internal = parent;
    for (j = 1; j < right->num_keys; j++) {
      right->data[j - 1] = right->data[j];
    }
    if (!right->is_leaf) {
      for (j = 1; j <= right->num_keys; j++) {
        right->children[j - 1] = right->children[j];
      }
      right->children[right->num_keys] = NULL;
    }
    right->num_keys--;
  } else if (left != NULL && left->num_keys > t - 1) {
    /* the separator comes down, left's last key goes up */
    for (j = node->num_keys; j > 0; j--) {
      node->data[j] = node->data[j - 1];
    }
    if (!node->is_leaf) {
      for (j = node->num_keys + 1; j > 0; j--) {
        node->children[j] = node->children[j - 1];
      }
      node->children[0] = left->children[left->num_keys];
      node->children[0]->parent = node;
      left->children[left->num_keys] = NULL;
    }
    node->data[0] = parent->data[i - 1];
    node->data[0]->internal = node;
    node->num_keys++;
    parent->data[i - 1] = left->data[left->num_keys - 1];
    parent->data[i - 1]->internal = parent;
    left->num_keys--;
  } else {
    /* merge next into recipient around the separator at sep */
    ds_btree_internal_node_t *recipient, *next;
    int sep, base;
    if (left != NULL) {
      recipient = left;
      next = node;
      sep = i - 1;
    } else {
      recipient = node;
      next = right;
      sep = i;
    }
    base = recipient->num_keys;
    recipient->data[base] = parent->data[sep];
    recipient->data[base]->internal = recipient;
    for (j = 0; j < next->num_keys; j++) {
      recipient->data[base + 1 + j] = next->data[j];
      recipient->data[base + 1 + j]->internal = recipient;
    }
    if (!recipient->is_leaf) {
      for (j = 0; j <= next->num_keys; j++) {
        recipient->children[base + 1 + j] = next->children[j];
        recipient->children[base + 1 + j]->parent = recipient;
      }
    }
    recipient->num_keys = base + 1 + next->num_keys;
    bt_free(tree, next->children);
    bt_free(tree, next->data);
    bt_free(tree, next);
    for (j = sep + 1; j < parent->num_keys; j++) {
      parent->data[j - 1] = parent->data[j];
    }
    for (j = sep + 2; j <= parent->num_keys; j++) {
      parent->children[j - 1] = parent->children[j];
    }
    parent->children[parent->num_keys] = NULL;
    parent->num_keys--;
    if (parent == tree->root && parent->num_keys == 0) {
      tree->root = recipient;
      tree->root->parent = NULL;
      bt_free(tree, parent->children);
      bt_free(tree, parent->data);
      bt_free(tree, parent);
    } else if (parent != tree->root && parent->num_keys < t - 1) {
      ds_btree_rebalance(tree, parent);
    }
  }
}

void ds_btree_delete_node(ds_btree_t *tree, ds_btree_node_t *key) {
  int i;
  bool deleted;
  ds_btree_internal_node_t *node;
  ds_btree_internal_node_t *borrowed_internal;
  ds_btree_node_t *borrowed;
  if (tree == NULL || key == NULL || key == tree->nil)
    return;
  node = key->internal;
  deleted = false;
  if (node == NULL) {
    return;
  }
  {
    ds_btree_internal_node_t *root = node;
    while (root->parent != NULL)
      root = root->parent;
    if (root != tree->root)
      return;
  }
  i = 0;
  while (i < node->num_keys && node->data[i] != key) {
    i++;
  }
  if (i == node->num_keys)
    return;
  if (node->is_leaf) {
    for (i = i + 1; i < node->num_keys; i++) {
      node->data[i - 1] = node->data[i];
    }
    node->num_keys--;
    if (node->num_keys < tree->degree - 1) {
      ds_btree_rebalance(tree, node);
    }
    deleted = true;
  } else {
    if (node->children[i + 1]->num_keys > tree->degree - 1) {
      borrowed = ds_btree_local_minimum(tree, node->children[i + 1]);
    } else {
      borrowed = ds_btree_local_maximum(tree, node->children[i]);
    }
    node->data[i] = borrowed;

    borrowed_internal = borrowed->internal;
    i = 0;
    while (i < borrowed_internal->num_keys &&
           borrowed_internal->data[i] != borrowed) {
      i++;
    }

    for (i = i + 1; i < borrowed_internal->num_keys; i++) {
      borrowed_internal->data[i - 1] = borrowed_internal->data[i];
    }
    borrowed_internal->num_keys--;

    borrowed->internal = node;
    if (borrowed_internal->num_keys < tree->degree - 1) {
      ds_btree_rebalance(tree, borrowed_internal);
    }
    deleted = true;
  }
  if (deleted && tree->size != 0U) {
    tree->size -= 1U;
  }
  if (deleted)
    key->internal = NULL;
}

ds_status_t ds_btree_remove_ex(ds_btree_t *tree, ds_btree_node_t *node,
                               ds_btree_node_t **out) {
  size_t before;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (tree == NULL || node == NULL)
    return DS_ERR_NULL;
  before = tree->size;
  ds_btree_delete_node(tree, node);
  if (tree->size == before)
    return DS_NOT_FOUND;
  *out = node;
  return DS_OK;
}

void ds_btree_destroy_node(ds_btree_t *tree, ds_btree_node_t *node,
                           void (*destroy)(ds_btree_node_t *)) {
  ds_btree_node_t *removed;
  if (ds_btree_remove_ex(tree, node, &removed) == DS_OK && destroy != NULL)
    destroy(removed);
}

void ds_btree_destroy_recursive(ds_btree_t *tree,
                                ds_btree_internal_node_t *node,
                                void (*destroy)(ds_btree_node_t *)) {
  int i;
  if (node == NULL) {
    return;
  }
  if (!node->is_leaf) {
    for (i = 0; i <= node->num_keys; i++) {
      ds_btree_destroy_recursive(tree, node->children[i], destroy);
    }
  }
  for (i = 0; i < node->num_keys; i++) {
    node->data[i]->internal = NULL;
    if (destroy != NULL)
      destroy(node->data[i]);
  }
  bt_free(tree, node->children);
  bt_free(tree, node->data);
  bt_free(tree, node);
}

void ds_btree_destroy_tree(ds_btree_t *tree,
                           void (*destroy)(ds_btree_node_t *)) {
  if (tree == NULL)
    return;
  ds_btree_destroy_recursive(tree, tree->root, destroy);
  bt_free(tree, tree->nil);
  bt_free(tree, tree);
}

void ds_btree_inorder_walk_helper(ds_btree_t *tree,
                                  ds_btree_internal_node_t *node,
                                  void (*callback)(ds_btree_node_t *)) {
  int i;
  if (node == NULL) {
    return;
  }
  for (i = 0; i < node->num_keys; i++) {
    if (!node->is_leaf) {
      ds_btree_inorder_walk_helper(tree, node->children[i], callback);
    }
    callback(node->data[i]);
  }
  if (!node->is_leaf) {
    ds_btree_inorder_walk_helper(tree, node->children[i], callback);
  }
}

void ds_btree_inorder_walk(ds_btree_t *tree, ds_btree_internal_node_t *node,
                           void (*callback)(ds_btree_node_t *)) {
  ds_btree_inorder_walk_helper(tree, node, callback);
}

void ds_btree_inorder_walk_tree(ds_btree_t *tree,
                                void (*callback)(ds_btree_node_t *)) {
  ds_btree_inorder_walk_helper(tree, tree->root, callback);
}

void ds_btree_preorder_walk_helper(ds_btree_t *tree,
                                   ds_btree_internal_node_t *node,
                                   void (*callback)(ds_btree_node_t *)) {
  int i;
  if (node == NULL) {
    return;
  }
  for (i = 0; i < node->num_keys; i++) {
    callback(node->data[i]);
  }
  if (!node->is_leaf) {
    for (i = 0; i <= node->num_keys; i++) {
      ds_btree_preorder_walk_helper(tree, node->children[i], callback);
    }
  }
}

void ds_btree_preorder_walk(ds_btree_t *tree, ds_btree_internal_node_t *node,
                            void (*callback)(ds_btree_node_t *)) {
  ds_btree_preorder_walk_helper(tree, node, callback);
}

void ds_btree_preorder_walk_tree(ds_btree_t *tree,
                                 void (*callback)(ds_btree_node_t *)) {
  ds_btree_preorder_walk_helper(tree, tree->root, callback);
}

void ds_btree_postorder_walk_helper(ds_btree_t *tree,
                                    ds_btree_internal_node_t *node,
                                    void (*callback)(ds_btree_node_t *)) {
  int i;
  if (node == NULL) {
    return;
  }
  if (!node->is_leaf) {
    for (i = 0; i <= node->num_keys; i++) {
      ds_btree_postorder_walk_helper(tree, node->children[i], callback);
    }
  }
  for (i = 0; i < node->num_keys; i++) {
    callback(node->data[i]);
  }
}

void ds_btree_postorder_walk(ds_btree_t *tree, ds_btree_internal_node_t *node,
                             void (*callback)(ds_btree_node_t *)) {
  ds_btree_postorder_walk_helper(tree, node, callback);
}

void ds_btree_postorder_walk_tree(ds_btree_t *tree,
                                  void (*callback)(ds_btree_node_t *)) {
  ds_btree_postorder_walk_helper(tree, tree->root, callback);
}

/* a copy of the subtree at node; *out gets its root (NULL for no node or
 * when that node cannot be allocated). Keys are cloned before children
 * and counted as they come, and children start NULL, so after a failure
 * *out is still a tree that ds_btree_destroy_tree takes apart, holding
 * every clone made. DS_ERR_ALLOC for a node, DS_ERR_CALLBACK for a NULL
 * from clone_node. The recursion is as deep as the tree: O(log n). */
static ds_status_t
btree_clone_subtree(const ds_btree_t *tree, ds_btree_t *new_tree,
                    ds_btree_internal_node_t *node,
                    ds_btree_node_t *(*clone_node)(ds_btree_node_t *),
                    ds_btree_clone_fn clone_with, void *user,
                    ds_btree_internal_node_t **out) {
  int i;
  ds_btree_internal_node_t *copy;
  *out = NULL;
  if (node == NULL) {
    return DS_OK;
  }
  copy = ds_btree_create_node(new_tree, node->is_leaf);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  *out = copy;
  for (i = 0; i < node->num_keys; i++) {
    ds_btree_node_t *key = NULL;
    ds_status_t status = DS_OK;
    if (clone_with != NULL)
      status = clone_with(node->data[i], user, &key);
    else
      key = clone_node(node->data[i]);
    if (status != DS_OK)
      return status < DS_OK ? status : DS_ERR_CALLBACK;
    if (key == NULL || key == node->data[i]) {
      return DS_ERR_CALLBACK;
    }
    key->internal = copy;
    copy->data[i] = key;
    copy->num_keys = i + 1;
  }
  if (!node->is_leaf) {
    for (i = 0; i <= node->num_keys; i++) {
      ds_status_t st =
          btree_clone_subtree(tree, new_tree, node->children[i], clone_node,
                              clone_with, user, &copy->children[i]);
      if (copy->children[i] != NULL) {
        copy->children[i]->parent = copy;
      }
      if (st != DS_OK) {
        return st;
      }
    }
  }
  return DS_OK;
}

ds_btree_internal_node_t *
ds_btree_clone_recursive(ds_btree_t *tree, ds_btree_t *new_tree,
                         ds_btree_internal_node_t *node,
                         ds_btree_node_t *(*clone_node)(ds_btree_node_t *)) {
  ds_btree_internal_node_t *copy;
  (void)btree_clone_subtree(tree, new_tree, node, clone_node, NULL, NULL,
                            &copy);
  return copy;
}

ds_btree_t *ds_btree_clone(ds_btree_t *tree,
                           ds_btree_node_t *(*clone_node)(ds_btree_node_t *)) {
  ds_btree_t *new_tree;
  if (tree == NULL || clone_node == NULL) {
    return NULL;
  }
  new_tree = btree_shell(tree->degree, tree->allocator, tree->deallocator,
                         tree->context, tree->use_context);
  if (new_tree == NULL) {
    return NULL;
  }
  if (btree_clone_subtree(tree, new_tree, tree->root, clone_node, NULL, NULL,
                          &new_tree->root) != DS_OK) {
    ds_btree_destroy_tree(new_tree, NULL); /* the clones are not released */
    return NULL;
  }
  new_tree->size = tree->size;

  return new_tree;
}

ds_status_t ds_btree_clone_ex(const ds_btree_t *tree,
                              ds_btree_node_t *(*clone_node)(ds_btree_node_t *),
                              void (*destroy_clone)(ds_btree_node_t *),
                              ds_btree_t **out) {
  ds_btree_t *new_tree;
  ds_status_t st;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (tree == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  new_tree = btree_shell(tree->degree, tree->allocator, tree->deallocator,
                         tree->context, tree->use_context);
  if (new_tree == NULL) {
    return DS_ERR_ALLOC;
  }
  st = btree_clone_subtree(tree, new_tree, tree->root, clone_node, NULL, NULL,
                           &new_tree->root);
  if (st != DS_OK) {
    ds_btree_destroy_tree(new_tree, destroy_clone);
    return st;
  }
  new_tree->size = tree->size;
  *out = new_tree;
  return DS_OK;
}

static ds_status_t bt_walk(ds_btree_internal_node_t *node,
                           ds_btree_visit_fn visit, void *user) {
  int i;
  ds_status_t st;
  if (node == NULL)
    return DS_OK;
  for (i = 0; i < node->num_keys; i++) {
    if (!node->is_leaf) {
      st = bt_walk(node->children[i], visit, user);
      if (st != DS_OK)
        return st;
    }
    st = visit(node->data[i], user);
    if (st != DS_OK)
      return st;
  }
  return node->is_leaf ? DS_OK : bt_walk(node->children[i], visit, user);
}

ds_status_t ds_btree_walk_with(ds_btree_t *tree, ds_btree_visit_fn visit,
                               void *user) {
  return tree == NULL || visit == NULL ? DS_ERR_NULL
                                       : bt_walk(tree->root, visit, user);
}

static void bt_destroy(ds_btree_t *tree, ds_btree_internal_node_t *node,
                       ds_btree_destroy_fn destroy, void *user) {
  int i;
  if (node == NULL)
    return;
  if (!node->is_leaf)
    for (i = 0; i <= node->num_keys; i++)
      bt_destroy(tree, node->children[i], destroy, user);
  for (i = 0; i < node->num_keys; i++) {
    node->data[i]->internal = NULL;
    if (destroy != NULL)
      destroy(node->data[i], user);
  }
  bt_node_free(tree, node);
}

void ds_btree_destroy_with(ds_btree_t *tree, ds_btree_destroy_fn destroy,
                           void *user) {
  if (tree != NULL) {
    bt_destroy(tree, tree->root, destroy, user);
    bt_free(tree, tree->nil);
    bt_free(tree, tree);
  }
}

ds_status_t ds_btree_clone_with(const ds_btree_t *tree, ds_btree_clone_fn clone,
                                ds_btree_destroy_fn destroy, void *user,
                                ds_btree_t **out) {
  ds_btree_t *copy;
  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (tree == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  copy = btree_shell(tree->degree, tree->allocator, tree->deallocator,
                     tree->context, tree->use_context);
  if (copy == NULL)
    return DS_ERR_ALLOC;
  st = btree_clone_subtree(tree, copy, tree->root, NULL, clone, user,
                           &copy->root);
  if (st != DS_OK) {
    ds_btree_destroy_with(copy, destroy, user);
    return st;
  }
  copy->size = tree->size;
  *out = copy;
  return DS_OK;
}
