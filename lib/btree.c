#include "btree.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static btree_internal_node_t *btree_create_node(btree_t *tree, bool is_leaf) {
  btree_internal_node_t *node =
      (btree_internal_node_t *)malloc(sizeof(btree_internal_node_t));
  node->num_keys = 0;
  node->is_leaf = is_leaf;
  node->data =
      (btree_node_t **)malloc((2 * tree->degree - 1) * sizeof(btree_node_t *));
  node->children = (btree_internal_node_t **)malloc(
      2 * tree->degree * sizeof(btree_internal_node_t *));
  memset(node->children, 0, 2 * tree->degree * sizeof(btree_internal_node_t *));
  return node;
}

btree_t *btree_create(int degree) {
  btree_t *tree = (btree_t *)malloc(sizeof(btree_t));
  tree->degree = degree;
  tree->root = btree_create_node(tree, true);
  tree->nil = (btree_node_t *)malloc(sizeof(btree_node_t));

#ifdef BTREE_THREAD_SAFE
  LOCK_INIT_RECURSIVE(tree)
#endif

  return tree;
}

static void btree_split_child(btree_t *tree, btree_internal_node_t *parent,
                              int index, btree_internal_node_t *child) {
  btree_internal_node_t *new_node = btree_create_node(tree, child->is_leaf);
  new_node->num_keys = tree->degree - 1;

  for (int i = 0; i < tree->degree - 1; i++) {
    new_node->data[i] = child->data[i + tree->degree];
  }

  if (!child->is_leaf) {
    for (int i = 0; i < tree->degree; i++) {
      new_node->children[i] = child->children[i + tree->degree];
    }
  }

  child->num_keys = tree->degree - 1;

  for (int i = parent->num_keys; i >= index + 1; i--) {
    parent->children[i + 1] = parent->children[i];
  }
  parent->children[index + 1] = new_node;

  for (int i = parent->num_keys - 1; i >= index; i--) {
    parent->data[i + 1] = parent->data[i];
  }
  parent->data[index] = child->data[tree->degree - 1];
  parent->num_keys++;
}

static void btree_insert_non_full(btree_t *tree, btree_internal_node_t *node,
                                  btree_node_t *data,
                                  int (*compare)(btree_node_t *,
                                                 btree_node_t *)) {
  int i = node->num_keys - 1;

  if (node->is_leaf) {
    while (i >= 0 && compare(data, node->data[i]) < 0) {
      node->data[i + 1] = node->data[i];
      i--;
    }
    node->data[i + 1] = data;
    node->num_keys++;
  } else {
    while (i >= 0 && compare(data, node->data[i]) < 0) {
      i--;
    }
    i++;
    if (node->children[i]->num_keys == 2 * tree->degree - 1) {
      btree_split_child(tree, node, i, node->children[i]);
      if (compare(data, node->data[i]) > 0) {
        i++;
      }
    }
    btree_insert_non_full(tree, node->children[i], data, compare);
  }
}

