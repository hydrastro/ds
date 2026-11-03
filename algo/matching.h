#ifndef DS_ALGO_MATCHING_H
#define DS_ALGO_MATCHING_H

/*
 * Matchings.
 *
 *   ds_matching_hopcroft_karp()  maximum cardinality bipartite matching
 *                                (Hopcroft and Karp, 1973), O(E sqrt V):
 *                                phases of shortest vertex-disjoint
 *                                augmenting paths found by BFS + DFS.
 *   ds_matching_hungarian()      assignment problem: minimum (or maximum)
 *                                weight perfect matching of the rows of an
 *                                n x m cost matrix (n <= m) into columns
 *                                (Kuhn-Munkres with potentials), O(n^2 m).
 *   ds_matching_blossom()        maximum cardinality matching in a general
 *                                graph (Edmonds, 1965): augmenting paths
 *                                with odd cycles ("blossoms") contracted,
 *                                O(V^3).
 *   ds_matching_stable()         stable marriage (Gale and Shapley, 1962):
 *                                the proposer-optimal stable matching,
 *                                O(n^2).
 *
 * Graphs are read through ds_gview_t. For Hopcroft-Karp the view lists the
 * left vertices 0 .. left_count - 1 and edge targets are right vertex ids
 * 0 .. right_count - 1. Blossom expects a symmetric (undirected) view.
 * Outputs go to caller arrays with DS_MATCHING_NONE for unmatched
 * vertices; working memory comes from the ds_context_t. All iterative.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include "graph_view.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_MATCHING_NONE (~(size_t)0)

ds_status_t ds_matching_hopcroft_karp(const ds_gview_t *left,
                                      size_t right_count,
                                      size_t *out_match_left,
                                      size_t *out_match_right, size_t *out_size,
                                      ds_context_t *context);

/* cost is row-major n x m with n <= m; out_assignment[i] is the column of
 * row i. maximize selects a maximum-weight assignment instead. */
ds_status_t ds_matching_hungarian(const double *cost, size_t n, size_t m,
                                  bool maximize, size_t *out_assignment,
                                  double *out_total, ds_context_t *context);

ds_status_t ds_matching_blossom(const ds_gview_t *g, size_t *out_mate,
                                size_t *out_size, ds_context_t *context);

/* proposer_pref and acceptor_pref are row-major n x n: row i lists
 * acceptors (proposers) in decreasing preference. out_match[p] is the
 * acceptor of proposer p. */
ds_status_t ds_matching_stable(const size_t *proposer_pref,
                               const size_t *acceptor_pref, size_t n,
                               size_t *out_match, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_MATCHING_H */
