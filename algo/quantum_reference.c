#include "quantum_reference.h"
#include "../lib/checked.h"
#include "numeric.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#define QR_PI 3.14159265358979323846264338327950288
#define QR_INVSQRT2 0.70710678118654752440084436210484904

void ds_qref_config_init(ds_qref_config_t *c) {
  if (c == NULL)
    return;
  memset(c, 0, sizeof(*c));
  c->max_states = 65536;
  c->max_shots = 64;
  c->max_iterations = 65536;
}
static void qr_config(ds_qref_config_t *c, const ds_qref_config_t *input,
                      ds_rng_t *local) {
  if (input == NULL)
    ds_qref_config_init(c);
  else
    *c = *input;
  if (c->rng == NULL) {
    ds_rng_seed(local, 0x51524546UL);
    c->rng = local;
  }
}
static ds_status_t qr_count(unsigned int bits, const ds_qref_config_t *c,
                            size_t *out) {
  size_t count;
  if (bits >= sizeof(size_t) * CHAR_BIT)
    return DS_ERR_RANGE;
  if (bits >= sizeof(unsigned long) * CHAR_BIT)
    return DS_ERR_RANGE;
  count = (size_t)1 << bits;
  if (count > c->max_states)
    return DS_ERR_RANGE;
  *out = count;
  return DS_OK;
}
/* ds_rng_bounded consumes a 32-bit draw. Extend it for larger word bounds. */
static unsigned long qr_bounded(ds_rng_t *rng, unsigned long bound) {
  unsigned long x, threshold;
  if (bound <= 0xFFFFFFFFUL)
    return ds_rng_bounded(rng, bound);
  threshold = (0UL - bound) % bound;
  do {
    unsigned long chunk = 0;
    unsigned int bit, left = 0;
    x = 0;
    for (bit = 0; bit < sizeof(unsigned long) * CHAR_BIT; bit++) {
      if (left == 0) {
        chunk = ds_rng_next(rng);
        left = 32;
      }
      x = (x << 1) | (chunk & 1UL);
      chunk >>= 1;
      left--;
    }
  } while (x < threshold);
  return x % bound;
}
static ds_status_t qr_state(size_t count, ds_context_t *ctx,
                            ds_complex_t **out) {
  size_t bytes;
  if (!ds_size_mul(count, sizeof(ds_complex_t), &bytes))
    return DS_ERR_OVERFLOW;
  *out = (ds_complex_t *)ds_context_alloc(ctx, bytes);
  return *out == NULL ? DS_ERR_ALLOC : DS_OK;
}
static ds_status_t qr_table(size_t count, size_t limit,
                            ds_qref_oracle_fn oracle, void *user,
                            ds_context_t *ctx, size_t **out) {
  size_t i, bytes, *table;
  ds_status_t st;
  *out = NULL;
  if (oracle == NULL)
    return DS_ERR_NULL;
  if (!ds_size_mul(count, sizeof(size_t), &bytes))
    return DS_ERR_OVERFLOW;
  table = (size_t *)ds_context_alloc(ctx, bytes);
  if (table == NULL)
    return DS_ERR_ALLOC;
  for (i = 0; i < count; i++) {
    st = oracle(i, &table[i], user);
    if (st != DS_OK || table[i] >= limit) {
      ds_context_free(ctx, table);
      return st != DS_OK ? st : DS_ERR_INVALID;
    }
  }
  *out = table;
  return DS_OK;
}

