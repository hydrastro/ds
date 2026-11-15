#include "exact_sum.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define XS_M32 0xFFFFFFFFUL
#define XS_BIAS 1074 /* bit 0 counts 2^-1074 */
#define XS_TOP (DS_XSUM_WORDS - 1)
#define XS_TWO32 4294967296.0

typedef enum xs_kind { XS_FINITE, XS_NAN, XS_PINF, XS_NINF } xs_kind_t;

static xs_kind_t xs_kind(double x) {
  if (x > DBL_MAX) {
    return XS_PINF;
  }
  if (x < -DBL_MAX) {
    return XS_NINF;
  }
  return x >= -DBL_MAX ? XS_FINITE : XS_NAN; /* NaN compares false */
}

static double xs_nan(void) {
  volatile double inf = HUGE_VAL;
  return inf - inf;
}

void ds_xsum_init(ds_xsum_t *s) {
  if (s != NULL) {
    memset(s, 0, sizeof(*s));
  }
}

/* adds (or subtracts) the 96-bit value p[0] + p[1] 2^32 + p[2] 2^64 at
 * digit k, then carries (or borrows) upwards */
static void xs_put(ds_xsum_t *s, size_t k, const unsigned long *p,
                   bool negative) {
  unsigned long c = 0;
  size_t i;
  for (i = 0; i < 3; i++) {
    unsigned long a = s->w[k + i];
    unsigned long t;
    unsigned long u;
    if (!negative) {
      t = (a + p[i]) & XS_M32;
      u = (t + c) & XS_M32;
      c = (unsigned long)(t < a) + (unsigned long)(u < t);
    } else {
      t = (a - p[i]) & XS_M32;
      u = (t - c) & XS_M32;
      c = (unsigned long)(a < p[i]) + (unsigned long)(t < c);
    }
    s->w[k + i] = u;
  }
  for (i = k + 3; c != 0 && i < DS_XSUM_WORDS; i++) {
    if (!negative) {
      s->w[i] = (s->w[i] + 1) & XS_M32;
      c = (unsigned long)(s->w[i] == 0);
    } else {
      c = (unsigned long)(s->w[i] == 0);
      s->w[i] = (s->w[i] - 1) & XS_M32;
    }
  }
}

static void xs_add_finite(ds_xsum_t *s, double x, bool negate) {
  bool negative = (x < 0) != negate;
  double m;
  int e;
  long shift;
  unsigned long hi;
  unsigned long lo;
  unsigned long p[3];
  unsigned b;
  if (!(x < 0) && !(x > 0)) {
    return; /* a zero of either sign */
  }
  /* x = m 2^e with m in [1/2, 1): the integer M = m 2^53 < 2^53 counts
   * units of 2^(e - 53) */
  m = ldexp(frexp(x < 0 ? -x : x, &e), 53);
  shift = (long)e - 53 + XS_BIAS;
  if (shift < 0) {
    m = ldexp(m, (int)shift); /* a subnormal: the bits dropped are zero */
    shift = 0;
  }
  hi = (unsigned long)(m / XS_TWO32);
  lo = (unsigned long)(m - (double)hi * XS_TWO32);
  b = (unsigned)(shift % 32);
  if (b == 0) {
    p[0] = lo;
    p[1] = hi;
    p[2] = 0;
  } else {
    p[0] = (lo << b) & XS_M32;
    p[1] = ((lo >> (32 - b)) | (hi << b)) & XS_M32;
    p[2] = (hi >> (32 - b)) & XS_M32;
  }
  xs_put(s, (size_t)(shift / 32), p, negative);
}

void ds_xsum_add(ds_xsum_t *s, double x) {
  if (s == NULL) {
    return;
  }
  switch (xs_kind(x)) {
  case XS_NAN:
    s->nans++;
    break;
  case XS_PINF:
    s->pinf++;
    break;
  case XS_NINF:
    s->ninf++;
    break;
  case XS_FINITE:
  default:
    xs_add_finite(s, x, false);
    break;
  }
}

ds_status_t ds_xsum_remove(ds_xsum_t *s, double x) {
  unsigned long *count;
  if (s == NULL) {
    return DS_ERR_NULL;
  }
  switch (xs_kind(x)) {
  case XS_NAN:
    count = &s->nans;
    break;
  case XS_PINF:
    count = &s->pinf;
    break;
  case XS_NINF:
    count = &s->ninf;
    break;
  case XS_FINITE:
  default:
    xs_add_finite(s, x, true);
    return DS_OK;
  }
  if (*count == 0) {
    return DS_NOT_FOUND;
  }
  (*count)--;
  return DS_OK;
}

void ds_xsum_merge(ds_xsum_t *s, const ds_xsum_t *t) {
  unsigned long c = 0;
  size_t i;
  if (s == NULL || t == NULL) {
    return;
  }
  for (i = 0; i < DS_XSUM_WORDS; i++) {
    unsigned long a = s->w[i];
    unsigned long v = (a + t->w[i]) & XS_M32;
    unsigned long u = (v + c) & XS_M32;
    c = (unsigned long)(v < a) + (unsigned long)(u < v);
    s->w[i] = u;
  }
  s->nans += t->nans;
  s->pinf += t->pinf;
  s->ninf += t->ninf;
}

static bool xs_negative(const unsigned long *w) {
  return (w[XS_TOP] & 0x80000000UL) != 0;
}

