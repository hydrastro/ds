#ifndef DS_ALGO_GRAPH_NP_H
#define DS_ALGO_GRAPH_NP_H

/*
 * NP-hard graph problems: exact exponential algorithms for small inputs
 * next to fast approximations and heuristics for large ones. Graphs are
 * n x n adjacency matrices of bytes (non-zero = edge), symmetric for the
 * undirected problems; vertex sets are written as lists of vertex indices
 * (or byte flags where noted).
 *
 * Cliques and independent sets
 *   ds_maximal_cliques        Bron-Kerbosch with Tomita pivoting: every
 *                             maximal clique through a callback
 *   ds_max_clique             a maximum clique (the clique problem)
 *   ds_max_independent_set    a maximum independent set (cliques of the
 *                             complement)
 *   ds_maximal_independent_set  greedy (minimum degree first) or Luby's
 *                             randomized rounds
 * Vertex cover
 *   ds_min_vertex_cover       exact (complement of a maximum independent
 *                             set)
 *   ds_vertex_cover_approx    2-approximation from a maximal matching
 * Colouring
 *   ds_color_greedy           greedy colouring in order or DSATUR
 *   ds_k_coloring             decide k-colourability (backtracking with
 *                             DSATUR ordering); a colouring when one exists
 *   ds_chromatic_number       exact chromatic number (increasing k from a
 *                             clique lower bound) and an optimal colouring
 *   ds_edge_color             Misra-Gries: a proper edge colouring with at
 *                             most Delta + 1 colours
 *   ds_chromatic_index        exact: Delta or Delta + 1 (Vizing), decided by
 *                             backtracking
 * Paths and cycles
 *   ds_hamiltonian            a Hamiltonian path or cycle by Held-Karp
 *                             dynamic programming over subsets (n <= 20) or
 *                             by backtracking with pruning (any n)
 *   ds_path_cover             minimum vertex-disjoint path cover of a DAG
 *                             (n minus a maximum bipartite matching)
 *   ds_feedback_vertex_set    minimum set of vertices meeting every cycle
 *                             (directed or undirected): exact by increasing
 *                             size, or a greedy heuristic
 * Structure
 *   ds_graph_isomorphism      an isomorphism between two graphs
 *   ds_subgraph_isomorphism   an embedding of a pattern graph into a target
 *                             (induced or not), VF2-style backtracking with
 *                             degree pruning
 *   ds_graph_bisection        Kernighan-Lin bisection (weighted), or exact
 *                             minimum bisection for small n
 *
 * Scratch memory comes from the ds_context_t; exponential searches accept
 * a node budget (0: unlimited) and report DS_STOP when it runs out.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* visit(clique, size, user): return false to stop. */
ds_status_t ds_maximal_cliques(const unsigned char *adj, size_t n,
                               bool (*visit)(const size_t *clique, size_t size,
                                             void *user),
                               void *user, ds_context_t *context);
ds_status_t ds_max_clique(const unsigned char *adj, size_t n, size_t *out,
                          size_t *out_size, ds_context_t *context);
ds_status_t ds_max_independent_set(const unsigned char *adj, size_t n,
                                   size_t *out, size_t *out_size,
                                   ds_context_t *context);

typedef enum ds_mis_method { DS_MIS_GREEDY = 0, DS_MIS_LUBY } ds_mis_method_t;

/* in_set: n flags. seed for Luby. */
ds_status_t ds_maximal_independent_set(const unsigned char *adj, size_t n,
                                       ds_mis_method_t method,
                                       unsigned long seed,
                                       unsigned char *in_set,
                                       size_t *out_rounds,
                                       ds_context_t *context);
ds_status_t ds_min_vertex_cover(const unsigned char *adj, size_t n, size_t *out,
                                size_t *out_size, ds_context_t *context);
ds_status_t ds_vertex_cover_approx(const unsigned char *adj, size_t n,
                                   unsigned char *in_cover, size_t *out_size);

typedef enum ds_color_order {
  DS_COLOR_IN_ORDER = 0,
  DS_COLOR_DSATUR
} ds_color_order_t;

/* color[v] in [0, *out_colors). */
ds_status_t ds_color_greedy(const unsigned char *adj, size_t n,
                            ds_color_order_t order, size_t *color,
                            size_t *out_colors, ds_context_t *context);
/* DS_OK and color when k-colourable, DS_NOT_FOUND when not, DS_STOP when
 * the node budget ran out. */
ds_status_t ds_k_coloring(const unsigned char *adj, size_t n, size_t k,
                          size_t *color, size_t budget, ds_context_t *context);
ds_status_t ds_chromatic_number(const unsigned char *adj, size_t n,
                                size_t *color, size_t *out_chi, size_t budget,
                                ds_context_t *context);
/* ecolor: n x n matrix of colours (1-based, 0 = no edge); *out_colors
 * colours used (<= Delta + 1). */
ds_status_t ds_edge_color(const unsigned char *adj, size_t n, size_t *ecolor,
                          size_t *out_colors, ds_context_t *context);
ds_status_t ds_chromatic_index(const unsigned char *adj, size_t n,
                               size_t *ecolor, size_t *out_index, size_t budget,
                               ds_context_t *context);

typedef enum ds_ham_method { DS_HAM_DP = 0, DS_HAM_BACKTRACK } ds_ham_method_t;

/* adj may be directed. order: n vertices. cycle: also require the edge
 * from the last vertex back to the first. DS_NOT_FOUND when none. */
ds_status_t ds_hamiltonian(const unsigned char *adj, size_t n, bool cycle,
                           ds_ham_method_t method, size_t *order, size_t budget,
                           ds_context_t *context);
/* DAG (directed adj): path_id[v] in [0, *out_paths) and next[v] the
 * following vertex on its path (or ~0). */
ds_status_t ds_path_cover(const unsigned char *adj, size_t n, size_t *path_id,
                          size_t *next, size_t *out_paths,
                          ds_context_t *context);

typedef enum ds_fvs_method { DS_FVS_EXACT = 0, DS_FVS_GREEDY } ds_fvs_method_t;

/* in_set: n flags. */
ds_status_t ds_feedback_vertex_set(const unsigned char *adj, size_t n,
                                   bool directed, ds_fvs_method_t method,
                                   unsigned char *in_set, size_t *out_size,
                                   ds_context_t *context);

/* map[v] = image of v. DS_NOT_FOUND when none exists. */
ds_status_t ds_graph_isomorphism(const unsigned char *a, const unsigned char *b,
                                 size_t n, size_t *map, size_t budget,
                                 ds_context_t *context);
/* map: pattern vertex -> target vertex. */
ds_status_t ds_subgraph_isomorphism(const unsigned char *pattern, size_t np,
                                    const unsigned char *target, size_t nt,
                                    bool induced, size_t *map, size_t budget,
                                    ds_context_t *context);

typedef enum ds_bisect_method {
  DS_BISECT_KERNIGHAN_LIN = 0,
  DS_BISECT_EXACT
} ds_bisect_method_t;

/* w: n x n symmetric edge weights (0 = no edge). side[v] in {0, 1} with
 * sides of sizes floor(n/2) and ceil(n/2); *out_cut the cut weight. */
ds_status_t ds_graph_bisection(const double *w, size_t n,
                               ds_bisect_method_t method, unsigned long seed,
                               unsigned char *side, double *out_cut,
                               ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_GRAPH_NP_H */
