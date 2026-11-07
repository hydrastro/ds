#include "lattice.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static double lt_dot(const double *a, const double *b, size_t m) {
  size_t i;
  double s = 0.0;
  for (i = 0; i < m; i++) {
    s += a[i] * b[i];
  }
  return s;
}

static double lt_round(double x) { return floor(x + 0.5); }

/* Gram-Schmidt: bstar (n x m), mu (n x n), b2 (n) */
static void lt_gram_schmidt(const double *b, size_t n, size_t m, double *bs,
                            double *mu, double *b2) {
  size_t i;
  size_t j;
  size_t k;
  for (i = 0; i < n; i++) {
    memcpy(bs + i * m, b + i * m, m * sizeof(double));
    for (j = 0; j < i; j++) {
      mu[i * n + j] =
          b2[j] > 0.0 ? lt_dot(b + i * m, bs + j * m, m) / b2[j] : 0.0;
      for (k = 0; k < m; k++) {
        bs[i * m + k] -= mu[i * n + j] * bs[j * m + k];
      }
    }
    b2[i] = lt_dot(bs + i * m, bs + i * m, m);
  }
}

ds_status_t ds_lll_reduce(double *basis, size_t n, size_t m, double delta,
                          ds_context_t *context) {
  double *bs;
  double *mu;
  double *b2;
  size_t k = 1;
  size_t guard = 0;
  size_t i;
  size_t j;
  if (basis == NULL) {
    return DS_ERR_NULL;
  }
  if (!(delta > 0.0)) {
    delta = 0.75;
  }
  if (n == 0 || m == 0 || !(delta > 0.25) || !(delta < 1.0)) {
    return DS_ERR_INVALID;
  }
  bs = (double *)ds_context_alloc(context, n * m * sizeof(double));
  mu = (double *)ds_context_alloc(context, n * n * sizeof(double));
  b2 = (double *)ds_context_alloc(context, n * sizeof(double));
  if (bs == NULL || mu == NULL || b2 == NULL) {
    ds_context_free(context, bs);
    ds_context_free(context, mu);
    ds_context_free(context, b2);
    return DS_ERR_ALLOC;
  }
  lt_gram_schmidt(basis, n, m, bs, mu, b2);
  for (i = 0; i < n; i++) {
    if (!(b2[i] > 0.0)) {
      ds_context_free(context, bs);
      ds_context_free(context, mu);
      ds_context_free(context, b2);
      return DS_ERR_INVALID; /* dependent rows */
    }
  }
  while (k < n && guard++ < 1000000) {
    /* size reduction of row k */
    for (j = k; j-- > 0;) {
      double q = lt_round(mu[k * n + j]);
      if (q > 0.5 || q < -0.5) {
        for (i = 0; i < m; i++) {
          basis[k * m + i] -= q * basis[j * m + i];
        }
        for (i = 0; i <= j; i++) {
          mu[k * n + i] -= q * (i == j ? 1.0 : mu[j * n + i]);
        }
      }
    }
    if (b2[k] >= (delta - mu[k * n + k - 1] * mu[k * n + k - 1]) * b2[k - 1]) {
      k++;
    } else {
      for (i = 0; i < m; i++) {
        double t = basis[k * m + i];
        basis[k * m + i] = basis[(k - 1) * m + i];
        basis[(k - 1) * m + i] = t;
      }
      lt_gram_schmidt(basis, n, m, bs, mu, b2);
      k = k > 1 ? k - 1 : 1;
    }
  }
  ds_context_free(context, bs);
  ds_context_free(context, mu);
  ds_context_free(context, b2);
  return k >= n ? DS_OK : DS_ERR_STATE;
}

bool ds_lll_is_reduced(const double *basis, size_t n, size_t m, double delta,
                       ds_context_t *context) {
  double *bs;
  double *mu;
  double *b2;
  size_t i;
  size_t j;
  bool ok = true;
  if (basis == NULL || n == 0 || m == 0) {
    return false;
  }
  if (!(delta > 0.25) || !(delta < 1.0)) {
    delta = 0.75;
  }
  bs = (double *)ds_context_alloc(context, n * m * sizeof(double));
  mu = (double *)ds_context_alloc(context, n * n * sizeof(double));
  b2 = (double *)ds_context_alloc(context, n * sizeof(double));
  if (bs == NULL || mu == NULL || b2 == NULL) {
    ds_context_free(context, bs);
    ds_context_free(context, mu);
    ds_context_free(context, b2);
    return false;
  }
  lt_gram_schmidt(basis, n, m, bs, mu, b2);
  for (i = 1; i < n && ok; i++) {
    for (j = 0; j < i && ok; j++) {
      ok = fabs(mu[i * n + j]) <= 0.5 + 1e-9;
    }
    ok = ok && b2[i] >= (delta - mu[i * n + i - 1] * mu[i * n + i - 1]) *
                            b2[i - 1] * (1.0 - 1e-9);
  }
  ds_context_free(context, bs);
  ds_context_free(context, mu);
  ds_context_free(context, b2);
  return ok;
}

