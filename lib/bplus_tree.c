#include "bplus_tree.h"
#include <stdlib.h>

/*
 * B+ tree: all records live in leaves; internal nodes hold separator keys for
 * routing only (each separator is the smallest record of the subtree to its
 * right). Leaves are doubly linked for ordered/range scans. A node holds at
 * most (order - 1) keys and, when internal, (order) children.
 *
 * The bstar flag applies the B* split policy to every level: a node that
 * overflows first hands keys to an adjacent sibling with room, and when its
 * adjacent siblings are full it splits together with one of them, two nodes
 * into three, each about two-thirds full; only the root splits in two.
 * Insertions therefore leave nodes fuller than plain splitting does.
 * Deletion keeps every node but the root at least half full in both modes
 * (a two-thirds bound cannot survive a split of the root).
 */

static int bplus_max_keys(ds_bplus_tree_t *tree) { return tree->order - 1; }

static int bplus_min_keys(ds_bplus_tree_t *tree) {
  return (tree->order - 1) / 2;
}

static ds_bplus_tree_internal_node_t *bplus_create_node(ds_bplus_tree_t *tree,
                                                        bool is_leaf) {
  ds_bplus_tree_internal_node_t *node =
      (ds_bplus_tree_internal_node_t *)tree->allocator(
          sizeof(ds_bplus_tree_internal_node_t));
  if (node == NULL) {
    return NULL;
  }
  node->num_keys = 0;
  node->is_leaf = is_leaf;
  node->parent = NULL;
  node->next = NULL;
  node->prev = NULL;
  /* allocate room for one extra slot to simplify overflow-before-split */
  node->keys = (ds_bplus_tree_node_t **)tree->allocator(
      (size_t)(tree->order + 1) * sizeof(ds_bplus_tree_node_t *));
  if (node->keys == NULL) {
    tree->deallocator(node);
    return NULL;
  }
  if (is_leaf) {
    node->children = NULL;
  } else {
    node->children = (ds_bplus_tree_internal_node_t **)tree->allocator(
        (size_t)(tree->order + 2) * sizeof(ds_bplus_tree_internal_node_t *));
    if (node->children == NULL) {
      tree->deallocator(node->keys);
      tree->deallocator(node);
      return NULL;
    }
  }
  return node;
}

static void bplus_free_node(ds_bplus_tree_t *tree,
                            ds_bplus_tree_internal_node_t *node) {
  tree->deallocator(node->keys);
  if (node->children != NULL) {
    tree->deallocator(node->children);
  }
  tree->deallocator(node);
}

ds_bplus_tree_t *ds_bplus_tree_create(int order,
                                      int (*compare)(ds_bplus_tree_node_t *,
                                                     ds_bplus_tree_node_t *)) {
  return ds_bplus_tree_create_alloc(order, false, compare, malloc, free);
}

ds_bplus_tree_t *ds_bplus_tree_create_alloc(
    int order, bool bstar,
    int (*compare)(ds_bplus_tree_node_t *, ds_bplus_tree_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *)) {
  ds_bplus_tree_t *tree;
  int clamped = order < 3 ? 3 : order;
  if (allocator == NULL || deallocator == NULL ||
      (size_t)clamped > (size_t)-1 / sizeof(void *) - 2) {
    return NULL;
  }
  tree = (ds_bplus_tree_t *)allocator(sizeof(ds_bplus_tree_t));
  if (tree == NULL) {
    return NULL;
  }
  tree->order = order < 3 ? 3 : order;
  tree->bstar = bstar;
  tree->compare = compare;
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->size = 0;
  tree->nil = (ds_bplus_tree_node_t *)allocator(sizeof(ds_bplus_tree_node_t));
  if (tree->nil == NULL) {
    deallocator(tree);
    return NULL;
  }
  tree->nil->internal = NULL;
  tree->root = bplus_create_node(tree, true);
  if (tree->root == NULL) {
    deallocator(tree->nil);
    deallocator(tree);
    return NULL;
  }
  tree->head = tree->root;
  return tree;
}

