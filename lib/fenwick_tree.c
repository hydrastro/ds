#include "fenwick_tree.h"
#include <stdlib.h>

ds_fenwick_tree_t *FUNC(ds_fenwick_tree_create)(size_t size) {
  return FUNC(ds_fenwick_tree_create_alloc)(size, malloc, free);
}

ds_fenwick_tree_t *
FUNC(ds_fenwick_tree_create_alloc)(size_t size, void *(*allocator)(size_t),
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
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(tree);
#endif
  return tree;
}

void FUNC(ds_fenwick_tree_update)(ds_fenwick_tree_t *tree, size_t index,
                                  long delta) {
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  if (index >= tree->size) {
#ifdef DS_THREAD_SAFE
    UNLOCK(tree);
#endif
    return;
  }
  i = index + 1;
  while (i <= tree->size) {
    tree->data[i] += delta;
    i += i & (~i + 1);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
}

void FUNC(ds_fenwick_tree_set)(ds_fenwick_tree_t *tree, size_t index,
                               long value) {
  long current;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  if (index >= tree->size) {
#ifdef DS_THREAD_SAFE
    UNLOCK(tree);
#endif
    return;
  }
  current = FUNC(ds_fenwick_tree_at)(tree, index);
  FUNC(ds_fenwick_tree_update)(tree, index, value - current);
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
}

long FUNC(ds_fenwick_tree_prefix_sum)(ds_fenwick_tree_t *tree, size_t index) {
  long sum = 0;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  if (tree->size == 0) {
#ifdef DS_THREAD_SAFE
    UNLOCK(tree);
#endif
    return 0;
  }
  i = index + 1 <= tree->size ? index + 1 : tree->size;
  while (i > 0) {
    sum += tree->data[i];
    i -= i & (~i + 1);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return sum;
}

long FUNC(ds_fenwick_tree_range_sum)(ds_fenwick_tree_t *tree, size_t left,
                                     size_t right) {
  long result;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  if (left > right || left >= tree->size) {
#ifdef DS_THREAD_SAFE
    UNLOCK(tree);
#endif
    return 0;
  }
  if (left == 0) {
    result = FUNC(ds_fenwick_tree_prefix_sum)(tree, right);
  } else {
    result = FUNC(ds_fenwick_tree_prefix_sum)(tree, right) -
             FUNC(ds_fenwick_tree_prefix_sum)(tree, left - 1);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return result;
}

long FUNC(ds_fenwick_tree_at)(ds_fenwick_tree_t *tree, size_t index) {
  long result;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  if (index >= tree->size) {
#ifdef DS_THREAD_SAFE
    UNLOCK(tree);
#endif
    return 0;
  }
  if (index == 0) {
    result = FUNC(ds_fenwick_tree_prefix_sum)(tree, 0);
  } else {
    result = FUNC(ds_fenwick_tree_prefix_sum)(tree, index) -
             FUNC(ds_fenwick_tree_prefix_sum)(tree, index - 1);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return result;
}

size_t FUNC(ds_fenwick_tree_size)(ds_fenwick_tree_t *tree) {
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

void FUNC(ds_fenwick_tree_clear)(ds_fenwick_tree_t *tree) {
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  for (i = 0; i <= tree->size; i++) {
    tree->data[i] = 0;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
}

ds_fenwick_tree_t *FUNC(ds_fenwick_tree_clone)(ds_fenwick_tree_t *tree) {
  ds_fenwick_tree_t *new_tree;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(tree);
#endif
  new_tree = FUNC(ds_fenwick_tree_create_alloc)(tree->size, tree->allocator,
                                                tree->deallocator);
  for (i = 0; i <= tree->size; i++) {
    new_tree->data[i] = tree->data[i];
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(tree);
#endif
  return new_tree;
}

void FUNC(ds_fenwick_tree_destroy)(ds_fenwick_tree_t *tree) {
  tree->deallocator(tree->data);
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(tree);
#endif
  tree->deallocator(tree);
}
