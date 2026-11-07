#ifndef DS_ALGO_GRAPH_CONN_H
#define DS_ALGO_GRAPH_CONN_H

/*
 * More path and connectivity queries on graph views (algo/graph_view.h).
 *
 *   ds_greverse_build             the reverse of a directed view, as an
 *                                 owned CSR graph with its own view
 *   ds_gview_single_destination   shortest paths from every vertex to one
 *                                 target (Dijkstra on the reverse graph):
 *                                 distances and the next hop towards the
 *                                 target
 *   ds_gview_separation_pairs     every pair of vertices whose removal
 *                                 disconnects a biconnected undirected
 *                                 graph (vertex v plus each articulation
 *                                 point of the graph without v),
 *                                 O(n (n + m))
 *   ds_gview_is_triconnected      at least four vertices, connected, no
 *                                 articulation point and no separation
 *                                 pair: removing any two vertices leaves it
 *                                 connected
 *
 * Undirected graphs are views listing every edge in both directions.
 */

#include "graph_view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_greverse {
  ds_gview_csr_t csr;
  size_t *offsets;
  size_t *targets;
  double *weights;
  ds_gview_t view;
  ds_context_t *context;
} ds_greverse_t;

ds_status_t ds_greverse_build(ds_greverse_t *r, const ds_gview_t *g,
                              ds_context_t *context);
void ds_greverse_destroy(ds_greverse_t *r);

/* dist[v]: shortest distance from v to target (HUGE_VAL if none);
 * next_hop[v] (may be NULL): the vertex after v on such a path. */
ds_status_t ds_gview_single_destination(const ds_gview_t *g, size_t target,
                                        double *dist, size_t *next_hop,
                                        ds_context_t *context);

/* visit(u, v) with u < v for each separation pair; return false to stop. */
ds_status_t
ds_gview_separation_pairs(const ds_gview_t *g,
                          bool (*visit)(size_t u, size_t v, void *user),
                          void *user, size_t *out_count, ds_context_t *context);
ds_status_t ds_gview_is_triconnected(const ds_gview_t *g, bool *out_result,
                                     ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_GRAPH_CONN_H */
