#include "btree.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

btree_internal_node_t *btree_create_node(btree_t *tree, bool is_leaf) {
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
  tree->root->parent = NULL;
  tree->nil = (btree_node_t *)malloc(sizeof(btree_node_t));
  tree->size = 0;

#ifdef BTREE_THREAD_SAFE
  LOCK_INIT_RECURSIVE(tree)
#endif

  return tree;
}

void btree_split_child(btree_t *tree, btree_internal_node_t *parent,
                       int index) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  btree_internal_node_t *child = parent->children[index];
  btree_internal_node_t *new_node = btree_create_node(tree, child->is_leaf);
  new_node->num_keys = tree->degree - 1;
  new_node->parent = parent;

  for (int i = 0; i < tree->degree - 1; i++) {
    new_node->data[i] = child->data[i + tree->degree];
    new_node->data[i]->internal = new_node;
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
    parent->data[i + 1]->internal = parent;
  }
  parent->data[index] = child->data[tree->degree - 1];
  parent->data[index]->internal = parent;
  parent->num_keys++;
#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void btree_insert_non_full(btree_t *tree, btree_internal_node_t *node,
                           btree_node_t *data,
                           int (*compare)(btree_node_t *, btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  int i = node->num_keys - 1;

  if (node->is_leaf) {
    while (i >= 0 && compare(data, node->data[i]) < 0) {
      node->data[i + 1] = node->data[i];
      i--;
    }
    node->data[i + 1] = data;
    data->internal = node;
    node->num_keys++;
  } else {
    while (i >= 0 && compare(data, node->data[i]) < 0) {
      i--;
    }
    i++;
    if (node->children[i]->num_keys == 2 * tree->degree - 1) {
      btree_split_child(tree, node, i);
      if (compare(data, node->data[i]) > 0) {
        i++;
      }
    }
    btree_insert_non_full(tree, node->children[i], data, compare);
  }
#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void btree_insert(btree_t *tree, btree_node_t *data,
                  int (*compare)(btree_node_t *, btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  data->internal = NULL;
  btree_internal_node_t *root = tree->root;
  if (root->num_keys == 2 * tree->degree - 1) {
    btree_internal_node_t *new_root = btree_create_node(tree, false);
    root->parent = new_root;
    new_root->parent = NULL;
    new_root->children[0] = root;
    btree_split_child(tree, new_root, 0);
    tree->root = new_root;
    btree_insert_non_full(tree, new_root, data, compare);
  } else {
    btree_insert_non_full(tree, root, data, compare);
  }
  tree->size += 1;

#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}

btree_node_t *
btree_search_node(btree_t *tree, btree_internal_node_t *node, btree_node_t *key,
                  int (*compare)(btree_node_t *, btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
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
  btree_node_t *result =
      btree_search_node(tree, node->children[i], key, compare);
#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
  return result;
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

  return result;
}

void *btree_local_minimum(btree_t *tree, btree_internal_node_t *node) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif

  while (!node->is_leaf) {
    node = node->children[0];
  }

#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif

  return node->data[0];
}

void *btree_local_maximum(btree_t *tree, btree_internal_node_t *node) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif

  while (!node->is_leaf) {
    node = node->children[node->num_keys];
  }

#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif

  return node->data[node->num_keys - 1];
}

void *btree_minimum(btree_t *tree) {
  return btree_local_minimum(tree, tree->root);
}

void *btree_maximum(btree_t *tree) {
  return btree_local_maximum(tree, tree->root);
}

void btree_rebalance(btree_t *tree, btree_internal_node_t *node) {
  int i, j;
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  btree_internal_node_t *parent = node->parent;
  i = 0;
  while (i <= parent->num_keys && parent->children[i] != node) {
    i++;
  }
  if (parent->num_keys > i &&
      parent->children[i + 1]->num_keys > tree->degree - 1) {
    node->data[node->num_keys] = parent->data[i];
    node->data[node->num_keys]->internal = node;
    node->num_keys++;
    parent->data[i] = parent->children[i + 1]->data[0];
    parent->data[i]->internal = parent;
    for (j = 1; j < parent->children[i + 1]->num_keys; j++) {
      parent->children[i + 1]->data[j - 1] = parent->children[i + 1]->data[j];
    }
    parent->children[i + 1]->num_keys--;
  } else if (i != 0 && parent->children[i - 1]->num_keys > tree->degree - 1) {
    for (j = node->num_keys; j > 0; j--) {
      node->data[j] = node->data[j - 1];
    }
    node->data[0] = parent->data[i];
    node->data[0]->internal = node;
    node->num_keys++;
    parent->data[i] =
        parent->children[i - 1]->data[parent->children[i - 1]->num_keys - 1];
    parent->data[i]->internal = parent;
    parent->children[i - 1]->num_keys--;
  } else {
    btree_internal_node_t *recipient, *next;
    if (i != 0) {
      recipient = parent->children[i - 1];
      next = node;
    } else {
      recipient = node;
      next = parent->children[i + 1];
    }
    recipient->data[recipient->num_keys] = parent->data[i];
    recipient->data[recipient->num_keys]->internal = recipient;
    recipient->num_keys++;
    for (j = 0; j < next->num_keys; j++) {
      recipient->data[recipient->num_keys] = next->data[j];
      recipient->data[recipient->num_keys]->internal = recipient;
      recipient->num_keys++;
      next->num_keys--;
    }
    free(next->data);
    free(next->children);
    free(next);

    for (j = i + 1; j < parent->num_keys; j++) {
      parent->data[j - 1] = parent->data[j];
    }

    for (j = i + 2; j <= parent->num_keys; j++) {
      parent->children[j - 1] = parent->children[j];
    }
    parent->num_keys--;
    if (parent == tree->root && parent->num_keys == 0) {
      tree->root = recipient;
      free(parent);
    } else if (parent->num_keys < tree->degree - 1) {
      btree_rebalance(tree, parent);
    }
  }
#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void btree_delete_node(btree_t *tree, btree_node_t *key) {
  int i;
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  btree_internal_node_t *node = key->internal;
  btree_node_t *borrowed;
  i = 0;
  while (i < node->num_keys && node->data[i] != key) {
    i++;
  }
  if (node->is_leaf) {
    for (i = i + 1; i < node->num_keys; i++) {
      node->data[i - 1] = node->data[i];
    }
    node->num_keys--;

    if (node->num_keys < tree->degree - 1) {
      btree_rebalance(tree, node);
    }
  } else {
    if (node->num_keys > i &&
        node->children[i + 1]->num_keys > tree->degree - 1) {
      borrowed = btree_local_minimum(tree, node->children[i + 1]);
    } else {
      borrowed = btree_local_maximum(tree, node->children[i]);
    }
    node->data[i] = borrowed;

    btree_internal_node_t *borrowed_internal = borrowed->internal;
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
    if (borrowed->internal->num_keys < tree->degree - 1) {
      btree_rebalance(tree, borrowed->internal);
    }
  }
#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void btree_destroy_node(btree_t *tree, btree_node_t *node,
                        void (*destroy)(btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  btree_delete_node(tree, node);
  destroy(node);
#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void btree_destroy_recursive(btree_t *tree, btree_internal_node_t *node,
                             void (*destroy)(btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  if (node == NULL) {
    return;
  }

  if (!node->is_leaf) {
    for (int i = 0; i <= node->num_keys; i++) {
      btree_destroy_recursive(tree, node->children[i], destroy);
    }
  }
  for (int i = 0; i < node->num_keys; i++) {
    destroy(node->data[i]);
  }

#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void btree_destroy_tree(btree_t *tree, void (*destroy)(btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  btree_destroy_recursive(tree, tree->root, destroy);
  free(tree->nil);
#ifdef BTREE_THREAD_SAFE
  LOCK_DESTROY(tree)
#endif
  free(tree);
}

void btree_inorder_walk(btree_internal_node_t *node,
                        void (*callback)(btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  if (node == NULL) {
#ifdef BTREE_THREAD_SAFE
    UNLOCK(tree)
#endif
    return;
  }
  int i;
  for (i = 0; i < node->num_keys; i++) {
    if (!node->is_leaf) {
      btree_inorder_walk(node->children[i], callback);
    }
    callback(node->data[i]);
  }
  if (!node->is_leaf) {
    btree_inorder_walk(node->children[i], callback);
  }
#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}
void btree_inorder_walk_tree(btree_t *tree, void (*callback)(btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  btree_inorder_walk(tree->root, callback);
#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}
void btree_preorder_walk(btree_internal_node_t *node,
                         void (*callback)(btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  if (node == NULL) {
#ifdef BTREE_THREAD_SAFE
    UNLOCK(tree)
#endif
    return;
  }
  for (int i = 0; i < node->num_keys; i++) {
    callback(node->data[i]);
  }
  if (!node->is_leaf) {
    for (int i = 0; i <= node->num_keys; i++) {
      btree_preorder_walk(node->children[i], callback);
    }
  }
#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}
void btree_preorder_walk_tree(btree_t *tree, void (*callback)(btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  btree_preorder_walk(tree->root, callback);
#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}
void btree_postorder_walk(btree_internal_node_t *node,
                          void (*callback)(btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  if (node == NULL) {
#ifdef BTREE_THREAD_SAFE
    UNLOCK(tree)
#endif
    return;
  }
  if (!node->is_leaf) {
    for (int i = 0; i <= node->num_keys; i++) {
      btree_postorder_walk(node->children[i], callback);
    }
  }
  for (int i = 0; i < node->num_keys; i++) {
    callback(node->data[i]);
  }
#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}

void btree_postorder_walk_tree(btree_t *tree,
                               void (*callback)(btree_node_t *)) {
#ifdef BTREE_THREAD_SAFE
  LOCK(tree)
#endif
  btree_postorder_walk(tree->root, callback);
#ifdef BTREE_THREAD_SAFE
  UNLOCK(tree)
#endif
}