/* |w| into out */
static void xs_magnitude(const unsigned long *w, unsigned long *out) {
  size_t i;
  if (!xs_negative(w)) {
    memcpy(out, w, sizeof(unsigned long) * DS_XSUM_WORDS);
    return;
  }
  for (i = 0; i < DS_XSUM_WORDS; i++) {
    out[i] = ~w[i] & XS_M32;
  }
  for (i = 0; i < DS_XSUM_WORDS; i++) {
    out[i] = (out[i] + 1) & XS_M32;
    if (out[i] != 0) {
      break;
    }
  }
}

/* bits [pos, pos + n) of a magnitude, n <= 32, pos >= 0 */
static unsigned long xs_bits(const unsigned long *w, size_t pos, unsigned n) {
  size_t k = pos / 32;
  unsigned off = (unsigned)(pos % 32);
  unsigned long v = w[k] >> off;
  if (off != 0 && k + 1 < DS_XSUM_WORDS) {
    v |= (w[k + 1] << (32 - off)) & XS_M32;
  }
  return n >= 32 ? v & XS_M32 : v & ((1UL << n) - 1UL);
}

/* any bit below pos */
static bool xs_sticky(const unsigned long *w, size_t pos) {
  size_t k = pos / 32;
  unsigned off = (unsigned)(pos % 32);
  size_t i;
  for (i = 0; i < k; i++) {
    if (w[i] != 0) {
      return true;
    }
  }
  return off != 0 && (w[k] & ((1UL << off) - 1UL)) != 0;
}

static unsigned xs_bitlen(unsigned long v) {
  unsigned n = 0;
  while (v != 0) {
    v >>= 1;
    n++;
  }
  return n;
}

/* the finite part rounded to nearest, ties to even */
static ds_status_t xs_finite(const ds_xsum_t *s, double *out) {
  unsigned long mag[DS_XSUM_WORDS];
  bool negative = xs_negative(s->w);
  size_t top = DS_XSUM_WORDS;
  size_t len;
  size_t q;
  unsigned long hi;
  unsigned long lo;
  double v;
  ds_status_t st = DS_OK;
  xs_magnitude(s->w, mag);
  while (top > 0 && mag[top - 1] == 0) {
    top--;
  }
  if (top == 0) {
    *out = 0.0;
    return DS_OK;
  }
  len = (top - 1) * 32 + xs_bitlen(mag[top - 1]);
  if (len <= 53) {
    /* exact: an integer below 2^53 units of 2^-1074 */
    v = ldexp((double)mag[1] * XS_TWO32 + (double)mag[0], -XS_BIAS);
  } else {
    q = len - 53; /* the lowest bit kept */
    lo = xs_bits(mag, q, 32);
    hi = xs_bits(mag, q + 32, 21);
    if (xs_bits(mag, q - 1, 1) != 0 &&
        (xs_sticky(mag, q - 1) || (lo & 1UL) != 0)) {
      lo = (lo + 1) & XS_M32;
      if (lo == 0) {
        hi++;
        if (hi == (1UL << 21)) { /* 2^53: one more binade */
          hi = 1UL << 20;
          q++;
        }
      }
    }
    if (q > (size_t)(1024 - 53 + XS_BIAS)) {
      v = HUGE_VAL;
      st = DS_ERR_RANGE;
    } else {
      v = ldexp((double)hi * XS_TWO32 + (double)lo, (int)q - XS_BIAS);
    }
  }
  *out = negative ? -v : v;
  return st;
}

ds_status_t ds_xsum_round(const ds_xsum_t *s, double *out) {
  if (s == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (s->nans != 0 || (s->pinf != 0 && s->ninf != 0)) {
    *out = xs_nan();
    return DS_OK;
  }
  if (s->pinf != 0) {
    *out = HUGE_VAL;
    return DS_OK;
  }
  if (s->ninf != 0) {
    *out = -HUGE_VAL;
    return DS_OK;
  }
  return xs_finite(s, out);
}

double ds_xsum_value(const ds_xsum_t *s) {
  double v = 0.0;
  if (s != NULL) {
    (void)ds_xsum_round(s, &v);
  }
  return v;
}

int ds_xsum_sign(const ds_xsum_t *s) {
  size_t i;
  if (s == NULL) {
    return 0;
  }
  if (xs_negative(s->w)) {
    return -1;
  }
  for (i = 0; i < DS_XSUM_WORDS; i++) {
    if (s->w[i] != 0) {
      return 1;
    }
  }
  return 0;
}

int ds_xsum_compare(const ds_xsum_t *a, const ds_xsum_t *b) {
  bool na;
  bool nb;
  size_t i;
  if (a == NULL || b == NULL) {
    return a == b ? 0 : (a == NULL ? -1 : 1);
  }
  na = xs_negative(a->w);
  nb = xs_negative(b->w);
  if (na != nb) {
    return na ? -1 : 1;
  }
  /* same sign: two's complement digits compare as unsigned */
  for (i = DS_XSUM_WORDS; i-- > 0;) {
    if (a->w[i] != b->w[i]) {
      return a->w[i] < b->w[i] ? -1 : 1;
    }
  }
  return 0;
}

bool ds_xsum_is_zero(const ds_xsum_t *s) {
  return s != NULL && s->nans == 0 && s->pinf == 0 && s->ninf == 0 &&
         ds_xsum_sign(s) == 0;
}

double ds_fsum(const double *x, size_t n) {
  ds_xsum_t s;
  size_t i;
  ds_xsum_init(&s);
  for (i = 0; i < n && x != NULL; i++) {
    ds_xsum_add(&s, x[i]);
  }
  return ds_xsum_value(&s);
}
