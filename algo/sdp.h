#ifndef DS_ALGO_SDP_H
#define DS_ALGO_SDP_H

/*
 * Semidefinite programming, in the block-diagonal form of SDPA and CSDP:
 *
 *   (P)  maximize   <C, X>   subject to  <A_i, X> = a_i  (i = 1 .. m),
 *                                        X positive semidefinite
 *   (D)  minimize   a^T y    subject to  Z = sum_i y_i A_i - C,
 *                                        Z positive semidefinite
 *
 * with <U, V> = trace(U V). C, the A_i, X and Z are symmetric and block
 * diagonal with a common block structure; a block is either dense (a
 * semidefinite block) or diagonal (its entries nonnegative: a block of a
 * linear program, so LPs are SDPs with diagonal blocks only). Weak
 * duality <C, X> <= a^T y holds for every feasible pair, with equality at
 * optimal pairs when both problems are strictly feasible.
 *
 * ds_sdp_solve is an infeasible primal-dual interior-point method: X, Z
 * positive definite and y free, driven to feasibility and to X Z = 0 at
 * once. Each iteration takes the HKM direction (Helmberg, Rendl,
 * Vanderbei and Wolkowicz; Kojima, Shindoh and Hara; Monteiro): the
 * Schur complement system M dy = r with M_ij = <A_i, X A_j Z^-1>, built
 * from the sparse A_i in O(m s nnz) and solved by Cholesky; then dZ and a
 * symmetrized dX. Mehrotra's predictor-corrector picks the centering:
 * an affine step first, then sigma = (gap after it / gap)^3 and a
 * second-order correction, with the same factorization. Step lengths are
 * exact: the largest t with X + t dX positive semidefinite comes from the
 * least eigenvalue of L^-1 dX L^-T (X = L L^T), by Jacobi rotations;
 * steps are 0.95 of them. Factorizations, solves and eigenvalues come
 * from algo/matrix. Each iteration costs O(m^2 s^2 + m s^3) for dense
 * data (s the order of the largest block), far less for sparse A_i.
 *
 * It stops when the relative primal and dual infeasibilities and the
 * relative gap are below the tolerance (DS_SDP_OPTIMAL). When rounding
 * stops progress first (the iterates lose centrality as X Z nears 0 on
 * degenerate problems), the best iterate is returned, as
 * DS_SDP_NEAR_OPTIMAL if within 1000 times the tolerance. Otherwise it
 * reports a certificate: DS_SDP_PRIMAL_INFEASIBLE with y scaled to a^T y = -1
 * and sum y_i A_i (nearly) positive semidefinite, which no feasible X allows;
 * DS_SDP_DUAL_INFEASIBLE with X scaled to <C, X> = 1 and A(X) (nearly) 0,
 * a direction along which (P) is unbounded. Constraints whose A_i are
 * combinations of others are found first (a pivoted Cholesky
 * factorization of the Gram matrix <A_i, A_j>) and dropped with y_i = 0,
 * or, when their a_i disagree, give the certificate of infeasibility at
 * once. Rounding in the products with Z^-1, which grows near the
 * optimum, is kept out of A(dX) by projecting dX onto A(dX) = a - A(X);
 * when Mehrotra's correction shortens the step it is dropped for a plain
 * centering step.
 *
 * Problems are built entry by entry or read from the sparse SDPA format
 * (.dat-s), which CSDP and SDPA read: its F_0 is C and its F_i are A_i.
 * Two classical uses come with it: Lovasz's theta function of a graph
 * (sandwiched between the independence number and the clique cover
 * number, so also a bound for algo/graph_np's exact searches) and the
 * Goemans-Williamson max-cut approximation (the SDP bound, then random
 * hyperplanes through the Gram vectors: an expected 0.878 of the optimum
 * for nonnegative weights).
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_sdp_entry {
  size_t matrix; /* 0: C, i: A_i */
  size_t block;
  size_t row; /* row <= col */
  size_t col;
  double value;
} ds_sdp_entry_t;

typedef struct ds_sdp {
  size_t m;                /* constraints */
  size_t blocks;           /* in X */
  size_t *dim;             /* order of each block */
  unsigned char *diagonal; /* 1 for a diagonal (linear) block */
  double *rhs;             /* a_1 .. a_m at rhs[0 .. m - 1] */
  ds_sdp_entry_t *entry;   /* as added; equal positions add up */
  size_t entries;
  size_t capacity;
  ds_context_t *context;
} ds_sdp_t;

