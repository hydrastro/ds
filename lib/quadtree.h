#ifndef DS_QUADTREE_H
#define DS_QUADTREE_H

/*
 * Point access methods by regular decomposition.
 *
 * ds_regtree_t   a region tree over a box in d dimensions (1 <= d <= 8)
 *                that splits a full bucket into equal cells:
 *                  DS_REGTREE_ALL_AXES  2^d children per split: the PR
 *                    (point-region) quadtree for d = 2 (a quad trie: the
 *                    regular decomposition of the plane), the octree for
 *                    d = 3
 *                  DS_REGTREE_BINTREE   2 children per split, cycling
 *                    through the axes (the bintree / k-d trie)
 *                Points are d doubles plus a size_t value; buckets hold up
 *                to b points; insert, remove (collapsing cells that fit in
 *                one bucket again), box queries, nearest neighbours
 *                (best-first), counts.
 * ds_point_quadtree_t  the point quadtree of Finkel and Bentley: every
 *                node is a data point splitting the plane into four
 *                quadrants; insert, exact search, removal (the removed
 *                node's subtree is reinserted), box queries, nearest
 *                neighbour.
 * ds_linear_quadtree_t  a linear quadtree: points with 16-bit integer
 *                coordinates kept as a sorted array of Morton (Z-order)
 *                codes; box queries decompose the box into quadrant cells,
 *                i.e. code intervals found by binary search.
 *
 * Iterative traversals with explicit stacks; memory from the
 * ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_REGTREE_MAX_DIM 8

typedef enum ds_regtree_mode {
  DS_REGTREE_ALL_AXES = 0,
  DS_REGTREE_BINTREE
} ds_regtree_mode_t;

typedef bool (*ds_spatial_visit_func_t)(const double *point, size_t value,
                                        void *user);

typedef struct ds_regtree {
  size_t dim;
  ds_regtree_mode_t mode;
  size_t fanout;
  size_t bucket;    /* points per leaf before a split */
  size_t max_depth; /* leaves at this depth just grow */
  double lo[DS_REGTREE_MAX_DIM];
  double hi[DS_REGTREE_MAX_DIM];
  /* nodes */
  size_t *child; /* first child (children contiguous) or ~0 for a leaf */
  size_t *parent;
  size_t *count; /* points in the subtree */
  size_t *head;  /* leaf: first point (linked through pnext) */
  unsigned char *depth;
  size_t nodes;
  size_t node_capacity;
  size_t node_free; /* free blocks of `fanout` nodes */
  /* points */
  double *coord;
  size_t *value;
  size_t *pnext;
  size_t points;
  size_t point_capacity;
  size_t point_free;
  size_t size;
  size_t *stack;
  size_t stack_capacity;
  ds_context_t *context;
} ds_regtree_t;

/* lo/hi: the box covered (points outside are rejected). */
ds_status_t ds_regtree_init(ds_regtree_t *t, size_t dim, ds_regtree_mode_t mode,
                            const double *lo, const double *hi, size_t bucket,
                            size_t max_depth, ds_context_t *context);
void ds_regtree_destroy(ds_regtree_t *t);
size_t ds_regtree_size(const ds_regtree_t *t);
ds_status_t ds_regtree_insert(ds_regtree_t *t, const double *point,
                              size_t value);
/* Removes one point equal to point with this value. */
ds_status_t ds_regtree_remove(ds_regtree_t *t, const double *point,
                              size_t value);
/* Points inside [qlo, qhi] (inclusive). */
ds_status_t ds_regtree_query(ds_regtree_t *t, const double *qlo,
                             const double *qhi, ds_spatial_visit_func_t visit,
                             void *user);
/* The k points nearest to q (Euclidean), closest first. out_points: k x
 * dim, out_values: k; *out_found <= k. */
ds_status_t ds_regtree_nearest(ds_regtree_t *t, const double *q, size_t k,
                               double *out_points, size_t *out_values,
                               size_t *out_found);
size_t ds_regtree_count(ds_regtree_t *t, const double *qlo, const double *qhi);
size_t ds_regtree_leaves(const ds_regtree_t *t);

typedef struct ds_point_quadtree {
  double *x;
  double *y;
  size_t *value;
  size_t *child; /* 4 per node: NE, NW, SW, SE */
  size_t root;
  size_t nodes;
  size_t capacity;
  size_t free_list;
  size_t size;
  size_t *stack;
  size_t stack_capacity;
  ds_context_t *context;
} ds_point_quadtree_t;

ds_status_t ds_point_quadtree_init(ds_point_quadtree_t *t,
                                   ds_context_t *context);
void ds_point_quadtree_destroy(ds_point_quadtree_t *t);
size_t ds_point_quadtree_size(const ds_point_quadtree_t *t);
/* Duplicate coordinates are kept (they go to the NE quadrant). */
ds_status_t ds_point_quadtree_insert(ds_point_quadtree_t *t, double x, double y,
                                     size_t value);
ds_status_t ds_point_quadtree_find(ds_point_quadtree_t *t, double x, double y,
                                   size_t *out_value);
ds_status_t ds_point_quadtree_remove(ds_point_quadtree_t *t, double x, double y,
                                     size_t value);
ds_status_t ds_point_quadtree_query(ds_point_quadtree_t *t, double xlo,
                                    double ylo, double xhi, double yhi,
                                    ds_spatial_visit_func_t visit, void *user);
ds_status_t ds_point_quadtree_nearest(ds_point_quadtree_t *t, double x,
                                      double y, double *out_x, double *out_y,
                                      size_t *out_value);

typedef struct ds_linear_quadtree {
  unsigned long *code; /* sorted Morton codes */
  size_t *value;
  size_t size;
  size_t capacity;
  ds_context_t *context;
} ds_linear_quadtree_t;

ds_status_t ds_linear_quadtree_init(ds_linear_quadtree_t *t,
                                    ds_context_t *context);
void ds_linear_quadtree_destroy(ds_linear_quadtree_t *t);
size_t ds_linear_quadtree_size(const ds_linear_quadtree_t *t);
/* Coordinates below 65536. */
ds_status_t ds_linear_quadtree_insert(ds_linear_quadtree_t *t, unsigned x,
                                      unsigned y, size_t value);
ds_status_t ds_linear_quadtree_remove(ds_linear_quadtree_t *t, unsigned x,
                                      unsigned y, size_t value);
/* Points with xlo <= x <= xhi and ylo <= y <= yhi, in Z order. */
ds_status_t ds_linear_quadtree_query(const ds_linear_quadtree_t *t,
                                     unsigned xlo, unsigned ylo, unsigned xhi,
                                     unsigned yhi,
                                     bool (*visit)(unsigned x, unsigned y,
                                                   size_t value, void *user),
                                     void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_QUADTREE_H */
