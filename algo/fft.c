#include "fft.h"
#include <math.h>
#include <string.h>

static const double FT_PI = 3.14159265358979323846;

ds_status_t ds_dft(const ds_complex_t *in, ds_complex_t *out, size_t n,
                   bool inverse) {
  size_t j;
  size_t k;
  double ang;
  double s = inverse ? 2.0 : -2.0;
  double re;
  double im;
  if (n > 0 && (in == NULL || out == NULL)) {
    return DS_ERR_NULL;
  }
  for (k = 0; k < n; k++) {
    re = 0.0;
    im = 0.0;
    for (j = 0; j < n; j++) {
      /* reduce j * k mod n first for accuracy */
      ang = s * FT_PI * (double)((j * k) % n) / (double)n;
      re += in[j].re * cos(ang) - in[j].im * sin(ang);
      im += in[j].re * sin(ang) + in[j].im * cos(ang);
    }
    if (inverse) {
      re /= (double)n;
      im /= (double)n;
    }
    out[k].re = re;
    out[k].im = im;
  }
  return DS_OK;
}

static bool ft_pow2(size_t n) { return n != 0 && (n & (n - 1)) == 0; }

/* Radix-2 iterative FFT, no scaling. */
static void ft_radix2(ds_complex_t *a, size_t n, bool inverse) {
  size_t i;
  size_t j = 0;
  size_t bit;
  size_t len;
  size_t k;
  double ang;
  ds_complex_t w;
  ds_complex_t wl;
  ds_complex_t u;
  ds_complex_t v;
  ds_complex_t t;
  for (i = 1; i < n; i++) {
    for (bit = n >> 1; j & bit; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      t = a[i];
      a[i] = a[j];
      a[j] = t;
    }
  }
  for (len = 2; len <= n; len <<= 1) {
    ang = (inverse ? 2.0 : -2.0) * FT_PI / (double)len;
    wl.re = cos(ang);
    wl.im = sin(ang);
    for (i = 0; i < n; i += len) {
      w.re = 1.0;
      w.im = 0.0;
      for (k = 0; k < len / 2; k++) {
        u = a[i + k];
        v.re = a[i + k + len / 2].re * w.re - a[i + k + len / 2].im * w.im;
        v.im = a[i + k + len / 2].re * w.im + a[i + k + len / 2].im * w.re;
        a[i + k].re = u.re + v.re;
        a[i + k].im = u.im + v.im;
        a[i + k + len / 2].re = u.re - v.re;
        a[i + k + len / 2].im = u.im - v.im;
        t.re = w.re * wl.re - w.im * wl.im;
        t.im = w.re * wl.im + w.im * wl.re;
        w = t;
      }
    }
  }
}

/* Bluestein: the DFT of any length as a convolution of chirps. */
static ds_status_t ft_bluestein(ds_complex_t *a, size_t n, bool inverse,
                                ds_context_t *context) {
  size_t m = 1;
  size_t i;
  ds_complex_t *w;
  ds_complex_t *x;
  ds_complex_t *y;
  double ang;
  double s = inverse ? 1.0 : -1.0;
  ds_complex_t t;
  while (m < 2 * n - 1) {
    if (m > ((size_t)-1) / 2 / (3 * sizeof(ds_complex_t))) {
      return DS_ERR_OVERFLOW;
    }
    m <<= 1;
  }
  w = (ds_complex_t *)ds_context_alloc(context, 3 * m * sizeof(ds_complex_t));
  if (w == NULL) {
    return DS_ERR_ALLOC;
  }
  x = w + m;
  y = x + m;
  /* chirp w_k = e^(s i pi k^2 / n), k^2 reduced mod 2n for accuracy */
  for (i = 0; i < n; i++) {
    ang = s * FT_PI * (double)((i * i) % (2 * n)) / (double)n;
    w[i].re = cos(ang);
    w[i].im = sin(ang);
  }
  for (i = 0; i < m; i++) {
    x[i].re = x[i].im = 0.0;
    y[i].re = y[i].im = 0.0;
  }
  for (i = 0; i < n; i++) {
    x[i].re = a[i].re * w[i].re - a[i].im * w[i].im;
    x[i].im = a[i].re * w[i].im + a[i].im * w[i].re;
  }
  y[0].re = w[0].re;
  y[0].im = -w[0].im;
  for (i = 1; i < n; i++) {
    y[i].re = y[m - i].re = w[i].re;
    y[i].im = y[m - i].im = -w[i].im;
  }
  ft_radix2(x, m, false);
  ft_radix2(y, m, false);
  for (i = 0; i < m; i++) {
    t.re = x[i].re * y[i].re - x[i].im * y[i].im;
    t.im = x[i].re * y[i].im + x[i].im * y[i].re;
    x[i] = t;
  }
  ft_radix2(x, m, true);
  for (i = 0; i < n; i++) {
    t.re = (x[i].re * w[i].re - x[i].im * w[i].im) / (double)m;
    t.im = (x[i].re * w[i].im + x[i].im * w[i].re) / (double)m;
    a[i] = t;
  }
  ds_context_free(context, w);
  return DS_OK;
}

