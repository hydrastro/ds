#include "rope.h"
#include <stdlib.h>
#include <string.h>

#define DS_ROPE_MAX_LEAF 64

static ds_rope_node_t *rope_new_leaf(ds_rope_t *rope, const char *str,
                                     size_t length) {
  ds_rope_node_t *node =
      (ds_rope_node_t *)rope->allocator(sizeof(ds_rope_node_t));
  size_t i;
  node->left = NULL;
  node->right = NULL;
  node->is_leaf = true;
  node->weight = length;
  node->length = length;
  node->data = (char *)rope->allocator(length + 1);
  for (i = 0; i < length; i++) {
    node->data[i] = str[i];
  }
  node->data[length] = '\0';
  return node;
}

static ds_rope_node_t *rope_new_internal(ds_rope_t *rope, ds_rope_node_t *left,
                                         ds_rope_node_t *right) {
  ds_rope_node_t *node =
      (ds_rope_node_t *)rope->allocator(sizeof(ds_rope_node_t));
  node->left = left;
  node->right = right;
  node->is_leaf = false;
  node->data = NULL;
  node->weight = left != NULL ? left->length : 0;
  node->length =
      (left != NULL ? left->length : 0) + (right != NULL ? right->length : 0);
  return node;
}

ds_rope_t *ds_rope_create(void) {
  return ds_rope_create_alloc(malloc, free);
}

ds_rope_t *ds_rope_create_alloc(void *(*allocator)(size_t),
                                void (*deallocator)(void *)) {
  ds_rope_t *rope = (ds_rope_t *)allocator(sizeof(ds_rope_t));
  rope->root = NULL;
  rope->allocator = allocator;
  rope->deallocator = deallocator;
  return rope;
}

/* build a balanced tree from a contiguous buffer, splitting into leaves */
static ds_rope_node_t *rope_build(ds_rope_t *rope, const char *str,
                                  size_t length) {
  if (length <= DS_ROPE_MAX_LEAF) {
    return rope_new_leaf(rope, str, length);
  }
  {
    size_t mid = length / 2;
    ds_rope_node_t *left = rope_build(rope, str, mid);
    ds_rope_node_t *right = rope_build(rope, str + mid, length - mid);
    return rope_new_internal(rope, left, right);
  }
}

ds_rope_t *ds_rope_from_string(const char *str, size_t length) {
  ds_rope_t *rope = ds_rope_create();
  if (length > 0) {
    rope->root = rope_build(rope, str, length);
  }
  return rope;
}

size_t ds_rope_length(ds_rope_t *rope) {
  size_t result;
  result = rope->root != NULL ? rope->root->length : 0;
  return result;
}

static char rope_char_at_node(ds_rope_node_t *node, size_t index) {
  while (!node->is_leaf) {
    if (index < node->weight) {
      node = node->left;
    } else {
      index -= node->weight;
      node = node->right;
    }
    if (node == NULL) {
      return '\0';
    }
  }
  if (index < node->length) {
    return node->data[index];
  }
  return '\0';
}

char ds_rope_char_at(ds_rope_t *rope, size_t index) {
  char result = '\0';
  if (rope->root != NULL && index < rope->root->length) {
    result = rope_char_at_node(rope->root, index);
  }
  return result;
}

/* collect leaves left-to-right into an array */
static void rope_collect_leaves(ds_rope_node_t *node, ds_rope_node_t **leaves,
                                size_t *count) {
  if (node == NULL) {
    return;
  }
  if (node->is_leaf) {
    leaves[*count] = node;
    *count += 1;
    return;
  }
  rope_collect_leaves(node->left, leaves, count);
  rope_collect_leaves(node->right, leaves, count);
}

/* count leaves in a subtree */
static size_t rope_count_leaves(ds_rope_node_t *node) {
  if (node == NULL) {
    return 0;
  }
  if (node->is_leaf) {
    return 1;
  }
  return rope_count_leaves(node->left) + rope_count_leaves(node->right);
}

