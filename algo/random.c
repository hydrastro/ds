#include "random.h"
#include <math.h>

#define M32 0xFFFFFFFFUL
#define RNG_SIZE_MAX (~(size_t)0)

static unsigned long rng_rotl(unsigned long x, unsigned int r) {
  return ((x << r) | ((x & M32) >> (32U - r))) & M32;
}

static unsigned long rng_mix(unsigned long h) {
  h &= M32;
  h ^= h >> 16;
  h = (h * 0x7FEB352DUL) & M32;
  h ^= h >> 15;
  h = (h * 0x846CA68BUL) & M32;
  h ^= h >> 16;
  return h;
}

void ds_rng_seed(ds_rng_t *rng, unsigned long seed) {
  unsigned long x = seed & M32;
  unsigned int i;
  for (i = 0; i < 4; i++) {
    x = (x + 0x9E3779B9UL) & M32;
    rng->s[i] = rng_mix(x ^ (seed >> 16 >> 16));
  }
  if ((rng->s[0] | rng->s[1] | rng->s[2] | rng->s[3]) == 0) {
    rng->s[0] = 1; /* the all-zero state is a fixed point */
  }
}

unsigned long ds_rng_next(ds_rng_t *rng) {
  unsigned long *s = rng->s;
  unsigned long result = (rng_rotl((s[1] * 5UL) & M32, 7) * 9UL) & M32;
  unsigned long t = (s[1] << 9) & M32;
  s[2] ^= s[0];
  s[3] ^= s[1];
  s[1] ^= s[2];
  s[0] ^= s[3];
  s[2] ^= t;
  s[3] = rng_rotl(s[3], 11);
  return result;
}

unsigned long ds_rng_callback(void *rng) {
  return ds_rng_next((ds_rng_t *)rng);
}

unsigned long ds_rng_bounded(ds_rng_t *rng, unsigned long bound) {
  unsigned long threshold;
  unsigned long x;
  if (bound == 0 || bound > M32) {
    return ds_rng_next(rng);
  }
  threshold = (M32 - bound + 1UL) % bound;
  for (;;) {
    x = ds_rng_next(rng);
    if (x >= threshold) {
      return x % bound;
    }
  }
}

long ds_rng_range(ds_rng_t *rng, long low, long high) {
  unsigned long span;
  unsigned long r;
  unsigned long limit;
  if (high <= low) {
    return low;
  }
  span = (unsigned long)high - (unsigned long)low; /* high - low, no overflow */
  if (span < M32) {
    return (long)((unsigned long)low + ds_rng_bounded(rng, span + 1UL));
  }
  /* Only reachable when long is wider than 32 bits: combine two draws. */
  limit = span == ~0UL ? ~0UL : ~0UL - ~0UL % (span + 1UL);
  do {
    r = (ds_rng_next(rng) << 16 << 16) | ds_rng_next(rng);
  } while (span != ~0UL && r >= limit);
  if (span != ~0UL) {
    r %= span + 1UL;
  }
  return (long)((unsigned long)low + r);
}

double ds_rng_double(ds_rng_t *rng) {
  unsigned long a = ds_rng_next(rng) >> 5; /* 27 bits */
  unsigned long b = ds_rng_next(rng) >> 6; /* 26 bits */
  return ((double)a * 67108864.0 + (double)b) / 9007199254740992.0;
}

double ds_rng_normal(ds_rng_t *rng) {
  double u;
  double v;
  double s;
  /* Marsaglia's polar method. */
  do {
    u = 2.0 * ds_rng_double(rng) - 1.0;
    v = 2.0 * ds_rng_double(rng) - 1.0;
    s = u * u + v * v;
  } while (s >= 1.0 || s <= 0.0);
  return u * sqrt(-2.0 * log(s) / s);
}

double ds_rng_exponential(ds_rng_t *rng, double rate) {
  return -log(1.0 - ds_rng_double(rng)) / rate;
}

bool ds_rng_bernoulli(ds_rng_t *rng, double p) {
  return ds_rng_double(rng) < p;
}

