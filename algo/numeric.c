#include "numeric.h"
#include "../lib/checked.h"
#include <limits.h>
#include <string.h>

#define NUM_HALF_BITS ((unsigned int)(CHAR_BIT * sizeof(unsigned long) / 2))
#define NUM_HALF_LIMIT (1UL << NUM_HALF_BITS)

unsigned long ds_gcd(unsigned long a, unsigned long b) {
  unsigned long t;
  while (b != 0) {
    t = a % b;
    a = b;
    b = t;
  }
  return a;
}

unsigned long ds_binary_gcd(unsigned long a, unsigned long b) {
  unsigned int shift = 0;
  unsigned long t;
  if (a == 0) {
    return b;
  }
  if (b == 0) {
    return a;
  }
  while (((a | b) & 1UL) == 0) {
    a >>= 1;
    b >>= 1;
    shift++;
  }
  while ((a & 1UL) == 0) {
    a >>= 1;
  }
  do {
    while ((b & 1UL) == 0) {
      b >>= 1;
    }
    if (a > b) {
      t = a;
      a = b;
      b = t;
    }
    b -= a;
  } while (b != 0);
  return a << shift;
}

ds_status_t ds_lcm(unsigned long a, unsigned long b, unsigned long *out) {
  unsigned long q;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  if (a == 0 || b == 0) {
    *out = 0;
    return DS_OK;
  }
  q = a / ds_gcd(a, b);
  if (q > ULONG_MAX / b) {
    return DS_ERR_OVERFLOW;
  }
  *out = q * b;
  return DS_OK;
}

long ds_ext_gcd(long a, long b, long *out_x, long *out_y) {
  long old_r = a;
  long r = b;
  long old_s = 1;
  long s = 0;
  long old_t = 0;
  long t = 1;
  long q;
  long tmp;
  while (r != 0) {
    q = old_r / r;
    tmp = old_r - q * r;
    old_r = r;
    r = tmp;
    tmp = old_s - q * s;
    old_s = s;
    s = tmp;
    tmp = old_t - q * t;
    old_t = t;
    t = tmp;
  }
  if (old_r < 0) {
    old_r = -old_r;
    old_s = -old_s;
    old_t = -old_t;
  }
  if (out_x != NULL) {
    *out_x = old_s;
  }
  if (out_y != NULL) {
    *out_y = old_t;
  }
  return old_r;
}

unsigned long ds_mulmod(unsigned long a, unsigned long b, unsigned long m) {
  unsigned long result = 0;
  if (m == 0) {
    return 0;
  }
  a %= m;
  b %= m;
  if (a < NUM_HALF_LIMIT && b < NUM_HALF_LIMIT) {
    return (a * b) % m; /* the product fits */
  }
  /* Double-and-add, never exceeding m on either side of an addition. */
  while (b != 0) {
    if ((b & 1UL) != 0) {
      result = result >= m - a ? result - (m - a) : result + a;
    }
    a = a >= m - a ? a - (m - a) : a + a;
    b >>= 1;
  }
  return result;
}

unsigned long ds_powmod(unsigned long base, unsigned long exponent,
                        unsigned long m) {
  unsigned long result;
  if (m == 0) {
    return 0;
  }
  result = 1UL % m;
  base %= m;
  while (exponent != 0) {
    if ((exponent & 1UL) != 0) {
      result = ds_mulmod(result, base, m);
    }
    base = ds_mulmod(base, base, m);
    exponent >>= 1;
  }
  return result;
}

ds_status_t ds_modinv(unsigned long a, unsigned long m, unsigned long *out) {
  unsigned long t = 0;
  unsigned long new_t = 1;
  unsigned long r = m;
  unsigned long new_r;
  unsigned long q;
  unsigned long tmp;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  if (m < 2) {
    return DS_ERR_INVALID;
  }
  new_r = a % m;
  /* Coefficients are kept reduced mod m, so nothing goes negative. */
  while (new_r != 0) {
    q = r / new_r;
    tmp = ds_mulmod(q, new_t, m);
    tmp = t >= tmp ? t - tmp : t + (m - tmp); /* (t - q * new_t) mod m */
    t = new_t;
    new_t = tmp;
    tmp = r - q * new_r;
    r = new_r;
    new_r = tmp;
  }
  if (r != 1) {
    return DS_NOT_FOUND; /* not invertible */
  }
  *out = t;
  return DS_OK;
}