/* find the leaf where key belongs */
static ds_bplus_tree_internal_node_t *
bplus_find_leaf(ds_bplus_tree_t *tree, ds_bplus_tree_node_t *key) {
  ds_bplus_tree_internal_node_t *node = tree->root;
  while (!node->is_leaf) {
    int i = 0;
    while (i < node->num_keys && tree->compare(key, node->keys[i]) >= 0) {
      i += 1;
    }
    node = node->children[i];
  }
  return node;
}

ds_bplus_tree_node_t *ds_bplus_tree_search(ds_bplus_tree_t *tree,
                                           ds_bplus_tree_node_t *key) {
  ds_bplus_tree_internal_node_t *leaf;
  ds_bplus_tree_node_t *result;
  int i;
  leaf = bplus_find_leaf(tree, key);
  result = tree->nil;
  for (i = 0; i < leaf->num_keys; i++) {
    if (tree->compare(key, leaf->keys[i]) == 0) {
      result = leaf->keys[i];
      break;
    }
  }
  return result;
}

bool ds_bplus_tree_is_empty(ds_bplus_tree_t *tree) {
  bool result;
  result = tree->size == 0;
  return result;
}

size_t ds_bplus_tree_size(ds_bplus_tree_t *tree) {
  size_t result;
  result = tree->size;
  return result;
}

ds_bplus_tree_node_t *ds_bplus_tree_minimum(ds_bplus_tree_t *tree) {
  ds_bplus_tree_node_t *result;
  result = (tree->head->num_keys > 0) ? tree->head->keys[0] : tree->nil;
  return result;
}

ds_bplus_tree_node_t *ds_bplus_tree_maximum(ds_bplus_tree_t *tree) {
  ds_bplus_tree_internal_node_t *node;
  ds_bplus_tree_node_t *result;
  node = tree->root;
  while (!node->is_leaf) {
    node = node->children[node->num_keys];
  }
  result = (node->num_keys > 0) ? node->keys[node->num_keys - 1] : tree->nil;
  return result;
}

/* insert a separator key + right child into an internal node at position idx */
static void bplus_internal_insert_at(ds_bplus_tree_internal_node_t *node,
                                     int idx, ds_bplus_tree_node_t *key,
                                     ds_bplus_tree_internal_node_t *right) {
  int j;
  for (j = node->num_keys; j > idx; j--) {
    node->keys[j] = node->keys[j - 1];
    node->children[j + 1] = node->children[j];
  }
  node->keys[idx] = key;
  node->children[idx + 1] = right;
  node->num_keys += 1;
}

/* leftmost key of the subtree rooted at node (used as separator) */
static ds_bplus_tree_node_t *
bplus_subtree_min(ds_bplus_tree_internal_node_t *node) {
  while (!node->is_leaf) {
    node = node->children[0];
  }
  return node->keys[0];
}

static void bplus_insert_into_parent(ds_bplus_tree_t *tree,
                                     ds_bplus_tree_internal_node_t *left,
                                     ds_bplus_tree_node_t *key,
                                     ds_bplus_tree_internal_node_t *right);
static void bplus_overflow(ds_bplus_tree_t *tree,
                           ds_bplus_tree_internal_node_t *node);

/* split an internal node that has overflowed (num_keys == order) */
static void bplus_split_internal(ds_bplus_tree_t *tree,
                                 ds_bplus_tree_internal_node_t *node) {
  int total = node->num_keys;
  int mid = total / 2;
  ds_bplus_tree_node_t *up = node->keys[mid];
  ds_bplus_tree_internal_node_t *right = bplus_create_node(tree, false);
  int i;
  int r = 0;
  for (i = mid + 1; i < total; i++) {
    right->keys[r] = node->keys[i];
    right->children[r] = node->children[i];
    right->children[r]->parent = right;
    r += 1;
  }
  right->children[r] = node->children[total];
  right->children[r]->parent = right;
  right->num_keys = total - mid - 1;
  node->num_keys = mid;
  bplus_insert_into_parent(tree, node, up, right);
}

