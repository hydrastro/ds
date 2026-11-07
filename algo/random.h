#ifndef DS_ALGO_RANDOM_H
#define DS_ALGO_RANDOM_H

/*
 * Pseudo-random numbers and sampling. Not for cryptography.
 *
 * ds_rng_t is xoshiro128** (Blackman & Vigna): 128 bits of state, 32-bit
 * outputs, period 2^128 - 1, and identical sequences on every platform. It
 * keeps all state in the struct, so independent generators never share
 * hidden globals. ds_rng_jump() advances by 2^64 steps to split one seed
 * into non-overlapping parallel streams. ds_rng_callback() adapts a
 * generator to ds_random_func_t (e.g. for ds_shuffle).
 *
 * ds_rng_bounded() is unbiased (rejection sampling, no modulo bias).
 *
 * ds_alias_table_t (Vose's alias method) samples index i with probability
 * weight[i] / sum(weights) in O(1) after O(n) setup.
 *
 * Classic generators and sampling:
 *   ds_lcg_t                 linear congruential generator
 *                            x' = (a x + c) mod m with any parameters (m 0
 *                            means 2^bits of unsigned long), overflow-free
 *                            multiplication; presets for MINSTD and the
 *                            Numerical Recipes generator; Hull-Dobell
 *                            full-period test
 *   ds_zipf_t                Zipfian (Zipf / Yule) distribution on 1..n with
 *                            exponent s > 0: P(k) proportional to k^-s,
 *                            sampled in O(1) expected time by Hoermann and
 *                            Derflinger's rejection-inversion, with the
 *                            probability mass function alongside
 *   ds_spectral_test_2d      the spectral test in two dimensions: nu_2, the
 *                            largest distance between the parallel lines
 *                            covering the points (x_n, x_n+1), inverted to
 *                            the length of the shortest dual lattice vector,
 *                            and the figure of merit mu_2
 *   ds_reservoir_sample      k items uniformly from a stream of unknown
 *                            length: Algorithm R, or Algorithm L, which
 *                            draws geometric skips (O(k (1 + log(n / k)))
 *                            random numbers)
 *   ds_selection_sample      random sampling of k of n indices, in
 *                            increasing order (Knuth's Algorithm S)
 *   ds_von_neumann_bit       unbiased coin flipping from a biased coin:
 *                            flip twice, keep the first of an unequal pair
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_rng {
  unsigned long s[4];
} ds_rng_t;

void ds_rng_seed(ds_rng_t *rng, unsigned long seed);
unsigned long ds_rng_next(ds_rng_t *rng);
unsigned long ds_rng_callback(void *rng);
unsigned long ds_rng_bounded(ds_rng_t *rng, unsigned long bound);
long ds_rng_range(ds_rng_t *rng, long low, long high);
double ds_rng_double(ds_rng_t *rng);
double ds_rng_normal(ds_rng_t *rng);
double ds_rng_exponential(ds_rng_t *rng, double rate);
bool ds_rng_bernoulli(ds_rng_t *rng, double p);
void ds_rng_jump(ds_rng_t *rng);

typedef struct ds_alias_table {
  double *prob;
  size_t *alias;
  size_t count;
  ds_context_t *context;
  bool fixed;
} ds_alias_table_t;

ds_status_t ds_alias_table_init(ds_alias_table_t *table, const double *weights,
                                size_t count, ds_context_t *context);
ds_status_t ds_alias_table_init_buffer(ds_alias_table_t *table,
                                       const double *weights, size_t count,
                                       double *prob, size_t *alias,
                                       size_t *work);
size_t ds_alias_table_sample(const ds_alias_table_t *table, ds_rng_t *rng);
void ds_alias_table_destroy(ds_alias_table_t *table);

typedef struct ds_lcg {
  unsigned long state;
  unsigned long a;
  unsigned long c;
  unsigned long m; /* 0: modulus 2^(bits of unsigned long) */
} ds_lcg_t;

/* DS_ERR_INVALID if m != 0 and (a >= m or c >= m); the seed is reduced
 * mod m. */
ds_status_t ds_lcg_init(ds_lcg_t *lcg, unsigned long a, unsigned long c,
                        unsigned long m, unsigned long seed);
/* Park-Miller MINSTD (a = 48271, c = 0, m = 2^31 - 1; seed 0 becomes 1). */
void ds_lcg_init_minstd(ds_lcg_t *lcg, unsigned long seed);
/* a = 1664525, c = 1013904223, m = 2^32. */
void ds_lcg_init_numerical_recipes(ds_lcg_t *lcg, unsigned long seed);
unsigned long ds_lcg_next(ds_lcg_t *lcg);
/* Period m for every seed (Hull-Dobell: c coprime to m, a - 1 divisible by
 * every prime factor of m, and by 4 when 4 divides m). m >= 2. */
bool ds_lcg_full_period(unsigned long a, unsigned long c, unsigned long m);
/* Spectral test for x' = a x + c mod m in two dimensions (c does not
 * matter; 2 <= m < 2^31; squared lengths compared in double, exact for
 * m < 2^26): out_nu = nu_2, out_mu = pi nu_2^2 / m (may be NULL). */
ds_status_t ds_spectral_test_2d(unsigned long a, unsigned long m,
                                double *out_nu, double *out_mu);

typedef struct ds_zipf {
  unsigned long n;
  double s;
  double h_x1;    /* H(1.5) - 1 */
  double h_n;     /* H(n + 0.5) */
  double squeeze; /* 2 - H^-1(H(2.5) - h(2)) */
  double norm;    /* generalised harmonic number H_{n,s} (0 until needed) */
} ds_zipf_t;

/* n >= 1, s > 0 (finite). */
ds_status_t ds_zipf_init(ds_zipf_t *z, unsigned long n, double s);
unsigned long ds_zipf_sample(const ds_zipf_t *z, ds_rng_t *rng);
/* P(K = k) (0 outside 1..n); the first call sums the normaliser, O(n). */
double ds_zipf_pmf(ds_zipf_t *z, unsigned long k);

typedef enum ds_reservoir_algorithm {
  DS_RESERVOIR_R = 0,
  DS_RESERVOIR_L = 1
} ds_reservoir_algorithm_t;

/* Reads the stream through next (DS_NOT_FOUND at its end; other statuses
 * are returned) into reservoir (k elements). The number of stream items
 * goes to *out_seen; when it is below k only that many are filled.
 * Elements above 64 bytes need one scratch element from the context. */
typedef ds_status_t (*ds_stream_next_func_t)(void *out_elem, void *user);
ds_status_t ds_reservoir_sample(ds_stream_next_func_t next, void *user,
                                size_t elem_size, size_t k, void *reservoir,
                                ds_reservoir_algorithm_t algorithm,
                                ds_rng_t *rng, size_t *out_seen,
                                ds_context_t *context);
/* k distinct indices of [0, n) in increasing order (k <= n). */
ds_status_t ds_selection_sample(size_t n, size_t k, size_t *out_indices,
                                ds_rng_t *rng);

/* biased() returns 0 or 1 with a fixed unknown probability p in (0, 1);
 * the result is 0 or 1 with probability exactly 1/2, after an expected
 * 1 / (p (1 - p)) flips. */
typedef int (*ds_coin_func_t)(void *state);
int ds_von_neumann_bit(ds_coin_func_t biased, void *state);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_RANDOM_H */
