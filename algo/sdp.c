#include "sdp.h"
#include "matrix.h"
#include "random.h"
#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- problems ------------------------------------------------------------ */

static bool sd_finite(double x) { return fabs(x) <= DBL_MAX; }

ds_status_t ds_sdp_init(ds_sdp_t *p, size_t m, size_t blocks, const long *sizes,
                        ds_context_t *context) {
  size_t b;
  if (p == NULL || (blocks > 0 && sizes == NULL)) {
    return DS_ERR_NULL;
  }
  memset(p, 0, sizeof(*p));
  if (blocks == 0) {
    return DS_ERR_INVALID;
  }
  for (b = 0; b < blocks; b++) {
    if (sizes[b] == 0) {
      return DS_ERR_INVALID;
    }
  }
  if (blocks > (size_t)-1 / sizeof(size_t) ||
      m >= (size_t)-1 / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  p->dim = (size_t *)ds_context_alloc(context, blocks * sizeof(size_t));
  p->diagonal = (unsigned char *)ds_context_alloc(context, blocks);
  p->rhs = (double *)ds_context_calloc(context, m + 1, sizeof(double));
  if (p->dim == NULL || p->diagonal == NULL || p->rhs == NULL) {
    ds_context_free(context, p->dim);
    ds_context_free(context, p->diagonal);
    ds_context_free(context, p->rhs);
    memset(p, 0, sizeof(*p));
    return DS_ERR_ALLOC;
  }
  for (b = 0; b < blocks; b++) {
    /* -(sizes + 1) + 1 cannot overflow, even for LONG_MIN */
    p->dim[b] =
        sizes[b] > 0 ? (size_t)sizes[b] : (size_t)(-(sizes[b] + 1)) + 1u;
    p->diagonal[b] = (unsigned char)(sizes[b] < 0);
  }
  p->m = m;
  p->blocks = blocks;
  p->context = context;
  return DS_OK;
}

void ds_sdp_destroy(ds_sdp_t *p) {
  if (p == NULL) {
    return;
  }
  ds_context_free(p->context, p->dim);
  ds_context_free(p->context, p->diagonal);
  ds_context_free(p->context, p->rhs);
  ds_context_free(p->context, p->entry);
  memset(p, 0, sizeof(*p));
}

ds_status_t ds_sdp_set_rhs(ds_sdp_t *p, size_t i, double value) {
  if (p == NULL || p->dim == NULL) {
    return DS_ERR_NULL;
  }
  if (i == 0 || i > p->m) {
    return DS_ERR_RANGE;
  }
  if (!sd_finite(value)) {
    return DS_ERR_INVALID;
  }
  p->rhs[i - 1] = value;
  return DS_OK;
}

ds_status_t ds_sdp_add(ds_sdp_t *p, size_t k, size_t block, size_t row,
                       size_t col, double value) {
  ds_sdp_entry_t *e;
  if (p == NULL || p->dim == NULL) {
    return DS_ERR_NULL;
  }
  if (k > p->m || block >= p->blocks || row >= p->dim[block] ||
      col >= p->dim[block]) {
    return DS_ERR_RANGE;
  }
  if (!sd_finite(value) || (p->diagonal[block] && row != col)) {
    return DS_ERR_INVALID;
  }
  if (p->entries == p->capacity) {
    size_t cap = p->capacity < 16 ? 32 : p->capacity * 2;
    ds_sdp_entry_t *q;
    if (cap > (size_t)-1 / sizeof(ds_sdp_entry_t)) {
      return DS_ERR_OVERFLOW;
    }
    q = (ds_sdp_entry_t *)ds_context_realloc(p->context, p->entry,
                                             cap * sizeof(ds_sdp_entry_t));
    if (q == NULL) {
      return DS_ERR_ALLOC;
    }
    p->entry = q;
    p->capacity = cap;
  }
  e = &p->entry[p->entries++];
  e->matrix = k;
  e->block = block;
  e->row = row < col ? row : col;
  e->col = row < col ? col : row;
  e->value = value;
  return DS_OK;
}

size_t ds_sdp_block_offset(const ds_sdp_t *p, size_t block) {
  size_t b;
  size_t at = 0;
  if (p == NULL || p->dim == NULL) {
    return 0;
  }
  for (b = 0; b < block && b < p->blocks; b++) {
    at += p->diagonal[b] ? p->dim[b] : p->dim[b] * p->dim[b];
  }
  return at;
}

/* ---- the SDPA format ----------------------------------------------------- */

static const char *sd_space(const char *s) {
  while (*s != '\0' && (isspace((unsigned char)*s) || *s == ',' || *s == '{' ||
                        *s == '}' || *s == '(' || *s == ')')) {
    s++;
  }
  return s;
}

/* past the end of the current line: the header lines of the SDPA format
 * may carry comments after their numbers */
static const char *sd_line(const char *s) {
  while (*s != '\0' && *s != '\n') {
    s++;
  }
  return s;
}

static bool sd_long(const char **s, long *out) {
  char *end;
  const char *t = sd_space(*s);
  *out = strtol(t, &end, 10);
  if (end == t) {
    return false;
  }
  *s = end;
  return true;
}

static bool sd_double(const char **s, double *out) {
  char *end;
  const char *t = sd_space(*s);
  *out = strtod(t, &end);
  if (end == t || !sd_finite(*out)) {
    return false;
  }
  *s = end;
  return true;
}

ds_status_t ds_sdp_parse_sdpa(ds_sdp_t *p, const char *text,
                              ds_context_t *context) {
  const char *s = text;
  long m;
  long nb;
  long i;
  long *sizes;
  ds_status_t st;
  if (p == NULL || text == NULL) {
    return DS_ERR_NULL;
  }
  memset(p, 0, sizeof(*p));
  for (;;) { /* comment lines */
    const char *t = s;
    while (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n') {
      t++;
    }
    if (*t != '"' && *t != '*') {
      break;
    }
    while (*t != '\0' && *t != '\n') {
      t++;
    }
    s = t;
  }
  if (!sd_long(&s, &m) || m < 0) {
    return DS_ERR_INVALID;
  }
  s = sd_line(s);
  if (!sd_long(&s, &nb) || nb <= 0 ||
      (unsigned long)nb > (size_t)-1 / sizeof(long)) {
    return DS_ERR_INVALID;
  }
  s = sd_line(s);
  sizes = (long *)ds_context_alloc(context, (size_t)nb * sizeof(long));
  if (sizes == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < nb; i++) {
    if (!sd_long(&s, &sizes[i]) || sizes[i] == 0) {
      ds_context_free(context, sizes);
      return DS_ERR_INVALID;
    }
  }
  s = sd_line(s);
  st = ds_sdp_init(p, (size_t)m, (size_t)nb, sizes, context);
  ds_context_free(context, sizes);
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < m && st == DS_OK; i++) {
    double a;
    st = sd_double(&s, &a) ? ds_sdp_set_rhs(p, (size_t)i + 1, a)
                           : DS_ERR_INVALID;
  }
  s = sd_line(s);
  while (st == DS_OK && *sd_space(s) != '\0') {
    long k;
    long b;
    long r;
    long c;
    double v;
    if (!sd_long(&s, &k) || !sd_long(&s, &b) || !sd_long(&s, &r) ||
        !sd_long(&s, &c) || !sd_double(&s, &v) || k < 0 || b < 1 || r < 1 ||
        c < 1) {
      st = DS_ERR_INVALID;
      break;
    }
    st = ds_sdp_add(p, (size_t)k, (size_t)b - 1, (size_t)r - 1, (size_t)c - 1,
                    v);
    if (st == DS_ERR_RANGE) {
      st = DS_ERR_INVALID;
    }
  }
  if (st != DS_OK) {
    ds_sdp_destroy(p);
  }
  return st;
}