void ds_rng_jump(ds_rng_t *rng) {
  static const unsigned long jump[4] = {0x8764000BUL, 0xF542D2D3UL,
                                        0x6FA035C3UL, 0x77F2DB5BUL};
  unsigned long t[4] = {0, 0, 0, 0};
  unsigned int i;
  unsigned int b;
  for (i = 0; i < 4; i++) {
    for (b = 0; b < 32; b++) {
      if ((jump[i] & (1UL << b)) != 0) {
        t[0] ^= rng->s[0];
        t[1] ^= rng->s[1];
        t[2] ^= rng->s[2];
        t[3] ^= rng->s[3];
      }
      (void)ds_rng_next(rng);
    }
  }
  for (i = 0; i < 4; i++) {
    rng->s[i] = t[i];
  }
}

static ds_status_t alias_build(ds_alias_table_t *table, const double *weights,
                               size_t *work) {
  size_t n = table->count;
  size_t small = 0; /* stack growing up from work[0] */
  size_t large = n; /* stack growing down from work[n - 1] */
  size_t i;
  size_t s;
  size_t l;
  double sum = 0.0;
  for (i = 0; i < n; i++) {
    if (!(weights[i] >= 0.0)) {
      return DS_ERR_INVALID; /* negative or NaN */
    }
    sum += weights[i];
  }
  if (!(sum > 0.0)) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    table->prob[i] = weights[i] * (double)n / sum;
    table->alias[i] = i;
    if (table->prob[i] < 1.0) {
      work[small++] = i;
    } else {
      work[--large] = i;
    }
  }
  while (small > 0 && large < n) {
    s = work[--small];
    l = work[large++];
    table->alias[s] = l;
    table->prob[l] = (table->prob[l] + table->prob[s]) - 1.0;
    if (table->prob[l] < 1.0) {
      work[small++] = l;
    } else {
      work[--large] = l;
    }
  }
  /* Leftovers are 1 up to rounding error. */
  while (large < n) {
    table->prob[work[large++]] = 1.0;
  }
  while (small > 0) {
    table->prob[work[--small]] = 1.0;
  }
  return DS_OK;
}

ds_status_t ds_alias_table_init(ds_alias_table_t *table, const double *weights,
                                size_t count, ds_context_t *context) {
  size_t *work;
  ds_status_t status;
  if (table == NULL || weights == NULL) {
    return DS_ERR_NULL;
  }
  if (count == 0) {
    return DS_ERR_INVALID;
  }
  if (count > RNG_SIZE_MAX / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  table->count = count;
  table->context = context != NULL ? context : ds_default_context();
  table->fixed = false;
  table->prob =
      (double *)ds_context_alloc(table->context, count * sizeof(double));
  table->alias =
      (size_t *)ds_context_alloc(table->context, count * sizeof(size_t));
  work = (size_t *)ds_context_alloc(table->context, count * sizeof(size_t));
  if (table->prob == NULL || table->alias == NULL || work == NULL) {
    ds_context_free(table->context, work);
    ds_alias_table_destroy(table);
    return DS_ERR_ALLOC;
  }
  status = alias_build(table, weights, work);
  ds_context_free(table->context, work);
  if (status != DS_OK) {
    ds_alias_table_destroy(table);
  }
  return status;
}

ds_status_t ds_alias_table_init_buffer(ds_alias_table_t *table,
                                       const double *weights, size_t count,
                                       double *prob, size_t *alias,
                                       size_t *work) {
  if (table == NULL || weights == NULL || prob == NULL || alias == NULL ||
      work == NULL) {
    return DS_ERR_NULL;
  }
  if (count == 0) {
    return DS_ERR_INVALID;
  }
  table->prob = prob;
  table->alias = alias;
  table->count = count;
  table->context = NULL;
  table->fixed = true;
  return alias_build(table, weights, work);
}

size_t ds_alias_table_sample(const ds_alias_table_t *table, ds_rng_t *rng) {
  size_t i;
  if (table->count <= M32) {
    i = (size_t)ds_rng_bounded(rng, (unsigned long)table->count);
  } else {
    i = (size_t)ds_rng_range(rng, 0, (long)(table->count - 1));
  }
  return ds_rng_double(rng) < table->prob[i] ? i : table->alias[i];
}

void ds_alias_table_destroy(ds_alias_table_t *table) {
  if (table == NULL) {
    return;
  }
  if (!table->fixed) {
    ds_context_free(table->context, table->prob);
    ds_context_free(table->context, table->alias);
  }
  table->prob = NULL;
  table->alias = NULL;
  table->count = 0;
}
