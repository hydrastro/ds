#ifndef DS_ALGO_GRAPH_PATH_H
#define DS_ALGO_GRAPH_PATH_H

/*
 * Path and spanning-structure algorithms over ds_gview_t (see
 * graph_view.h) and over edge lists:
 *
 *   ds_gview_johnson()           all-pairs shortest paths with negative
 *                                weights: Bellman-Ford potentials, then
 *                                Dijkstra from every vertex, O(VE log V)
 *   ds_gview_dag_shortest()      single-source shortest (or, with longest,
 *                                longest) paths of a DAG in O(V + E); the
 *                                longest version is the critical path
 *   ds_gview_best_first()        best-first search: priority
 *                                g_weight * distance + h(v); A* with
 *                                g_weight 1 and an admissible heuristic,
 *                                greedy best-first with g_weight 0,
 *                                Dijkstra with h = NULL
 *   ds_gview_k_shortest()        Yen's k shortest loopless paths
 *   ds_gview_simple_paths()      enumerate every simple s-t path
 *   ds_gview_min_arborescence()  minimum spanning arborescence (directed
 *                                MST rooted at a vertex), Chu-Liu/Edmonds
 *   ds_kruskal(), ds_boruvka()   minimum spanning forest of an edge list
 *   ds_chinese_postman()         shortest closed walk using every edge of
 *                                a connected undirected graph at least once
 *
 * Weights are doubles read through the view. Outputs go to caller arrays;
 * working memory comes from the ds_context_t. All iterative.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include "graph_view.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Heuristic estimate of the remaining distance from v. */
typedef double (*ds_gview_heuristic_func_t)(size_t v, void *user);
/* A path of `length` vertices with total weight `cost`; return DS_OK to
 * continue or anything else to stop the enumeration (returned). */
typedef ds_status_t (*ds_gview_path_func_t)(const size_t *path, size_t length,
                                            double cost, void *user);

/* out_dist: n x n row-major (HUGE_VAL when unreachable). DS_ERR_INVALID on
 * a negative cycle. */
ds_status_t ds_gview_johnson(const ds_gview_t *g, double *out_dist,
                             ds_context_t *context);

/* DS_ERR_INVALID if the view has a cycle. Unreachable: HUGE_VAL (shortest)
 * or -HUGE_VAL (longest). out_parent may be NULL. */
ds_status_t ds_gview_dag_shortest(const ds_gview_t *g, size_t source,
                                  bool longest, double *out_dist,
                                  size_t *out_parent, ds_context_t *context);

/* Search from source until target is settled (target DS_GVIEW_NONE:
 * settle everything). out_dist/out_parent have vertex_count entries; the
 * number of vertices expanded goes to *out_expanded (may be NULL).
 * DS_NOT_FOUND when target is unreachable. */
ds_status_t ds_gview_best_first(const ds_gview_t *g, size_t source,
                                size_t target, ds_gview_heuristic_func_t h,
                                void *user, double g_weight, double *out_dist,
                                size_t *out_parent, size_t *out_expanded,
                                ds_context_t *context);

/* Up to k shortest loopless s-t paths in nondecreasing cost (weights
 * >= 0), reported through visit. The number found in *out_found. */
ds_status_t ds_gview_k_shortest(const ds_gview_t *g, size_t source,
                                size_t target, size_t k,
                                ds_gview_path_func_t visit, void *user,
                                size_t *out_found, ds_context_t *context);

/* Every simple path from source to target (DFS order). */
ds_status_t ds_gview_simple_paths(const ds_gview_t *g, size_t source,
                                  size_t target, ds_gview_path_func_t visit,
                                  void *user, ds_context_t *context);

/* out_parent_edge[v] = global edge index (see ds_gview_edge_offsets) of
 * v's incoming arborescence edge, DS_GVIEW_NONE for the root.
 * DS_NOT_FOUND if some vertex is unreachable from root. */
ds_status_t ds_gview_min_arborescence(const ds_gview_t *g, size_t root,
                                      size_t *out_parent_edge,
                                      double *out_total, ds_context_t *context);

/* Minimum spanning forest of m edges (u[i], v[i], w[i]) on n vertices:
 * out_in_forest[i] = 1 for chosen edges; total weight in *out_total. */
ds_status_t ds_kruskal(size_t n, const size_t *u, const size_t *v,
                       const double *w, size_t m, unsigned char *out_in_forest,
                       double *out_total, ds_context_t *context);
ds_status_t ds_boruvka(size_t n, const size_t *u, const size_t *v,
                       const double *w, size_t m, unsigned char *out_in_forest,
                       double *out_total, ds_context_t *context);

/* Closed walk from start through every edge (weights >= 0, graph
 * connected on its edges). The walk (vertices) goes to out_route when
 * non-NULL (capacity entries); its length to *out_route_length and its
 * cost to *out_cost. At most DS_CHINESE_POSTMAN_MAX_ODD odd-degree
 * vertices (exact pairing by dynamic programming). */
#define DS_CHINESE_POSTMAN_MAX_ODD 20
ds_status_t ds_chinese_postman(size_t n, const size_t *u, const size_t *v,
                               const double *w, size_t m, size_t start,
                               size_t *out_route, size_t capacity,
                               size_t *out_route_length, double *out_cost,
                               ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_GRAPH_PATH_H */