unsigned long ds_isqrt(unsigned long n) {
  unsigned long x;
  unsigned long y;
  if (n < 2) {
    return n;
  }
  /* Newton's method from an over-estimate converges from above. */
  x = n;
  y = x / 2 + 1;
  if (y < x) {
    x = y;
  }
  for (;;) {
    y = (x + n / x) / 2;
    if (y >= x) {
      return x;
    }
    x = y;
  }
}

static bool num_miller_rabin(unsigned long n, unsigned long a, unsigned long d,
                             unsigned int s) {
  unsigned long x;
  unsigned int r;
  a %= n;
  if (a == 0) {
    return true;
  }
  x = ds_powmod(a, d, n);
  if (x == 1 || x == n - 1) {
    return true;
  }
  for (r = 1; r < s; r++) {
    x = ds_mulmod(x, x, n);
    if (x == n - 1) {
      return true;
    }
  }
  return false;
}

bool ds_is_prime(unsigned long n) {
  static const unsigned long small[] = {2,  3,  5,  7,  11, 13,
                                        17, 19, 23, 29, 31, 37};
  static const unsigned long base32[] = {2, 7, 61};
  static const unsigned long base64[] = {2,      325,     9375,      28178,
                                         450775, 9780504, 1795265022};
  const unsigned long *bases;
  size_t nbases;
  unsigned long d;
  unsigned int s = 0;
  size_t i;
  if (n < 2) {
    return false;
  }
  for (i = 0; i < sizeof(small) / sizeof(small[0]); i++) {
    if (n == small[i]) {
      return true;
    }
    if (n % small[i] == 0) {
      return false;
    }
  }
  d = n - 1;
  while ((d & 1UL) == 0) {
    d >>= 1;
    s++;
  }
  if (!DS_ABOVE_U32(n)) {
    bases = base32;
    nbases = 3;
  } else {
    bases = base64;
    nbases = 7;
  }
  for (i = 0; i < nbases; i++) {
    if (!num_miller_rabin(n, bases[i], d, s)) {
      return false;
    }
  }
  return true;
}

size_t ds_sieve(unsigned char *is_prime, size_t n) {
  size_t i;
  size_t j;
  size_t count = 0;
  if (is_prime == NULL || n == 0) {
    return 0;
  }
  memset(is_prime, 1, n);
  is_prime[0] = 0;
  if (n > 1) {
    is_prime[1] = 0;
  }
  for (i = 4; i < n; i += 2) {
    is_prime[i] = 0;
  }
  for (i = 3; i <= n / i; i += 2) {
    if (is_prime[i]) {
      for (j = i * i; j < n; j += 2 * i) {
        is_prime[j] = 0;
      }
    }
  }
  for (i = 0; i < n; i++) {
    count += is_prime[i];
  }
  return count;
}

double ds_kahan_sum(const double *values, size_t count) {
  double sum = 0.0;
  double compensation = 0.0;
  double t;
  size_t i;
  if (values == NULL) {
    return 0.0;
  }
  for (i = 0; i < count; i++) {
    t = sum + values[i];
    if ((sum >= 0 ? sum : -sum) >= (values[i] >= 0 ? values[i] : -values[i])) {
      compensation += (sum - t) + values[i];
    } else {
      compensation += (values[i] - t) + sum;
    }
    sum = t;
  }
  return sum + compensation;
}

void ds_welford_init(ds_welford_t *w) {
  w->count = 0;
  w->mean = 0.0;
  w->m2 = 0.0;
}

void ds_welford_add(ds_welford_t *w, double x) {
  double delta;
  w->count++;
  delta = x - w->mean;
  w->mean += delta / (double)w->count;
  w->m2 += delta * (x - w->mean);
}

double ds_welford_mean(const ds_welford_t *w) { return w->mean; }

double ds_welford_variance(const ds_welford_t *w) {
  return w->count == 0 ? 0.0 : w->m2 / (double)w->count;
}

double ds_welford_sample_variance(const ds_welford_t *w) {
  return w->count < 2 ? 0.0 : w->m2 / (double)(w->count - 1);
}
