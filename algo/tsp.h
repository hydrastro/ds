#ifndef DS_ALGO_TSP_H
#define DS_ALGO_TSP_H

/*
 * The traveling salesman problem and relatives over an n x n distance
 * matrix (row major doubles; symmetric unless noted).
 *
 * ds_tsp methods:
 *   DS_TSP_HELD_KARP         exact dynamic programming over subsets,
 *                            O(2^n n^2) time (n <= 18; any asymmetric
 *                            matrix)
 *   DS_TSP_NEAREST_NEIGHBOR  greedy tour
 *   DS_TSP_TWO_OPT           nearest neighbour improved by 2-opt moves
 *   DS_TSP_CHRISTOFIDES      Christofides' 3/2-approximation for metric
 *                            instances: minimum spanning tree, a minimum
 *                            weight perfect matching on its odd vertices
 *                            (exact for up to 20 odd vertices, greedy
 *                            above), an Euler tour, shortcuts
 *   DS_TSP_CHRISTOFIDES_TWO_OPT  Christofides followed by 2-opt
 * ds_tsp_euclidean builds the matrix from points in the plane first.
 * ds_tsp_bottleneck minimises the longest edge of the tour (exact: the
 * smallest threshold whose graph has a Hamiltonian cycle, n <= 20).
 * ds_vrp_savings solves the capacitated vehicle routing problem with the
 * Clarke-Wright savings heuristic (vertex 0 is the depot).
 *
 * Tours list every vertex once starting at vertex 0; their length closes
 * the cycle. Scratch memory comes from the ds_context_t.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_tsp_method {
  DS_TSP_HELD_KARP = 0,
  DS_TSP_NEAREST_NEIGHBOR,
  DS_TSP_TWO_OPT,
  DS_TSP_CHRISTOFIDES,
  DS_TSP_CHRISTOFIDES_TWO_OPT
} ds_tsp_method_t;

ds_status_t ds_tsp(const double *dist, size_t n, ds_tsp_method_t method,
                   size_t *tour, double *out_length, ds_context_t *context);
/* xy: n points as x, y pairs. */
ds_status_t ds_tsp_euclidean(const double *xy, size_t n, ds_tsp_method_t method,
                             size_t *tour, double *out_length,
                             ds_context_t *context);
/* Length of a given tour. */
double ds_tsp_tour_length(const double *dist, size_t n, const size_t *tour);
ds_status_t ds_tsp_bottleneck(const double *dist, size_t n, size_t *tour,
                              double *out_max_edge, ds_context_t *context);
/* demand: n entries (demand[0] ignored); every demand <= capacity. order
 * receives the n - 1 customers route after route, route_start the
 * *out_routes + 1 offsets into order (n entries suffice). */
ds_status_t ds_vrp_savings(const double *dist, size_t n, const double *demand,
                           double capacity, size_t *order, size_t *route_start,
                           size_t *out_routes, double *out_length,
                           ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_TSP_H */