ds_status_t ds_qref_hadamard(ds_complex_t *state, size_t n) {
  size_t step, start, j;
  if (state == NULL)
    return DS_ERR_NULL;
  if (n == 0 || (n & (n - 1)) != 0)
    return DS_ERR_INVALID;
  for (step = 1; step < n; step *= 2) {
    for (start = 0; start < n; start += 2 * step) {
      for (j = 0; j < step; j++) {
        ds_complex_t *a = &state[start + j], *b = &state[start + j + step];
        double re = a->re, im = a->im;
        a->re = (re + b->re) * QR_INVSQRT2;
        a->im = (im + b->im) * QR_INVSQRT2;
        b->re = (re - b->re) * QR_INVSQRT2;
        b->im = (im - b->im) * QR_INVSQRT2;
      }
    }
  }
  return DS_OK;
}
static double qr_weight(const ds_complex_t *x) {
  return x->re * x->re + x->im * x->im;
}
ds_status_t ds_qref_measure(const ds_complex_t *state, size_t n, ds_rng_t *rng,
                            size_t *out) {
  size_t i, last = 0;
  double total = 0.0, target;
  if (state == NULL || rng == NULL || out == NULL)
    return DS_ERR_NULL;
  if (n == 0)
    return DS_ERR_INVALID;
  for (i = 0; i < n; i++) {
    double weight = qr_weight(&state[i]);
    if (!(weight >= 0.0 && weight <= DBL_MAX) || total > DBL_MAX - weight)
      return DS_ERR_INVALID;
    total += weight;
    if (weight > 0.0)
      last = i;
  }
  if (!(total > 0.0))
    return DS_ERR_INVALID;
  target = ds_rng_double(rng) * total;
  for (i = 0; i < n; i++) {
    double weight = qr_weight(&state[i]);
    if (target < weight) {
      *out = i;
      return DS_OK;
    }
    target -= weight;
  }
  *out = last; /* rounding at the upper endpoint */
  return DS_OK;
}

ds_status_t ds_qref_deutsch_jozsa(unsigned int bits, ds_qref_oracle_fn oracle,
                                  void *user, const ds_qref_config_t *config,
                                  bool *out_constant, double *out_probability) {
  ds_qref_config_t c;
  ds_rng_t local;
  ds_complex_t *state = NULL;
  size_t n, i, ones = 0, *table = NULL;
  ds_status_t st;
  double scale, probability;
  if (out_constant == NULL)
    return DS_ERR_NULL;
  qr_config(&c, config, &local);
  st = qr_count(bits, &c, &n);
  if (st != DS_OK)
    return st;
  st = qr_table(n, 2, oracle, user, c.context, &table);
  if (st != DS_OK)
    return st;
  for (i = 0; i < n; i++)
    ones += table[i];
  if (ones != 0 && ones != n && ones != n / 2) {
    st = DS_ERR_INVALID;
    goto done;
  }
  st = qr_state(n, c.context, &state);
  if (st != DS_OK)
    goto done;
  scale = 1.0 / sqrt((double)n);
  for (i = 0; i < n; i++) {
    state[i].re = table[i] ? -scale : scale;
    state[i].im = 0.0;
  }
  st = ds_qref_hadamard(state, n);
  if (st != DS_OK)
    goto done;
  probability = qr_weight(&state[0]);
  *out_constant = probability > 0.5;
  if (out_probability != NULL)
    *out_probability = probability;
done:
  ds_context_free(c.context, state);
  ds_context_free(c.context, table);
  return st;
}

