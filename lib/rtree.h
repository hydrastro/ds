#ifndef DS_RTREE_H
#define DS_RTREE_H

/*
 * Spatial access methods for boxes (axis-aligned rectangles in d <= 8
 * dimensions) with a size_t value each.
 *
 * ds_rtree_t, a dynamic R-tree whose policies are selectable:
 *   DS_RTREE_GUTTMAN  Guttman's R-tree: least-enlargement subtree choice,
 *                     quadratic split, deletion with condensing and
 *                     reinsertion of underfull nodes
 *   DS_RTREE_RSTAR    the R*-tree of Beckmann et al.: overlap-minimising
 *                     choice at the leaf level, forced reinsertion of the
 *                     30% farthest entries on the first overflow per level,
 *                     and the margin / overlap driven split
 * Window queries (all boxes intersecting a box), point queries, k nearest
 * neighbours by box distance, removal of an exact (box, value) entry.
 *
 * ds_rplus_t, the R+-tree (Sellis et al.), built in bulk: sibling regions
 * never overlap (they are half-open boxes partitioning their parent, the
 * root region being all of space), objects crossing a partition are stored
 * in every leaf they meet, and queries report each object once (in the
 * leaf holding the low corner of its intersection with the query).
 *
 * Memory comes from the ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_RTREE_MAX_DIM 8

typedef enum ds_rtree_variant {
  DS_RTREE_GUTTMAN = 0,
  DS_RTREE_RSTAR
} ds_rtree_variant_t;

/* box: lo[0..d), hi[0..d) as 2d doubles. */
typedef bool (*ds_rtree_visit_func_t)(const double *box, size_t value,
                                      void *user);

typedef struct ds_rtree {
  size_t dim;
  size_t max_entries;
  size_t min_entries;
  ds_rtree_variant_t variant;
  /* nodes: count, leaf flag, level; entries max_entries + 1 per node */
  size_t *count;
  unsigned char *leaf;
  size_t *level;
  size_t *parent;
  double *box; /* per entry: 2 * dim */
  size_t *ref; /* per entry: child node or value */
  size_t nodes;
  size_t capacity;
  size_t free_list;
  size_t root;
  size_t size;
  size_t height;
  unsigned char *reinserted; /* R*: per level, during one insertion */
  size_t *stack;
  size_t stack_capacity;
  ds_context_t *context;
} ds_rtree_t;

/* max_entries >= 4 (0: 8). */
ds_status_t ds_rtree_init(ds_rtree_t *t, size_t dim, size_t max_entries,
                          ds_rtree_variant_t variant, ds_context_t *context);
void ds_rtree_destroy(ds_rtree_t *t);
size_t ds_rtree_size(const ds_rtree_t *t);
size_t ds_rtree_height(const ds_rtree_t *t);
ds_status_t ds_rtree_insert(ds_rtree_t *t, const double *box, size_t value);
ds_status_t ds_rtree_remove(ds_rtree_t *t, const double *box, size_t value);
ds_status_t ds_rtree_query(ds_rtree_t *t, const double *box,
                           ds_rtree_visit_func_t visit, void *user);
/* Boxes containing the point. */
ds_status_t ds_rtree_query_point(ds_rtree_t *t, const double *point,
                                 ds_rtree_visit_func_t visit, void *user);
/* The k entries nearest to the point (distance to the box, 0 inside),
 * closest first. */
ds_status_t ds_rtree_nearest(ds_rtree_t *t, const double *point, size_t k,
                             size_t *out_values, double *out_dist,
                             size_t *out_found);
/* Parent boxes cover their children, levels are even, fill is legal. */
bool ds_rtree_valid(const ds_rtree_t *t);

typedef struct ds_rplus {
  size_t dim;
  size_t nodes;
  double *region; /* per node: 2 * dim */
  size_t *first;  /* internal: first child; leaf: first entry */
  size_t *count;  /* children or entries */
  unsigned char *leaf;
  double *ebox; /* entries (with duplicates): 2 * dim */
  size_t *evalue;
  size_t entries;
  size_t objects;
  ds_context_t *context;
} ds_rplus_t;

/* boxes: n x 2d; values may be NULL (the value is then the box index);
 * leaf_capacity >= 2 (0: 8) is also the fan-out. Leaves of objects that no
 * cut separates may exceed it. */
ds_status_t ds_rplus_build(ds_rplus_t *t, size_t dim, const double *boxes,
                           const size_t *values, size_t n, size_t leaf_capacity,
                           ds_context_t *context);
void ds_rplus_destroy(ds_rplus_t *t);
/* Each intersecting object once. */
ds_status_t ds_rplus_query(const ds_rplus_t *t, const double *box,
                           ds_rtree_visit_func_t visit, void *user);
/* Stored entries including duplicates (>= objects). */
size_t ds_rplus_entries(const ds_rplus_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_RTREE_H */
