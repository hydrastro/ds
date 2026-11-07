#include "random.h"
#include <limits.h>
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

/* ---- linear congruential generators --------------------------------- */

static unsigned long rnd_addmod(unsigned long x, unsigned long y,
                                unsigned long m) {
  /* x, y < m */
  return x >= m - y ? x - (m - y) : x + y;
}

static unsigned long rnd_mulmod(unsigned long a, unsigned long b,
                                unsigned long m) {
  unsigned long r = 0;
  a %= m;
  b %= m;
  while (b != 0) {
    if (b & 1UL) {
      r = rnd_addmod(r, a, m);
    }
    a = rnd_addmod(a, a, m);
    b >>= 1;
  }
  return r;
}

ds_status_t ds_lcg_init(ds_lcg_t *lcg, unsigned long a, unsigned long c,
                        unsigned long m, unsigned long seed) {
  if (lcg == NULL) {
    return DS_ERR_NULL;
  }
  if (m != 0 && (a >= m || c >= m || m < 2)) {
    return DS_ERR_INVALID;
  }
  lcg->a = a;
  lcg->c = c;
  lcg->m = m;
  lcg->state = m != 0 ? seed % m : seed;
  return DS_OK;
}

void ds_lcg_init_minstd(ds_lcg_t *lcg, unsigned long seed) {
  seed %= 2147483647UL;
  ds_lcg_init(lcg, 48271UL, 0UL, 2147483647UL, seed == 0 ? 1UL : seed);
}

void ds_lcg_init_numerical_recipes(ds_lcg_t *lcg, unsigned long seed) {
  lcg->a = 1664525UL;
  lcg->c = 1013904223UL;
  /* modulus 2^32: plain wrap-around when unsigned long has 32 bits */
#if ULONG_MAX == 0xFFFFFFFFUL
  lcg->m = 0UL;
#else
  lcg->m = 0xFFFFFFFFUL + 1UL;
#endif
  lcg->state = seed & 0xFFFFFFFFUL;
}

unsigned long ds_lcg_next(ds_lcg_t *lcg) {
  if (lcg->m == 0) {
    lcg->state = lcg->a * lcg->state + lcg->c;
  } else {
    lcg->state =
        rnd_addmod(rnd_mulmod(lcg->a, lcg->state, lcg->m), lcg->c, lcg->m);
  }
  return lcg->state;
}

static unsigned long rnd_gcd(unsigned long x, unsigned long y) {
  unsigned long t;
  while (y != 0) {
    t = x % y;
    x = y;
    y = t;
  }
  return x;
}

bool ds_lcg_full_period(unsigned long a, unsigned long c, unsigned long m) {
  unsigned long rest = m;
  unsigned long p;
  unsigned long am1;
  if (m < 2 || a >= m || c >= m) {
    return false;
  }
  if (rnd_gcd(c, m) != 1) {
    return false;
  }
  am1 = a == 0 ? m - 1 : a - 1; /* a - 1 mod m */
  if (a == 0) {
    return false;
  }
  for (p = 2; p <= rest / p; p++) {
    if (rest % p == 0) {
      if (am1 % p != 0) {
        return false;
      }
      while (rest % p == 0) {
        rest /= p;
      }
    }
  }
  if (rest > 1 && am1 % rest != 0) {
    return false;
  }
  if (m % 4 == 0 && am1 % 4 != 0) {
    return false;
  }
  return true;
}

static double rnd_sq2(long u, long v) {
  return (double)u * (double)u + (double)v * (double)v;
}

