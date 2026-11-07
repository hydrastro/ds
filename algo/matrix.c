#include "matrix.h"
#include <stdbool.h>
#include <string.h>

/* ---- multiplication -------------------------------------------------------
 */

static void mx_naive(const double *a, size_t lda, const double *b, size_t ldb,
                     double *c, size_t ldc, size_t n, size_t m, size_t p) {
  size_t i;
  size_t k;
  size_t j;
  double aik;
  for (i = 0; i < n; i++) {
    for (j = 0; j < p; j++) {
      c[i * ldc + j] = 0.0;
    }
    for (k = 0; k < m; k++) {
      aik = a[i * lda + k];
      for (j = 0; j < p; j++) {
        c[i * ldc + j] += aik * b[k * ldb + j];
      }
    }
  }
}

static void mx_blocked(const double *a, const double *b, double *c, size_t n,
                       size_t m, size_t p, size_t bs) {
  size_t ii;
  size_t kk;
  size_t jj;
  size_t i;
  size_t k;
  size_t j;
  size_t ie;
  size_t ke;
  size_t je;
  double aik;
  for (i = 0; i < n * p; i++) {
    c[i] = 0.0;
  }
  for (ii = 0; ii < n; ii += bs) {
    ie = ii + bs < n ? ii + bs : n;
    for (kk = 0; kk < m; kk += bs) {
      ke = kk + bs < m ? kk + bs : m;
      for (jj = 0; jj < p; jj += bs) {
        je = jj + bs < p ? jj + bs : p;
        for (i = ii; i < ie; i++) {
          for (k = kk; k < ke; k++) {
            aik = a[i * m + k];
            for (j = jj; j < je; j++) {
              c[i * p + j] += aik * b[k * p + j];
            }
          }
        }
      }
    }
  }
}

/* s x s blocks with leading dimension ld (s a power of two). */
static void mx_add(const double *x, const double *y, double *z, size_t s,
                   size_t ldx, size_t ldy, size_t ldz, double sign) {
  size_t i;
  size_t j;
  for (i = 0; i < s; i++) {
    for (j = 0; j < s; j++) {
      z[i * ldz + j] = x[i * ldx + j] + sign * y[i * ldy + j];
    }
  }
}

/* C = A B for s x s blocks; work holds the temporaries of every level. */
static void mx_strassen(const double *a, size_t lda, const double *b,
                        size_t ldb, double *c, size_t ldc, size_t s,
                        size_t cutoff, double *work) {
  size_t h = s / 2;
  size_t hh = h * h;
  const double *a11 = a;
  const double *a12 = a + h;
  const double *a21 = a + h * lda;
  const double *a22 = a + h * lda + h;
  const double *b11 = b;
  const double *b12 = b + h;
  const double *b21 = b + h * ldb;
  const double *b22 = b + h * ldb + h;
  double *c11 = c;
  double *c12 = c + h;
  double *c21 = c + h * ldc;
  double *c22 = c + h * ldc + h;
  double *t1 = work;
  double *t2 = t1 + hh;
  double *m1 = t2 + hh;
  double *m2 = m1 + hh;
  double *m3 = m2 + hh;
  double *m4 = m3 + hh;
  double *m5 = m4 + hh;
  double *m6 = m5 + hh;
  double *m7 = m6 + hh;
  double *next = m7 + hh;
  size_t i;
  size_t j;
  if (s <= cutoff) {
    mx_naive(a, lda, b, ldb, c, ldc, s, s, s);
    return;
  }
  mx_add(a11, a22, t1, h, lda, lda, h, 1.0);
  mx_add(b11, b22, t2, h, ldb, ldb, h, 1.0);
  mx_strassen(t1, h, t2, h, m1, h, h, cutoff, next);
  mx_add(a21, a22, t1, h, lda, lda, h, 1.0);
  mx_strassen(t1, h, b11, ldb, m2, h, h, cutoff, next);
  mx_add(b12, b22, t2, h, ldb, ldb, h, -1.0);
  mx_strassen(a11, lda, t2, h, m3, h, h, cutoff, next);
  mx_add(b21, b11, t2, h, ldb, ldb, h, -1.0);
  mx_strassen(a22, lda, t2, h, m4, h, h, cutoff, next);
  mx_add(a11, a12, t1, h, lda, lda, h, 1.0);
  mx_strassen(t1, h, b22, ldb, m5, h, h, cutoff, next);
  mx_add(a21, a11, t1, h, lda, lda, h, -1.0);
  mx_add(b11, b12, t2, h, ldb, ldb, h, 1.0);
  mx_strassen(t1, h, t2, h, m6, h, h, cutoff, next);
  mx_add(a12, a22, t1, h, lda, lda, h, -1.0);
  mx_add(b21, b22, t2, h, ldb, ldb, h, 1.0);
  mx_strassen(t1, h, t2, h, m7, h, h, cutoff, next);
  for (i = 0; i < h; i++) {
    for (j = 0; j < h; j++) {
      size_t k = i * h + j;
      c11[i * ldc + j] = m1[k] + m4[k] - m5[k] + m7[k];
      c12[i * ldc + j] = m3[k] + m5[k];
      c21[i * ldc + j] = m2[k] + m4[k];
      c22[i * ldc + j] = m1[k] - m2[k] + m3[k] + m6[k];
    }
  }
}

