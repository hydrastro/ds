#ifndef DS_FENWICK_TREE_H
#define DS_FENWICK_TREE_H

#include "common.h"
#include "context.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fenwick_tree {
  long *data;
  size_t size;
  ds_context_t *context; /* NULL for legacy allocator constructors */
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_fenwick_tree_t;

/* NULL when memory runs out, for a size too large, or without an
 * allocator and a deallocator (create_alloc and clone too) */
ds_fenwick_tree_t *ds_fenwick_tree_create(size_t size);
ds_fenwick_tree_t *ds_fenwick_tree_create_alloc(size_t size,
                                                void *(*allocator)(size_t),
                                                void (*deallocator)(void *));
void ds_fenwick_tree_update(ds_fenwick_tree_t *tree, size_t index, long delta);
/* Checked operations use half-open ranges. prefix_ex(count) sums [0,count);
 * range_ex(left,right) sums [left,right). Empty ranges give zero. Queries
 * cancel opposite-signed terms before checking overflow, so representable
 * results do not fail merely because a particular summation order overflows.
 * Updates stage affected sums before writing, in O(log n) time and bounded
 * word-depth scratch; allocation is not needed. Errors preserve tree/output.
 * The legacy arithmetic operations require all their intermediates to fit long.
 */
ds_status_t ds_fenwick_tree_update_ex(ds_fenwick_tree_t *tree, size_t index,
                                      long delta);
ds_status_t ds_fenwick_tree_set_ex(ds_fenwick_tree_t *tree, size_t index,
                                   long value);
ds_status_t ds_fenwick_tree_prefix_ex(const ds_fenwick_tree_t *tree,
                                      size_t count, long *out);
ds_status_t ds_fenwick_tree_range_ex(const ds_fenwick_tree_t *tree, size_t left,
                                     size_t right, long *out);
ds_status_t ds_fenwick_tree_at_ex(const ds_fenwick_tree_t *tree, size_t index,
                                  long *out);
void ds_fenwick_tree_set(ds_fenwick_tree_t *tree, size_t index, long value);
long ds_fenwick_tree_prefix_sum(ds_fenwick_tree_t *tree, size_t index);
long ds_fenwick_tree_range_sum(ds_fenwick_tree_t *tree, size_t left,
                               size_t right);
long ds_fenwick_tree_at(ds_fenwick_tree_t *tree, size_t index);
size_t ds_fenwick_tree_size(ds_fenwick_tree_t *tree);
void ds_fenwick_tree_clear(ds_fenwick_tree_t *tree);
ds_fenwick_tree_t *ds_fenwick_tree_clone(ds_fenwick_tree_t *tree);
void ds_fenwick_tree_destroy(ds_fenwick_tree_t *tree);

/* Context-owned internal storage; NULL context chooses the default.
 * The context must outlive the container. On failure *out is NULL. */
ds_status_t ds_fenwick_tree_create_context(size_t size, ds_context_t *context,
                                           ds_fenwick_tree_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_FENWICK_TREE_H */
