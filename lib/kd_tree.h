#ifndef DS_KD_TREE_H
#define DS_KD_TREE_H

/*
 * k-d tree over points in `dims` dimensions with double coordinates.
 *
 * The tree is implicit and static: build() copies the points and reorders
 * them so that every subtree's median (on the axis depth % dims) sits at the
 * middle of its range. There are no node structures or pointers, only the
 * point array and the original indices, so the tree is compact, cache
 * friendly and can be built into caller memory. Rebuild to change the set.
 *
 * Queries report points by their index in the array passed to build():
 *   nearest   the closest point (squared Euclidean distance)
 *   knn       the k closest, sorted by distance, into caller arrays
 *   box       every point inside an axis-aligned box (inclusive)
 *   radius    every point within a distance of a centre
 *
 * Storage: dynamic (ds_kd_tree_init) or caller arrays of count * dims
 * doubles and count size_t (ds_kd_tree_init_buffer).
 *
 * Complexity: build O(n log n) expected; nearest O(log n) expected on
 * well-spread data; box/radius O(sqrt(n) + matches) in 2D.
 * Concurrency: queries do not modify the tree and may run in parallel.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*ds_kd_visit_func_t)(size_t index, const double *point,
                                   void *user);

typedef struct ds_kd_tree {
  double *points;
  size_t *ids;
  size_t count;
  size_t dims;
  ds_context_t *context;
  bool fixed;
} ds_kd_tree_t;

ds_status_t ds_kd_tree_init(ds_kd_tree_t *tree, const double *points,
                            size_t count, size_t dims, ds_context_t *context);
ds_status_t ds_kd_tree_init_buffer(ds_kd_tree_t *tree, const double *points,
                                   size_t count, size_t dims,
                                   double *point_buffer, size_t *id_buffer);
void ds_kd_tree_destroy(ds_kd_tree_t *tree);

ds_status_t ds_kd_tree_nearest(const ds_kd_tree_t *tree, const double *query,
                               size_t *out_index, double *out_dist2);
size_t ds_kd_tree_knn(const ds_kd_tree_t *tree, const double *query, size_t k,
                      size_t *out_indices, double *out_dist2);
ds_status_t ds_kd_tree_box(const ds_kd_tree_t *tree, const double *low,
                           const double *high, ds_kd_visit_func_t visit,
                           void *user);
ds_status_t ds_kd_tree_radius(const ds_kd_tree_t *tree, const double *centre,
                              double radius, ds_kd_visit_func_t visit,
                              void *user);
size_t ds_kd_tree_size(const ds_kd_tree_t *tree);

#ifdef __cplusplus
}
#endif

#endif /* DS_KD_TREE_H */