ds_status_t ds_spectral_test_2d(unsigned long a, unsigned long m,
                                double *out_nu, double *out_mu) {
  /* Knuth, TAOCP 3.3.4, Algorithm S for t = 2: a Euclid-like walk over
   * the dual lattice {(u, v) : u + a v = 0 mod m}. Every coordinate stays
   * below m < 2^31 in magnitude; the squared lengths are compared in
   * double (exact below 2^53). */
  long h;
  long hp;
  long p;
  long pp;
  long q;
  long u;
  long v;
  double sq;
  if (out_nu == NULL) {
    return DS_ERR_NULL;
  }
  if (m < 2 || m >= 2147483648UL || a == 0 || a >= m) {
    return DS_ERR_INVALID;
  }
  h = (long)a;
  hp = (long)m;
  p = 1;
  pp = 0;
  sq = 1.0 + (double)a * (double)a;
  for (;;) {
    q = hp / h;
    u = hp - q * h;
    v = pp - q * p;
    if (rnd_sq2(u, v) < sq) {
      sq = rnd_sq2(u, v);
      hp = h;
      h = u;
      pp = p;
      p = v;
      if (h == 0) {
        break;
      }
      continue;
    }
    u -= h;
    v -= p;
    if (rnd_sq2(u, v) < sq) {
      sq = rnd_sq2(u, v);
    }
    break;
  }
  *out_nu = sqrt(sq);
  if (out_mu != NULL) {
    *out_mu = 3.14159265358979323846 * sq / (double)m;
  }
  return DS_OK;
}

/* ---- sampling ------------------------------------------------------- */

static double rnd_unit_open(ds_rng_t *rng) {
  /* (0, 1): never 0, so logarithms are finite */
  double u;
  do {
    u = ds_rng_double(rng);
  } while (!(u > 0.0));
  return u;
}

static size_t rnd_index(ds_rng_t *rng, size_t bound) {
  size_t j;
  if (bound - 1 <= 0xFFFFFFFFUL) {
    return (size_t)ds_rng_bounded(rng, (unsigned long)bound);
  }
  j = (size_t)(ds_rng_double(rng) * (double)bound);
  return j < bound ? j : bound - 1;
}

ds_status_t ds_reservoir_sample(ds_stream_next_func_t next, void *user,
                                size_t elem_size, size_t k, void *reservoir,
                                ds_reservoir_algorithm_t algorithm,
                                ds_rng_t *rng, size_t *out_seen,
                                ds_context_t *context) {
  unsigned char *res = (unsigned char *)reservoir;
  union {
    long l;
    double d;
    void *p;
    unsigned char b[64];
  } local;
  unsigned char *scratch = local.b;
  size_t seen = 0;
  size_t j;
  double w;
  double skip;
  ds_status_t st = DS_OK;
  if (next == NULL || rng == NULL || (k > 0 && reservoir == NULL)) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 ||
      (algorithm != DS_RESERVOIR_R && algorithm != DS_RESERVOIR_L)) {
    return DS_ERR_INVALID;
  }
  if (out_seen != NULL) {
    *out_seen = 0;
  }
  if (k == 0) {
    return DS_OK;
  }
  if (elem_size > sizeof(local)) {
    scratch = (unsigned char *)ds_context_alloc(context, elem_size);
    if (scratch == NULL) {
      return DS_ERR_ALLOC;
    }
  }
  while (seen < k) {
    st = next(res + seen * elem_size, user);
    if (st != DS_OK) {
      break;
    }
    seen++;
  }
  if (st == DS_OK && algorithm == DS_RESERVOIR_R) {
    for (;;) {
      /* the next item replaces a random slot with probability
       * k / (seen + 1) */
      j = rnd_index(rng, seen + 1);
      st = next(j < k ? res + j * elem_size : scratch, user);
      if (st != DS_OK) {
        break;
      }
      seen++;
    }
  } else if (st == DS_OK) {
    w = exp(log(rnd_unit_open(rng)) / (double)k);
    for (;;) {
      /* skip a geometric number of items, then replace a random slot */
      skip = w < 1.0 ? floor(log(rnd_unit_open(rng)) / log(1.0 - w)) : 0.0;
      while (st == DS_OK && skip >= 1.0) {
        st = next(scratch, user);
        if (st == DS_OK) {
          seen++;
          skip -= 1.0;
        }
      }
      if (st != DS_OK) {
        break;
      }
      st = next(res + rnd_index(rng, k) * elem_size, user);
      if (st != DS_OK) {
        break;
      }
      seen++;
      w *= exp(log(rnd_unit_open(rng)) / (double)k);
    }
  }
  if (scratch != local.b) {
    ds_context_free(context, scratch);
  }
  if (out_seen != NULL) {
    *out_seen = seen;
  }
  return st == DS_NOT_FOUND ? DS_OK : st;
}

