#ifndef DS_ALGO_GRAPH_VIEW_H
#define DS_ALGO_GRAPH_VIEW_H

/*
 * Graph algorithms over any graph representation.
 *
 * Algorithms read the graph only through ds_gview_t: a vertex count,
 * the out-degree of a vertex, and its i-th outgoing edge (target and
 * weight). Adapters can wrap an adjacency matrix (ds_gview_from_adj_matrix()
 * for lib/adj_matrix), ds's graph module, a CSR array set
 * (ds_gview_from_csr()), or an implicit graph whose
 * edges are computed on demand (grids, state spaces). Vertices are
 * 0 .. vertex_count - 1.
 *
 * Output arrays have vertex_count entries and are supplied by the caller;
 * working memory comes from the ds_context_t (NULL for the default).
 * DS_GVIEW_NONE marks "no vertex" (unreachable, no parent).
 *
 * Undirected algorithms (bipartite check, Prim) expect a symmetric view:
 * every edge present in both directions.
 */

#include "../lib/adj_matrix.h"
#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_GVIEW_NONE (~(size_t)0)

typedef struct ds_gview {
  size_t vertex_count;
  size_t (*degree)(const void *graph, size_t vertex);
  void (*edge)(const void *graph, size_t vertex, size_t index,
               size_t *out_target, double *out_weight);
  const void *graph;
} ds_gview_t;

/* Compressed sparse row: edges of v are targets[offsets[v] .. offsets[v+1]).
 * weights may be NULL (every weight is 1). */
typedef struct ds_gview_csr {
  size_t vertex_count;
  const size_t *offsets;
  const size_t *targets;
  const double *weights;
} ds_gview_csr_t;

void ds_gview_from_csr(const ds_gview_csr_t *csr, ds_gview_t *out_view);
/* a view of a lib/adj_matrix graph: each row's edges in O(1) apiece; the
 * matrix must outlive the view and not change while an algorithm runs */
void ds_gview_from_adj_matrix(const ds_adj_matrix_t *matrix,
                              ds_gview_t *out_view);

ds_status_t ds_gview_bfs(const ds_gview_t *g, size_t source, size_t *out_hops,
                         size_t *out_parent, ds_context_t *context);
ds_status_t ds_gview_dijkstra(const ds_gview_t *g, size_t source,
                              double *out_dist, size_t *out_parent,
                              ds_context_t *context);
ds_status_t ds_gview_bellman_ford(const ds_gview_t *g, size_t source,
                                  double *out_dist, size_t *out_parent,
                                  bool *out_negative_cycle);
ds_status_t ds_gview_topological_sort(const ds_gview_t *g, size_t *out_order,
                                      ds_context_t *context);
ds_status_t ds_gview_scc(const ds_gview_t *g, size_t *out_component,
                         size_t *out_count, ds_context_t *context);
ds_status_t ds_gview_bipartite(const ds_gview_t *g, unsigned char *out_side,
                               bool *out_bipartite, ds_context_t *context);
ds_status_t ds_gview_prim(const ds_gview_t *g, size_t *out_parent,
                          double *out_total, ds_context_t *context);
void ds_gview_floyd_warshall(double *dist, size_t n, size_t *out_next);
size_t ds_gview_path(const size_t *parent, size_t target, size_t *out_path,
                     size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_GRAPH_VIEW_H */