ds_status_t ds_qref_grover(unsigned int bits, ds_qref_oracle_fn oracle,
                           void *user, size_t iterations,
                           const ds_qref_config_t *config, size_t *out_index,
                           bool *out_marked, double *out_probability) {
  ds_qref_config_t c;
  ds_rng_t local;
  ds_complex_t *state = NULL;
  size_t n, i, k, marked = 0, selected, *table = NULL;
  double scale, success = 0.0, total = 0.0;
  ds_status_t st;
  if (out_index == NULL || out_marked == NULL)
    return DS_ERR_NULL;
  qr_config(&c, config, &local);
  st = qr_count(bits, &c, &n);
  if (st != DS_OK)
    return st;
  st = qr_table(n, 2, oracle, user, c.context, &table);
  if (st != DS_OK)
    return st;
  for (i = 0; i < n; i++)
    marked += table[i];
  if (marked == 0) {
    st = DS_NOT_FOUND;
    goto done;
  }
  if (iterations == DS_QREF_AUTO) {
    double optimal =
        floor(QR_PI / (4.0 * asin(sqrt((double)marked / (double)n))));
    iterations = (size_t)optimal;
  }
  if (iterations > c.max_iterations) {
    st = DS_ERR_RANGE;
    goto done;
  }
  st = qr_state(n, c.context, &state);
  if (st != DS_OK)
    goto done;
  scale = 1.0 / sqrt((double)n);
  for (i = 0; i < n; i++) {
    state[i].re = scale;
    state[i].im = 0.0;
  }
  for (k = 0; k < iterations; k++) {
    double average = 0.0;
    for (i = 0; i < n; i++) {
      if (table[i])
        state[i].re = -state[i].re;
      average += state[i].re;
    }
    average /= (double)n;
    for (i = 0; i < n; i++)
      state[i].re = 2.0 * average - state[i].re;
  }
  st = ds_qref_measure(state, n, c.rng, &selected);
  if (st != DS_OK)
    goto done;
  for (i = 0; i < n; i++) {
    double weight = qr_weight(&state[i]);
    total += weight;
    if (table[i])
      success += weight;
  }
  *out_index = selected;
  *out_marked = table[selected] != 0;
  if (out_probability != NULL)
    *out_probability = success / total;
done:
  ds_context_free(c.context, state);
  ds_context_free(c.context, table);
  return st;
}