/* ---- the problem in solving form ----------------------------------------- */

typedef struct sd {
  size_t m;
  size_t blocks;
  const size_t *dim;
  const unsigned char *diag;
  size_t *off;  /* blocks + 1 offsets into the flat arrays */
  size_t len;   /* flat length */
  size_t order; /* sum of the block orders */
  size_t maxdim;
  ds_sdp_entry_t *e; /* sorted by (matrix, block, row, col), merged */
  size_t ne;
  size_t *start; /* matrix k: e[start[k] .. start[k + 1]) */
  const double *a;
  double *a_own; /* the right-hand sides once constraints were dropped */
  size_t *orig;  /* constraint k (from 1) -> its index in the problem */
  size_t m_orig;
  ds_context_t *ctx;
} sd_t;

static int sd_cmp(const void *x, const void *y) {
  const ds_sdp_entry_t *a = (const ds_sdp_entry_t *)x;
  const ds_sdp_entry_t *b = (const ds_sdp_entry_t *)y;
  if (a->matrix != b->matrix) {
    return a->matrix < b->matrix ? -1 : 1;
  }
  if (a->block != b->block) {
    return a->block < b->block ? -1 : 1;
  }
  if (a->row != b->row) {
    return a->row < b->row ? -1 : 1;
  }
  if (a->col != b->col) {
    return a->col < b->col ? -1 : 1;
  }
  return 0;
}

static void sd_release(sd_t *s) {
  ds_context_free(s->ctx, s->off);
  ds_context_free(s->ctx, s->e);
  ds_context_free(s->ctx, s->start);
  ds_context_free(s->ctx, s->a_own);
  ds_context_free(s->ctx, s->orig);
}

static ds_status_t sd_prepare(sd_t *s, const ds_sdp_t *p) {
  size_t b;
  size_t i;
  size_t k;
  memset(s, 0, sizeof(*s));
  s->m = p->m;
  s->m_orig = p->m;
  s->blocks = p->blocks;
  s->dim = p->dim;
  s->diag = p->diagonal;
  s->a = p->rhs;
  s->ctx = p->context;
  s->off = (size_t *)ds_context_alloc(s->ctx, (p->blocks + 1) * sizeof(size_t));
  s->start = (size_t *)ds_context_calloc(s->ctx, p->m + 2, sizeof(size_t));
  s->e = (ds_sdp_entry_t *)ds_context_alloc(s->ctx, (p->entries + 1) *
                                                        sizeof(ds_sdp_entry_t));
  if (s->off == NULL || s->start == NULL || s->e == NULL) {
    sd_release(s);
    return DS_ERR_ALLOC;
  }
  for (b = 0; b < p->blocks; b++) {
    size_t d = p->dim[b];
    size_t sz;
    if (!p->diagonal[b] && d > (size_t)-1 / d) {
      sd_release(s);
      return DS_ERR_OVERFLOW;
    }
    sz = p->diagonal[b] ? d : d * d;
    s->off[b] = s->len;
    if (s->len > (size_t)-1 / sizeof(double) / 16 - sz) {
      sd_release(s);
      return DS_ERR_OVERFLOW;
    }
    s->len += sz;
    s->order += d;
    if (d > s->maxdim) {
      s->maxdim = d;
    }
  }
  s->off[p->blocks] = s->len;
  if (p->entries > 0) {
    memcpy(s->e, p->entry, p->entries * sizeof(ds_sdp_entry_t));
    qsort(s->e, p->entries, sizeof(ds_sdp_entry_t), sd_cmp);
  }
  for (i = 0; i < p->entries; i++) { /* merge equal positions, drop zeros */
    if (s->ne > 0 && sd_cmp(&s->e[s->ne - 1], &s->e[i]) == 0) {
      s->e[s->ne - 1].value += s->e[i].value;
    } else {
      s->e[s->ne++] = s->e[i];
    }
  }
  k = 0;
  for (i = 0; i < s->ne; i++) {
    if (s->e[i].value < 0.0 || s->e[i].value > 0.0) {
      s->e[k++] = s->e[i];
    }
  }
  s->ne = k;
  for (i = 0; i < s->ne; i++) {
    s->start[s->e[i].matrix + 1]++;
  }
  for (k = 0; k <= p->m; k++) {
    s->start[k + 1] += s->start[k];
  }
  return DS_OK;
}

/* <matrix k, W>, W stored flat (not necessarily symmetric) */
static double sd_inner(const sd_t *s, size_t k, const double *w) {
  double t = 0.0;
  size_t i;
  for (i = s->start[k]; i < s->start[k + 1]; i++) {
    const ds_sdp_entry_t *e = &s->e[i];
    size_t o = s->off[e->block];
    size_t d = s->dim[e->block];
    if (s->diag[e->block]) {
      t += e->value * w[o + e->row];
    } else if (e->row == e->col) {
      t += e->value * w[o + e->row * d + e->row];
    } else {
      t += e->value * (w[o + e->row * d + e->col] + w[o + e->col * d + e->row]);
    }
  }
  return t;
}

/* W += coef * matrix k */
static void sd_axpy(const sd_t *s, size_t k, double coef, double *w) {
  size_t i;
  for (i = s->start[k]; i < s->start[k + 1]; i++) {
    const ds_sdp_entry_t *e = &s->e[i];
    size_t o = s->off[e->block];
    size_t d = s->dim[e->block];
    if (s->diag[e->block]) {
      w[o + e->row] += coef * e->value;
    } else {
      w[o + e->row * d + e->col] += coef * e->value;
      if (e->row != e->col) {
        w[o + e->col * d + e->row] += coef * e->value;
      }
    }
  }
}

static double sd_norm_matrix(const sd_t *s, size_t k) {
  double t = 0.0;
  size_t i;
  for (i = s->start[k]; i < s->start[k + 1]; i++) {
    const ds_sdp_entry_t *e = &s->e[i];
    double v = e->value * e->value;
    t += !s->diag[e->block] && e->row != e->col ? 2.0 * v : v;
  }
  return sqrt(t);
}

static double sd_dot(const sd_t *s, const double *u, const double *v) {
  double t = 0.0;
  size_t i;
  for (i = 0; i < s->len; i++) {
    t += u[i] * v[i];
  }
  return t;
}

static double sd_norm2(const double *u, size_t n) {
  double t = 0.0;
  size_t i;
  for (i = 0; i < n; i++) {
    t += u[i] * u[i];
  }
  return sqrt(t);
}

/* W = U V block by block */
static ds_status_t sd_prod(const sd_t *s, const double *u, const double *v,
                           double *w) {
  size_t b;
  size_t i;
  for (b = 0; b < s->blocks; b++) {
    size_t o = s->off[b];
    size_t d = s->dim[b];
    if (s->diag[b]) {
      for (i = 0; i < d; i++) {
        w[o + i] = u[o + i] * v[o + i];
      }
    } else {
      ds_status_t st = ds_matrix_multiply(u + o, v + o, w + o, d, d, d,
                                          DS_MATMUL_NAIVE, 0, s->ctx);
      if (st != DS_OK) {
        return st;
      }
    }
  }
  return DS_OK;
}

