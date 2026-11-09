#ifndef DS_ALGO_WEIGHTED_MATCHING_H
#define DS_ALGO_WEIGHTED_MATCHING_H

/*
 * Maximum weight matching in general (non-bipartite) graphs: Edmonds'
 * blossom algorithm with dual variables, in Galil's O(n^3) formulation
 * (the structure follows Van Rantwijk's well-known implementation).
 * Blossoms nest as deep as the graph allows, so every blossom traversal
 * (leaves, expansion, augmentation) uses explicit stacks, not recursion.
 *
 *   ds_matching_max_weight          a matching of maximum total weight;
 *                                   with max_cardinality, of maximum
 *                                   weight among the maximum cardinality
 *                                   matchings
 *   ds_matching_min_weight_perfect  a perfect matching of minimum total
 *                                   weight (DS_ERR_STATE if the graph has
 *                                   no perfect matching), by maximising
 *                                   W - w among maximum matchings
 *
 * Graphs are edge lists (u[k], v[k], w[k]), vertices 0 .. n-1, no self
 * loops; parallel edges are allowed. Weights are doubles, and the
 * numerical guarantee depends on them:
 *
 *   integer mode   every weight is an integer of magnitude at most
 *                  DS_WMATCH_EXACT_MAX (2^50; 2^48 for min_weight_perfect,
 *                  which transforms w to 2B + 1 - w). Detected
 *                  automatically. Dual arithmetic stays in integers,
 *                  comparisons use no tolerance, and the matching is
 *                  exactly optimal. Without max_cardinality the duals
 *                  provably stay within [0, 2 * max weight]; in any mode
 *                  every dual update is checked against 2^51 (keeping
 *                  slacks below 2^53), and a run that leaves that range
 *                  is solved again in floating mode.
 *   floating mode  any other finite weights. Dual comparisons accept a
 *                  slack up to 1e-10 * (1 + max weight), so the matching
 *                  is optimal only up to that tolerance per edge: weights
 *                  closer than it may be treated as equal. Scale weights
 *                  to integers when exactness matters.
 *
 * out_weight is a double sum of the chosen weights (exact in integer mode
 * while the sum stays below 2^53). out_mate[v] is v's partner or
 * DS_WMATCH_NONE. Memory comes from the ds_context_t.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_WMATCH_NONE (~(size_t)0)
/* largest weight magnitude of integer mode (2^50) */
#define DS_WMATCH_EXACT_MAX 1125899906842624.0

ds_status_t ds_matching_max_weight(size_t n, const size_t *u, const size_t *v,
                                   const double *w, size_t m,
                                   bool max_cardinality, size_t *out_mate,
                                   double *out_weight, ds_context_t *context);
ds_status_t ds_matching_min_weight_perfect(size_t n, const size_t *u,
                                           const size_t *v, const double *w,
                                           size_t m, size_t *out_mate,
                                           double *out_weight,
                                           ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_WEIGHTED_MATCHING_H */