/* split a leaf that has overflowed (num_keys == order) */
static void bplus_split_leaf(ds_bplus_tree_t *tree,
                             ds_bplus_tree_internal_node_t *leaf) {
  int total = leaf->num_keys;
  int mid = (total + 1) / 2;
  ds_bplus_tree_internal_node_t *right = bplus_create_node(tree, true);
  int i;
  int r = 0;
  for (i = mid; i < total; i++) {
    right->keys[r] = leaf->keys[i];
    r += 1;
  }
  right->num_keys = total - mid;
  leaf->num_keys = mid;
  right->next = leaf->next;
  right->prev = leaf;
  if (leaf->next != NULL) {
    leaf->next->prev = right;
  }
  leaf->next = right;
  bplus_insert_into_parent(tree, leaf, right->keys[0], right);
}

/* B*: an overflowing node (one key too many) hands one key to an adjacent
 * sibling with room, through the parent's separator. Leaves move a record
 * and reset the separator to the right node's minimum; internal nodes
 * rotate: the separator comes down with the edge child, and the next key
 * goes up. Returns true when a sibling took it. */
static bool bplus_bstar_relieve(ds_bplus_tree_t *tree,
                                ds_bplus_tree_internal_node_t *node) {
  ds_bplus_tree_internal_node_t *parent = node->parent;
  int max_keys = bplus_max_keys(tree);
  int idx;
  int i;
  if (parent == NULL) {
    return false;
  }
  idx = 0;
  while (idx <= parent->num_keys && parent->children[idx] != node) {
    idx += 1;
  }
  if (idx > 0 && parent->children[idx - 1]->num_keys < max_keys) {
    ds_bplus_tree_internal_node_t *left = parent->children[idx - 1];
    if (node->is_leaf) {
      left->keys[left->num_keys] = node->keys[0];
      left->num_keys += 1;
      for (i = 0; i < node->num_keys - 1; i++) {
        node->keys[i] = node->keys[i + 1];
      }
      node->num_keys -= 1;
      parent->keys[idx - 1] = node->keys[0];
    } else {
      left->keys[left->num_keys] = parent->keys[idx - 1];
      left->children[left->num_keys + 1] = node->children[0];
      left->children[left->num_keys + 1]->parent = left;
      left->num_keys += 1;
      parent->keys[idx - 1] = node->keys[0];
      for (i = 0; i < node->num_keys - 1; i++) {
        node->keys[i] = node->keys[i + 1];
      }
      for (i = 0; i < node->num_keys; i++) {
        node->children[i] = node->children[i + 1];
      }
      node->num_keys -= 1;
    }
    return true;
  }
  if (idx < parent->num_keys &&
      parent->children[idx + 1]->num_keys < max_keys) {
    ds_bplus_tree_internal_node_t *right = parent->children[idx + 1];
    for (i = right->num_keys; i > 0; i--) {
      right->keys[i] = right->keys[i - 1];
    }
    if (node->is_leaf) {
      right->keys[0] = node->keys[node->num_keys - 1];
      right->num_keys += 1;
      node->num_keys -= 1;
      parent->keys[idx] = right->keys[0];
    } else {
      for (i = right->num_keys + 1; i > 0; i--) {
        right->children[i] = right->children[i - 1];
      }
      right->keys[0] = parent->keys[idx];
      right->children[0] = node->children[node->num_keys];
      right->children[0]->parent = right;
      right->num_keys += 1;
      parent->keys[idx] = node->keys[node->num_keys - 1];
      node->num_keys -= 1;
    }
    return true;
  }
  return false;
}

