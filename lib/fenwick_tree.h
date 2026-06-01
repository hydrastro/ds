#ifndef DS_FENWICK_TREE_H
#define DS_FENWICK_TREE_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fenwick_tree {
  long *data;
  size_t size;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} ds_fenwick_tree_t;

ds_fenwick_tree_t *FUNC(ds_fenwick_tree_create)(size_t size);
ds_fenwick_tree_t *
    FUNC(ds_fenwick_tree_create_alloc)(size_t size, void *(*allocator)(size_t),
                                       void (*deallocator)(void *));
void FUNC(ds_fenwick_tree_update)(ds_fenwick_tree_t *tree, size_t index,
                                  long delta);
void FUNC(ds_fenwick_tree_set)(ds_fenwick_tree_t *tree, size_t index,
                               long value);
long FUNC(ds_fenwick_tree_prefix_sum)(ds_fenwick_tree_t *tree, size_t index);
long FUNC(ds_fenwick_tree_range_sum)(ds_fenwick_tree_t *tree, size_t left,
                                     size_t right);
long FUNC(ds_fenwick_tree_at)(ds_fenwick_tree_t *tree, size_t index);
size_t FUNC(ds_fenwick_tree_size)(ds_fenwick_tree_t *tree);
void FUNC(ds_fenwick_tree_clear)(ds_fenwick_tree_t *tree);
ds_fenwick_tree_t *FUNC(ds_fenwick_tree_clone)(ds_fenwick_tree_t *tree);
void FUNC(ds_fenwick_tree_destroy)(ds_fenwick_tree_t *tree);

#ifdef __cplusplus
}
#endif

#endif /* DS_FENWICK_TREE_H */
