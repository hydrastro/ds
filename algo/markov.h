#ifndef DS_MARKOV_H
#define DS_MARKOV_H

/*
 * Finite discrete-time Markov chains.
 *
 * A ds_markov_chain_t holds a dense row-stochastic transition matrix
 * (p[i * states + j] = P(next = j | now = i)). Around it:
 *
 *   ds_markov_validate     nonnegative rows summing to 1 (within tol)
 *   ds_markov_fit          maximum-likelihood estimate from observed
 *                          sequences, with additive (Laplace) smoothing
 *   ds_markov_step/walk    simulation with a ds_rng_t
 *   ds_markov_evolve       distribution after k steps, O(k n^2)
 *   ds_markov_power        P^k by repeated squaring, O(n^3 log k)
 *   ds_markov_classes      communicating classes (Tarjan, iterative) with
 *                          closed = recurrent flags and periods
 *   ds_markov_stationary   the stationary distribution of an irreducible
 *                          chain (or one with a single closed class), by
 *                          Gaussian elimination with partial pivoting
 *   ds_markov_absorption   for every state, the probability of ending in
 *                          each closed class and the expected number of
 *                          steps until a recurrent state is reached
 *   ds_markov_hitting      probability of ever reaching a target set and
 *                          the expected hitting time. Which states hit with
 *                          certainty is decided exactly on the graph (their
 *                          times are finite, the others HUGE_VAL); the
 *                          finite times and the probabilities come from
 *                          Gaussian elimination and carry its rounding
 *   ds_markov_entropy_rate -sum_i pi_i sum_j p_ij log p_ij (nats)
 *
 * Memory comes from the ds_context_t given at init.
 */

#include "context.h"
#include "random.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_markov_chain {
  size_t states;
  double *p;
  ds_context_t *context;
} ds_markov_chain_t;

/* zero matrix (fill it with set or write p directly, then validate) */
ds_status_t ds_markov_init(ds_markov_chain_t *chain, size_t states,
                           ds_context_t *context);
ds_status_t ds_markov_init_matrix(ds_markov_chain_t *chain, size_t states,
                                  const double *matrix, ds_context_t *context);
void ds_markov_destroy(ds_markov_chain_t *chain);
ds_status_t ds_markov_set(ds_markov_chain_t *chain, size_t from, size_t to,
                          double probability);
double ds_markov_get(const ds_markov_chain_t *chain, size_t from, size_t to);
/* DS_ERR_INVALID unless every entry is >= 0 and every row sums to 1+-tol */
ds_status_t ds_markov_validate(const ds_markov_chain_t *chain, double tol);
/* Re-estimate the matrix from sequences: seqs[k] has lengths[k] states.
 * Each row becomes (count + alpha) / (row total + alpha * states); a row
 * with no observations and alpha == 0 becomes a self-loop. */
ds_status_t ds_markov_fit(ds_markov_chain_t *chain, const size_t *const *seqs,
                          const size_t *lengths, size_t count, double alpha);

/* next state, or DS_ERR_RANGE for a bad state */
ds_status_t ds_markov_step(const ds_markov_chain_t *chain, size_t state,
                           ds_rng_t *rng, size_t *out);
/* out[0] = start, out[1..steps] the walk */
ds_status_t ds_markov_walk(const ds_markov_chain_t *chain, size_t start,
                           size_t steps, ds_rng_t *rng, size_t *out);
/* out = in * P^steps (in and out may alias) */
ds_status_t ds_markov_evolve(const ds_markov_chain_t *chain, const double *in,
                             size_t steps, double *out);
/* out (states * states) = P^k */
ds_status_t ds_markov_power(const ds_markov_chain_t *chain, unsigned long k,
                            double *out);

/* class_of[i] in [0, *out_classes); classes are numbered in a reverse
 * topological order (a class only reaches classes with smaller numbers).
 * closed and period (optional, size states) are indexed by class. */
ds_status_t ds_markov_classes(const ds_markov_chain_t *chain, size_t *class_of,
                              size_t *out_classes, bool *closed,
                              size_t *period);
bool ds_markov_is_irreducible(const ds_markov_chain_t *chain);
/* irreducible and aperiodic */
bool ds_markov_is_ergodic(const ds_markov_chain_t *chain);
/* DS_ERR_STATE when there are several closed classes (no unique answer) */
ds_status_t ds_markov_stationary(const ds_markov_chain_t *chain, double *out);
/* absorb[i * classes + c] = P(state i ends in closed class c), with
 * classes and class_of as from ds_markov_classes (absorb has states * that
 * many entries; pass states * states to be safe); steps[i] = expected
 * steps to reach a recurrent state. Either output may be NULL. */
ds_status_t ds_markov_absorption(const ds_markov_chain_t *chain, double *absorb,
                                 double *steps);
/* target[i] true marks the set A. prob[i] = P(reach A from i), time[i] =
 * E[steps to reach A] (0 on A, HUGE_VAL if prob < 1). Either may be NULL. */
ds_status_t ds_markov_hitting(const ds_markov_chain_t *chain,
                              const bool *target, double *prob, double *time);
ds_status_t ds_markov_entropy_rate(const ds_markov_chain_t *chain, double *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_MARKOV_H */