/* B*: an overflowing node and a full adjacent sibling become three nodes,
 * each about two-thirds full. The keys of both (and, between internal
 * nodes, their separator) are laid out in order in scratch arrays and dealt
 * out again; the new third node goes to the parent with its separator.
 * Returns false when there is no sibling or no memory for the scratch. */
static bool bplus_bstar_split(ds_bplus_tree_t *tree,
                              ds_bplus_tree_internal_node_t *node) {
  ds_bplus_tree_internal_node_t *parent = node->parent;
  ds_bplus_tree_internal_node_t *a, *b, *c;
  ds_bplus_tree_node_t **keys;
  ds_bplus_tree_internal_node_t **kids = NULL;
  ds_bplus_tree_node_t *up1, *up2;
  int idx, si, i, n, na, nb, nc, k;
  if (parent == NULL) {
    return false;
  }
  idx = 0;
  while (idx <= parent->num_keys && parent->children[idx] != node) {
    idx += 1;
  }
  si = idx < parent->num_keys ? idx : idx - 1; /* a = children[si] */
  if (si < 0) {
    return false;
  }
  a = parent->children[si];
  b = parent->children[si + 1];
  n = a->num_keys + b->num_keys + (node->is_leaf ? 0 : 1);
  keys = (ds_bplus_tree_node_t **)tree->allocator((size_t)n * sizeof(*keys));
  if (keys == NULL) {
    return false;
  }
  if (!node->is_leaf) {
    kids = (ds_bplus_tree_internal_node_t **)tree->allocator((size_t)(n + 1) *
                                                             sizeof(*kids));
    if (kids == NULL) {
      tree->deallocator(keys);
      return false;
    }
  }
  c = bplus_create_node(tree, node->is_leaf);
  if (c == NULL) {
    if (kids != NULL) {
      tree->deallocator(kids);
    }
    tree->deallocator(keys);
    return false;
  }
  k = 0;
  for (i = 0; i < a->num_keys; i++) {
    keys[k++] = a->keys[i];
  }
  if (!node->is_leaf) {
    keys[k++] = parent->keys[si];
  }
  for (i = 0; i < b->num_keys; i++) {
    keys[k++] = b->keys[i];
  }
  if (node->is_leaf) {
    /* n records: a third each, the remainder to the later nodes */
    na = n / 3;
    nb = (n + 1) / 3;
    nc = n - na - nb;
    for (i = 0; i < na; i++) {
      a->keys[i] = keys[i];
    }
    for (i = 0; i < nb; i++) {
      b->keys[i] = keys[na + i];
    }
    for (i = 0; i < nc; i++) {
      c->keys[i] = keys[na + nb + i];
    }
    a->num_keys = na;
    b->num_keys = nb;
    c->num_keys = nc;
    up1 = b->keys[0];
    up2 = c->keys[0];
    c->next = b->next;
    c->prev = b;
    if (b->next != NULL) {
      b->next->prev = c;
    }
    b->next = c;
  } else {
    /* n keys and n + 1 children: two keys go up, the rest is dealt out */
    for (i = 0; i <= a->num_keys; i++) {
      kids[i] = a->children[i];
    }
    for (i = 0; i <= b->num_keys; i++) {
      kids[a->num_keys + 1 + i] = b->children[i];
    }
    na = (n - 2) / 3;
    nb = (n - 1) / 3;
    nc = n - 2 - na - nb;
    for (i = 0; i < na; i++) {
      a->keys[i] = keys[i];
    }
    for (i = 0; i <= na; i++) {
      a->children[i] = kids[i];
      kids[i]->parent = a;
    }
    up1 = keys[na];
    for (i = 0; i < nb; i++) {
      b->keys[i] = keys[na + 1 + i];
    }
    for (i = 0; i <= nb; i++) {
      b->children[i] = kids[na + 1 + i];
      kids[na + 1 + i]->parent = b;
    }
    up2 = keys[na + 1 + nb];
    for (i = 0; i < nc; i++) {
      c->keys[i] = keys[na + nb + 2 + i];
    }
    for (i = 0; i <= nc; i++) {
      c->children[i] = kids[na + nb + 2 + i];
      kids[na + nb + 2 + i]->parent = c;
    }
    a->num_keys = na;
    b->num_keys = nb;
    c->num_keys = nc;
    tree->deallocator(kids);
  }
  tree->deallocator(keys);
  parent->keys[si] = up1;
  c->parent = parent;
  bplus_internal_insert_at(parent, si + 1, up2, c);
  if (parent->num_keys >= tree->order) {
    bplus_overflow(tree, parent);
  }
  return true;
}

