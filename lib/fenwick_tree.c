#include "fenwick_tree.h"
#include <stdlib.h>

ds_fenwick_tree_t *ds_fenwick_tree_create(size_t size) {
  return ds_fenwick_tree_create_alloc(size, malloc, free);
}

ds_fenwick_tree_t *ds_fenwick_tree_create_alloc(size_t size,
                                                void *(*allocator)(size_t),
                                                void (*deallocator)(void *)) {
  ds_fenwick_tree_t *tree;
  size_t i;
  tree = (ds_fenwick_tree_t *)allocator(sizeof(ds_fenwick_tree_t));
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->size = size;
  tree->data = (long *)allocator((size + 1) * sizeof(long));
  for (i = 0; i <= size; i++) {
    tree->data[i] = 0;
  }
  return tree;
}

void ds_fenwick_tree_update(ds_fenwick_tree_t *tree, size_t index, long delta) {
  size_t i;
  if (index >= tree->size) {
    return;
  }
  i = index + 1;
  while (i <= tree->size) {
    tree->data[i] += delta;
    i += i & (~i + 1);
  }
}

void ds_fenwick_tree_set(ds_fenwick_tree_t *tree, size_t index, long value) {
  long current;
  if (index >= tree->size) {
    return;
  }
  current = ds_fenwick_tree_at(tree, index);
  ds_fenwick_tree_update(tree, index, value - current);
}

long ds_fenwick_tree_prefix_sum(ds_fenwick_tree_t *tree, size_t index) {
  long sum = 0;
  size_t i;
  if (tree->size == 0) {
    return 0;
  }
  i = index + 1 <= tree->size ? index + 1 : tree->size;
  while (i > 0) {
    sum += tree->data[i];
    i -= i & (~i + 1);
  }
  return sum;
}

long ds_fenwick_tree_range_sum(ds_fenwick_tree_t *tree, size_t left,
                               size_t right) {
  long result;
  if (left > right || left >= tree->size) {
    return 0;
  }
  if (left == 0) {
    result = ds_fenwick_tree_prefix_sum(tree, right);
  } else {
    result = ds_fenwick_tree_prefix_sum(tree, right) -
             ds_fenwick_tree_prefix_sum(tree, left - 1);
  }
  return result;
}

long ds_fenwick_tree_at(ds_fenwick_tree_t *tree, size_t index) {
  long result;
  if (index >= tree->size) {
    return 0;
  }
  if (index == 0) {
    result = ds_fenwick_tree_prefix_sum(tree, 0);
  } else {
    result = ds_fenwick_tree_prefix_sum(tree, index) -
             ds_fenwick_tree_prefix_sum(tree, index - 1);
  }
  return result;
}

size_t ds_fenwick_tree_size(ds_fenwick_tree_t *tree) {
  size_t result;
  result = tree->size;
  return result;
}

void ds_fenwick_tree_clear(ds_fenwick_tree_t *tree) {
  size_t i;
  for (i = 0; i <= tree->size; i++) {
    tree->data[i] = 0;
  }
}

ds_fenwick_tree_t *ds_fenwick_tree_clone(ds_fenwick_tree_t *tree) {
  ds_fenwick_tree_t *new_tree;
  size_t i;
  new_tree = ds_fenwick_tree_create_alloc(tree->size, tree->allocator,
                                          tree->deallocator);
  for (i = 0; i <= tree->size; i++) {
    new_tree->data[i] = tree->data[i];
  }
  return new_tree;
}

void ds_fenwick_tree_destroy(ds_fenwick_tree_t *tree) {
  tree->deallocator(tree->data);
  tree->deallocator(tree);
}