ds_status_t ds_selection_sample(size_t n, size_t k, size_t *out_indices,
                                ds_rng_t *rng) {
  size_t t;
  size_t chosen = 0;
  if (rng == NULL || (k > 0 && out_indices == NULL)) {
    return DS_ERR_NULL;
  }
  if (k > n) {
    return DS_ERR_RANGE;
  }
  for (t = 0; t < n && chosen < k; t++) {
    /* select t with probability (k - chosen) / (n - t) */
    if ((double)(n - t) * ds_rng_double(rng) < (double)(k - chosen)) {
      out_indices[chosen++] = t;
    }
  }
  return DS_OK;
}

int ds_von_neumann_bit(ds_coin_func_t biased, void *state) {
  int x;
  int y;
  if (biased == NULL) {
    return 0;
  }
  for (;;) {
    x = biased(state) != 0;
    y = biased(state) != 0;
    if (x != y) {
      return x;
    }
  }
}

/* ---- Zipf distribution ------------------------------------------------------
 */

/* log(1 + x) / x and (exp(x) - 1) / x, accurate near 0 */
static double rz_helper1(double x) {
  if (fabs(x) > 1e-8) {
    return log(1.0 + x) / x;
  }
  return 1.0 - x * (0.5 - x * (1.0 / 3.0 - 0.25 * x));
}

static double rz_helper2(double x) {
  if (fabs(x) > 1e-8) {
    return (exp(x) - 1.0) / x;
  }
  return 1.0 + x * 0.5 * (1.0 + x / 3.0 * (1.0 + 0.25 * x));
}

static double rz_h(double s, double x) { return exp(-s * log(x)); }

static double rz_hint(double s, double x) {
  double lx = log(x);
  return rz_helper2((1.0 - s) * lx) * lx;
}

static double rz_hinv(double s, double x) {
  double t = x * (1.0 - s);
  if (t < -1.0) {
    t = -1.0;
  }
  return exp(rz_helper1(t) * x);
}

ds_status_t ds_zipf_init(ds_zipf_t *z, unsigned long n, double s) {
  if (z == NULL) {
    return DS_ERR_NULL;
  }
  if (n == 0 || !(s > 0.0) || !(s < HUGE_VAL)) {
    return DS_ERR_INVALID;
  }
  z->n = n;
  z->s = s;
  z->h_x1 = rz_hint(s, 1.5) - 1.0;
  z->h_n = rz_hint(s, (double)n + 0.5);
  z->squeeze = 2.0 - rz_hinv(s, rz_hint(s, 2.5) - rz_h(s, 2.0));
  z->norm = 0.0;
  return DS_OK;
}

unsigned long ds_zipf_sample(const ds_zipf_t *z, ds_rng_t *rng) {
  if (z == NULL || rng == NULL) {
    return 0;
  }
  for (;;) {
    double u = z->h_n + ds_rng_double(rng) * (z->h_x1 - z->h_n);
    double x = rz_hinv(z->s, u);
    double kd = floor(x + 0.5);
    unsigned long k;
    if (kd < 1.0) {
      kd = 1.0;
    } else if (kd > (double)z->n) {
      kd = (double)z->n;
    }
    k = (unsigned long)kd;
    if (kd - x <= z->squeeze || u >= rz_hint(z->s, kd + 0.5) - rz_h(z->s, kd)) {
      return k;
    }
  }
}

double ds_zipf_pmf(ds_zipf_t *z, unsigned long k) {
  if (z == NULL || k == 0 || k > z->n) {
    return 0.0;
  }
  if (!(z->norm > 0.0)) {
    unsigned long i;
    double sum = 0.0;
    for (i = z->n; i >= 1; i--) {
      sum += rz_h(z->s, (double)i); /* small terms first */
    }
    z->norm = sum;
  }
  return rz_h(z->s, (double)k) / z->norm;
}
