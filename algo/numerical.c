#include "numerical.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NM_PI 3.14159265358979323846

double ds_horner(const double *coef, size_t degree, double x,
                 double *out_derivative) {
  double p = coef[degree];
  double d = 0;
  size_t i;
  for (i = degree; i-- > 0;) {
    d = d * x + p;
    p = p * x + coef[i];
  }
  if (out_derivative != NULL) {
    *out_derivative = d;
  }
  return p;
}

ds_status_t ds_root_bisection(ds_real_func_t f, void *user, double a, double b,
                              double tolerance, unsigned int max_iter,
                              double *out_root) {
  double fa;
  double fb;
  double m = a;
  double fm;
  unsigned int i;
  if (f == NULL || out_root == NULL) {
    return DS_ERR_NULL;
  }
  fa = f(a, user);
  fb = f(b, user);
  if (!(fa < 0 || fa > 0)) {
    *out_root = a;
    return DS_OK;
  }
  if (!(fb < 0 || fb > 0)) {
    *out_root = b;
    return DS_OK;
  }
  if ((fa < 0) == (fb < 0)) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < max_iter; i++) {
    m = a + (b - a) / 2;
    if (fabs(b - a) / 2 <= tolerance || !(m > a && m < b) ||
        !(m < a || m > a)) {
      break;
    }
    fm = f(m, user);
    if (!(fm < 0 || fm > 0)) {
      break;
    }
    if ((fm < 0) == (fa < 0)) {
      a = m;
      fa = fm;
    } else {
      b = m;
    }
  }
  *out_root = m;
  return DS_OK;
}