static void sd_symmetrize(const sd_t *s, double *w) {
  size_t b;
  size_t i;
  size_t j;
  for (b = 0; b < s->blocks; b++) {
    size_t o = s->off[b];
    size_t d = s->dim[b];
    if (s->diag[b]) {
      continue;
    }
    for (i = 0; i < d; i++) {
      for (j = i + 1; j < d; j++) {
        double m = 0.5 * (w[o + i * d + j] + w[o + j * d + i]);
        w[o + i * d + j] = m;
        w[o + j * d + i] = m;
      }
    }
  }
}

/* l: the Cholesky factor of every dense block of x, x itself on diagonal
 * blocks; false if x is not positive definite */
static bool sd_factor(const sd_t *s, const double *x, double *l) {
  size_t b;
  size_t i;
  memcpy(l, x, s->len * sizeof(double));
  for (b = 0; b < s->blocks; b++) {
    size_t o = s->off[b];
    size_t d = s->dim[b];
    if (s->diag[b]) {
      for (i = 0; i < d; i++) {
        if (!(l[o + i] > 0.0)) {
          return false;
        }
      }
    } else if (ds_cholesky(l + o, d) != DS_OK) {
      return false;
    }
  }
  return true;
}

/* zi = Z^-1 from the factor l of Z; col: 2 maxdim scratch */
static void sd_inverse(const sd_t *s, const double *l, double *zi,
                       double *col) {
  size_t b;
  size_t i;
  size_t j;
  for (b = 0; b < s->blocks; b++) {
    size_t o = s->off[b];
    size_t d = s->dim[b];
    if (s->diag[b]) {
      for (i = 0; i < d; i++) {
        zi[o + i] = 1.0 / l[o + i];
      }
      continue;
    }
    for (j = 0; j < d; j++) {
      for (i = 0; i < d; i++) {
        col[i] = i == j ? 1.0 : 0.0;
      }
      (void)ds_cholesky_solve(l + o, d, col, col + d);
      for (i = 0; i < d; i++) {
        zi[o + i * d + j] = col[d + i];
      }
    }
  }
  sd_symmetrize(s, zi);
}

/* columns of the d x d row-major y become L^-1 times them */
static void sd_lower_solve(const double *l, size_t d, double *y) {
  size_t i;
  size_t j;
  size_t k;
  for (j = 0; j < d; j++) {
    for (i = 0; i < d; i++) {
      double t = y[i * d + j];
      for (k = 0; k < i; k++) {
        t -= l[i * d + k] * y[k * d + j];
      }
      y[i * d + j] = t / l[i * d + i];
    }
  }
}

/* the largest t with X + t D positive semidefinite, X = L L^T per block
 * (DBL_MAX when unbounded); scratch: 2 maxdim^2 + maxdim */
static ds_status_t sd_max_step(const sd_t *s, const double *l, const double *dx,
                               double *scratch, double *out) {
  double t = DBL_MAX;
  size_t b;
  size_t i;
  size_t j;
  for (b = 0; b < s->blocks; b++) {
    size_t o = s->off[b];
    size_t d = s->dim[b];
    if (s->diag[b]) {
      for (i = 0; i < d; i++) {
        if (dx[o + i] < 0.0 && -l[o + i] / dx[o + i] < t) {
          t = -l[o + i] / dx[o + i];
        }
      }
    } else {
      double *y = scratch;
      double *w = scratch + d * d;
      double *ev = scratch + 2 * d * d;
      ds_status_t st;
      memcpy(y, dx + o, d * d * sizeof(double));
      sd_lower_solve(l + o, d, y); /* L^-1 D */
      for (i = 0; i < d; i++) {    /* (L^-1 D)^T = D L^-T */
        for (j = 0; j < d; j++) {
          w[i * d + j] = y[j * d + i];
        }
      }
      sd_lower_solve(l + o, d, w); /* L^-1 D L^-T */
      st = ds_symmetric_eigen(w, d, ev, NULL);
      if (st != DS_OK) {
        return st;
      }
      if (ev[0] < 0.0 && -1.0 / ev[0] < t) {
        t = -1.0 / ev[0];
      }
    }
  }
  *out = t;
  return DS_OK;
}

/* M_ij = <A_i, X A_j Z^-1> (i, j from 1), m x m at M */
static void sd_schur(const sd_t *s, const double *x, const double *zi,
                     double *M, double *T, size_t *cols, unsigned char *mark,
                     double *dv) {
  size_t m = s->m;
  size_t i;
  size_t j;
  memset(M, 0, m * m * sizeof(double));
  for (j = 1; j <= m; j++) {
    size_t g0 = s->start[j];
    while (g0 < s->start[j + 1]) {
      size_t b = s->e[g0].block;
      size_t o = s->off[b];
      size_t d = s->dim[b];
      size_t g1 = g0;
      size_t t;
      size_t ncols = 0;
      while (g1 < s->start[j + 1] && s->e[g1].block == b) {
        g1++;
      }
      if (s->diag[b]) {
        for (t = g0; t < g1; t++) {
          size_t r = s->e[t].row;
          dv[r] = s->e[t].value * x[o + r] * zi[o + r];
        }
      } else { /* T = X A_j on this block, columns touched by A_j only */
        for (t = g0; t < g1; t++) {
          size_t two[2];
          size_t q;
          size_t r;
          two[0] = s->e[t].row;
          two[1] = s->e[t].col;
          for (q = 0; q < 2; q++) {
            if (!mark[two[q]]) {
              mark[two[q]] = 1;
              cols[ncols++] = two[q];
              for (r = 0; r < d; r++) {
                T[r * d + two[q]] = 0.0;
              }
            }
          }
        }
        for (t = g0; t < g1; t++) {
          size_t rr = s->e[t].row;
          size_t cc = s->e[t].col;
          double v = s->e[t].value;
          size_t r;
          for (r = 0; r < d; r++) {
            T[r * d + cc] += v * x[o + r * d + rr];
          }
          if (rr != cc) {
            for (r = 0; r < d; r++) {
              T[r * d + rr] += v * x[o + r * d + cc];
            }
          }
        }
      }
      for (i = 1; i <= j; i++) { /* <A_i, T Z^-1> over this block */
        size_t lo = s->start[i];
        size_t hi = s->start[i + 1];
        size_t u;
        double acc = 0.0;
        while (lo < hi) { /* the first entry of A_i in block b */
          size_t mid = lo + (hi - lo) / 2;
          if (s->e[mid].block < b) {
            lo = mid + 1;
          } else {
            hi = mid;
          }
        }
        for (u = lo; u < s->start[i + 1] && s->e[u].block == b; u++) {
          size_t r = s->e[u].row;
          size_t c = s->e[u].col;
          double v = s->e[u].value;
          if (s->diag[b]) {
            acc += v * dv[r];
          } else {
            double wrc = 0.0;
            size_t q;
            for (q = 0; q < ncols; q++) {
              wrc += T[r * d + cols[q]] * zi[o + cols[q] * d + c];
            }
            acc += v * wrc;
            if (r != c) {
              double wcr = 0.0;
              for (q = 0; q < ncols; q++) {
                wcr += T[c * d + cols[q]] * zi[o + cols[q] * d + r];
              }
              acc += v * wcr;
            }
          }
        }
        M[(i - 1) * m + (j - 1)] += acc;
      }
      if (s->diag[b]) {
        for (t = g0; t < g1; t++) {
          dv[s->e[t].row] = 0.0;
        }
      } else {
        for (t = 0; t < ncols; t++) {
          mark[cols[t]] = 0;
        }
      }
      g0 = g1;
    }
  }
  for (i = 0; i < m; i++) {
    for (j = 0; j < i; j++) {
      M[i * m + j] = M[j * m + i];
    }
  }
}

