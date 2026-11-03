#ifndef DS_ALGO_GRAPH_STRUCT_H
#define DS_ALGO_GRAPH_STRUCT_H

/*
 * Structural graph algorithms over ds_gview_t (see graph_view.h):
 *
 *   ds_gview_dfs_forest()         discovery/finish times, parents and an
 *                                 optional edge classification callback
 *                                 (tree, back, forward, cross)
 *   ds_gview_articulation()       articulation points (cut vertices) and
 *                                 bridges of an undirected graph (Tarjan);
 *                                 parallel edges are handled
 *   ds_gview_biconnected()        biconnected components as edge sets
 *                                 (Hopcroft-Tarjan edge stack)
 *   ds_gview_euler_directed()     Eulerian path or cycle (Hierholzer)
 *   ds_euler_undirected()         the same for an undirected edge list
 *   ds_gview_condensation()       reduced digraph: the DAG of strongly
 *                                 connected components
 *   ds_gview_transitive_closure() reachability matrix
 *   ds_gview_transitive_reduction() minimal edge subset of a DAG with the
 *                                 same reachability
 *   ds_feedback_edge_set()        edges outside a spanning forest of an
 *                                 undirected edge list (removing them
 *                                 leaves a forest)
 *   ds_gview_tree_diameter()      longest path of a weighted tree
 *   ds_gview_tree_centroid()      vertices whose removal leaves components
 *                                 of at most n / 2 vertices
 *
 * Undirected graphs are symmetric views (every edge in both directions).
 * Edges of a view are numbered globally in vertex order:
 * ds_gview_edge_offsets() gives the first global index of each vertex.
 * Everything is iterative; working memory comes from the ds_context_t.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include "graph_view.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_gview_edge_class {
  DS_GVIEW_TREE_EDGE = 0,
  DS_GVIEW_BACK_EDGE = 1,
  DS_GVIEW_FORWARD_EDGE = 2,
  DS_GVIEW_CROSS_EDGE = 3
} ds_gview_edge_class_t;

typedef void (*ds_gview_edge_class_func_t)(size_t from, size_t edge_index,
                                           size_t to,
                                           ds_gview_edge_class_t kind,
                                           void *user);
/* Called for each edge of a biconnected component or a bridge. */
typedef void (*ds_gview_component_edge_func_t)(size_t component, size_t u,
                                               size_t v, void *user);

/* out_offsets: vertex_count + 1 entries; returns the edge count. */
size_t ds_gview_edge_offsets(const ds_gview_t *g, size_t *out_offsets);

/* Any output may be NULL. Times run 0 .. 2n - 1. */
ds_status_t ds_gview_dfs_forest(const ds_gview_t *g, size_t *out_discovery,
                                size_t *out_finish, size_t *out_parent,
                                ds_gview_edge_class_func_t classify, void *user,
                                ds_context_t *context);

/* out_is_cut: vertex_count flags (may be NULL); bridge called once per
 * bridge (may be NULL); bridge count in *out_bridges (may be NULL). */
ds_status_t ds_gview_articulation(const ds_gview_t *g,
                                  unsigned char *out_is_cut,
                                  ds_gview_component_edge_func_t bridge,
                                  void *user, size_t *out_bridges,
                                  ds_context_t *context);
/* Each undirected edge is reported once with its component id. */
ds_status_t ds_gview_biconnected(const ds_gview_t *g,
                                 ds_gview_component_edge_func_t visit,
                                 void *user, size_t *out_count,
                                 ds_context_t *context);

/* Eulerian trail of a directed view using every edge once: out_path gets
 * edge_count + 1 vertices. DS_NOT_FOUND if none exists. start may be
 * DS_GVIEW_NONE to pick one. */
ds_status_t ds_gview_euler_directed(const ds_gview_t *g, size_t start,
                                    size_t *out_path, size_t *out_length,
                                    ds_context_t *context);
/* Undirected multigraph given as m edges (u[i], v[i]) on n vertices. */
ds_status_t ds_euler_undirected(size_t n, const size_t *u, const size_t *v,
                                size_t m, size_t start, size_t *out_path,
                                size_t *out_length, ds_context_t *context);

/* out_component: vertex -> component (topologically ordered: every DAG
 * edge goes from a lower to a higher component id). The DAG is written as
 * CSR: out_offsets (components + 1, sized vertex_count + 1) and
 * out_targets (capacity entries, distinct targets per component). */
ds_status_t ds_gview_condensation(const ds_gview_t *g, size_t *out_component,
                                  size_t *out_count, size_t *out_offsets,
                                  size_t *out_targets, size_t capacity,
                                  size_t *out_edges, ds_context_t *context);

/* out_reach: n x n row-major, out_reach[u * n + v] = 1 iff v is reachable
 * from u (every vertex reaches itself). */
ds_status_t ds_gview_transitive_closure(const ds_gview_t *g,
                                        unsigned char *out_reach,
                                        ds_context_t *context);
/* For a DAG: out_keep[global edge index] = 1 for the edges of the unique
 * transitive reduction (parallel copies of an edge keep only the first).
 * DS_ERR_INVALID if the graph has a cycle. */
ds_status_t ds_gview_transitive_reduction(const ds_gview_t *g,
                                          unsigned char *out_keep,
                                          ds_context_t *context);

/* out_in_set[i] = 1 for the edges outside a spanning forest. */
ds_status_t ds_feedback_edge_set(size_t n, const size_t *u, const size_t *v,
                                 size_t m, unsigned char *out_in_set,
                                 size_t *out_count, ds_context_t *context);

/* Weighted tree (symmetric view, weights >= 0): the diameter length and,
 * optionally, its endpoints. */
ds_status_t ds_gview_tree_diameter(const ds_gview_t *g, double *out_length,
                                   size_t *out_a, size_t *out_b,
                                   ds_context_t *context);
/* One or two centroids of a tree. */
ds_status_t ds_gview_tree_centroid(const ds_gview_t *g, size_t *out_centroids,
                                   size_t *out_count, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_GRAPH_STRUCT_H */
