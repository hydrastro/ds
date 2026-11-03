#ifndef DS_ALGO_FLOW_H
#define DS_ALGO_FLOW_H

/*
 * Network flows: maximum flow, minimum cut, minimum-cost flow and the
 * connectivity numbers built on them.
 *
 * A ds_flow_network_t is a residual network: every edge added gets a
 * forward arc with its capacity and a reverse arc with capacity 0, stored
 * as the pair (2e, 2e + 1). Algorithms change residual capacities in place;
 * ds_flow_network_flow() reads the flow of an edge back and
 * ds_flow_network_reset() undoes every flow. Capacities and costs are
 * doubles: exact for integers below 2^53; for real-valued capacities set
 * `epsilon` (residual capacities at or below it count as saturated).
 *
 * Maximum flow, all in place on the network (pick with
 * ds_flow_algorithm_t or call one directly):
 *   Edmonds-Karp              Ford-Fulkerson with shortest augmenting
 *                             paths (BFS), O(V E^2)
 *   Dinic                     blocking flows on BFS level (layered)
 *                             graphs with current-arc DFS, O(V^2 E);
 *                             O(E sqrt V) on unit networks
 *   Malhotra-Kumar-Maheshwari blocking flows by vertex potentials:
 *                             saturate the minimum-potential vertex by
 *                             pushing to t and pulling from s, O(V^3)
 *   push-relabel              Goldberg-Tarjan, FIFO selection with the gap
 *                             heuristic, O(V^3)
 * Every implementation is iterative (no recursion on graph size).
 *
 * ds_flow_min_cut() marks the source side of a minimum s-t cut after a
 * maximum flow. ds_flow_min_cost_flow() sends up to a given amount at
 * minimum total cost by successive shortest paths (Bellman-Ford once for
 * potentials, then Dijkstra with reduced costs); negative costs are fine,
 * negative cycles are not (DS_ERR_INVALID).
 *
 * Working memory comes from the ds_context_t passed to each call (NULL for
 * the default); outputs go to caller arrays.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include "graph_view.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_FLOW_NONE (~(size_t)0)

typedef struct ds_flow_network {
  size_t vertex_count;
  size_t arc_count;
  size_t arc_capacity;
  size_t *head; /* first arc of each vertex, DS_FLOW_NONE terminated lists */
  size_t *next; /* per arc */
  size_t *to;   /* per arc */
  double *cap;  /* residual capacity per arc */
  double *cost; /* per arc (reverse arcs hold the negated cost) */
  double *original; /* capacity per edge (arc pair) */
  double epsilon;
  ds_context_t *context;
} ds_flow_network_t;

typedef enum ds_flow_algorithm {
  DS_FLOW_EDMONDS_KARP = 0,
  DS_FLOW_DINIC = 1,
  DS_FLOW_MKM = 2,
  DS_FLOW_PUSH_RELABEL = 3
} ds_flow_algorithm_t;

ds_status_t ds_flow_network_init(ds_flow_network_t *net, size_t vertex_count,
                                 ds_context_t *context);
void ds_flow_network_destroy(ds_flow_network_t *net);
/* Edge from -> to with capacity >= 0 and cost per unit; its id (0, 1, ...)
 * goes to *out_edge (may be NULL). */
ds_status_t ds_flow_network_add_edge(ds_flow_network_t *net, size_t from,
                                     size_t to, double capacity, double cost,
                                     size_t *out_edge);
size_t ds_flow_network_edge_count(const ds_flow_network_t *net);
double ds_flow_network_flow(const ds_flow_network_t *net, size_t edge);
void ds_flow_network_reset(ds_flow_network_t *net);

/* Add a maximum s-t flow to the current flow; total added in *out_value. */
ds_status_t ds_flow_max_flow(ds_flow_network_t *net, size_t source, size_t sink,
                             ds_flow_algorithm_t algorithm, double *out_value,
                             ds_context_t *context);
ds_status_t ds_flow_edmonds_karp(ds_flow_network_t *net, size_t source,
                                 size_t sink, double *out_value,
                                 ds_context_t *context);
ds_status_t ds_flow_dinic(ds_flow_network_t *net, size_t source, size_t sink,
                          double *out_value, ds_context_t *context);
ds_status_t ds_flow_mkm(ds_flow_network_t *net, size_t source, size_t sink,
                        double *out_value, ds_context_t *context);
ds_status_t ds_flow_push_relabel(ds_flow_network_t *net, size_t source,
                                 size_t sink, double *out_value,
                                 ds_context_t *context);

/* After a maximum flow: out_side[v] = 1 for vertices reachable from the
 * source in the residual network (the source side of a minimum cut). */
ds_status_t ds_flow_min_cut(const ds_flow_network_t *net, size_t source,
                            unsigned char *out_side, ds_context_t *context);

/* Send up to `limit` units (HUGE_VAL: as much as possible) from source to
 * sink at minimum cost, on top of a zero flow. */
ds_status_t ds_flow_min_cost_flow(ds_flow_network_t *net, size_t source,
                                  size_t sink, double limit, double *out_flow,
                                  double *out_cost, ds_context_t *context);

/* Connectivity of a graph given as a view (each listed edge is used as an
 * arc; give both directions for an undirected graph). Edge connectivity:
 * the minimum number of edges whose removal disconnects some vertex from
 * another (minimum over max flows). Vertex connectivity: the same for
 * vertices, by vertex splitting (V - 1 for a complete graph). */
ds_status_t ds_flow_edge_connectivity(const ds_gview_t *g, size_t *out_value,
                                      ds_context_t *context);
ds_status_t ds_flow_vertex_connectivity(const ds_gview_t *g, size_t *out_value,
                                        ds_context_t *context);
/* Minimum number of vertices whose removal separates s from t (s and t
 * not adjacent); the vertices go to out_cut (vertex_count entries), their
 * number to *out_count. */
ds_status_t ds_flow_min_vertex_cut(const ds_gview_t *g, size_t s, size_t t,
                                   size_t *out_cut, size_t *out_count,
                                   ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_FLOW_H */
