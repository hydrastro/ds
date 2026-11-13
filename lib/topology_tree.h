#ifndef DS_TOPOLOGY_TREE_H
#define DS_TOPOLOGY_TREE_H

/*
 * Topology trees (Frederickson 1985) over a dynamic forest of maximum
 * degree 3.
 *
 * Each tree of the forest is clustered hierarchically: level 0 has one
 * cluster per vertex; a level l + 1 cluster is one level l cluster or the
 * union of two adjacent ones whose external degrees sum to at most 4 (a
 * degree-3 cluster joins only a degree-1 one, so every cluster of degree 3
 * is a single vertex), and the partition is maximal: no two adjacent
 * single-child clusters could have been combined. A cluster with no
 * external edge is the root of its tree's topology tree, whose height is
 * O(log n).
 *
 * Every cluster keeps, for each of its (at most three) external edges, the
 * cluster of its own level on the other side, so the neighbours of a
 * cluster are found in O(1). link and cut delete the clusters that contain
 * an endpoint and re-cluster the orphans level by level: an orphan pairs
 * with a free neighbour, or takes a neighbour out of a single-child cluster
 * (deleting that cluster's ancestors, all next to the deleted chains), or
 * stays alone. Only O(1) clusters per level are touched (Frederickson's
 * bound; ds_topology_tree_work reports the count), so an update takes
 * O(log n) worst-case time; the new clusters link to their neighbours
 * level by level, once the level below is complete.
 *
 * Clusters carry their size and two aggregates of vertex values under a
 * user combine (values by value, with a user pointer): the whole cluster,
 * and the path between its two boundary vertices when it has two external
 * edges (a degree-3 cluster's is its vertex's value). That gives component
 * sizes and aggregates, and the aggregate over the vertices of the path
 * between two vertices, each in O(log n); a vertex value changes in
 * O(log n).
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
  size_t nbr[3];   /* the cluster of this level across each external edge */
  size_t size;     /* vertices */
  bool alive;
  bool fresh; /* built by the running update, neighbours not linked yet */
} ds_topology_cluster_t;

typedef struct ds_topology_tree {
  size_t vertices;
  size_t *adj; /* vertices * 3 */
  unsigned char *degree;
  ds_topology_cluster_t *clusters; /* 0 .. vertices-1: level-0 clusters */
  unsigned char *values; /* per cluster, value_size bytes: the aggregate */
  unsigned char *spines; /* per cluster: the boundary-to-boundary path */
  size_t count;
  size_t capacity;
  size_t free_list;
  size_t dead; /* clusters deleted by the running update */
  size_t work; /* clusters built by the last update */
  size_t value_size;
  ds_topology_combine_fn combine;
  void *user;
  /* pending clusters per level (re-clustering work lists) */
  size_t **pending;
  size_t *pending_count;
  size_t *pending_capacity;
  size_t levels;
  int broken; /* an update failed after editing the adjacency */
  ds_context_t *context;
} ds_topology_tree_t;

/* values (optional) holds vertices * value_size bytes of initial vertex
 * values; value_size may be 0 (then combine may be NULL) */
ds_status_t ds_topology_tree_init(ds_topology_tree_t *t, size_t vertices,
                                  size_t value_size, const void *values,
                                  ds_topology_combine_fn combine, void *user,
                                  ds_context_t *context);
void ds_topology_tree_destroy(ds_topology_tree_t *t);
/*
 * Failure contract: link and cut reserve cluster slots and per-level work
 * lists for the bound on the re-clustering before touching the forest, so
 * an update that changed an edge never allocates. Should an allocation
 * still fail after the edge was changed, the edge change stands, the call
 * returns the error and the tree is marked broken: queries then answer as
 * for an absent vertex (connected false, sizes 0, DS_ERR_STATE) and
 * updates return DS_ERR_STATE until ds_topology_tree_rebuild succeeds.
 *
 * Aggregates combine values in an order that follows the clustering, which
 * changes with updates: use a commutative combine (or accept that the
 * order is unspecified).
 */
/* rebuild the whole hierarchy from the adjacency (clears `broken`) */
ds_status_t ds_topology_tree_rebuild(ds_topology_tree_t *t);
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
/* aggregate of the values on the path from u to v, both included:
 * DS_NOT_FOUND when they are not connected */
ds_status_t ds_topology_tree_path_value(const ds_topology_tree_t *t, size_t u,
                                        size_t v, void *out);
ds_status_t ds_topology_tree_set_value(ds_topology_tree_t *t, size_t v,
                                       const void *value);
/* the root cluster of v's topology tree, and its level (the height) */
size_t ds_topology_tree_root(const ds_topology_tree_t *t, size_t v);
size_t ds_topology_tree_height(const ds_topology_tree_t *t, size_t v);
/* v's ancestor cluster at a level (NONE above the root) */
size_t ds_topology_tree_cluster(const ds_topology_tree_t *t, size_t v,
                                size_t level);
/* clusters built by the last link or cut: O(log n), at most a constant
 * per level */
size_t ds_topology_tree_work(const ds_topology_tree_t *t);
/* every cluster's children, level, edges, neighbour links, size and
 * aggregates, the combination rules and maximality, in O(n log n) */
ds_status_t ds_topology_tree_check(const ds_topology_tree_t *t,
                                   bool *out_valid);

#ifdef __cplusplus
}
#endif

#endif /* DS_TOPOLOGY_TREE_H */