ds_status_t ds_fft(ds_complex_t *a, size_t n, bool inverse,
                   ds_context_t *context) {
  size_t i;
  ds_status_t st;
  if (n > 0 && a == NULL) {
    return DS_ERR_NULL;
  }
  if (n <= 1) {
    return DS_OK;
  }
  if (ft_pow2(n)) {
    ft_radix2(a, n, inverse);
  } else {
    st = ft_bluestein(a, n, inverse, context);
    if (st != DS_OK) {
      return st;
    }
  }
  if (inverse) {
    for (i = 0; i < n; i++) {
      a[i].re /= (double)n;
      a[i].im /= (double)n;
    }
  }
  return DS_OK;
}

ds_status_t ds_fft_convolve(const double *a, size_t na, const double *b,
                            size_t nb, double *out, ds_context_t *context) {
  size_t n = 1;
  size_t need;
  size_t i;
  ds_complex_t *fa;
  ds_complex_t *fb;
  ds_complex_t t;
  if ((na > 0 && a == NULL) || (nb > 0 && b == NULL) ||
      (na > 0 && nb > 0 && out == NULL)) {
    return DS_ERR_NULL;
  }
  if (na == 0 || nb == 0) {
    return DS_OK;
  }
  if (na > ((size_t)-1) - nb) {
    return DS_ERR_OVERFLOW;
  }
  need = na + nb - 1;
  while (n < need) {
    if (n > ((size_t)-1) / 4 / sizeof(ds_complex_t)) {
      return DS_ERR_OVERFLOW;
    }
    n <<= 1;
  }
  fa = (ds_complex_t *)ds_context_calloc(context, 2 * n, sizeof(ds_complex_t));
  if (fa == NULL) {
    return DS_ERR_ALLOC;
  }
  fb = fa + n;
  for (i = 0; i < na; i++) {
    fa[i].re = a[i];
  }
  for (i = 0; i < nb; i++) {
    fb[i].re = b[i];
  }
  ft_radix2(fa, n, false);
  ft_radix2(fb, n, false);
  for (i = 0; i < n; i++) {
    t.re = fa[i].re * fb[i].re - fa[i].im * fb[i].im;
    t.im = fa[i].re * fb[i].im + fa[i].im * fb[i].re;
    fa[i] = t;
  }
  ft_radix2(fa, n, true);
  for (i = 0; i < need; i++) {
    out[i] = fa[i].re / (double)n;
  }
  ds_context_free(context, fa);
  return DS_OK;
}

/* ---- number-theoretic transform ------------------------------------------ */

/* a * b mod DS_NTT_MOD without 64-bit types: shift-and-add on 30-bit
 * residues (all intermediates stay below 2^31). */
