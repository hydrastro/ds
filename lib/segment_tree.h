#ifndef DS_SEGMENT_TREE_H
#define DS_SEGMENT_TREE_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Segment tree over a fixed-length array of long values with lazy propagation,
 * supporting range updates and range queries in O(log n). The aggregation is
 * selected at creation time:
 *   DS_SEGMENT_TREE_SUM  - query returns the sum over the range
 *   DS_SEGMENT_TREE_MIN  - query returns the minimum over the range
 *   DS_SEGMENT_TREE_MAX  - query returns the maximum over the range
 * Two update kinds are supported per call: range-assign (set every element in
 * the range to a value) and range-add (add a delta to every element). Lazy tags
 * defer work to keep both updates and queries logarithmic.
 */

typedef enum {
  DS_SEGMENT_TREE_SUM,
  DS_SEGMENT_TREE_MIN,
  DS_SEGMENT_TREE_MAX
} ds_segment_tree_op_t;

typedef struct segment_tree {
  long *tree;      /* aggregate per node */
  long *lazy_add;  /* pending add per node */
  long *lazy_set;  /* pending assign value per node */
  bool *has_set;   /* whether a pending assign exists */
  size_t n;        /* logical array length */
  size_t capacity; /* number of tree nodes (4n) */
  ds_segment_tree_op_t op;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} ds_segment_tree_t;

ds_segment_tree_t *FUNC(ds_segment_tree_create)(const long *values, size_t n,
                                                ds_segment_tree_op_t op);
ds_segment_tree_t *FUNC(ds_segment_tree_create_alloc)(
    const long *values, size_t n, ds_segment_tree_op_t op,
    void *(*allocator)(size_t), void (*deallocator)(void *));
/* add delta to every element in [left, right] */
void FUNC(ds_segment_tree_update_add)(ds_segment_tree_t *st, size_t left,
                                      size_t right, long delta);
/* set every element in [left, right] to value */
void FUNC(ds_segment_tree_update_assign)(ds_segment_tree_t *st, size_t left,
                                         size_t right, long value);
/* aggregate query over [left, right] */
long FUNC(ds_segment_tree_query)(ds_segment_tree_t *st, size_t left,
                                 size_t right);
size_t FUNC(ds_segment_tree_size)(ds_segment_tree_t *st);
void FUNC(ds_segment_tree_destroy)(ds_segment_tree_t *st);

#ifdef __cplusplus
}
#endif

#endif /* DS_SEGMENT_TREE_H */