ds_status_t ds_matrix_multiply(const double *a, const double *b, double *c,
                               size_t n, size_t m, size_t p,
                               ds_matmul_algorithm_t algorithm, size_t param,
                               ds_context_t *context) {
  size_t s;
  size_t mx;
  size_t work;
  size_t lvl;
  size_t i;
  double *pa;
  double *pb;
  double *pc;
  double *pw;
  if ((n * m > 0 && a == NULL) || (m * p > 0 && b == NULL) ||
      (n * p > 0 && c == NULL)) {
    return DS_ERR_NULL;
  }
  if ((m != 0 && n > ((size_t)-1) / m) || (p != 0 && m > ((size_t)-1) / p) ||
      (p != 0 && n > ((size_t)-1) / p)) {
    return DS_ERR_OVERFLOW;
  }
  if (algorithm == DS_MATMUL_NAIVE) {
    mx_naive(a, m, b, p, c, p, n, m, p);
    return DS_OK;
  }
  if (algorithm == DS_MATMUL_BLOCKED) {
    mx_blocked(a, b, c, n, m, p, param == 0 ? 64 : param);
    return DS_OK;
  }
  if (algorithm != DS_MATMUL_STRASSEN) {
    return DS_ERR_INVALID;
  }
  if (param == 0) {
    param = 64;
  }
  mx = n > m ? n : m;
  mx = mx > p ? mx : p;
  if (mx <= param) {
    mx_naive(a, m, b, p, c, p, n, m, p);
    return DS_OK;
  }
  for (s = 1; s < mx; s <<= 1) {
    if (s > ((size_t)-1) / 4) {
      return DS_ERR_OVERFLOW;
    }
  }
  /* temporaries: 9 blocks of (s/2)^2 per level, levels shrink by 4 */
  work = 0;
  for (lvl = s; lvl > param; lvl /= 2) {
    work += 9 * (lvl / 2) * (lvl / 2);
  }
  if (s > ((size_t)-1) / s / sizeof(double) / 4 ||
      work > ((size_t)-1) / sizeof(double) - 3 * s * s) {
    return DS_ERR_OVERFLOW;
  }
  pa = (double *)ds_context_calloc(context, 3 * s * s + work, sizeof(double));
  if (pa == NULL) {
    return DS_ERR_ALLOC;
  }
  pb = pa + s * s;
  pc = pb + s * s;
  pw = pc + s * s;
  for (i = 0; i < n; i++) {
    memcpy(pa + i * s, a + i * m, m * sizeof(double));
  }
  for (i = 0; i < m; i++) {
    memcpy(pb + i * s, b + i * p, p * sizeof(double));
  }
  mx_strassen(pa, s, pb, s, pc, s, s, param, pw);
  for (i = 0; i < n; i++) {
    memcpy(c + i * p, pc + i * s, p * sizeof(double));
  }
  ds_context_free(context, pa);
  return DS_OK;
}