/* a node with one key too many: under B*, a sibling takes a key or two
 * full nodes become three; otherwise (and at the root) it splits in two */
static void bplus_overflow(ds_bplus_tree_t *tree,
                           ds_bplus_tree_internal_node_t *node) {
  if (tree->bstar &&
      (bplus_bstar_relieve(tree, node) || bplus_bstar_split(tree, node))) {
    return;
  }
  if (node->is_leaf) {
    bplus_split_leaf(tree, node);
  } else {
    bplus_split_internal(tree, node);
  }
}

static void bplus_insert_into_parent(ds_bplus_tree_t *tree,
                                     ds_bplus_tree_internal_node_t *left,
                                     ds_bplus_tree_node_t *key,
                                     ds_bplus_tree_internal_node_t *right) {
  ds_bplus_tree_internal_node_t *parent = left->parent;
  int idx;
  if (parent == NULL) {
    ds_bplus_tree_internal_node_t *new_root = bplus_create_node(tree, false);
    new_root->keys[0] = key;
    new_root->children[0] = left;
    new_root->children[1] = right;
    new_root->num_keys = 1;
    left->parent = new_root;
    right->parent = new_root;
    tree->root = new_root;
    return;
  }
  right->parent = parent;
  idx = 0;
  while (idx <= parent->num_keys && parent->children[idx] != left) {
    idx += 1;
  }
  bplus_internal_insert_at(parent, idx, key, right);
  if (parent->num_keys >= tree->order) {
    bplus_overflow(tree, parent);
  }
}

bool ds_bplus_tree_insert(ds_bplus_tree_t *tree, ds_bplus_tree_node_t *data) {
  ds_bplus_tree_internal_node_t *leaf;
  int i;
  int pos;
  bool result = true;
  if (data == tree->nil) {
    return false;
  }
  leaf = bplus_find_leaf(tree, data);
  /* find insertion position; reject duplicates */
  pos = 0;
  while (pos < leaf->num_keys && tree->compare(data, leaf->keys[pos]) > 0) {
    pos += 1;
  }
  if (pos < leaf->num_keys && tree->compare(data, leaf->keys[pos]) == 0) {
    return false;
  }
  for (i = leaf->num_keys; i > pos; i--) {
    leaf->keys[i] = leaf->keys[i - 1];
  }
  leaf->keys[pos] = data;
  leaf->num_keys += 1;
  tree->size += 1;
  /* refresh parent separator if we changed the leaf minimum */
  if (pos == 0 && leaf->parent != NULL) {
    ds_bplus_tree_internal_node_t *child = leaf;
    ds_bplus_tree_internal_node_t *parent = leaf->parent;
    while (parent != NULL) {
      int c = 0;
      while (c <= parent->num_keys && parent->children[c] != child) {
        c += 1;
      }
      if (c > 0) {
        parent->keys[c - 1] = bplus_subtree_min(parent->children[c]);
        break;
      }
      child = parent;
      parent = parent->parent;
    }
  }
  if (leaf->num_keys >= tree->order) {
    bplus_overflow(tree, leaf);
  }
  return result;
}

void ds_bplus_tree_walk_forward(ds_bplus_tree_t *tree,
                                void (*callback)(ds_bplus_tree_node_t *)) {
  ds_bplus_tree_internal_node_t *leaf;
  int i;
  leaf = tree->head;
  while (leaf != NULL) {
    for (i = 0; i < leaf->num_keys; i++) {
      callback(leaf->keys[i]);
    }
    leaf = leaf->next;
  }
}