/* ---- dependent constraints ----------------------------------------------- */

/* Drops every constraint whose A_k is a combination of earlier kept ones,
 * found by a pivoted Cholesky factorization of the Gram matrix <A_i, A_j>
 * (a relative residual below 1e-14). A dropped constraint whose a_k does
 * not agree with the same combination of the kept a_j makes (P)
 * infeasible: *out_infeasible, and cert (m entries) gets y with
 * A^T y = 0 and a^T y = -1. Otherwise the problem is renumbered in place
 * over the kept constraints. */
static ds_status_t sd_reduce(sd_t *s, double *cert, bool *out_infeasible) {
  size_t m = s->m;
  double *G;
  double *L;
  double *t;
  double *c;
  size_t *keep;
  size_t *newidx;
  size_t nk = 0;
  size_t i;
  size_t j;
  size_t k;
  ds_status_t st = DS_OK;
  *out_infeasible = false;
  if (m == 0) {
    return DS_OK;
  }
  if (m > (size_t)-1 / sizeof(double) / m) {
    return DS_ERR_OVERFLOW;
  }
  G = (double *)ds_context_alloc(s->ctx, m * m * sizeof(double));
  L = (double *)ds_context_alloc(s->ctx, m * m * sizeof(double));
  t = (double *)ds_context_calloc(s->ctx, s->len + 1, sizeof(double));
  c = (double *)ds_context_alloc(s->ctx, m * sizeof(double));
  keep = (size_t *)ds_context_alloc(s->ctx, m * sizeof(size_t));
  newidx = (size_t *)ds_context_calloc(s->ctx, m + 1, sizeof(size_t));
  if (G == NULL || L == NULL || t == NULL || c == NULL || keep == NULL ||
      newidx == NULL) {
    st = DS_ERR_ALLOC;
  }
  for (k = 1; k <= m && st == DS_OK; k++) {
    sd_axpy(s, k, 1.0, t);
    for (j = 1; j <= k; j++) {
      double g = sd_inner(s, j, t);
      G[(j - 1) * m + (k - 1)] = g;
      G[(k - 1) * m + (j - 1)] = g;
    }
    for (i = s->start[k]; i < s->start[k + 1]; i++) { /* exact zeros */
      const ds_sdp_entry_t *e = &s->e[i];
      size_t o = s->off[e->block];
      size_t d = s->dim[e->block];
      if (s->diag[e->block]) {
        t[o + e->row] = 0.0;
      } else {
        t[o + e->row * d + e->col] = 0.0;
        t[o + e->col * d + e->row] = 0.0;
      }
    }
  }
  for (k = 1; k <= m && st == DS_OK; k++) {
    double gkk = G[(k - 1) * m + (k - 1)];
    double r = gkk;
    size_t q;
    for (q = 0; q < nk; q++) { /* c = L^-1 G[keep, k] */
      double v = G[(keep[q] - 1) * m + (k - 1)];
      for (j = 0; j < q; j++) {
        v -= L[q * m + j] * c[j];
      }
      c[q] = v / L[q * m + q];
      r -= c[q] * c[q];
    }
    if (gkk > 0.0 && r > 1e-14 * gkk) {
      for (q = 0; q < nk; q++) {
        L[nk * m + q] = c[q];
      }
      L[nk * m + nk] = sqrt(r);
      keep[nk++] = k;
      newidx[k] = nk;
    } else { /* A_k = sum_q c_q A_keep[q], c = L^-T (L^-1 G[keep, k]) */
      double delta = s->a[k - 1];
      double scale = 1.0 + fabs(s->a[k - 1]);
      for (q = nk; q-- > 0;) {
        double v = c[q];
        for (j = q + 1; j < nk; j++) {
          v -= L[j * m + q] * c[j];
        }
        c[q] = v / L[q * m + q];
      }
      for (q = 0; q < nk; q++) {
        delta -= c[q] * s->a[keep[q] - 1];
        scale += fabs(c[q] * s->a[keep[q] - 1]);
      }
      if (fabs(delta) > 1e-9 * scale) { /* inconsistent: a certificate */
        memset(cert, 0, m * sizeof(double));
        cert[k - 1] = -1.0 / delta;
        for (q = 0; q < nk; q++) {
          cert[keep[q] - 1] = c[q] / delta;
        }
        *out_infeasible = true;
        break;
      }
    }
  }
  if (st == DS_OK && !*out_infeasible && nk < m) { /* renumber */
    size_t at = 0;
    s->a_own = (double *)ds_context_alloc(s->ctx, (nk + 1) * sizeof(double));
    if (s->a_own == NULL) {
      st = DS_ERR_ALLOC;
    } else {
      for (i = 0; i < s->ne; i++) {
        size_t mk = s->e[i].matrix;
        if (mk == 0 || newidx[mk] != 0) {
          s->e[at] = s->e[i];
          s->e[at].matrix = mk == 0 ? 0 : newidx[mk];
          at++;
        }
      }
      s->ne = at;
      memset(s->start, 0, (m + 2) * sizeof(size_t));
      for (i = 0; i < s->ne; i++) {
        s->start[s->e[i].matrix + 1]++;
      }
      for (k = 0; k <= nk; k++) {
        s->start[k + 1] += s->start[k];
      }
      for (k = 0; k < nk; k++) {
        s->a_own[k] = s->a[keep[k] - 1];
      }
      s->a = s->a_own;
      s->orig = keep;
      keep = NULL;
      s->m = nk;
    }
  }
  ds_context_free(s->ctx, G);
  ds_context_free(s->ctx, L);
  ds_context_free(s->ctx, t);
  ds_context_free(s->ctx, c);
  ds_context_free(s->ctx, keep);
  ds_context_free(s->ctx, newidx);
  return st;
}

/* ---- the solver ---------------------------------------------------------- */

/* chol(M) into M from the Schur matrix in Mc; when rounding (or dependent
 * constraints, whose system stays consistent) leaves it numerically
 * singular, a growing ridge on the diagonal */
static bool sd_factor_schur(size_t m, const double *mc, double *M) {
  double big = 0.0;
  double ridge = 0.0;
  size_t i;
  int t;
  for (i = 0; i < m; i++) {
    if (mc[i * m + i] > big) {
      big = mc[i * m + i];
    }
  }
  if (!(big > 0.0)) {
    big = 1.0;
  }
  for (t = 0; t < 6; t++) {
    memcpy(M, mc, m * m * sizeof(double));
    for (i = 0; i < m; i++) {
      M[i * m + i] += ridge;
    }
    if (ds_cholesky(M, m) == DS_OK) {
      return true;
    }
    ridge = ridge > 0.0 ? ridge * 100.0 : big * 1e-14;
  }
  return false;
}

typedef struct sd_work {
  double *x, *z, *y;
  double *px, *pz, *py; /* the previous iterate */
  double *lx, *lz, *zi, *rd, *g, *t1, *t2;
  double *dxa, *dza, *dya, *dx, *dz, *dy;
  double *rp, *rhs, *tm, *M, *Mc, *G, *T, *dv, *scratch;
  size_t *cols;
  unsigned char *mark;
} sd_work_t;

