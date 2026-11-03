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

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_RANDOM_H */