ds_status_t ds_root_secant(ds_real_func_t f, void *user, double x0, double x1,
                           double tolerance, unsigned int max_iter,
                           double *out_root) {
  double f0;
  double f1;
  double x2;
  unsigned int i;
  if (f == NULL || out_root == NULL) {
    return DS_ERR_NULL;
  }
  f0 = f(x0, user);
  f1 = f(x1, user);
  for (i = 0; i < max_iter; i++) {
    if (!(f1 < 0 || f1 > 0)) {
      *out_root = x1;
      return DS_OK;
    }
    if (!(f1 - f0 < 0 || f1 - f0 > 0)) {
      return DS_NOT_FOUND; /* flat secant */
    }
    x2 = x1 - f1 * (x1 - x0) / (f1 - f0);
    if (fabs(x2 - x1) <= tolerance * (1 + fabs(x2))) {
      *out_root = x2;
      return DS_OK;
    }
    x0 = x1;
    f0 = f1;
    x1 = x2;
    f1 = f(x1, user);
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_root_newton(ds_real_func_t f, ds_real_func_t df, void *user,
                           double x0, double tolerance, unsigned int max_iter,
                           double *out_root) {
  double fx;
  double d;
  double x1;
  unsigned int i;
  if (f == NULL || df == NULL || out_root == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < max_iter; i++) {
    fx = f(x0, user);
    if (!(fx < 0 || fx > 0)) {
      *out_root = x0;
      return DS_OK;
    }
    d = df(x0, user);
    if (!(d < 0 || d > 0)) {
      return DS_NOT_FOUND;
    }
    x1 = x0 - fx / d;
    if (fabs(x1 - x0) <= tolerance * (1 + fabs(x1))) {
      *out_root = x1;
      return DS_OK;
    }
    x0 = x1;
  }
  return DS_NOT_FOUND;
}

double ds_sqrt_newton(double x) {
  double y;
  double next;
  int e;
  unsigned int i;
  if (x < 0) {
    return sqrt(x); /* NaN with the platform's domain error behaviour */
  }
  if (!(x > 0) || !(x < HUGE_VAL)) {
    return x;
  }
  (void)frexp(x, &e);
  /* 2^ceil(e / 2) is above the root, so Newton decreases monotonically */
  y = ldexp(1.0, e >= 0 ? (e + 1) / 2 : -((-e) / 2));
  for (i = 0; i < 200; i++) {
    next = (y + x / y) / 2;
    if (!(next < y)) {
      break;
    }
    y = next;
  }
  /* the iteration can stop an ulp high: x / y is the neighbour below */
  next = x / y;
  {
    long double ry = (long double)y * (long double)y - (long double)x;
    long double rn = (long double)next * (long double)next - (long double)x;
    if ((rn < 0 ? -rn : rn) < (ry < 0 ? -ry : ry)) {
      y = next;
    }
  }
  return y;
}

double ds_cbrt_newton(double x) {
  double y;
  double prev;
  double y3;
  int e;
  unsigned int i;
  if (!(x < 0 || x > 0) || !(fabs(x) < HUGE_VAL)) {
    return x;
  }
  if (x < 0) {
    return -ds_cbrt_newton(-x);
  }
  (void)frexp(x, &e);
  y = ldexp(1.0, e / 3);
  for (i = 0; i < 100; i++) { /* Halley's iteration, cubic convergence */
    prev = y;
    y3 = y * y * y;
    y = y * (y3 + 2 * x) / (2 * y3 + x);
    if (fabs(y - prev) <= 1e-16 * y) {
      break;
    }
  }
  {
    /* one Newton step in extended precision fixes the last ulp */
    long double yl = (long double)y;
    yl -= (yl * yl * yl - (long double)x) / (3 * yl * yl);
    y = (double)yl;
  }
  return y;
}

/* ---- CORDIC -------------------------------------------------------------- */

#define NM_CORDIC_MAX 60

static double nm_cordic_atan[NM_CORDIC_MAX];
static double nm_cordic_gain[NM_CORDIC_MAX + 1]; /* 1 / prod sqrt(1+2^-2i) */
static int nm_cordic_ready = 0;

static void nm_cordic_init(void) {
  unsigned int i;
  double k = 1;
  if (nm_cordic_ready) {
    return;
  }
  nm_cordic_gain[0] = 1;
  for (i = 0; i < NM_CORDIC_MAX; i++) {
    nm_cordic_atan[i] = atan(ldexp(1.0, -(int)i));
    k *= 1 / sqrt(1 + ldexp(1.0, -2 * (int)i));
    nm_cordic_gain[i + 1] = k;
  }
  nm_cordic_ready = 1;
}

void ds_cordic_sincos(double angle, unsigned int iterations, double *out_sin,
                      double *out_cos) {
  double x = 1;
  double y = 0;
  double z;
  double t;
  double sign = 1;
  double p2 = 1;
  unsigned int i;
  nm_cordic_init();
  if (iterations > NM_CORDIC_MAX) {
    iterations = NM_CORDIC_MAX;
  }
  /* reduce to [-pi/2, pi/2] */
  z = fmod(angle, 2 * NM_PI);
  if (z > NM_PI) {
    z -= 2 * NM_PI;
  } else if (z < -NM_PI) {
    z += 2 * NM_PI;
  }
  if (z > NM_PI / 2) {
    z -= NM_PI;
    sign = -1;
  } else if (z < -NM_PI / 2) {
    z += NM_PI;
    sign = -1;
  }
  for (i = 0; i < iterations; i++) {
    if (z >= 0) {
      t = x - y * p2;
      y = y + x * p2;
      x = t;
      z -= nm_cordic_atan[i];
    } else {
      t = x + y * p2;
      y = y - x * p2;
      x = t;
      z += nm_cordic_atan[i];
    }
    p2 /= 2;
  }
  if (out_cos != NULL) {
    *out_cos = sign * x * nm_cordic_gain[iterations];
  }
  if (out_sin != NULL) {
    *out_sin = sign * y * nm_cordic_gain[iterations];
  }
}

void ds_cordic_atan2(double y, double x, unsigned int iterations,
                     double *out_angle, double *out_radius) {
  double z = 0;
  double t;
  double p2 = 1;
  unsigned int i;
  nm_cordic_init();
  if (iterations > NM_CORDIC_MAX) {
    iterations = NM_CORDIC_MAX;
  }
  if (x < 0) { /* rotate by pi into the right half-plane */
    z = y < 0 ? -NM_PI : NM_PI;
    x = -x;
    y = -y;
  }
  for (i = 0; i < iterations; i++) {
    /* rotate every step (even when y is 0) so the gain stays exact */
    if (y >= 0) {
      t = x + y * p2;
      y = y - x * p2;
      x = t;
      z += nm_cordic_atan[i];
    } else {
      t = x - y * p2;
      y = y + x * p2;
      x = t;
      z -= nm_cordic_atan[i];
    }
    p2 /= 2;
  }
  if (out_angle != NULL) {
    *out_angle = z;
  }
  if (out_radius != NULL) {
    *out_radius = x * nm_cordic_gain[iterations];
  }
}

/* ---- Gamma, Stirling ----------------------------------------------------- */

static const double nm_lanczos[9] = {
    0.99999999999980993,  676.5203681218851,     -1259.1392167224028,
    771.32342877765313,   -176.61502916214059,   12.507343278686905,
    -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7};

double ds_lgamma(double x) {
  double a;
  double t;
  unsigned int i;
  if (x < 0.5) {
    /* reflection: Gamma(x) Gamma(1 - x) = pi / sin(pi x) */
    return log(NM_PI / fabs(sin(NM_PI * x))) - ds_lgamma(1 - x);
  }
  x -= 1;
  a = nm_lanczos[0];
  t = x + 7.5;
  for (i = 1; i < 9; i++) {
    a += nm_lanczos[i] / (x + (double)i);
  }
  return 0.5 * log(2 * NM_PI) + (x + 0.5) * log(t) - t + log(a);
}

double ds_gamma(double x) {
  double a;
  double t;
  unsigned int i;
  if (x < 0.5) {
    return NM_PI / (sin(NM_PI * x) * ds_gamma(1 - x));
  }
  x -= 1;
  a = nm_lanczos[0];
  t = x + 7.5;
  for (i = 1; i < 9; i++) {
    a += nm_lanczos[i] / (x + (double)i);
  }
  return sqrt(2 * NM_PI) * pow(t, x + 0.5) * exp(-t) * a;
}

double ds_stirling(double n) {
  /* n! ~ sqrt(2 pi n) (n / e)^n (1 + 1/12n + 1/288n^2 - 139/51840n^3) */
  double c;
  if (!(n > 0)) {
    return 1;
  }
  c = 1 + 1 / (12 * n) + 1 / (288 * n * n) - 139 / (51840 * n * n * n);
  return sqrt(2 * NM_PI * n) * pow(n / exp(1.0), n) * c;
}

double ds_log_factorial(unsigned long n) {
  return ds_lgamma((double)n + 1);
}

/* ---- BBP ----------------------------------------------------------------- */

/* fractional part of sum_k 16^(d-k) / (8k + j) */
static double nm_bbp_series(unsigned long d, unsigned long j) {
  double s = 0;
  double t;
  unsigned long k;
  unsigned long den;
  double r;
  double base;
  unsigned long e;
  for (k = 0; k <= d; k++) {
    den = 8 * k + j;
    /* 16^(d - k) mod den by repeated squaring in doubles (den < 2^26
     * keeps the products exact) */
    r = 1;
    base = fmod(16.0, (double)den);
    e = d - k;
    while (e > 0) {
      if (e & 1UL) {
        r = fmod(r * base, (double)den);
      }
      base = fmod(base * base, (double)den);
      e >>= 1;
    }
    s += r / (double)den;
    s -= floor(s);
  }
  for (k = d + 1; k <= d + 100; k++) {
    t = pow(16.0, (double)d - (double)k) / (double)(8 * k + j);
    if (t < 1e-17) {
      break;
    }
    s += t;
    s -= floor(s);
  }
  return s;
}

ds_status_t ds_bbp_pi_hex(unsigned long position, size_t count, char *out) {
  static const char hex[] = "0123456789abcdef";
  double x;
  size_t i;
  unsigned long p;
  int digit;
  if (out == NULL && count > 0) {
    return DS_ERR_NULL;
  }
  if (position > 10000000UL) {
    return DS_ERR_RANGE; /* beyond the double-precision guarantee */
  }
  i = 0;
  while (i < count) {
    p = position + (unsigned long)i;
    x = 4 * nm_bbp_series(p, 1) - 2 * nm_bbp_series(p, 4) -
        nm_bbp_series(p, 5) - nm_bbp_series(p, 6);
    x -= floor(x);
    /* the first 6 digits of each evaluation are reliable */
    {
      size_t k;
      double fl;
      for (k = 0; k < 6 && i < count; k++) {
        x *= 16;
        fl = floor(x);
        digit = (int)fl;
        x -= digit;
        out[i++] = hex[digit & 15];
      }
    }
  }
  return DS_OK;
}

/* ---- statistics ---------------------------------------------------------- */

static int nm_cmp(const void *a, const void *b) {
  double x = *(const double *)a;
  double y = *(const double *)b;
  return (x > y) - (x < y);
}

ds_status_t ds_mode(const double *values, size_t n, double *out_mode,
                    size_t *out_frequency, ds_context_t *context) {
  double *c;
  size_t i;
  size_t run = 1;
  size_t best = 0;
  double bv = 0;
  if (values == NULL || out_mode == NULL) {
    return DS_ERR_NULL;
  }
  if (n == 0) {
    return DS_EMPTY;
  }
  c = (double *)ds_context_alloc(context, n * sizeof(double));
  if (c == NULL) {
    return DS_ERR_ALLOC;
  }
  memcpy(c, values, n * sizeof(double));
  qsort(c, n, sizeof(double), nm_cmp);
  for (i = 1; i <= n; i++) {
    if (i < n && !(c[i] < c[i - 1] || c[i] > c[i - 1])) {
      run++;
    } else {
      if (run > best) {
        best = run;
        bv = c[i - 1];
      }
      run = 1;
    }
  }
  *out_mode = bv;
  if (out_frequency != NULL) {
    *out_frequency = best;
  }
  ds_context_free(context, c);
  return DS_OK;
}

double ds_midrange(const double *values, size_t n) {
  double lo;
  double hi;
  size_t i;
  if (n == 0) {
    return 0;
  }
  lo = hi = values[0];
  for (i = 1; i < n; i++) {
    if (values[i] < lo) {
      lo = values[i];
    }
    if (values[i] > hi) {
      hi = values[i];
    }
  }
  return lo / 2 + hi / 2;
}