/* the HKM direction for target mu, with Mehrotra's second-order term
 * from the affine step when corrector is set; M holds chol(M) */
static ds_status_t sd_direction(const sd_t *s, sd_work_t *w, double mu,
                                bool corrector, double *dx, double *dy,
                                double *dz) {
  size_t i;
  size_t k;
  ds_status_t st;
  /* G = mu Z^-1 - X + X Rd Z^-1 (- dXa dZa Z^-1) */
  st = sd_prod(s, w->x, w->rd, w->t1);
  if (st == DS_OK) {
    st = sd_prod(s, w->t1, w->zi, w->g);
  }
  if (st == DS_OK && corrector) {
    st = sd_prod(s, w->dxa, w->dza, w->t1);
    if (st == DS_OK) {
      st = sd_prod(s, w->t1, w->zi, w->t2);
    }
    for (i = 0; i < s->len && st == DS_OK; i++) {
      w->g[i] -= w->t2[i];
    }
  }
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < s->len; i++) {
    w->g[i] += mu * w->zi[i] - w->x[i];
  }
  for (k = 1; k <= s->m; k++) {
    w->rhs[k - 1] = sd_inner(s, k, w->g) - w->rp[k - 1];
  }
  if (s->m > 0) { /* with one step of refinement against M itself */
    size_t j;
    (void)ds_cholesky_solve(w->M, s->m, w->rhs, dy);
    for (i = 0; i < s->m; i++) {
      double r = w->rhs[i];
      for (j = 0; j < s->m; j++) {
        r -= w->Mc[i * s->m + j] * dy[j];
      }
      w->tm[i] = r;
    }
    (void)ds_cholesky_solve(w->M, s->m, w->tm, w->tm);
    for (i = 0; i < s->m; i++) {
      dy[i] += w->tm[i];
    }
  }
  /* dZ = A^T(dy) - Rd */
  for (i = 0; i < s->len; i++) {
    dz[i] = -w->rd[i];
  }
  for (k = 1; k <= s->m; k++) {
    sd_axpy(s, k, dy[k - 1], dz);
  }
  /* dX = mu Z^-1 - X - (X dZ (+ dXa dZa)) Z^-1, symmetrized */
  st = sd_prod(s, w->x, dz, w->t1);
  if (st == DS_OK && corrector) {
    st = sd_prod(s, w->dxa, w->dza, w->t2);
    for (i = 0; i < s->len && st == DS_OK; i++) {
      w->t1[i] += w->t2[i];
    }
  }
  if (st == DS_OK) {
    st = sd_prod(s, w->t1, w->zi, w->t2);
  }
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < s->len; i++) {
    dx[i] = mu * w->zi[i] - w->x[i] - w->t2[i];
  }
  sd_symmetrize(s, dx);
  /* rounding in the products with Z^-1, which grows near the optimum,
   * would leak into A(dX): project dX back onto A(dX) = rp through the
   * Gram matrix of the constraints */
  if (s->m > 0) {
    for (k = 1; k <= s->m; k++) {
      w->tm[k - 1] = w->rp[k - 1] - sd_inner(s, k, dx);
    }
    (void)ds_cholesky_solve(w->G, s->m, w->tm, w->tm);
    for (k = 1; k <= s->m; k++) {
      sd_axpy(s, k, w->tm[k - 1], dx);
    }
  }
  return DS_OK;
}

static void sd_free_work(const sd_t *s, sd_work_t *w) {
  double **d[27];
  size_t i;
  size_t n = 0;
  d[n++] = &w->x;
  d[n++] = &w->z;
  d[n++] = &w->y;
  d[n++] = &w->px;
  d[n++] = &w->pz;
  d[n++] = &w->py;
  d[n++] = &w->lx;
  d[n++] = &w->lz;
  d[n++] = &w->zi;
  d[n++] = &w->rd;
  d[n++] = &w->g;
  d[n++] = &w->t1;
  d[n++] = &w->t2;
  d[n++] = &w->dxa;
  d[n++] = &w->dza;
  d[n++] = &w->dya;
  d[n++] = &w->dx;
  d[n++] = &w->dz;
  d[n++] = &w->dy;
  d[n++] = &w->rp;
  d[n++] = &w->rhs;
  d[n++] = &w->tm;
  d[n++] = &w->M;
  d[n++] = &w->Mc;
  d[n++] = &w->G;
  d[n++] = &w->T;
  d[n++] = &w->dv;
  for (i = 0; i < n; i++) {
    ds_context_free(s->ctx, *d[i]);
    *d[i] = NULL;
  }
  ds_context_free(s->ctx, w->scratch);
  ds_context_free(s->ctx, w->cols);
  ds_context_free(s->ctx, w->mark);
  w->scratch = NULL;
  w->cols = NULL;
  w->mark = NULL;
}

static ds_status_t sd_alloc_work(const sd_t *s, sd_work_t *w) {
  size_t L = s->len + 1;
  size_t m = s->m + 1;
  size_t md = s->maxdim + 1;
  bool ok;
  memset(w, 0, sizeof(*w));
  if (s->m > (size_t)-1 / sizeof(double) / (s->m + 1) ||
      md > (size_t)-1 / sizeof(double) / md / 4) {
    return DS_ERR_OVERFLOW;
  }
  w->x = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->z = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->px = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->pz = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->lx = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->lz = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->zi = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->rd = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->g = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->t1 = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->t2 = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->dxa = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->dza = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->dx = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->dz = (double *)ds_context_alloc(s->ctx, L * sizeof(double));
  w->y = (double *)ds_context_calloc(s->ctx, m, sizeof(double));
  w->py = (double *)ds_context_calloc(s->ctx, m, sizeof(double));
  w->dya = (double *)ds_context_calloc(s->ctx, m, sizeof(double));
  w->dy = (double *)ds_context_calloc(s->ctx, m, sizeof(double));
  w->rp = (double *)ds_context_calloc(s->ctx, m, sizeof(double));
  w->rhs = (double *)ds_context_calloc(s->ctx, m, sizeof(double));
  w->tm = (double *)ds_context_calloc(s->ctx, m, sizeof(double));
  w->M = (double *)ds_context_alloc(s->ctx, m * m * sizeof(double));
  w->Mc = (double *)ds_context_alloc(s->ctx, m * m * sizeof(double));
  w->G = (double *)ds_context_alloc(s->ctx, m * m * sizeof(double));
  w->T = (double *)ds_context_alloc(s->ctx, md * md * sizeof(double));
  w->dv = (double *)ds_context_calloc(s->ctx, md, sizeof(double));
  w->scratch =
      (double *)ds_context_alloc(s->ctx, (2 * md * md + md) * sizeof(double));
  w->cols = (size_t *)ds_context_alloc(s->ctx, md * sizeof(size_t));
  w->mark = (unsigned char *)ds_context_calloc(s->ctx, md, 1);
  ok = w->x && w->z && w->px && w->pz && w->lx && w->lz && w->zi && w->rd &&
       w->g && w->t1 && w->t2 && w->dxa && w->dza && w->dx && w->dz && w->y &&
       w->py && w->dya && w->dy && w->rp && w->rhs && w->tm && w->M && w->Mc &&
       w->G && w->T && w->dv && w->scratch && w->cols && w->mark;
  if (!ok) {
    sd_free_work(s, w);
    return DS_ERR_ALLOC;
  }
  return DS_OK;
}

