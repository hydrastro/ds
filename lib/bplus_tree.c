#include "bplus_tree.h"
#include <stdlib.h>

/*
 * B+ tree: all records live in leaves; internal nodes hold separator keys for
 * routing only. Leaves are doubly linked for ordered/range scans. A node holds
 * at most (order - 1) keys and, when internal, (order) children. The bstar flag
 * raises occupancy: on a full leaf, insertion first tries to redistribute into
 * an adjacent sibling and only splits (2 nodes -> 3) when the sibling is also
 * full, yielding ~2/3 minimum fill instead of ~1/2.
 */

static int bplus_max_keys(ds_bplus_tree_t *tree) {
  return tree->order - 1;
}

static int bplus_min_keys(ds_bplus_tree_t *tree) {
  if (tree->bstar) {
    return (2 * (tree->order - 1)) / 3;
  }
  return (tree->order - 1) / 2;
}

static ds_bplus_tree_internal_node_t *bplus_create_node(ds_bplus_tree_t *tree,
                                                        bool is_leaf) {
  ds_bplus_tree_internal_node_t *node =
      (ds_bplus_tree_internal_node_t *)tree->allocator(
          sizeof(ds_bplus_tree_internal_node_t));
  node->num_keys = 0;
  node->is_leaf = is_leaf;
  node->parent = NULL;
  node->next = NULL;
  node->prev = NULL;
  /* allocate room for one extra slot to simplify overflow-before-split */
  node->keys = (ds_bplus_tree_node_t **)tree->allocator(
      (size_t)(tree->order + 1) * sizeof(ds_bplus_tree_node_t *));
  if (is_leaf) {
    node->children = NULL;
  } else {
    node->children = (ds_bplus_tree_internal_node_t **)tree->allocator(
        (size_t)(tree->order + 2) * sizeof(ds_bplus_tree_internal_node_t *));
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
  ds_bplus_tree_t *tree = (ds_bplus_tree_t *)allocator(sizeof(ds_bplus_tree_t));
  tree->order = order < 3 ? 3 : order;
  tree->bstar = bstar;
  tree->compare = compare;
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->size = 0;
  tree->nil = (ds_bplus_tree_node_t *)allocator(sizeof(ds_bplus_tree_node_t));
  tree->nil->internal = NULL;
  tree->root = bplus_create_node(tree, true);
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

/* B* leaf overflow: try to push the overflow record into a sibling that has
 * room before resorting to a split. Returns true if redistribution handled it.
 */
static bool bplus_bstar_relieve_leaf(ds_bplus_tree_t *tree,
                                     ds_bplus_tree_internal_node_t *leaf) {
  ds_bplus_tree_internal_node_t *parent = leaf->parent;
  int max_keys = bplus_max_keys(tree);
  int idx;
  int i;
  if (parent == NULL) {
    return false;
  }
  idx = 0;
  while (idx <= parent->num_keys && parent->children[idx] != leaf) {
    idx += 1;
  }
  /* try left sibling */
  if (idx > 0) {
    ds_bplus_tree_internal_node_t *left = parent->children[idx - 1];
    if (left->num_keys < max_keys) {
      ds_bplus_tree_node_t *moved = leaf->keys[0];
      left->keys[left->num_keys] = moved;
      left->num_keys += 1;
      for (i = 0; i < leaf->num_keys - 1; i++) {
        leaf->keys[i] = leaf->keys[i + 1];
      }
      leaf->num_keys -= 1;
      parent->keys[idx - 1] = leaf->keys[0];
      return true;
    }
  }
  /* try right sibling */
  if (idx < parent->num_keys) {
    ds_bplus_tree_internal_node_t *right = parent->children[idx + 1];
    if (right->num_keys < max_keys) {
      ds_bplus_tree_node_t *moved = leaf->keys[leaf->num_keys - 1];
      for (i = right->num_keys; i > 0; i--) {
        right->keys[i] = right->keys[i - 1];
      }
      right->keys[0] = moved;
      right->num_keys += 1;
      leaf->num_keys -= 1;
      parent->keys[idx] = right->keys[0];
      return true;
    }
  }
  return false;
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
    bplus_split_internal(tree, parent);
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
    if (!tree->bstar || !bplus_bstar_relieve_leaf(tree, leaf)) {
      bplus_split_leaf(tree, leaf);
    }
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

ds_bplus_tree_t *ds_bplus_tree_clone(
    ds_bplus_tree_t *tree,
    ds_bplus_tree_node_t *(*clone_node)(ds_bplus_tree_node_t *)) {
  ds_bplus_tree_t *new_tree;
  ds_bplus_tree_internal_node_t *leaf;
  int i;
  new_tree = ds_bplus_tree_create_alloc(tree->order, tree->bstar, tree->compare,
                                        tree->allocator, tree->deallocator);
  leaf = tree->head;
  while (leaf != NULL) {
    for (i = 0; i < leaf->num_keys; i++) {
      ds_bplus_tree_insert(new_tree, clone_node(leaf->keys[i]));
    }
    leaf = leaf->next;
  }
  return new_tree;
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

bool ds_bplus_tree_remove(ds_bplus_tree_t *tree, ds_bplus_tree_node_t *key,
                          ds_bplus_tree_node_t **out_data) {
  ds_bplus_tree_internal_node_t *leaf;
  int pos;
  int i;
  bool removed = false;
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
  if (out_data != NULL) {
    *out_data = leaf->keys[pos];
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
  return removed;
}
