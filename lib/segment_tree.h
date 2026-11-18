#ifndef DS_SEGMENT_TREE_H
#define DS_SEGMENT_TREE_H

#include "common.h"
#include "context.h"
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
  long *tree;    /* aggregate per node */
  long *minimum; /* per-element extrema for checked range additions */
  long *maximum;
  long *lazy_add;  /* pending add per node */
  long *lazy_set;  /* pending assign value per node */
  bool *has_set;   /* whether a pending assign exists */
  size_t n;        /* logical array length */
  size_t capacity; /* number of tree nodes (4n) */
  ds_segment_tree_op_t op;
  ds_context_t *context; /* NULL for legacy allocators */
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_segment_tree_t;

/* NULL when memory runs out, for n too large to size the tree, or without
 * an allocator and a deallocator */
ds_segment_tree_t *ds_segment_tree_create(const long *values, size_t n,
                                          ds_segment_tree_op_t op);
ds_segment_tree_t *ds_segment_tree_create_alloc(const long *values, size_t n,
                                                ds_segment_tree_op_t op,
                                                void *(*allocator)(size_t),
                                                void (*deallocator)(void *));
/* add delta to every element in [left, right] */
void ds_segment_tree_update_add(ds_segment_tree_t *st, size_t left,
                                size_t right, long delta);
/* set every element in [left, right] to value */
void ds_segment_tree_update_assign(ds_segment_tree_t *st, size_t left,
                                   size_t right, long value);
/* aggregate query over [left, right] */
long ds_segment_tree_query(ds_segment_tree_t *st, size_t left, size_t right);
size_t ds_segment_tree_size(ds_segment_tree_t *st);
void ds_segment_tree_destroy(ds_segment_tree_t *st);

/* Checked operations use inclusive [left,right]. Creation rejects invalid op,
 * oversized storage and unrepresentable node sums; out is NULL on failure.
 * NULL context selects the default; it must outlive the tree.
 * Updates journal O(log n) touched nodes, with a small local buffer and
 * optional context scratch. Any size/allocation/arithmetic error restores every
 * field. Per-element extrema reject overflowing additions for SUM/MIN/MAX
 * alike. Queries restore lazy caches, including on success, and preserve out on
 * error. All materialized node sums and lazy tags must fit long: an operation
 * may report overflow even if its final requested aggregate fits after
 * cancellation. A deferred child sum can overflow when a later operation
 * materializes it. Legacy modifiers ignore status (failure leaves the tree
 * unchanged); legacy query returns the aggregate identity on error. Public
 * layouts have changed. */
ds_status_t ds_segment_tree_create_context(const long *values, size_t n,
                                           ds_segment_tree_op_t op,
                                           ds_context_t *context,
                                           ds_segment_tree_t **out);
ds_status_t ds_segment_tree_update_add_ex(ds_segment_tree_t *st, size_t left,
                                          size_t right, long delta);
ds_status_t ds_segment_tree_update_assign_ex(ds_segment_tree_t *st, size_t left,
                                             size_t right, long value);
ds_status_t ds_segment_tree_query_ex(ds_segment_tree_t *st, size_t left,
                                     size_t right, long *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_SEGMENT_TREE_H */