/* build a balanced internal tree over an array of leaves [lo, hi) */
static ds_rope_node_t *rope_balance_leaves(ds_rope_t *rope,
                                           ds_rope_node_t **leaves, size_t lo,
                                           size_t hi) {
  if (hi - lo == 1) {
    leaves[lo]->weight = leaves[lo]->length;
    return leaves[lo];
  }
  {
    size_t mid = lo + (hi - lo) / 2;
    ds_rope_node_t *left = rope_balance_leaves(rope, leaves, lo, mid);
    ds_rope_node_t *right = rope_balance_leaves(rope, leaves, mid, hi);
    return rope_new_internal(rope, left, right);
  }
}

/* free only internal nodes, leaving leaves intact (used during rebalance) */
static void rope_free_internal(ds_rope_t *rope, ds_rope_node_t *node) {
  if (node == NULL || node->is_leaf) {
    return;
  }
  rope_free_internal(rope, node->left);
  rope_free_internal(rope, node->right);
  rope->deallocator(node);
}

void ds_rope_rebalance(ds_rope_t *rope) {
  if (rope->root != NULL && !rope->root->is_leaf) {
    size_t num_leaves = rope_count_leaves(rope->root);
    ds_rope_node_t **leaves = (ds_rope_node_t **)rope->allocator(
        num_leaves * sizeof(ds_rope_node_t *));
    size_t count = 0;
    rope_collect_leaves(rope->root, leaves, &count);
    rope_free_internal(rope, rope->root);
    rope->root = rope_balance_leaves(rope, leaves, 0, num_leaves);
    rope->deallocator(leaves);
  }
}

/* depth of a subtree */
static size_t rope_depth(ds_rope_node_t *node) {
  size_t l;
  size_t r;
  if (node == NULL || node->is_leaf) {
    return 1;
  }
  l = rope_depth(node->left);
  r = rope_depth(node->right);
  return 1 + (l > r ? l : r);
}

/* concatenate two subtrees, no locking */
static ds_rope_node_t *rope_concat_nodes(ds_rope_t *rope, ds_rope_node_t *a,
                                         ds_rope_node_t *b) {
  if (a == NULL) {
    return b;
  }
  if (b == NULL) {
    return a;
  }
  return rope_new_internal(rope, a, b);
}

static void rope_maybe_rebalance(ds_rope_t *rope) {
  if (rope->root != NULL && !rope->root->is_leaf) {
    size_t depth = rope_depth(rope->root);
    size_t leaves = rope_count_leaves(rope->root);
    size_t threshold = 4;
    size_t l = leaves;
    /* threshold ~ 2*log2(leaves) + 4 */
    while (l > 1) {
      threshold += 2;
      l >>= 1;
    }
    if (depth > threshold) {
      size_t num_leaves = rope_count_leaves(rope->root);
      ds_rope_node_t **arr = (ds_rope_node_t **)rope->allocator(
          num_leaves * sizeof(ds_rope_node_t *));
      size_t count = 0;
      rope_collect_leaves(rope->root, arr, &count);
      rope_free_internal(rope, rope->root);
      rope->root = rope_balance_leaves(rope, arr, 0, num_leaves);
      rope->deallocator(arr);
    }
  }
}

void ds_rope_append(ds_rope_t *rope, const char *str, size_t length) {
  if (length > 0) {
    ds_rope_node_t *leaf = rope_build(rope, str, length);
    rope->root = rope_concat_nodes(rope, rope->root, leaf);
    rope_maybe_rebalance(rope);
  }
}

/* split subtree at index into (left, right); returns via out params. The input
 * node is consumed (freed or reused). */
static void rope_split_node(ds_rope_t *rope, ds_rope_node_t *node, size_t index,
                            ds_rope_node_t **out_left,
                            ds_rope_node_t **out_right) {
  if (node == NULL) {
    *out_left = NULL;
    *out_right = NULL;
    return;
  }
  if (node->is_leaf) {
    if (index == 0) {
      *out_left = NULL;
      *out_right = node;
    } else if (index >= node->length) {
      *out_left = node;
      *out_right = NULL;
    } else {
      ds_rope_node_t *l = rope_new_leaf(rope, node->data, index);
      ds_rope_node_t *r =
          rope_new_leaf(rope, node->data + index, node->length - index);
      rope->deallocator(node->data);
      rope->deallocator(node);
      *out_left = l;
      *out_right = r;
    }
    return;
  }
  if (index < node->weight) {
    ds_rope_node_t *sl;
    ds_rope_node_t *sr;
    rope_split_node(rope, node->left, index, &sl, &sr);
    *out_left = sl;
    *out_right = rope_concat_nodes(rope, sr, node->right);
    rope->deallocator(node);
  } else if (index > node->weight) {
    ds_rope_node_t *sl;
    ds_rope_node_t *sr;
    rope_split_node(rope, node->right, index - node->weight, &sl, &sr);
    *out_left = rope_concat_nodes(rope, node->left, sl);
    *out_right = sr;
    rope->deallocator(node);
  } else {
    *out_left = node->left;
    *out_right = node->right;
    rope->deallocator(node);
  }
}