static unsigned int qr_parity(size_t x) {
  unsigned int result = 0;
  while (x != 0) {
    result ^= 1U;
    x &= x - 1;
  }
  return result;
}
ds_status_t ds_qref_simon(unsigned int bits, ds_qref_oracle_fn oracle,
                          void *user, const ds_qref_config_t *config,
                          size_t *out_mask) {
  ds_qref_config_t c;
  ds_rng_t local;
  ds_complex_t *state = NULL;
  size_t basis[sizeof(size_t) * CHAR_BIT];
  size_t n, i, shot, rank = 0, pairs = 0, *table = NULL;
  unsigned char *counts = NULL;
  ds_status_t st;
  if (out_mask == NULL)
    return DS_ERR_NULL;
  if (bits == 0)
    return DS_ERR_RANGE;
  qr_config(&c, config, &local);
  st = qr_count(bits, &c, &n);
  if (st != DS_OK)
    return st;
  st = qr_table(n, n, oracle, user, c.context, &table);
  if (st != DS_OK)
    return st;
  counts = (unsigned char *)ds_context_alloc(c.context, n);
  if (counts == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  memset(counts, 0, n);
  memset(basis, 0, sizeof(basis));
  for (i = 0; i < n; i++) {
    if (++counts[table[i]] > 2) {
      st = DS_ERR_INVALID;
      goto done;
    }
    if (counts[table[i]] == 2)
      pairs++;
  }
  if (pairs != 0 && pairs != n / 2) {
    st = DS_ERR_INVALID;
    goto done;
  }
  st = qr_state(n, c.context, &state);
  if (st != DS_OK)
    goto done;
  for (shot = 0; shot < c.max_shots; shot++) {
    size_t output = table[qr_bounded(c.rng, (unsigned long)n)], y;
    unsigned int bit;
    double scale = 1.0 / sqrt((double)counts[output]);
    for (i = 0; i < n; i++) {
      state[i].re = table[i] == output ? scale : 0.0;
      state[i].im = 0.0;
    }
    st = ds_qref_hadamard(state, n);
    if (st == DS_OK)
      st = ds_qref_measure(state, n, c.rng, &y);
    if (st != DS_OK)
      goto done;
    for (bit = bits; bit > 0; bit--) {
      size_t mask = (size_t)1 << (bit - 1);
      if ((y & mask) != 0) {
        if (basis[bit - 1] != 0)
          y ^= basis[bit - 1];
        else {
          basis[bit - 1] = y;
          rank++;
          break;
        }
      }
    }
    if (pairs == 0 && rank == bits) {
      *out_mask = 0;
      st = DS_OK;
      goto done;
    }
    if (pairs > 0 && rank == bits - 1) {
      size_t mask = 0;
      for (bit = 0; bit < bits; bit++)
        if (basis[bit] == 0) {
          mask = (size_t)1 << bit;
          break;
        }
      for (bit = 0; bit < bits; bit++)
        if (basis[bit] != 0 && qr_parity(basis[bit] & mask))
          mask |= (size_t)1 << bit;
      for (i = 0; i < n; i++)
        if (table[i] != table[i ^ mask]) {
          st = DS_ERR_INVALID;
          goto done;
        }
      *out_mask = mask;
      st = DS_OK;
      goto done;
    }
    if (pairs > 0 && rank == bits) {
      st = DS_ERR_INVALID;
      goto done;
    }
  }
  st = DS_STOP;
done:
  ds_context_free(c.context, state);
  ds_context_free(c.context, counts);
  ds_context_free(c.context, table);
  return st;
}

static ds_status_t qr_order_size(unsigned long modulus,
                                 const ds_qref_config_t *c, size_t *out) {
  unsigned long square, q = 1;
  if (modulus > ULONG_MAX / modulus)
    return DS_ERR_RANGE;
  square = modulus * modulus;
  while (q < square) {
    if (q > ULONG_MAX / 2)
      return DS_ERR_RANGE;
    q *= 2;
  }
  if (q > (size_t)-1 || (size_t)q > c->max_states)
    return DS_ERR_RANGE;
  *out = (size_t)q;
  return DS_OK;
}
static unsigned long qr_reduce_order(unsigned long base, unsigned long modulus,
                                     unsigned long order) {
  unsigned long p, remaining = order;
  for (p = 2; p <= remaining / p; p++) {
    if (remaining % p == 0) {
      while (remaining % p == 0)
        remaining /= p;
      while (order % p == 0 && ds_powmod(base, order / p, modulus) == 1)
        order /= p;
    }
  }
  if (remaining > 1)
    while (order % remaining == 0 &&
           ds_powmod(base, order / remaining, modulus) == 1)
      order /= remaining;
  return order;
}
static ds_status_t qr_order_once(unsigned long base, unsigned long modulus,
                                 ds_complex_t *state, size_t q,
                                 ds_qref_config_t *c, unsigned long *out) {
  size_t i, count = 0, phase;
  unsigned long measured =
      ds_powmod(base, qr_bounded(c->rng, (unsigned long)q), modulus);
  unsigned long value = 1, num, den, previous = 1, current = 0;
  ds_status_t st;
  double scale;
  for (i = 0; i < q; i++) {
    state[i].re = value == measured ? 1.0 : 0.0;
    state[i].im = 0.0;
    if (value == measured)
      count++;
    value = ds_mulmod(value, base, modulus);
  }
  if (count == 0)
    return DS_ERR_INTERNAL;
  scale = 1.0 / sqrt((double)count);
  for (i = 0; i < q; i++)
    state[i].re *= scale;
  /* Forward versus inverse changes conjugate phases, not probabilities of
   * the real collapsed state. Normalize the library's forward FFT by sqrt Q. */
  st = ds_fft(state, q, false, c->context);
  if (st != DS_OK)
    return st;
  scale = 1.0 / sqrt((double)q);
  for (i = 0; i < q; i++) {
    state[i].re *= scale;
    state[i].im *= scale;
  }
  st = ds_qref_measure(state, q, c->rng, &phase);
  if (st != DS_OK)
    return st;
  num = (unsigned long)phase;
  den = (unsigned long)q;
  while (den != 0) {
    unsigned long quotient = num / den, remainder = num % den, next;
    if (current != 0 && quotient > (modulus - previous) / current)
      break;
    next = quotient * current + previous;
    if (next > modulus)
      break;
    if (next != 0 && ds_powmod(base, next, modulus) == 1) {
      *out = qr_reduce_order(base, modulus, next);
      return DS_OK;
    }
    previous = current;
    current = next;
    num = den;
    den = remainder;
  }
  return DS_STOP;
}

ds_status_t ds_qref_shor_order(unsigned long base, unsigned long modulus,
                               const ds_qref_config_t *config,
                               unsigned long *out) {
  ds_qref_config_t c;
  ds_rng_t local;
  ds_complex_t *state = NULL;
  size_t q, shot;
  ds_status_t st;
  unsigned long result;
  if (out == NULL)
    return DS_ERR_NULL;
  if (modulus < 2)
    return DS_ERR_RANGE;
  base %= modulus;
  if (ds_gcd(base, modulus) != 1)
    return DS_ERR_INVALID;
  if (base == 1) {
    *out = 1;
    return DS_OK;
  }
  qr_config(&c, config, &local);
  st = qr_order_size(modulus, &c, &q);
  if (st != DS_OK)
    return st;
  st = qr_state(q, c.context, &state);
  if (st != DS_OK)
    return st;
  st = DS_STOP;
  for (shot = 0; shot < c.max_shots; shot++) {
    st = qr_order_once(base, modulus, state, q, &c, &result);
    if (st != DS_STOP)
      break;
  }
  if (st == DS_OK)
    *out = result;
  ds_context_free(c.context, state);
  return st;
}
static unsigned long qr_power_factor(unsigned long n) {
  unsigned int exponent;
  for (exponent = 2; exponent < sizeof(unsigned long) * CHAR_BIT; exponent++) {
    unsigned long low = 2, high = n / 2;
    while (low <= high) {
      unsigned long mid = low + (high - low) / 2, product = 1;
      unsigned int i;
      for (i = 0; i < exponent; i++) {
        if (product > n / mid)
          break;
        product *= mid;
      }
      if (i == exponent && product == n)
        return mid;
      if (i != exponent || product > n)
        high = mid - 1;
      else
        low = mid + 1;
    }
  }
  return 0;
}
ds_status_t ds_qref_shor_factor(unsigned long n, const ds_qref_config_t *config,
                                unsigned long *out) {
  ds_qref_config_t c;
  ds_rng_t local;
  ds_complex_t *state = NULL;
  size_t q, shot;
  unsigned long factor;
  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  if (n < 2)
    return DS_ERR_RANGE;
  if (ds_is_prime(n))
    return DS_NOT_FOUND;
  if (n % 2 == 0) {
    *out = 2;
    return DS_OK;
  }
  factor = qr_power_factor(n);
  if (factor != 0) {
    *out = factor;
    return DS_OK;
  }
  qr_config(&c, config, &local);
  st = qr_order_size(n, &c, &q);
  if (st != DS_OK)
    return st;
  st = qr_state(q, c.context, &state);
  if (st != DS_OK)
    return st;
  st = DS_STOP;
  for (shot = 0; shot < c.max_shots; shot++) {
    unsigned long base = 2 + qr_bounded(c.rng, n - 3), order, root;
    factor = ds_gcd(base, n);
    if (factor > 1 && factor < n) {
      st = DS_OK;
      break;
    }
    st = qr_order_once(base, n, state, q, &c, &order);
    if (st == DS_STOP)
      continue;
    if (st != DS_OK)
      break;
    st = DS_STOP;
    if (order % 2 != 0)
      continue;
    root = ds_powmod(base, order / 2, n);
    factor = ds_gcd(root - 1, n);
    if (factor > 1 && factor < n) {
      st = DS_OK;
      break;
    }
    factor = ds_gcd(root + 1, n);
    if (factor > 1 && factor < n) {
      st = DS_OK;
      break;
    }
  }
  if (st == DS_OK)
    *out = factor;
  ds_context_free(c.context, state);
  return st;
}