void ds_bplus_tree_range_walk(ds_bplus_tree_t *tree, ds_bplus_tree_node_t *low,
                              ds_bplus_tree_node_t *high,
                              void (*callback)(ds_bplus_tree_node_t *)) {
  ds_bplus_tree_internal_node_t *leaf;
  int i;
  leaf = bplus_find_leaf(tree, low);
  while (leaf != NULL) {
    for (i = 0; i < leaf->num_keys; i++) {
      if (tree->compare(leaf->keys[i], low) < 0) {
        continue;
      }
      if (tree->compare(leaf->keys[i], high) > 0) {
        return;
      }
      callback(leaf->keys[i]);
    }
    leaf = leaf->next;
  }
}

static void bplus_destroy_recursive(ds_bplus_tree_t *tree,
                                    ds_bplus_tree_internal_node_t *node,
                                    void (*destroy)(ds_bplus_tree_node_t *)) {
  int i;
  if (!node->is_leaf) {
    for (i = 0; i <= node->num_keys; i++) {
      bplus_destroy_recursive(tree, node->children[i], destroy);
    }
  } else if (destroy != NULL) {
    for (i = 0; i < node->num_keys; i++) {
      destroy(node->keys[i]);
    }
  }
  bplus_free_node(tree, node);
}

void ds_bplus_tree_destroy(ds_bplus_tree_t *tree,
                           void (*destroy)(ds_bplus_tree_node_t *)) {
  bplus_destroy_recursive(tree, tree->root, destroy);
  tree->deallocator(tree->nil);
  tree->deallocator(tree);
}

/* clones of the source's records, in order, inserted into new_tree;
 * false when clone_node returns NULL (the clones made so far are in
 * new_tree) */
static bool bplus_clone_records(
    const ds_bplus_tree_t *tree, ds_bplus_tree_t *new_tree,
    ds_bplus_tree_node_t *(*clone_node)(ds_bplus_tree_node_t *)) {
  ds_bplus_tree_internal_node_t *leaf;
  int i;
  for (leaf = tree->head; leaf != NULL; leaf = leaf->next) {
    for (i = 0; i < leaf->num_keys; i++) {
      ds_bplus_tree_node_t *copy = clone_node(leaf->keys[i]);
      if (copy == NULL) {
        return false;
      }
      ds_bplus_tree_insert(new_tree, copy);
    }
  }
  return true;
}

ds_bplus_tree_t *ds_bplus_tree_clone(
    ds_bplus_tree_t *tree,
    ds_bplus_tree_node_t *(*clone_node)(ds_bplus_tree_node_t *)) {
  ds_bplus_tree_t *new_tree;
  if (tree == NULL || clone_node == NULL) {
    return NULL;
  }
  new_tree = ds_bplus_tree_create_alloc(tree->order, tree->bstar, tree->compare,
                                        tree->allocator, tree->deallocator);
  if (new_tree != NULL && !bplus_clone_records(tree, new_tree, clone_node)) {
    ds_bplus_tree_destroy(new_tree, NULL); /* the clones are not released */
    new_tree = NULL;
  }
  return new_tree;
}