typedef enum ds_sdp_status {
  DS_SDP_OPTIMAL = 0,
  DS_SDP_NEAR_OPTIMAL,      /* stopped within 1000 times the tolerance */
  DS_SDP_PRIMAL_INFEASIBLE, /* y: a certificate */
  DS_SDP_DUAL_INFEASIBLE,   /* X: a certificate */
  DS_SDP_ITERATION_LIMIT,
  DS_SDP_STALLED /* no more progress (rounding, a singular M) */
} ds_sdp_status_t;

typedef struct ds_sdp_options {
  double tolerance;      /* relative infeasibilities and gap; 0: 1e-8 */
  size_t max_iterations; /* 0: 100 */
} ds_sdp_options_t;

typedef struct ds_sdp_solution {
  ds_sdp_status_t status;
  double primal_objective;     /* <C, X> */
  double dual_objective;       /* a^T y */
  double primal_infeasibility; /* ||a - A(X)|| / (1 + ||a||) */
  double dual_infeasibility;   /* ||sum y_i A_i - C - Z|| / (1 + ||C||) */
  double gap;                  /* |a^T y - <C, X>| / (1 + both) */
  size_t iterations;
  size_t length; /* of x and z: dim^2 per dense block, dim per diagonal */
  double *x;     /* block after block (ds_sdp_block_offset), row major */
  double *y;     /* m */
  double *z;
  ds_context_t *context;
} ds_sdp_solution_t;

/* sizes[b] > 0: a dense block of that order; < 0: a diagonal block of
 * order -sizes[b] (the SDPA convention); 0 is DS_ERR_INVALID */
ds_status_t ds_sdp_init(ds_sdp_t *p, size_t m, size_t blocks, const long *sizes,
                        ds_context_t *context);
void ds_sdp_destroy(ds_sdp_t *p);
/* a_i, i in 1 .. m */
ds_status_t ds_sdp_set_rhs(ds_sdp_t *p, size_t i, double value);
/* adds value at (row, col) and (col, row) of block `block` (from 0) of
 * matrix k (0: C, i: A_i); rows and columns from 0. Off the diagonal of a
 * diagonal block: DS_ERR_INVALID */
ds_status_t ds_sdp_add(ds_sdp_t *p, size_t k, size_t block, size_t row,
                       size_t col, double value);
/* the sparse SDPA format: comment lines first (starting with " or *),
 * then four lines holding m, the number of blocks, the block sizes and
 * the m values a_i (the rest of each of these lines is a comment), then
 * entries "matrix block row col value" with blocks, rows and columns from
 * 1; the punctuation ,{}() is skipped. p must not be initialized */
ds_status_t ds_sdp_parse_sdpa(ds_sdp_t *p, const char *text,
                              ds_context_t *context);
/* where block b starts in a solution's x and z */
size_t ds_sdp_block_offset(const ds_sdp_t *p, size_t block);

void ds_sdp_options_init(ds_sdp_options_t *options);
/* options may be NULL; out is initialized by the call (its arrays come
 * from the problem's context) and must be destroyed even after a
 * non-optimal status. DS_ERR_ALLOC and the argument errors return
 * without a solution. */
ds_status_t ds_sdp_solve(const ds_sdp_t *p, const ds_sdp_options_t *options,
                         ds_sdp_solution_t *out);
void ds_sdp_solution_destroy(ds_sdp_solution_t *s);

/* Lovasz's theta of the graph on n vertices with the given edges (loops
 * and repeats ignored): max <J, X> s.t. trace X = 1, X_uv = 0 on edges.
 * alpha(G) <= theta(G) <= the clique cover number of G. DS_ERR_STATE if
 * the solver does not reach the tolerance. */
ds_status_t ds_sdp_lovasz_theta(size_t n, const size_t *u, const size_t *v,
                                size_t edges, double *out,
                                ds_context_t *context);

/* Goemans and Williamson: the SDP bound max sum_uv w_uv (1 - X_uv) / 2
 * s.t. X_ii = 1, then `trials` random hyperplanes (from seed) through the
 * Gram vectors of X; side[i] (0 or 1) gets the best cut found, *out_cut
 * its weight, *out_bound (may be NULL) the SDP bound. */
ds_status_t ds_sdp_maxcut(size_t n, const size_t *u, const size_t *v,
                          const double *w, size_t edges, unsigned long seed,
                          size_t trials, unsigned char *side, double *out_cut,
                          double *out_bound, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_SDP_H */
