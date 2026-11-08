#ifndef DS_TOPOLOGY_TREE_H
#define DS_TOPOLOGY_TREE_H

/*
 * Topology trees (Frederickson 1985) over a dynamic forest of maximum
 * degree 3.
 *
 * Each tree of the forest is clustered hierarchically: level 0 has one
 * cluster per vertex; a level l + 1 cluster is one level l cluster or the
 * union of two adjacent ones whose external degrees sum to at most 4 (a
 * degree-3 cluster joins only a degree-1 one), and the partition is
 * maximal: no two adjacent single-child clusters could have been
 * combined. A cluster with no external edge is the root of its tree's
 * topology tree, whose height is O(log n).
 *
 * link and cut delete the ancestor chains of the clusters whose degree
 * changed, which orphans O(1) clusters per level, and re-cluster the
 * orphans level by level, pairing each with a free neighbour or by
 * absorbing an adjacent single-child cluster, so the restricted partition
 * and its maximality are restored locally; neighbours are found by
 * climbing from the far endpoint of an external edge, O(log^2 n) per
 * update. Clusters carry their size and an aggregate of vertex values
 * under a user monoid (values by value, combine callback with a user
 * pointer), giving connectivity, component size and component aggregate
 * queries in O(log n), and point updates of vertex values in O(log n).
 *
 * Memory comes from the ds_context_t given at init.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_TOPOLOGY_NONE (~(size_t)0)

typedef void (*ds_topology_combine_fn)(void *out, const void *a, const void *b,
                                       void *user);

typedef struct ds_topology_cluster {
  size_t level;
  size_t parent;
  size_t child[2]; /* child[1] == NONE for one child; level 0: vertex */
  size_t edges;    /* external edges */
  size_t inner[3]; /* external edge endpoints inside the cluster */
  size_t outer[3]; /* and outside */
  size_t size;     /* vertices */
  bool alive;
} ds_topology_cluster_t;

typedef struct ds_topology_tree {
  size_t vertices;
  size_t *adj; /* vertices * 3 */
  unsigned char *degree;
  ds_topology_cluster_t *clusters; /* 0 .. vertices-1: level-0 clusters */
  unsigned char *values;           /* per cluster, value_size bytes */
  size_t count;
  size_t capacity;
  size_t free_list;
  size_t value_size;
  ds_topology_combine_fn combine;
  void *user;
  /* pending clusters per level (re-clustering work lists) */
  size_t **pending;
  size_t *pending_count;
  size_t *pending_capacity;
  size_t levels;
  ds_context_t *context;
} ds_topology_tree_t;

/* values (optional) holds vertices * value_size bytes of initial vertex
 * values; value_size may be 0 (then combine may be NULL) */
ds_status_t ds_topology_tree_init(ds_topology_tree_t *t, size_t vertices,
                                  size_t value_size, const void *values,
                                  ds_topology_combine_fn combine, void *user,
                                  ds_context_t *context);
void ds_topology_tree_destroy(ds_topology_tree_t *t);
/* DS_ERR_STATE if u and v are connected or a degree would exceed 3 */
ds_status_t ds_topology_tree_link(ds_topology_tree_t *t, size_t u, size_t v);
/* DS_ERR_STATE if there is no edge u-v */
ds_status_t ds_topology_tree_cut(ds_topology_tree_t *t, size_t u, size_t v);
bool ds_topology_tree_connected(const ds_topology_tree_t *t, size_t u,
                                size_t v);
size_t ds_topology_tree_component_size(const ds_topology_tree_t *t, size_t v);
/* aggregate of the values of v's component */
ds_status_t ds_topology_tree_component_value(const ds_topology_tree_t *t,
                                             size_t v, void *out);
ds_status_t ds_topology_tree_set_value(ds_topology_tree_t *t, size_t v,
                                       const void *value);
/* the root cluster of v's topology tree, and its level (the height) */
size_t ds_topology_tree_root(const ds_topology_tree_t *t, size_t v);
size_t ds_topology_tree_height(const ds_topology_tree_t *t, size_t v);
/* v's ancestor cluster at a level (NONE above the root) */
size_t ds_topology_tree_cluster(const ds_topology_tree_t *t, size_t v,
                                size_t level);

#ifdef __cplusplus
}
#endif

#endif /* DS_TOPOLOGY_TREE_H */