/* ---- packed triangular matrices --------------------------------------------
 */

size_t ds_tri_size(size_t n, ds_tri_kind_t kind) {
  if (kind == DS_TRI_LOWER || kind == DS_TRI_UPPER) {
    return n % 2 == 0 ? n / 2 * (n + 1) : (n + 1) / 2 * n;
  }
  return n == 0 ? 0 : (n % 2 == 0 ? n / 2 * (n - 1) : (n - 1) / 2 * n);
}

/* Start of row i in the packed array. */
static size_t tr_row(size_t n, ds_tri_kind_t kind, size_t i) {
  switch (kind) {
  case DS_TRI_LOWER:
    return ds_tri_size(i, DS_TRI_LOWER);
  case DS_TRI_STRICT_LOWER:
    return ds_tri_size(i, DS_TRI_STRICT_LOWER);
  case DS_TRI_UPPER:
    return i * n - ds_tri_size(i, DS_TRI_STRICT_LOWER);
  case DS_TRI_STRICT_UPPER:
  default:
    return i * (n - 1) - ds_tri_size(i, DS_TRI_STRICT_LOWER);
  }
}

static bool tr_in(ds_tri_kind_t kind, size_t i, size_t j) {
  switch (kind) {
  case DS_TRI_LOWER:
    return j <= i;
  case DS_TRI_UPPER:
    return j >= i;
  case DS_TRI_STRICT_LOWER:
    return j < i;
  case DS_TRI_STRICT_UPPER:
  default:
    return j > i;
  }
}

/* Packed index of an entry known to be inside the triangle. */
static size_t tr_at(size_t n, ds_tri_kind_t kind, size_t i, size_t j) {
  size_t r = tr_row(n, kind, i);
  switch (kind) {
  case DS_TRI_LOWER:
  case DS_TRI_STRICT_LOWER:
    return r + j;
  case DS_TRI_UPPER:
    return r + (j - i);
  case DS_TRI_STRICT_UPPER:
  default:
    return r + (j - i - 1);
  }
}

static bool tr_kind_ok(ds_tri_kind_t kind) {
  return (int)kind >= 0 && kind <= DS_TRI_STRICT_UPPER;
}

ds_status_t ds_tri_index(size_t n, ds_tri_kind_t kind, size_t i, size_t j,
                         size_t *out_index) {
  if (out_index == NULL) {
    return DS_ERR_NULL;
  }
  if (!tr_kind_ok(kind)) {
    return DS_ERR_INVALID;
  }
  if (i >= n || j >= n || !tr_in(kind, i, j)) {
    return DS_ERR_RANGE;
  }
  *out_index = tr_at(n, kind, i, j);
  return DS_OK;
}

ds_status_t ds_tri_pack(const double *full, size_t n, ds_tri_kind_t kind,
                        double *packed) {
  size_t i;
  size_t j;
  if (n > 0 && (full == NULL || packed == NULL)) {
    return DS_ERR_NULL;
  }
  if (!tr_kind_ok(kind)) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    for (j = 0; j < n; j++) {
      if (tr_in(kind, i, j)) {
        packed[tr_at(n, kind, i, j)] = full[i * n + j];
      }
    }
  }
  return DS_OK;
}

ds_status_t ds_tri_unpack(const double *packed, size_t n, ds_tri_kind_t kind,
                          double *full) {
  size_t i;
  size_t j;
  if (n > 0 && (full == NULL || packed == NULL)) {
    return DS_ERR_NULL;
  }
  if (!tr_kind_ok(kind)) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    for (j = 0; j < n; j++) {
      full[i * n + j] = tr_in(kind, i, j) ? packed[tr_at(n, kind, i, j)] : 0.0;
    }
  }
  return DS_OK;
}