void ds_rope_insert(ds_rope_t *rope, size_t index, const char *str,
                    size_t length) {
  if (length > 0) {
    ds_rope_node_t *mid = rope_build(rope, str, length);
    size_t total = rope->root != NULL ? rope->root->length : 0;
    if (index >= total) {
      rope->root = rope_concat_nodes(rope, rope->root, mid);
    } else {
      ds_rope_node_t *left;
      ds_rope_node_t *right;
      rope_split_node(rope, rope->root, index, &left, &right);
      left = rope_concat_nodes(rope, left, mid);
      rope->root = rope_concat_nodes(rope, left, right);
    }
    rope_maybe_rebalance(rope);
  }
}

static void rope_destroy_node(ds_rope_t *rope, ds_rope_node_t *node) {
  if (node == NULL) {
    return;
  }
  if (node->is_leaf) {
    rope->deallocator(node->data);
    rope->deallocator(node);
    return;
  }
  rope_destroy_node(rope, node->left);
  rope_destroy_node(rope, node->right);
  rope->deallocator(node);
}

void ds_rope_delete(ds_rope_t *rope, size_t index, size_t count) {
  if (rope->root != NULL && count > 0 && index < rope->root->length) {
    ds_rope_node_t *left;
    ds_rope_node_t *mid_right;
    ds_rope_node_t *mid;
    ds_rope_node_t *right;
    size_t total = rope->root->length;
    if (index + count > total) {
      count = total - index;
    }
    rope_split_node(rope, rope->root, index, &left, &mid_right);
    rope_split_node(rope, mid_right, count, &mid, &right);
    rope_destroy_node(rope, mid);
    rope->root = rope_concat_nodes(rope, left, right);
    rope_maybe_rebalance(rope);
  }
}

static size_t rope_substring_node(ds_rope_node_t *node, size_t index,
                                  size_t count, char *out, size_t written) {
  if (node == NULL || count == 0) {
    return written;
  }
  if (node->is_leaf) {
    size_t i;
    for (i = index; i < node->length && count > 0; i++) {
      out[written] = node->data[i];
      written += 1;
      count -= 1;
    }
    return written;
  }
  if (index < node->weight) {
    size_t before = written;
    written = rope_substring_node(node->left, index, count, out, written);
    count -= (written - before);
    if (count > 0) {
      written = rope_substring_node(node->right, 0, count, out, written);
    }
  } else {
    written = rope_substring_node(node->right, index - node->weight, count, out,
                                  written);
  }
  return written;
}

size_t ds_rope_substring(ds_rope_t *rope, size_t index, size_t count,
                         char *out) {
  size_t written = 0;
  if (rope->root != NULL && index < rope->root->length) {
    if (index + count > rope->root->length) {
      count = rope->root->length - index;
    }
    written = rope_substring_node(rope->root, index, count, out, 0);
  }
  return written;
}

char *ds_rope_to_string(ds_rope_t *rope) {
  char *buffer;
  size_t length;
  length = rope->root != NULL ? rope->root->length : 0;
  buffer = (char *)rope->allocator(length + 1);
  if (length > 0) {
    rope_substring_node(rope->root, 0, length, buffer, 0);
  }
  buffer[length] = '\0';
  return buffer;
}

void ds_rope_concat(ds_rope_t *rope, ds_rope_t *other) {
  if (other->root != NULL) {
    rope->root = rope_concat_nodes(rope, rope->root, other->root);
    other->root = NULL;
    rope_maybe_rebalance(rope);
  }
}

void ds_rope_destroy(ds_rope_t *rope) {
  rope_destroy_node(rope, rope->root);
  rope->deallocator(rope);
}