ds_status_t ds_bplus_tree_clone_ex(
    const ds_bplus_tree_t *tree,
    ds_bplus_tree_node_t *(*clone_node)(ds_bplus_tree_node_t *),
    void (*destroy_clone)(ds_bplus_tree_node_t *), ds_bplus_tree_t **out) {
  ds_bplus_tree_t *new_tree;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (tree == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  new_tree = ds_bplus_tree_create_alloc(tree->order, tree->bstar, tree->compare,
                                        tree->allocator, tree->deallocator);
  if (new_tree == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!bplus_clone_records(tree, new_tree, clone_node)) {
    ds_bplus_tree_destroy(new_tree, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  *out = new_tree;
  return DS_OK;
}

/* index of child within its parent */
static int bplus_child_index(ds_bplus_tree_internal_node_t *parent,
                             ds_bplus_tree_internal_node_t *child) {
  int i = 0;
  while (i <= parent->num_keys && parent->children[i] != child) {
    i += 1;
  }
  return i;
}

/* refresh separators along the path so parent->keys hold subtree minimums */
static void bplus_refresh_separators(ds_bplus_tree_internal_node_t *node) {
  ds_bplus_tree_internal_node_t *child = node;
  ds_bplus_tree_internal_node_t *parent = node->parent;
  while (parent != NULL) {
    int c = bplus_child_index(parent, child);
    if (c > 0) {
      parent->keys[c - 1] = bplus_subtree_min(parent->children[c]);
    }
    child = parent;
    parent = parent->parent;
  }
}

static void bplus_fix_underflow(ds_bplus_tree_t *tree,
                                ds_bplus_tree_internal_node_t *node);

static void bplus_collapse_root(ds_bplus_tree_t *tree) {
  if (!tree->root->is_leaf && tree->root->num_keys == 0) {
    ds_bplus_tree_internal_node_t *old = tree->root;
    tree->root = old->children[0];
    tree->root->parent = NULL;
    bplus_free_node(tree, old);
  }
}

/* merge node into its left sibling (left absorbs node) */
static void bplus_merge_nodes(ds_bplus_tree_t *tree,
                              ds_bplus_tree_internal_node_t *left,
                              ds_bplus_tree_internal_node_t *node,
                              ds_bplus_tree_internal_node_t *parent,
                              int sep_index) {
  int i;
  if (left->is_leaf) {
    for (i = 0; i < node->num_keys; i++) {
      left->keys[left->num_keys + i] = node->keys[i];
    }
    left->num_keys += node->num_keys;
    left->next = node->next;
    if (node->next != NULL) {
      node->next->prev = left;
    }
  } else {
    left->keys[left->num_keys] = parent->keys[sep_index];
    left->num_keys += 1;
    for (i = 0; i < node->num_keys; i++) {
      left->keys[left->num_keys + i] = node->keys[i];
    }
    for (i = 0; i <= node->num_keys; i++) {
      left->children[left->num_keys + i] = node->children[i];
      left->children[left->num_keys + i]->parent = left;
    }
    left->num_keys += node->num_keys;
  }
  /* drop separator sep_index and the pointer to node from parent */
  for (i = sep_index; i < parent->num_keys - 1; i++) {
    parent->keys[i] = parent->keys[i + 1];
    parent->children[i + 1] = parent->children[i + 2];
  }
  parent->num_keys -= 1;
  bplus_free_node(tree, node);
  if (parent == tree->root) {
    bplus_collapse_root(tree);
  } else if (parent->num_keys < bplus_min_keys(tree)) {
    bplus_fix_underflow(tree, parent);
  }
}

static void bplus_fix_underflow(ds_bplus_tree_t *tree,
                                ds_bplus_tree_internal_node_t *node) {
  ds_bplus_tree_internal_node_t *parent = node->parent;
  int min_keys = bplus_min_keys(tree);
  int idx;
  int i;
  if (parent == NULL) {
    bplus_collapse_root(tree);
    return;
  }
  idx = bplus_child_index(parent, node);
  /* borrow from left sibling */
  if (idx > 0) {
    ds_bplus_tree_internal_node_t *left = parent->children[idx - 1];
    if (left->num_keys > min_keys) {
      if (node->is_leaf) {
        for (i = node->num_keys; i > 0; i--) {
          node->keys[i] = node->keys[i - 1];
        }
        node->keys[0] = left->keys[left->num_keys - 1];
        node->num_keys += 1;
        left->num_keys -= 1;
        parent->keys[idx - 1] = node->keys[0];
      } else {
        for (i = node->num_keys; i > 0; i--) {
          node->keys[i] = node->keys[i - 1];
        }
        for (i = node->num_keys + 1; i > 0; i--) {
          node->children[i] = node->children[i - 1];
        }
        node->keys[0] = parent->keys[idx - 1];
        node->children[0] = left->children[left->num_keys];
        node->children[0]->parent = node;
        node->num_keys += 1;
        parent->keys[idx - 1] = left->keys[left->num_keys - 1];
        left->num_keys -= 1;
      }
      return;
    }
  }
  /* borrow from right sibling */
  if (idx < parent->num_keys) {
    ds_bplus_tree_internal_node_t *right = parent->children[idx + 1];
    if (right->num_keys > min_keys) {
      if (node->is_leaf) {
        node->keys[node->num_keys] = right->keys[0];
        node->num_keys += 1;
        for (i = 0; i < right->num_keys - 1; i++) {
          right->keys[i] = right->keys[i + 1];
        }
        right->num_keys -= 1;
        parent->keys[idx] = right->keys[0];
      } else {
        node->keys[node->num_keys] = parent->keys[idx];
        node->children[node->num_keys + 1] = right->children[0];
        node->children[node->num_keys + 1]->parent = node;
        node->num_keys += 1;
        parent->keys[idx] = right->keys[0];
        for (i = 0; i < right->num_keys - 1; i++) {
          right->keys[i] = right->keys[i + 1];
        }
        for (i = 0; i < right->num_keys; i++) {
          right->children[i] = right->children[i + 1];
        }
        right->num_keys -= 1;
      }
      return;
    }
  }
  /* merge */
  if (idx > 0) {
    bplus_merge_nodes(tree, parent->children[idx - 1], node, parent, idx - 1);
  } else {
    bplus_merge_nodes(tree, node, parent->children[idx + 1], parent, idx);
  }
}

/* the removed record may still be a separator above its leaf (when its
 * leaf emptied, or merged away, before the separators were refreshed);
 * the path to where it was is walked once more, and any separator that is
 * the record becomes the minimum of the subtree to its right */
static void bplus_purge_separator(ds_bplus_tree_t *tree,
                                  ds_bplus_tree_node_t *gone) {
  ds_bplus_tree_internal_node_t *node = tree->root;
  while (!node->is_leaf) {
    int i = 0;
    while (i < node->num_keys && tree->compare(gone, node->keys[i]) >= 0) {
      i += 1;
    }
    if (i > 0 && node->keys[i - 1] == gone) {
      node->keys[i - 1] = bplus_subtree_min(node->children[i]);
    }
    node = node->children[i];
  }
}

bool ds_bplus_tree_remove(ds_bplus_tree_t *tree, ds_bplus_tree_node_t *key,
                          ds_bplus_tree_node_t **out_data) {
  ds_bplus_tree_internal_node_t *leaf;
  int pos;
  int i;
  bool removed = false;
  ds_bplus_tree_node_t *gone;
  leaf = bplus_find_leaf(tree, key);
  pos = -1;
  for (i = 0; i < leaf->num_keys; i++) {
    if (tree->compare(key, leaf->keys[i]) == 0) {
      pos = i;
      break;
    }
  }
  if (pos < 0) {
    return false;
  }
  gone = leaf->keys[pos];
  if (out_data != NULL) {
    *out_data = gone;
  }
  for (i = pos; i < leaf->num_keys - 1; i++) {
    leaf->keys[i] = leaf->keys[i + 1];
  }
  leaf->num_keys -= 1;
  tree->size -= 1;
  removed = true;
  if (leaf->num_keys > 0) {
    bplus_refresh_separators(leaf);
  }
  if (leaf != tree->root && leaf->num_keys < bplus_min_keys(tree)) {
    bplus_fix_underflow(tree, leaf);
  }
  bplus_purge_separator(tree, gone);
  return removed;
}