/* Column range [lo, hi) of row i inside the triangle. */
static void tr_cols(size_t n, ds_tri_kind_t kind, size_t i, size_t *lo,
                    size_t *hi) {
  switch (kind) {
  case DS_TRI_LOWER:
    *lo = 0;
    *hi = i + 1;
    break;
  case DS_TRI_STRICT_LOWER:
    *lo = 0;
    *hi = i;
    break;
  case DS_TRI_UPPER:
    *lo = i;
    *hi = n;
    break;
  case DS_TRI_STRICT_UPPER:
  default:
    *lo = i + 1;
    *hi = n;
    break;
  }
}

ds_status_t ds_tri_mul_vec(const double *packed, size_t n, ds_tri_kind_t kind,
                           const double *x, double *y) {
  size_t i;
  size_t j;
  size_t lo;
  size_t hi;
  size_t r;
  double s;
  if (n > 0 && (packed == NULL || x == NULL || y == NULL)) {
    return DS_ERR_NULL;
  }
  if (!tr_kind_ok(kind)) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    tr_cols(n, kind, i, &lo, &hi);
    s = 0.0;
    r = lo < hi ? tr_at(n, kind, i, lo) : 0;
    for (j = lo; j < hi; j++) {
      s += packed[r + (j - lo)] * x[j];
    }
    y[i] = s;
  }
  return DS_OK;
}

ds_status_t ds_tri_multiply(const double *a, const double *b, double *c,
                            size_t n, ds_tri_kind_t kind) {
  size_t i;
  size_t j;
  size_t k;
  size_t lo;
  size_t hi;
  size_t klo;
  size_t khi;
  double s;
  if (n > 0 && (a == NULL || b == NULL || c == NULL)) {
    return DS_ERR_NULL;
  }
  if (!tr_kind_ok(kind)) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    tr_cols(n, kind, i, &lo, &hi);
    for (j = lo; j < hi; j++) {
      /* k with (i, k) and (k, j) both inside the triangle */
      if (kind == DS_TRI_LOWER) {
        klo = j;
        khi = i + 1;
      } else if (kind == DS_TRI_STRICT_LOWER) {
        klo = j + 1;
        khi = i;
      } else if (kind == DS_TRI_UPPER) {
        klo = i;
        khi = j + 1;
      } else {
        klo = i + 1;
        khi = j;
      }
      s = 0.0;
      for (k = klo; k < khi; k++) {
        s += a[tr_at(n, kind, i, k)] * b[tr_at(n, kind, k, j)];
      }
      c[tr_at(n, kind, i, j)] = s;
    }
  }
  return DS_OK;
}

ds_status_t ds_tri_solve(const double *packed, size_t n, ds_tri_kind_t kind,
                         const double *b, double *x) {
  size_t i;
  size_t j;
  double s;
  double d;
  if (n > 0 && (packed == NULL || b == NULL || x == NULL)) {
    return DS_ERR_NULL;
  }
  if (kind != DS_TRI_LOWER && kind != DS_TRI_UPPER) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    d = packed[tr_at(n, kind, i, i)];
    if (!(d < 0.0 || d > 0.0)) {
      return DS_ERR_INVALID;
    }
  }
  if (kind == DS_TRI_LOWER) {
    for (i = 0; i < n; i++) {
      s = b[i];
      for (j = 0; j < i; j++) {
        s -= packed[tr_at(n, kind, i, j)] * x[j];
      }
      x[i] = s / packed[tr_at(n, kind, i, i)];
    }
  } else {
    for (i = n; i-- > 0;) {
      s = b[i];
      for (j = i + 1; j < n; j++) {
        s -= packed[tr_at(n, kind, i, j)] * x[j];
      }
      x[i] = s / packed[tr_at(n, kind, i, i)];
    }
  }
  return DS_OK;
}