/* the current iterate into the solution's arrays */
static void sd_output(const sd_t *s, const sd_work_t *w,
                      ds_sdp_solution_t *out) {
  size_t k;
  memcpy(out->x, w->x, s->len * sizeof(double));
  memcpy(out->z, w->z, s->len * sizeof(double));
  for (k = 0; k < s->m; k++) { /* back to the problem's numbering */
    out->y[s->orig != NULL ? s->orig[k] - 1 : k] = w->y[k];
  }
}

void ds_sdp_options_init(ds_sdp_options_t *options) {
  if (options != NULL) {
    options->tolerance = 0.0;
    options->max_iterations = 0;
  }
}

void ds_sdp_solution_destroy(ds_sdp_solution_t *s) {
  if (s == NULL) {
    return;
  }
  ds_context_free(s->context, s->x);
  ds_context_free(s->context, s->y);
  ds_context_free(s->context, s->z);
  s->x = NULL;
  s->y = NULL;
  s->z = NULL;
}

/* residuals and objectives of the current iterate into out */
static void sd_measure(const sd_t *s, sd_work_t *w, double norm_a,
                       double norm_c, ds_sdp_solution_t *out) {
  size_t i;
  size_t k;
  double pobj = sd_inner(s, 0, w->x);
  double dobj = 0.0;
  for (k = 1; k <= s->m; k++) {
    w->rp[k - 1] = s->a[k - 1] - sd_inner(s, k, w->x);
    dobj += s->a[k - 1] * w->y[k - 1];
  }
  /* Rd = C - A^T(y) + Z */
  for (i = 0; i < s->len; i++) {
    w->rd[i] = w->z[i];
  }
  sd_axpy(s, 0, 1.0, w->rd);
  for (k = 1; k <= s->m; k++) {
    sd_axpy(s, k, -w->y[k - 1], w->rd);
  }
  out->primal_objective = pobj;
  out->dual_objective = dobj;
  out->primal_infeasibility = sd_norm2(w->rp, s->m) / (1.0 + norm_a);
  out->dual_infeasibility = sd_norm2(w->rd, s->len) / (1.0 + norm_c);
  out->gap = fabs(dobj - pobj) / (1.0 + fabs(pobj) + fabs(dobj));
}

