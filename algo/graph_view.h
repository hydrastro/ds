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
#include "../lib/pq_iface.h"
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

/* ---- priority-queue algorithms with a chosen queue and scratch ----------
 *
 * The *_with variants run Dijkstra, Prim and A* on any graph view with
 * the priority queue the caller picks from lib/pq_iface, so a CSR,
 * implicit or matrix graph can use any queue without first being copied
 * into an owning graph. Each vertex is pushed when first reached and
 * decreased through its handle afterwards: Dijkstra makes at most V pushes
 * and pops and E decreases, so it inherits the backend's costs
 * (ops->costs): O((V + E) log V) worst case on the leftist backend, and
 * O(E + V log V) amortized up to the o(log V) amortized decrease of the
 * pairing heap. Prim makes the same calls per component; A* may push a
 * vertex again when an inconsistent heuristic reopens it.
 *
 * A workspace holds the per-vertex handles, states and keys and can be
 * reused across calls (one call at a time) to avoid allocating them each
 * time; queue nodes come from the policy's context, which can be a pool.
 */

typedef struct ds_gview_workspace {
  size_t capacity; /* vertices */
  ds_pq_handle_t *handle;
  unsigned char *state;
  double *key;
  ds_context_t *context;
} ds_gview_workspace_t;

ds_status_t ds_gview_workspace_init(ds_gview_workspace_t *w, size_t vertices,
                                    ds_context_t *context);
/* grow to at least `vertices` (calls grow it automatically) */
ds_status_t ds_gview_workspace_reserve(ds_gview_workspace_t *w,
                                       size_t vertices);
void ds_gview_workspace_destroy(ds_gview_workspace_t *w);

typedef struct ds_gview_policy {
  const ds_pq_ops_t *pq;           /* NULL: ds_pq_binary_ops() */
  ds_gview_workspace_t *workspace; /* NULL: scratch allocated per call */
  ds_context_t *context;           /* queue nodes and per-call scratch */
} ds_gview_policy_t;

/* the defaults: binary heap, scratch per call, default context */
void ds_gview_policy_init(ds_gview_policy_t *policy);

/* same results as ds_gview_dijkstra (policy NULL: the defaults);
 * DS_ERR_INVALID for a negative weight */
ds_status_t ds_gview_dijkstra_with(const ds_gview_t *g, size_t source,
                                   double *out_dist, size_t *out_parent,
                                   const ds_gview_policy_t *policy);
/* minimum spanning forest of an undirected view (each edge listed in both
 * directions), same results as ds_gview_prim */
ds_status_t ds_gview_prim_with(const ds_gview_t *g, size_t *out_parent,
                               double *out_total,
                               const ds_gview_policy_t *policy);

/* estimated remaining distance from vertex to the target; HUGE_VAL marks a
 * vertex from which the target cannot be reached */
typedef double (*ds_gview_heuristic_fn)(size_t vertex, void *user);

/* A* from source to target with an admissible heuristic (never above the
 * true remaining distance; NULL: zero, which is Dijkstra stopped at the
 * target). Consistent heuristics settle each vertex once; inconsistent
 * admissible ones reopen vertices and stay exact. out_dist holds the best
 * known distance of every vertex reached (HUGE_VAL elsewhere) and is exact
 * for the target. DS_OK when the target was reached, DS_NOT_FOUND when it
 * cannot be, DS_ERR_INVALID for a negative weight. */
ds_status_t ds_gview_astar_with(const ds_gview_t *g, size_t source,
                                size_t target, ds_gview_heuristic_fn heuristic,
                                void *user, double *out_dist,
                                size_t *out_parent,
                                const ds_gview_policy_t *policy);
size_t ds_gview_path(const size_t *parent, size_t target, size_t *out_path,
                     size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_GRAPH_VIEW_H */
