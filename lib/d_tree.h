#ifndef DS_D_TREE_H
#define DS_D_TREE_H

/*
 * ds_d_tree_t, the D-tree of Xu, Zheng, Lee and Lee: a height-balanced
 * binary tree over a planar subdivision that divides the set of regions
 * along their common boundaries, for planar point queries ("which region
 * holds this point?").
 *
 * The regions are simple polygons (either orientation) that tile part of
 * the plane, sharing edges with identical vertices. Each node splits its
 * regions into two halves by the median centroid along the wider axis;
 * the boundary of the first half's union (its edges not shared inside
 * it) is stored, and a query point goes left when a ray cast against that
 * boundary says it lies inside. The polyline where the halves meet is the
 * node's division. Built in O(n log n) edge sorts per level; queries take
 * O(log n) node tests.
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

typedef struct ds_d_tree {
  size_t regions;
  size_t *value; /* per region */
  double **poly; /* per region: 2 * n */
  size_t *poly_n;
  /* nodes: internal (boundary edges of the left half) or leaf (region) */
  size_t *left; /* (size_t)-1 for a leaf */
  size_t *right;
  size_t *region; /* leaf: region index */
  double **edges; /* internal: 4 per edge */
  size_t *edge_count;
  size_t *division; /* internal: edges shared by the two halves */
  size_t nodes;
  size_t root;
  size_t height;
  ds_context_t *context;
} ds_d_tree_t;

/* Region i has vertices xy[2 * offsets[i] ...] up to offsets[i + 1]
 * (each >= 3); values may be NULL (the value is then i). */
ds_status_t ds_d_tree_build(ds_d_tree_t *t, const double *xy,
                            const size_t *offsets, const size_t *values,
                            size_t n, ds_context_t *context);
void ds_d_tree_destroy(ds_d_tree_t *t);
size_t ds_d_tree_height(const ds_d_tree_t *t);
/* The region holding (x, y); DS_NOT_FOUND outside the subdivision. Points
 * on a shared boundary go to one of the regions meeting there. */
ds_status_t ds_d_tree_locate(const ds_d_tree_t *t, double x, double y,
                             size_t *out_value);
/* Length (in edges) of the division at the root. */
size_t ds_d_tree_root_division(const ds_d_tree_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_D_TREE_H */