ds_status_t ds_sdp_solve(const ds_sdp_t *p, const ds_sdp_options_t *options,
                         ds_sdp_solution_t *out) {
  sd_t s;
  sd_work_t w;
  double tol = 1e-8;
  size_t maxit = 100;
  double norm_a;
  double norm_c;
  double alpha = 1.0;
  double beta = 1.0;
  size_t i;
  size_t k;
  size_t it;
  size_t stalls = 0;
  size_t since_best = 0;
  bool broken;
  bool have_best = false;
  ds_sdp_solution_t best;
  double best_gap = 0.0;
  ds_status_t st;
  ds_sdp_status_t result = DS_SDP_ITERATION_LIMIT;
  if (p == NULL || out == NULL || p->dim == NULL) {
    return DS_ERR_NULL;
  }
  memset(out, 0, sizeof(*out));
  out->context = p->context;
  if (options != NULL) {
    if (options->tolerance > 0.0) {
      tol = options->tolerance;
    }
    if (options->max_iterations > 0) {
      maxit = options->max_iterations;
    }
  }
  st = sd_prepare(&s, p);
  if (st != DS_OK) {
    return st;
  }
  out->length = s.len;
  out->x = (double *)ds_context_calloc(s.ctx, s.len + 1, sizeof(double));
  out->z = (double *)ds_context_calloc(s.ctx, s.len + 1, sizeof(double));
  out->y = (double *)ds_context_calloc(s.ctx, p->m + 1, sizeof(double));
  if (out->x == NULL || out->y == NULL || out->z == NULL) {
    ds_sdp_solution_destroy(out);
    sd_release(&s);
    return DS_ERR_ALLOC;
  }
  st = sd_reduce(&s, out->y, &broken);
  if (st == DS_OK && broken) { /* inconsistent dependent constraints */
    out->status = DS_SDP_PRIMAL_INFEASIBLE;
    out->dual_objective = -1.0;
    sd_release(&s);
    return DS_OK;
  }
  if (st == DS_OK) {
    st = sd_alloc_work(&s, &w);
  }
  if (st != DS_OK) {
    ds_sdp_solution_destroy(out);
    sd_release(&s);
    return st;
  }
  /* the starting point of CSDP: X = 10 alpha I, Z = 10 beta I, y = 0 */
  norm_a = sd_norm2(s.a, s.m);
  norm_c = sd_norm_matrix(&s, 0);
  {
    double big = norm_c;
    for (k = 1; k <= s.m; k++) {
      double nk = sd_norm_matrix(&s, k);
      double r = (1.0 + fabs(s.a[k - 1])) / (1.0 + nk);
      if (r > alpha) {
        alpha = r;
      }
      if (nk > big) {
        big = nk;
      }
    }
    alpha *= (double)s.order;
    beta = (1.0 + big) / sqrt((double)s.order);
  }
  memset(w.x, 0, s.len * sizeof(double));
  memset(w.z, 0, s.len * sizeof(double));
  for (k = 0; k < s.blocks; k++) {
    size_t d = s.dim[k];
    for (i = 0; i < d; i++) {
      size_t at = s.off[k] + (s.diag[k] ? i : i * d + i);
      w.x[at] = 10.0 * alpha;
      w.z[at] = 10.0 * beta;
    }
  }
  broken = !sd_factor(&s, w.x, w.lx) || !sd_factor(&s, w.z, w.lz);
  /* the Gram matrix <A_i, A_j> of the constraints, factored */
  memset(w.t1, 0, s.len * sizeof(double));
  for (k = 1; k <= s.m; k++) {
    size_t j;
    sd_axpy(&s, k, 1.0, w.t1);
    for (j = 1; j <= k; j++) {
      double g = sd_inner(&s, j, w.t1);
      w.Mc[(j - 1) * s.m + (k - 1)] = g;
      w.Mc[(k - 1) * s.m + (j - 1)] = g;
    }
    sd_axpy(&s, k, -1.0, w.t1);
    for (i = s.start[k]; i < s.start[k + 1]; i++) { /* exact zeros again */
      const ds_sdp_entry_t *e = &s.e[i];
      size_t o = s.off[e->block];
      size_t d = s.dim[e->block];
      if (s.diag[e->block]) {
        w.t1[o + e->row] = 0.0;
      } else {
        w.t1[o + e->row * d + e->col] = 0.0;
        w.t1[o + e->col * d + e->row] = 0.0;
      }
    }
  }
  if (s.m > 0 && !sd_factor_schur(s.m, w.Mc, w.G)) {
    broken = true; /* NaN data */
  }
  if (broken) {
    sd_measure(&s, &w, norm_a, norm_c, out);
    result = DS_SDP_STALLED;
  }
  for (it = 0; !broken; it++) {
    double ap = 0.0;
    double ad = 0.0;
    double apa;
    double ada;
    double gap0;
    double gaff;
    double sigma;
    double mu;
    double score;
    int tries;
    sd_inverse(&s, w.lz, w.zi, w.scratch);
    sd_measure(&s, &w, norm_a, norm_c, out);
    out->iterations = it;
    score = out->primal_infeasibility;
    score = out->dual_infeasibility > score ? out->dual_infeasibility : score;
    score = out->gap > score ? out->gap : score;
    if (score < tol) {
      result = DS_SDP_OPTIMAL;
      break;
    }
    if (!have_best || score < 0.9 * best.gap) {
      /* the best iterate so far (best.gap holds its score), returned if
       * the tolerance is never met */
      best = *out;
      best.gap = score;
      best_gap = out->gap;
      have_best = true;
      since_best = 0;
      sd_output(&s, &w, out);
    } else if (++since_best > 15) {
      result = DS_SDP_STALLED;
      break;
    }
    /* certificates: a ray of (D) (y with a^T y -> -inf) or of (P) */
    if (out->dual_objective < 0.0) {
      for (i = 0; i < s.len; i++) {
        w.t1[i] = -w.rd[i];
      }
      sd_axpy(&s, 0, 1.0, w.t1); /* C - Rd = A^T(y) - Z */
      if (sd_norm2(w.t1, s.len) / -out->dual_objective < tol) {
        result = DS_SDP_PRIMAL_INFEASIBLE;
        break;
      }
    }
    if (out->primal_objective > 0.0) {
      double r = 0.0;
      for (k = 1; k <= s.m; k++) {
        double t = s.a[k - 1] - w.rp[k - 1]; /* A(X)_k */
        r += t * t;
      }
      if (sqrt(r) / out->primal_objective < tol) {
        result = DS_SDP_DUAL_INFEASIBLE;
        break;
      }
    }
    if (it >= maxit) {
      break;
    }
    sd_schur(&s, w.x, w.zi, w.Mc, w.T, w.cols, w.mark, w.dv);
    if (s.m > 0 && !sd_factor_schur(s.m, w.Mc, w.M)) {
      result = DS_SDP_STALLED;
      break;
    }
    /* predictor */
    st = sd_direction(&s, &w, 0.0, false, w.dxa, w.dya, w.dza);
    if (st == DS_OK) {
      st = sd_max_step(&s, w.lx, w.dxa, w.scratch, &ap);
    }
    if (st == DS_OK) {
      st = sd_max_step(&s, w.lz, w.dza, w.scratch, &ad);
    }
    if (st != DS_OK) {
      break;
    }
    ap = ap > 1.0 ? 1.0 : ap;
    ad = ad > 1.0 ? 1.0 : ad;
    apa = ap < ad ? ap : ad;
    gap0 = sd_dot(&s, w.x, w.z);
    gaff = gap0 + ap * sd_dot(&s, w.dxa, w.z) + ad * sd_dot(&s, w.x, w.dza) +
           ap * ad * sd_dot(&s, w.dxa, w.dza);
    sigma = gap0 > 0.0 ? gaff / gap0 : 0.0;
    sigma = sigma < 0.0 ? 0.0 : sigma > 1.0 ? 1.0 : sigma;
    sigma = sigma * sigma * sigma;
    mu = sigma * gap0 / (double)s.order;
    /* corrector */
    st = sd_direction(&s, &w, mu, true, w.dx, w.dy, w.dz);
    if (st == DS_OK) {
      st = sd_max_step(&s, w.lx, w.dx, w.scratch, &ap);
    }
    if (st == DS_OK) {
      st = sd_max_step(&s, w.lz, w.dz, w.scratch, &ad);
    }
    ada = ap < ad ? ap : ad;
    if (st == DS_OK && ada < 0.5 * apa) {
      /* the second-order term hurt: a plain centering direction instead,
       * at least halfway to the centre */
      mu = (sigma > 0.5 ? sigma : 0.5) * gap0 / (double)s.order;
      st = sd_direction(&s, &w, mu, false, w.dx, w.dy, w.dz);
      if (st == DS_OK) {
        st = sd_max_step(&s, w.lx, w.dx, w.scratch, &ap);
      }
      if (st == DS_OK) {
        st = sd_max_step(&s, w.lz, w.dz, w.scratch, &ad);
      }
    }
    if (st != DS_OK) {
      break;
    }
    ap = 0.95 * ap > 1.0 ? 1.0 : 0.95 * ap;
    ad = 0.95 * ad > 1.0 ? 1.0 : 0.95 * ad;
    memcpy(w.px, w.x, s.len * sizeof(double));
    memcpy(w.pz, w.z, s.len * sizeof(double));
    memcpy(w.py, w.y, s.m * sizeof(double));
    /* the step, shortened while rounding leaves X or Z not positive
     * definite */
    for (tries = 0;; tries++) {
      bool okx;
      bool okz;
      for (i = 0; i < s.len; i++) {
        w.x[i] = w.px[i] + ap * w.dx[i];
        w.z[i] = w.pz[i] + ad * w.dz[i];
      }
      okx = sd_factor(&s, w.x, w.lx);
      okz = sd_factor(&s, w.z, w.lz);
      if (okx && okz) {
        break;
      }
      if (tries == 40) {
        ap = 0.0;
        ad = 0.0;
        memcpy(w.x, w.px, s.len * sizeof(double));
        memcpy(w.z, w.pz, s.len * sizeof(double));
        (void)sd_factor(&s, w.x, w.lx);
        (void)sd_factor(&s, w.z, w.lz);
        break;
      }
      ap = okx ? ap : 0.5 * ap;
      ad = okz ? ad : 0.5 * ad;
    }
    for (k = 0; k < s.m; k++) {
      w.y[k] = w.py[k] + ad * w.dy[k];
    }
    stalls = ap < 1e-10 && ad < 1e-10 ? stalls + 1 : 0;
    if (stalls >= 3) {
      result = DS_SDP_STALLED;
      break;
    }
  }
  if (st == DS_ERR_STATE) { /* an eigenvalue computation did not converge */
    st = DS_OK;
    result = DS_SDP_STALLED;
  }
  if (st == DS_OK) {
    if (result == DS_SDP_ITERATION_LIMIT || result == DS_SDP_STALLED) {
      /* the best iterate, already in out's arrays */
      if (have_best) {
        size_t its = out->iterations;
        double score = best.gap;
        *out = best;
        out->gap = best_gap;
        out->iterations = its;
        if (score <= 1e3 * tol) {
          result = DS_SDP_NEAR_OPTIMAL;
        }
      }
    } else {
      sd_output(&s, &w, out);
    }
    out->status = result;
    if (result == DS_SDP_PRIMAL_INFEASIBLE) { /* scale to a^T y = -1 */
      double sc = -out->dual_objective;
      for (k = 0; k < s.m_orig; k++) {
        out->y[k] /= sc;
      }
      for (i = 0; i < s.len; i++) {
        out->z[i] /= sc;
      }
    } else if (result == DS_SDP_DUAL_INFEASIBLE) { /* to <C, X> = 1 */
      double sc = out->primal_objective;
      for (i = 0; i < s.len; i++) {
        out->x[i] /= sc;
      }
    }
  } else {
    ds_sdp_solution_destroy(out);
  }
  sd_free_work(&s, &w);
  sd_release(&s);
  return st;
}

/* ---- applications -------------------------------------------------------- */

static int sd_cmp_pair(const void *x, const void *y) {
  const size_t *a = (const size_t *)x;
  const size_t *b = (const size_t *)y;
  if (a[0] != b[0]) {
    return a[0] < b[0] ? -1 : 1;
  }
  return a[1] < b[1] ? -1 : a[1] > b[1] ? 1 : 0;
}