ds_status_t ds_lattice_shortest(const double *basis, size_t n, size_t m,
                                double *out, double *out_norm2,
                                ds_context_t *context) {
  double *b;
  double *bs;
  double *mu;
  double *b2;
  double x[32];
  double c[32];
  double l[33];
  double best[32];
  double base[32];
  double best_len;
  long step[32];
  size_t i;
  size_t k;
  ds_status_t st;
  if (basis == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (n == 0 || n > 32 || m == 0) {
    return DS_ERR_INVALID;
  }
  b = (double *)ds_context_alloc(context, n * m * sizeof(double));
  bs = (double *)ds_context_alloc(context, n * m * sizeof(double));
  mu = (double *)ds_context_alloc(context, n * n * sizeof(double));
  b2 = (double *)ds_context_alloc(context, n * sizeof(double));
  if (b == NULL || bs == NULL || mu == NULL || b2 == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  memcpy(b, basis, n * m * sizeof(double));
  st = ds_lll_reduce(b, n, m, 0.99, context);
  if (st != DS_OK) {
    goto done;
  }
  lt_gram_schmidt(b, n, m, bs, mu, b2);
  /* Schnorr-Euchner enumeration: at each level the candidates zig-zag
   * outward from the rounded centre, so their contributions never shrink
   * and a level ends at the first one over the bound. The top coordinate
   * only takes 0, 1, 2, ... (v and -v are the same length). */
  best_len = b2[0] * (1.0 + 1e-12);
  for (i = 0; i < n; i++) {
    best[i] = i == 0 ? 1.0 : 0.0;
    x[i] = c[i] = base[i] = 0.0;
    step[i] = 0;
  }
  l[n] = 0.0;
  k = n - 1;
  for (;;) {
    double v;
    double diff;
    double lk;
    if (k == n - 1) {
      v = (double)step[k];
    } else {
      long j = step[k];
      double sgn = c[k] >= base[k] ? 1.0 : -1.0;
      double z =
          (j % 2 == 1) ? (double)((j + 1) / 2) * sgn : -(double)(j / 2) * sgn;
      v = base[k] + z;
    }
    diff = v - c[k];
    lk = l[k + 1] + diff * diff * b2[k];
    if (lk < best_len) {
      x[k] = v;
      l[k] = lk;
      if (k == 0) {
        bool nonzero = false;
        for (i = 0; i < n; i++) {
          nonzero = nonzero || x[i] > 0.5 || x[i] < -0.5;
        }
        if (nonzero) {
          best_len = lk;
          memcpy(best, x, n * sizeof(double));
        }
        step[0]++;
        continue;
      }
      k--;
      c[k] = 0.0;
      for (i = k + 1; i < n; i++) {
        c[k] -= x[i] * mu[i * n + k];
      }
      base[k] = lt_round(c[k]);
      step[k] = 0;
      continue;
    }
    if (k == n - 1) {
      break;
    }
    k++;
    step[k]++;
  }
  for (i = 0; i < m; i++) {
    double s = 0.0;
    for (k = 0; k < n; k++) {
      s += best[k] * b[k * m + i];
    }
    out[i] = s;
  }
  if (out_norm2 != NULL) {
    *out_norm2 = lt_dot(out, out, m);
  }
done:
  ds_context_free(context, b);
  ds_context_free(context, bs);
  ds_context_free(context, mu);
  ds_context_free(context, b2);
  return st;
}

ds_status_t ds_spectral_test(unsigned long a, unsigned long m, size_t t,
                             double *out_nu, double *out_nu2,
                             ds_context_t *context) {
  double basis[64];
  double v[8];
  double n2 = 0.0;
  unsigned long p = 1;
  size_t i;
  ds_status_t st;
  if (out_nu == NULL && out_nu2 == NULL) {
    return DS_ERR_NULL;
  }
  if (t < 2 || t > 8 || m < 2 || m >= (1UL << 26)) {
    return DS_ERR_INVALID;
  }
  a %= m;
  memset(basis, 0, sizeof(basis));
  basis[0] = (double)m;
  for (i = 1; i < t; i++) {
    p = (unsigned long)(((double)p * (double)a) -
                        floor((double)p * (double)a / (double)m) * (double)m);
    p %= m;
    basis[i * t] = p == 0 ? 0.0 : -(double)p;
    basis[i * t + i] = 1.0;
  }
  st = ds_lattice_shortest(basis, t, t, v, &n2, context);
  if (st != DS_OK) {
    return st;
  }
  if (out_nu2 != NULL) {
    *out_nu2 = n2;
  }
  if (out_nu != NULL) {
    *out_nu = sqrt(n2);
  }
  return DS_OK;
}

static ds_status_t lt_relation_lll(const double *x, size_t n, double tol,
                                   double max_coeff, double *out,
                                   ds_context_t *ctx) {
  size_t m = n + 1;
  double *b;
  double norm = sqrt(lt_dot(x, x, n));
  double scale = 1.0 / tol;
  size_t i;
  size_t j;
  ds_status_t st;
  b = (double *)ds_context_calloc(ctx, n * m, sizeof(double));
  if (b == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    b[i * m + i] = 1.0;
    b[i * m + n] = lt_round(scale * x[i] / norm);
  }
  st = ds_lll_reduce(b, n, m, 0.99, ctx);
  if (st == DS_OK) {
    st = DS_NOT_FOUND;
    for (i = 0; i < n && st == DS_NOT_FOUND; i++) {
      double r = 0.0;
      double big = 0.0;
      bool nonzero = false;
      for (j = 0; j < n; j++) {
        r += b[i * m + j] * x[j];
        big = fabs(b[i * m + j]) > big ? fabs(b[i * m + j]) : big;
        nonzero = nonzero || b[i * m + j] > 0.5 || b[i * m + j] < -0.5;
      }
      if (nonzero && fabs(r) <= tol * norm * 10.0 && big <= max_coeff) {
        memcpy(out, b + i * m, n * sizeof(double));
        st = DS_OK;
      }
    }
  }
  ds_context_free(ctx, b);
  return st;
}

/* PSLQ (Ferguson, Bailey, Arno) in double precision */
static ds_status_t lt_relation_pslq(const double *x0, size_t n, double tol,
                                    double max_coeff, double *out,
                                    ds_context_t *ctx) {
  double *h;  /* n x (n - 1) */
  double *a;  /* n x n */
  double *bm; /* n x n */
  double *y;
  double *s;
  double norm = sqrt(lt_dot(x0, x0, n));
  double gamma = sqrt(4.0 / 3.0);
  size_t i;
  size_t j;
  size_t k;
  size_t it;
  size_t w = n - 1;
  ds_status_t st = DS_NOT_FOUND;
  h = (double *)ds_context_calloc(ctx, n * w + 1, sizeof(double));
  a = (double *)ds_context_calloc(ctx, n * n, sizeof(double));
  bm = (double *)ds_context_calloc(ctx, n * n, sizeof(double));
  y = (double *)ds_context_alloc(ctx, n * sizeof(double));
  s = (double *)ds_context_alloc(ctx, n * sizeof(double));
  if (h == NULL || a == NULL || bm == NULL || y == NULL || s == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < n; i++) {
    y[i] = x0[i] / norm;
    a[i * n + i] = bm[i * n + i] = 1.0;
  }
  for (k = n; k-- > 0;) {
    s[k] = y[k] * y[k] + (k + 1 < n ? s[k + 1] * s[k + 1] : 0.0);
    s[k] = sqrt(s[k]);
  }
  for (i = 0; i < n; i++) {
    for (j = 0; j < w; j++) {
      if (j > i) {
        h[i * w + j] = 0.0;
      } else if (j == i) {
        h[i * w + j] = s[i] > 0.0 ? s[i + 1] / s[i] : 0.0;
      } else {
        h[i * w + j] =
            s[j] * s[j + 1] > 0.0 ? -y[i] * y[j] / (s[j] * s[j + 1]) : 0.0;
      }
    }
  }
  for (it = 0; it < 10000; it++) {
    size_t r = 0;
    double best = -1.0;
    double ymin;
    size_t at = 0;
    /* Hermite reduction */
    for (i = 1; i < n; i++) {
      for (j = (i - 1 < w ? i - 1 : w - 1) + 1; j-- > 0;) {
        double q;
        if (!(fabs(h[j * w + j]) > 0.0)) {
          continue;
        }
        q = lt_round(h[i * w + j] / h[j * w + j]);
        if (!(q > 0.5 || q < -0.5)) {
          continue;
        }
        y[j] += q * y[i];
        for (k = 0; k <= j; k++) {
          h[i * w + k] -= q * h[j * w + k];
        }
        for (k = 0; k < n; k++) {
          a[i * n + k] -= q * a[j * n + k];
          bm[k * n + j] += q * bm[k * n + i];
        }
      }
    }
    /* a relation is a column of B where y is tiny */
    ymin = HUGE_VAL;
    for (i = 0; i < n; i++) {
      if (fabs(y[i]) < ymin) {
        ymin = fabs(y[i]);
        at = i;
      }
    }
    if (ymin < tol) {
      double big = 0.0;
      double res = 0.0;
      for (k = 0; k < n; k++) {
        out[k] = bm[k * n + at];
        big = fabs(out[k]) > big ? fabs(out[k]) : big;
        res += out[k] * x0[k];
      }
      st = big <= max_coeff && fabs(res) <= tol * norm * 100.0 ? DS_OK
                                                               : DS_NOT_FOUND;
      goto done;
    }
    {
      double big = 0.0;
      for (k = 0; k < n * n; k++) {
        big = fabs(a[k]) > big ? fabs(a[k]) : big;
      }
      if (big > max_coeff * 1e3 || big > 4.0e15) {
        goto done; /* out of precision or past the bound */
      }
    }
    /* exchange */
    for (i = 0; i < w; i++) {
      double v = pow(gamma, (double)(i + 1)) * fabs(h[i * w + i]);
      if (v > best) {
        best = v;
        r = i;
      }
    }
    {
      double t;
      t = y[r];
      y[r] = y[r + 1];
      y[r + 1] = t;
      for (k = 0; k < n; k++) {
        t = a[r * n + k];
        a[r * n + k] = a[(r + 1) * n + k];
        a[(r + 1) * n + k] = t;
        t = bm[k * n + r];
        bm[k * n + r] = bm[k * n + r + 1];
        bm[k * n + r + 1] = t;
      }
      for (k = 0; k < w; k++) {
        t = h[r * w + k];
        h[r * w + k] = h[(r + 1) * w + k];
        h[(r + 1) * w + k] = t;
      }
      if (r + 1 < w) {
        double t0 = sqrt(h[r * w + r] * h[r * w + r] +
                         h[r * w + r + 1] * h[r * w + r + 1]);
        double t1;
        double t2;
        if (!(t0 > 0.0)) {
          continue;
        }
        t1 = h[r * w + r] / t0;
        t2 = h[r * w + r + 1] / t0;
        for (i = r; i < n; i++) {
          double t3 = h[i * w + r];
          double t4 = h[i * w + r + 1];
          h[i * w + r] = t1 * t3 + t2 * t4;
          h[i * w + r + 1] = -t2 * t3 + t1 * t4;
        }
      }
    }
  }
done:
  ds_context_free(ctx, h);
  ds_context_free(ctx, a);
  ds_context_free(ctx, bm);
  ds_context_free(ctx, y);
  ds_context_free(ctx, s);
  return st;
}

ds_status_t ds_integer_relation(const double *x, size_t n,
                                ds_relation_method_t method, double tolerance,
                                double max_coeff, double *out,
                                ds_context_t *context) {
  size_t i;
  if (x == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!(tolerance > 0.0)) {
    tolerance = 1e-10;
  }
  if (!(max_coeff > 0.0)) {
    max_coeff = 1e6;
  }
  if (n < 2 || ((int)method != DS_RELATION_PSLQ && method != DS_RELATION_LLL)) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    if (!(x[i] > -HUGE_VAL && x[i] < HUGE_VAL)) {
      return DS_ERR_INVALID;
    }
  }
  if (!(sqrt(lt_dot(x, x, n)) > 0.0)) {
    return DS_ERR_INVALID;
  }
  return method == DS_RELATION_PSLQ
             ? lt_relation_pslq(x, n, tolerance, max_coeff, out, context)
             : lt_relation_lll(x, n, tolerance, max_coeff, out, context);
}
