#ifndef DS_ALGO_STEINER_H
#define DS_ALGO_STEINER_H

/*
 * Steiner trees: the cheapest tree connecting a set of terminals,
 * possibly through extra (Steiner) vertices or points.
 *
 *   ds_steiner_tree        in a weighted graph (n x n matrix of edge
 *                          weights, HUGE_VAL = no edge):
 *                            DS_STEINER_DREYFUS_WAGNER  exact dynamic
 *                              programming over terminal subsets,
 *                              O(3^k n + 2^k n^2) for k <= 16 terminals
 *                            DS_STEINER_MST  the 2-approximation of Kou,
 *                              Markowsky and Berman: an MST of the
 *                              terminals' metric closure, expanded into
 *                              paths, re-spanned and pruned
 *   ds_rectilinear_steiner the rectilinear Steiner minimum tree of points
 *                          in the plane, exact (Hanan's theorem: an
 *                          optimal tree lives on the grid of the points'
 *                          coordinates; Dreyfus-Wagner on that grid, up
 *                          to 12 points)
 *   ds_euclidean_steiner   a Euclidean Steiner tree heuristic: start from
 *                          the minimum spanning tree and insert Fermat
 *                          points where two edges meet at less than 120
 *                          degrees, re-spanning after each insertion; never
 *                          longer than the MST
 *
 * Trees are returned as edge lists (pairs of vertex or point indices) and
 * their total length. Scratch memory comes from the ds_context_t.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_steiner_method {
  DS_STEINER_DREYFUS_WAGNER = 0,
  DS_STEINER_MST
} ds_steiner_method_t;

/* eu, ev: room for n - 1 edges; *out_edges edges written. DS_NOT_FOUND
 * when the terminals are not connected. */
ds_status_t ds_steiner_tree(const double *w, size_t n, const size_t *term,
                            size_t k, ds_steiner_method_t method, size_t *eu,
                            size_t *ev, size_t *out_edges, double *out_length,
                            ds_context_t *context);

/* xy: k points. out_xy receives the tree's points: the k inputs first,
 * then the Steiner points (room for k * k points); edges index them
 * (room for k * k edges). */
ds_status_t ds_rectilinear_steiner(const double *xy, size_t k, double *out_xy,
                                   size_t *out_points, size_t *eu, size_t *ev,
                                   size_t *out_edges, double *out_length,
                                   ds_context_t *context);

/* out_xy: room for 2k - 2 points (inputs first); eu, ev: room for 2k - 3
 * edges. */
ds_status_t ds_euclidean_steiner(const double *xy, size_t k, double *out_xy,
                                 size_t *out_points, size_t *eu, size_t *ev,
                                 size_t *out_edges, double *out_length,
                                 ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_STEINER_H */
