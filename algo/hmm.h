#ifndef DS_ALGO_HMM_H
#define DS_ALGO_HMM_H

/*
 * Hidden Markov models with N hidden states and M discrete symbols: an
 * initial distribution pi (N), transitions a (N x N, row = from) and
 * emissions b (N x M, row = state), all row-stochastic doubles.
 *
 *   ds_hmm_forward     scaled forward algorithm: log P(observations)
 *   ds_hmm_backward    scaled backward algorithm (same scale factors)
 *   ds_hmm_posterior   forward-backward state posteriors gamma (T x N)
 *   ds_hmm_viterbi     most likely state path (log domain, no underflow)
 *   ds_hmm_baum_welch  Baum-Welch (EM) re-estimation over one or more
 *                      sequences until the log-likelihood gain falls below
 *                      a tolerance or an iteration limit; the likelihood
 *                      never decreases
 *
 * The model's arrays belong to the caller (ds_hmm_t only points at them),
 * so any storage works. Observations are size_t symbols in [0, M).
 * Scratch memory comes from the ds_context_t.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_hmm {
  size_t states;
  size_t symbols;
  double *pi; /* states */
  double *a;  /* states x states */
  double *b;  /* states x symbols */
} ds_hmm_t;

/* alpha (T x N, may be NULL) receives the scaled forward variables and
 * scale (T, may be NULL) the scale factors. DS_ERR_INVALID if the
 * observations are impossible under the model (zero probability). */
ds_status_t ds_hmm_forward(const ds_hmm_t *h, const size_t *obs, size_t t,
                           double *alpha, double *scale, double *out_loglik,
                           ds_context_t *context);
/* beta (T x N): scaled with the forward pass's scale factors. */
ds_status_t ds_hmm_backward(const ds_hmm_t *h, const size_t *obs, size_t t,
                            const double *scale, double *beta);
/* gamma (T x N): P(state at time t | observations). */
ds_status_t ds_hmm_posterior(const ds_hmm_t *h, const size_t *obs, size_t t,
                             double *gamma, double *out_loglik,
                             ds_context_t *context);
/* path (T): the most likely states; *out_logprob its log probability. */
ds_status_t ds_hmm_viterbi(const ds_hmm_t *h, const size_t *obs, size_t t,
                           size_t *path, double *out_logprob,
                           ds_context_t *context);
/* Re-estimates pi, a and b in place from `count` sequences. Returns the
 * final total log-likelihood and the iterations run (both may be NULL). */
ds_status_t ds_hmm_baum_welch(ds_hmm_t *h, const size_t *const *seqs,
                              const size_t *lens, size_t count,
                              size_t max_iterations, double tolerance,
                              double *out_loglik, size_t *out_iterations,
                              ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_HMM_H */