void btree_insert(btree_t *tree, btree_node_t *data,
                  int (*compare)(btree_node_t *, btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif

  btree_internal_node_t *root = tree->root;
  if (root->num_keys == 2 * tree->degree - 1) {
    btree_internal_node_t *new_root = btree_create_node(tree, false);
    new_root->children[0] = root;
    btree_split_child(tree, new_root, 0, root);
    tree->root = new_root;
    btree_insert_non_full(tree, new_root, data, compare);
  } else {
    btree_insert_non_full(tree, root, data, compare);
  }

#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}

static btree_node_t *
btree_search_node(btree_t *tree, btree_internal_node_t *node, btree_node_t *key,
                  int (*compare)(btree_node_t *, btree_node_t *)) {
  int i = 0;
  while (i < node->num_keys && compare(key, node->data[i]) > 0) {
    i++;
  }

  if (i < node->num_keys && compare(key, node->data[i]) == 0) {
    return node->data[i];
  }

  if (node->is_leaf) {
    return tree->nil;
  }

  return btree_search_node(tree, node->children[i], key, compare);
}

btree_node_t *btree_search(btree_t *tree, btree_node_t *key,
                           int (*compare)(btree_node_t *, btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif

  btree_node_t *result = btree_search_node(tree, tree->root, key, compare);

#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif

  return result ? result : tree->nil;
}

static void btree_merge_nodes(btree_t *tree, btree_internal_node_t *parent,
                              int index,
                              int (*compare)(btree_node_t *, btree_node_t *)) {
  btree_internal_node_t *child = parent->children[index];
  btree_internal_node_t *sibling = parent->children[index + 1];
  int t = tree->degree;

  child->data[t - 1] = parent->data[index];
  for (int i = 0; i < t - 1; i++) {
    child->data[t + i] = sibling->data[i];
  }
  if (!child->is_leaf) {
    for (int i = 0; i < t; i++) {
      child->children[t + i] = sibling->children[i];
    }
  }

  for (int i = index + 1; i < parent->num_keys; i++) {
    parent->data[i - 1] = parent->data[i];
  }
  for (int i = index + 2; i <= parent->num_keys; i++) {
    parent->children[i - 1] = parent->children[i];
  }

  child->num_keys = 2 * t - 1;
  parent->num_keys--;

  free(sibling->data);
  free(sibling->children);
  free(sibling);
}

static void
btree_delete_non_leaf(btree_t *tree, btree_internal_node_t *node, int index,
                      int (*compare)(btree_node_t *, btree_node_t *)) {
  btree_internal_node_t *child = node->children[index];
  btree_internal_node_t *sibling = node->children[index + 1];
  int t = tree->degree;

  if (child->num_keys >= t) {
    btree_internal_node_t *pred = child;
    while (!pred->is_leaf) {
      pred = pred->children[pred->num_keys];
    }
    node->data[index] = pred->data[pred->num_keys - 1];
    btree_delete_node(tree, child, node->data[index], compare);
  } else if (sibling->num_keys >= t) {
    btree_internal_node_t *succ = sibling;
    while (!succ->is_leaf) {
      succ = succ->children[0];
    }
    node->data[index] = succ->data[0];
    btree_delete_node(tree, sibling, node->data[index], compare);
  } else {
    btree_merge_nodes(tree, node, index, compare);
    btree_delete_node(tree, child, node->data[index], compare);
  }
}

static void btree_delete_node(btree_t *tree, btree_internal_node_t *node,
                              btree_node_t *key,
                              int (*compare)(btree_node_t *, btree_node_t *)) {
  int index = 0;
  while (index < node->num_keys && compare(key, node->data[index]) > 0) {
    index++;
  }

  if (index < node->num_keys && compare(key, node->data[index]) == 0) {
    if (node->is_leaf) {
      for (int i = index; i < node->num_keys - 1; i++) {
        node->data[i] = node->data[i + 1];
      }
      node->num_keys--;
    } else {
      btree_delete_non_leaf(tree, node, index, compare);
    }
  } else if (!node->is_leaf) {
    if (node->children[index]->num_keys == tree->degree - 1) {
      if (index != node->num_keys &&
          node->children[index + 1]->num_keys >= tree->degree) {
        btree_split_child(tree, node, index, node->children[index]);
      } else if (index != 0 &&
                 node->children[index - 1]->num_keys >= tree->degree) {
        btree_split_child(tree, node, index - 1, node->children[index - 1]);
      } else {
        if (index == node->num_keys) {
          index--;
        }
        btree_merge_nodes(tree, node, index, compare);
      }
    }
    btree_delete_node(tree, node->children[index], key, compare);
  }
}

void btree_delete(btree_t *tree, btree_node_t *key,
                  void (*destroy_data)(btree_node_t *),
                  int (*compare)(btree_node_t *, btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  btree_delete_node(tree, tree->root, key, compare);

  if (tree->root->num_keys == 0 && !tree->root->is_leaf) {
    btree_internal_node_t *old_root = tree->root;
    tree->root = tree->root->children[0];
    free(old_root->data);
    free(old_root->children);
    free(old_root);
  }

#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void btree_destroy_node(btree_t *tree, btree_internal_node_t *node,
                        void (*destroy_data)(btree_node_t *)) {
  if (!node->is_leaf) {
    for (int i = 0; i <= node->num_keys; i++) {
      btree_destroy_node(tree, node->children[i], destroy_data);
    }
  }
  for (int i = 0; i < node->num_keys; i++) {
    destroy_data(node->data[i]);
  }
  free(node->data);
  free(node->children);
  free(node);
}

void btree_destroy(btree_t *tree, void (*destroy_data)(btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  btree_destroy_node(tree, tree->root, destroy_data);
  free(tree);

#ifdef BTREE_THREAD_SAFE
  LOCK_DESTROY(tree)
#endif
}

void *btree_minimum(btree_t *tree) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif

  btree_internal_node_t *node = tree->root;
  while (!node->is_leaf) {
    node = node->children[0];
  }

#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif

  return node->data[0];
}

void *btree_maximum(btree_t *tree) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif

  btree_internal_node_t *node = tree->root;
  while (!node->is_leaf) {
    node = node->children[node->num_keys];
  }

#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif

  return node->data[node->num_keys - 1];
}