static unsigned long nt_mul(unsigned long a, unsigned long b) {
  unsigned long r = 0;
  const unsigned long m = DS_NTT_MOD;
  a %= m;
  b %= m;
  while (b != 0) {
    if (b & 1UL) {
      r += a;
      if (r >= m) {
        r -= m;
      }
    }
    a += a;
    if (a >= m) {
      a -= m;
    }
    b >>= 1;
  }
  return r;
}

static unsigned long nt_pow(unsigned long a, unsigned long e) {
  unsigned long r = 1;
  while (e != 0) {
    if (e & 1UL) {
      r = nt_mul(r, a);
    }
    a = nt_mul(a, a);
    e >>= 1;
  }
  return r;
}

ds_status_t ds_ntt(unsigned long *a, size_t n, bool inverse) {
  size_t i;
  size_t j = 0;
  size_t bit;
  size_t len;
  size_t k;
  unsigned long w;
  unsigned long wl;
  unsigned long u;
  unsigned long v;
  unsigned long t;
  const unsigned long m = DS_NTT_MOD;
  if (n > 0 && a == NULL) {
    return DS_ERR_NULL;
  }
  if (n == 0 || !ft_pow2(n) || n > ((size_t)1 << 23)) {
    return n == 0 ? DS_OK : DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    a[i] %= m;
  }
  for (i = 1; i < n; i++) {
    for (bit = n >> 1; j & bit; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      t = a[i];
      a[i] = a[j];
      a[j] = t;
    }
  }
  for (len = 2; len <= n; len <<= 1) {
    /* 3 is a primitive root of 998244353 */
    wl = nt_pow(3UL, (m - 1) / (unsigned long)len);
    if (inverse) {
      wl = nt_pow(wl, m - 2);
    }
    for (i = 0; i < n; i += len) {
      w = 1;
      for (k = 0; k < len / 2; k++) {
        u = a[i + k];
        v = nt_mul(a[i + k + len / 2], w);
        a[i + k] = u + v >= m ? u + v - m : u + v;
        a[i + k + len / 2] = u >= v ? u - v : u + m - v;
        w = nt_mul(w, wl);
      }
    }
  }
  if (inverse) {
    t = nt_pow((unsigned long)n % m, m - 2);
    for (i = 0; i < n; i++) {
      a[i] = nt_mul(a[i], t);
    }
  }
  return DS_OK;
}

ds_status_t ds_ntt_convolve(const unsigned long *a, size_t na,
                            const unsigned long *b, size_t nb,
                            unsigned long *out, ds_context_t *context) {
  size_t n = 1;
  size_t need;
  size_t i;
  unsigned long *fa;
  unsigned long *fb;
  if ((na > 0 && a == NULL) || (nb > 0 && b == NULL) ||
      (na > 0 && nb > 0 && out == NULL)) {
    return DS_ERR_NULL;
  }
  if (na == 0 || nb == 0) {
    return DS_OK;
  }
  if (na > ((size_t)-1) - nb) {
    return DS_ERR_OVERFLOW;
  }
  need = na + nb - 1;
  while (n < need) {
    n <<= 1;
    if (n > ((size_t)1 << 23)) {
      return DS_ERR_INVALID;
    }
  }
  fa =
      (unsigned long *)ds_context_calloc(context, 2 * n, sizeof(unsigned long));
  if (fa == NULL) {
    return DS_ERR_ALLOC;
  }
  fb = fa + n;
  for (i = 0; i < na; i++) {
    fa[i] = a[i] % DS_NTT_MOD;
  }
  for (i = 0; i < nb; i++) {
    fb[i] = b[i] % DS_NTT_MOD;
  }
  (void)ds_ntt(fa, n, false);
  (void)ds_ntt(fb, n, false);
  for (i = 0; i < n; i++) {
    fa[i] = nt_mul(fa[i], fb[i]);
  }
  (void)ds_ntt(fa, n, true);
  memcpy(out, fa, need * sizeof(unsigned long));
  ds_context_free(context, fa);
  return DS_OK;
}