ds_status_t ds_sdp_lovasz_theta(size_t n, const size_t *u, const size_t *v,
                                size_t edges, double *out,
                                ds_context_t *context) {
  size_t *pair;
  size_t k = 0;
  size_t i;
  size_t j;
  long size;
  ds_sdp_t p;
  ds_sdp_solution_t sol;
  ds_status_t st;
  if (out == NULL || (edges > 0 && (u == NULL || v == NULL))) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < edges; i++) {
    if (u[i] >= n || v[i] >= n) {
      return DS_ERR_RANGE;
    }
  }
  if (n == 0) {
    *out = 0.0;
    return DS_OK;
  }
  if (n > (size_t)LONG_MAX || edges > (size_t)-1 / (2 * sizeof(size_t))) {
    return DS_ERR_OVERFLOW;
  }
  pair = (size_t *)ds_context_alloc(context, (2 * edges + 2) * sizeof(size_t));
  if (pair == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < edges; i++) {
    if (u[i] != v[i]) {
      pair[2 * k] = u[i] < v[i] ? u[i] : v[i];
      pair[2 * k + 1] = u[i] < v[i] ? v[i] : u[i];
      k++;
    }
  }
  if (k > 0) {
    qsort(pair, k, 2 * sizeof(size_t), sd_cmp_pair);
  }
  j = 0;
  for (i = 0; i < k; i++) {
    if (j == 0 || pair[2 * i] != pair[2 * j - 2] ||
        pair[2 * i + 1] != pair[2 * j - 1]) {
      pair[2 * j] = pair[2 * i];
      pair[2 * j + 1] = pair[2 * i + 1];
      j++;
    }
  }
  k = j;
  size = (long)n;
  st = ds_sdp_init(&p, k + 1, 1, &size, context);
  for (i = 0; i < n && st == DS_OK; i++) {
    for (j = i; j < n && st == DS_OK; j++) {
      st = ds_sdp_add(&p, 0, 0, i, j, 1.0); /* C = J */
    }
    if (st == DS_OK) {
      st = ds_sdp_add(&p, 1, 0, i, i, 1.0); /* trace X = 1 */
    }
  }
  if (st == DS_OK) {
    st = ds_sdp_set_rhs(&p, 1, 1.0);
  }
  for (i = 0; i < k && st == DS_OK; i++) { /* X_uv = 0 */
    st = ds_sdp_add(&p, i + 2, 0, pair[2 * i], pair[2 * i + 1], 1.0);
  }
  ds_context_free(context, pair);
  if (st == DS_OK) {
    st = ds_sdp_solve(&p, NULL, &sol);
    if (st == DS_OK) {
      if (sol.status == DS_SDP_OPTIMAL) {
        *out = sol.dual_objective;
      } else {
        st = DS_ERR_STATE;
      }
      ds_sdp_solution_destroy(&sol);
    }
  }
  ds_sdp_destroy(&p);
  return st;
}

ds_status_t ds_sdp_maxcut(size_t n, const size_t *u, const size_t *v,
                          const double *w, size_t edges, unsigned long seed,
                          size_t trials, unsigned char *side, double *out_cut,
                          double *out_bound, ds_context_t *context) {
  ds_sdp_t p;
  ds_sdp_solution_t sol;
  double *gram = NULL;
  double *val = NULL;
  double *r = NULL;
  unsigned char *cur = NULL;
  long size;
  size_t i;
  size_t j;
  size_t t;
  double best = 0.0;
  ds_rng_t rng;
  ds_status_t st;
  if (out_cut == NULL || (n > 0 && side == NULL) ||
      (edges > 0 && (u == NULL || v == NULL || w == NULL))) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < edges; i++) {
    if (u[i] >= n || v[i] >= n) {
      return DS_ERR_RANGE;
    }
    if (!sd_finite(w[i])) {
      return DS_ERR_INVALID;
    }
  }
  if (n == 0) {
    *out_cut = 0.0;
    if (out_bound != NULL) {
      *out_bound = 0.0;
    }
    return DS_OK;
  }
  if (n > (size_t)LONG_MAX || n > (size_t)-1 / sizeof(double) / n) {
    return DS_ERR_OVERFLOW;
  }
  size = (long)n;
  st = ds_sdp_init(&p, n, 1, &size, context);
  for (i = 0; i < n && st == DS_OK; i++) { /* X_ii = 1 */
    st = ds_sdp_add(&p, i + 1, 0, i, i, 1.0);
    if (st == DS_OK) {
      st = ds_sdp_set_rhs(&p, i + 1, 1.0);
    }
  }
  for (i = 0; i < edges && st == DS_OK; i++) { /* C = L / 4 */
    if (u[i] == v[i]) {
      continue;
    }
    st = ds_sdp_add(&p, 0, 0, u[i], u[i], w[i] / 4.0);
    if (st == DS_OK) {
      st = ds_sdp_add(&p, 0, 0, v[i], v[i], w[i] / 4.0);
    }
    if (st == DS_OK) {
      st = ds_sdp_add(&p, 0, 0, u[i], v[i], -w[i] / 4.0);
    }
  }
  if (st == DS_OK) {
    st = ds_sdp_solve(&p, NULL, &sol);
    if (st == DS_OK && sol.status != DS_SDP_OPTIMAL) {
      ds_sdp_solution_destroy(&sol);
      st = DS_ERR_STATE;
    }
  }
  ds_sdp_destroy(&p);
  if (st != DS_OK) {
    return st;
  }
  if (out_bound != NULL) {
    *out_bound = sol.dual_objective;
  }
  /* Gram vectors: X = Q diag(l) Q^T, row i of Q diag(sqrt l) */
  gram = (double *)ds_context_alloc(context, n * n * sizeof(double));
  val = (double *)ds_context_alloc(context, n * sizeof(double));
  r = (double *)ds_context_alloc(context, n * sizeof(double));
  cur = (unsigned char *)ds_context_alloc(context, n);
  if (gram == NULL || val == NULL || r == NULL || cur == NULL) {
    st = DS_ERR_ALLOC;
  }
  if (st == DS_OK) {
    st = ds_symmetric_eigen(sol.x, n, val, gram);
  }
  if (st == DS_OK) {
    for (j = 0; j < n; j++) {
      double sq = val[j] > 0.0 ? sqrt(val[j]) : 0.0;
      for (i = 0; i < n; i++) {
        gram[i * n + j] *= sq;
      }
    }
    ds_rng_seed(&rng, seed);
    for (t = 0; t < (trials > 0 ? trials : 1); t++) {
      double cut = 0.0;
      for (j = 0; j < n; j++) {
        r[j] = ds_rng_normal(&rng);
      }
      for (i = 0; i < n; i++) {
        double dot = 0.0;
        for (j = 0; j < n; j++) {
          dot += gram[i * n + j] * r[j];
        }
        cur[i] = (unsigned char)(dot >= 0.0);
      }
      for (i = 0; i < edges; i++) {
        if (cur[u[i]] != cur[v[i]]) {
          cut += w[i];
        }
      }
      if (t == 0 || cut > best) {
        best = cut;
        memcpy(side, cur, n);
      }
    }
    *out_cut = best;
  }
  ds_sdp_solution_destroy(&sol);
  ds_context_free(context, gram);
  ds_context_free(context, val);
  ds_context_free(context, r);
  ds_context_free(context, cur);
  return st;
}
