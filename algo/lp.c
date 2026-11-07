#include "lp.h"
#include <math.h>
#include <string.h>

#define LP_NONE (~(size_t)0)
#define LP_EPS 1e-9

/* A working copy of a problem whose rows can grow (cuts, bounds). */
typedef struct lp_work {
  size_t rows;
  size_t cap;
  size_t cols;
  double *a;
  ds_lp_sense_t *sense;
  double *b;
  ds_context_t *context;
} lp_work_t;

static ds_status_t lw_init(lp_work_t *w, const ds_lp_t *lp,
                           ds_context_t *context) {
  size_t cap = lp->rows + lp->cols + 16;
  memset(w, 0, sizeof(*w));
  w->cols = lp->cols;
  w->context = context;
  if (cap > ((size_t)-1) / (lp->cols + 1) / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  w->a = (double *)ds_context_alloc(context,
                                    cap * (lp->cols + 1) * sizeof(double));
  w->sense =
      (ds_lp_sense_t *)ds_context_alloc(context, cap * sizeof(ds_lp_sense_t));
  w->b = (double *)ds_context_alloc(context, cap * sizeof(double));
  if (w->a == NULL || w->sense == NULL || w->b == NULL) {
    ds_context_free(context, w->a);
    ds_context_free(context, w->sense);
    ds_context_free(context, w->b);
    return DS_ERR_ALLOC;
  }
  w->cap = cap;
  if (lp->rows > 0) {
    memcpy(w->a, lp->a, lp->rows * lp->cols * sizeof(double));
    memcpy(w->sense, lp->sense, lp->rows * sizeof(ds_lp_sense_t));
    memcpy(w->b, lp->b, lp->rows * sizeof(double));
  }
  w->rows = lp->rows;
  return DS_OK;
}

static void lw_destroy(lp_work_t *w) {
  ds_context_free(w->context, w->a);
  ds_context_free(w->context, w->sense);
  ds_context_free(w->context, w->b);
}

static ds_status_t lw_add(lp_work_t *w, const double *row, ds_lp_sense_t s,
                          double rhs) {
  void *p;
  size_t cap;
  if (w->rows == w->cap) {
    cap = w->cap * 2;
    if (cap > ((size_t)-1) / (w->cols + 1) / sizeof(double)) {
      return DS_ERR_OVERFLOW;
    }
    p = ds_context_realloc(w->context, w->a,
                           cap * w->cols * sizeof(double) + sizeof(double));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    w->a = (double *)p;
    p = ds_context_realloc(w->context, w->sense, cap * sizeof(ds_lp_sense_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    w->sense = (ds_lp_sense_t *)p;
    p = ds_context_realloc(w->context, w->b, cap * sizeof(double));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    w->b = (double *)p;
    w->cap = cap;
  }
  memcpy(w->a + w->rows * w->cols, row, w->cols * sizeof(double));
  w->sense[w->rows] = s;
  w->b[w->rows] = rhs;
  w->rows++;
  return DS_OK;
}

/* ---- the tableau simplex ----------------------------------------------------
 */

typedef struct lp_tab {
  size_t m;
  size_t n;  /* original columns */
  size_t nc; /* all columns */
  size_t w;  /* row width: nc + 1 */
  double *t; /* (m + 1) x w, last row: reduced costs z_j - c_j */
  size_t *basis;
  size_t *unit;        /* per row: the column that started as e_i */
  double *flip;        /* per row: +1 or -1 */
  unsigned char *kind; /* per column: 0 x, 1 slack, 2 surplus, 3 art */
  size_t *rowof;       /* per non-x column: its row */
  double *cost;        /* per column, current phase */
} lp_tab_t;

static double *tb_at(lp_tab_t *t, size_t i, size_t j) {
  return t->t + i * t->w + j;
}

static void tb_pivot(lp_tab_t *t, size_t r, size_t e) {
  size_t i;
  size_t j;
  double p = *tb_at(t, r, e);
  double f;
  for (j = 0; j < t->w; j++) {
    *tb_at(t, r, j) /= p;
  }
  for (i = 0; i <= t->m; i++) {
    if (i == r) {
      continue;
    }
    f = *tb_at(t, i, e);
    if (f < -1e-15 || f > 1e-15) {
      for (j = 0; j < t->w; j++) {
        *tb_at(t, i, j) -= f * *tb_at(t, r, j);
      }
    }
  }
  t->basis[r] = e;
}

static void tb_price(lp_tab_t *t) {
  size_t i;
  size_t j;
  double *z = tb_at(t, t->m, 0);
  for (j = 0; j < t->w; j++) {
    z[j] = j < t->nc ? -t->cost[j] : 0.0;
  }
  for (i = 0; i < t->m; i++) {
    double cb = t->cost[t->basis[i]];
    if (cb < -1e-15 || cb > 1e-15) {
      for (j = 0; j < t->w; j++) {
        z[j] += cb * *tb_at(t, i, j);
      }
    }
  }
}

/* Maximise the current costs; returns DS_LP_OPTIMAL or DS_LP_UNBOUNDED. */
static ds_lp_result_t tb_run(lp_tab_t *t, bool allow_art) {
  size_t e;
  size_t r;
  size_t i;
  size_t j;
  double best;
  double ratio;
  for (;;) {
    e = LP_NONE;
    for (j = 0; j < t->nc; j++) {
      if ((allow_art || t->kind[j] != 3) && *tb_at(t, t->m, j) < -LP_EPS) {
        e = j; /* Bland: the lowest improving column */
        break;
      }
    }
    if (e == LP_NONE) {
      return DS_LP_OPTIMAL;
    }
    r = LP_NONE;
    best = HUGE_VAL;
    for (i = 0; i < t->m; i++) {
      double a = *tb_at(t, i, e);
      if (a > LP_EPS) {
        ratio = *tb_at(t, i, t->nc) / a;
        if (ratio < best - 1e-12 || (ratio < best + 1e-12 && r != LP_NONE &&
                                     t->basis[i] < t->basis[r])) {
          best = ratio;
          r = i;
        }
      }
    }
    if (r == LP_NONE) {
      return DS_LP_UNBOUNDED;
    }
    tb_pivot(t, r, e);
  }
}

static void tb_free(lp_tab_t *t, ds_context_t *context) {
  ds_context_free(context, t->t);
  ds_context_free(context, t->basis);
  ds_context_free(context, t->kind);
}

/* Two-phase simplex on (m x n rows, max c). On DS_LP_OPTIMAL the tableau
 * is kept in *t for the caller (who frees it with tb_free). */
static ds_status_t tb_solve(const double *a, const ds_lp_sense_t *sense,
                            const double *b, const double *c, size_t m,
                            size_t n, lp_tab_t *t, ds_lp_result_t *res,
                            ds_context_t *context) {
  size_t extra = 0;
  size_t i;
  size_t j;
  size_t col;
  ds_lp_result_t r;
  memset(t, 0, sizeof(*t));
  for (i = 0; i < m; i++) {
    double s = b[i] < 0.0 ? -1.0 : 1.0;
    ds_lp_sense_t sn = sense[i];
    if (s < 0.0 && sn != DS_LP_EQ) {
      sn = sn == DS_LP_LE ? DS_LP_GE : DS_LP_LE;
    }
    extra += sn == DS_LP_LE ? 1 : (sn == DS_LP_GE ? 2 : 1);
  }
  t->m = m;
  t->n = n;
  t->nc = n + extra;
  t->w = t->nc + 1;
  if (t->w > ((size_t)-1) / (m + 1) / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  t->t = (double *)ds_context_calloc(context, (m + 1) * t->w, sizeof(double));
  t->basis = (size_t *)ds_context_alloc(context, (3 * m + 2 * t->nc + 1) *
                                                     sizeof(size_t));
  t->kind = (unsigned char *)ds_context_calloc(context, t->nc + 1, 1);
  if (t->t == NULL || t->basis == NULL || t->kind == NULL) {
    tb_free(t, context);
    return DS_ERR_ALLOC;
  }
  t->unit = t->basis + m;
  t->rowof = t->unit + m;
  t->flip = (double *)(void *)(t->rowof + t->nc);
  t->cost = t->flip + m;
  /* build: flip rows with negative right-hand sides */
  col = n;
  for (i = 0; i < m; i++) {
    double s = b[i] < 0.0 ? -1.0 : 1.0;
    ds_lp_sense_t sn = sense[i];
    if (s < 0.0 && sn != DS_LP_EQ) {
      sn = sn == DS_LP_LE ? DS_LP_GE : DS_LP_LE;
    }
    t->flip[i] = s;
    for (j = 0; j < n; j++) {
      *tb_at(t, i, j) = s * a[i * n + j];
    }
    *tb_at(t, i, t->nc) = s * b[i];
    if (sn == DS_LP_LE) {
      *tb_at(t, i, col) = 1.0;
      t->kind[col] = 1;
      t->rowof[col] = i;
      t->unit[i] = col;
      t->basis[i] = col++;
    } else {
      if (sn == DS_LP_GE) {
        *tb_at(t, i, col) = -1.0;
        t->kind[col] = 2;
        t->rowof[col++] = i;
      }
      *tb_at(t, i, col) = 1.0;
      t->kind[col] = 3;
      t->rowof[col] = i;
      t->unit[i] = col;
      t->basis[i] = col++;
    }
  }
  /* phase 1: maximise minus the sum of the artificials */
  for (j = 0; j < t->nc; j++) {
    t->cost[j] = t->kind[j] == 3 ? -1.0 : 0.0;
  }
  tb_price(t);
  (void)tb_run(t, true);
  if (*tb_at(t, m, t->nc) < -1e-7) {
    *res = DS_LP_INFEASIBLE;
    return DS_OK;
  }
  /* drive zero artificials out of the basis where possible */
  for (i = 0; i < m; i++) {
    if (t->kind[t->basis[i]] == 3) {
      for (j = 0; j < t->nc; j++) {
        double v = *tb_at(t, i, j);
        if (t->kind[j] != 3 && (v > 1e-9 || v < -1e-9)) {
          tb_pivot(t, i, j);
          break;
        }
      }
    }
  }
  /* phase 2 */
  for (j = 0; j < t->nc; j++) {
    t->cost[j] = j < n ? c[j] : 0.0;
  }
  tb_price(t);
  r = tb_run(t, false);
  *res = r;
  return DS_OK;
}

static ds_status_t lp_check(const ds_lp_t *lp) {
  if (lp == NULL || (lp->cols > 0 && lp->c == NULL) ||
      (lp->rows > 0 && (lp->a == NULL || lp->b == NULL || lp->sense == NULL))) {
    return DS_ERR_NULL;
  }
  return DS_OK;
}

static ds_status_t lp_solve_rows(const double *a, const ds_lp_sense_t *sense,
                                 const double *b, size_t m, const ds_lp_t *lp,
                                 double *x, double *dual, double *obj,
                                 ds_lp_result_t *res, lp_tab_t *keep,
                                 ds_context_t *context) {
  lp_tab_t t;
  double *c;
  size_t i;
  size_t j;
  ds_status_t st;
  c = (double *)ds_context_alloc(context, (lp->cols + 1) * sizeof(double));
  if (c == NULL) {
    return DS_ERR_ALLOC;
  }
  for (j = 0; j < lp->cols; j++) {
    c[j] = lp->maximize ? lp->c[j] : -lp->c[j];
  }
  st = tb_solve(a, sense, b, c, m, lp->cols, &t, res, context);
  ds_context_free(context, c);
  if (st != DS_OK) {
    return st;
  }
  if (*res == DS_LP_OPTIMAL) {
    for (j = 0; j < lp->cols; j++) {
      x[j] = 0.0;
    }
    for (i = 0; i < m; i++) {
      if (t.basis[i] < lp->cols) {
        x[t.basis[i]] = *tb_at(&t, i, t.nc);
      }
    }
    *obj = *tb_at(&t, m, t.nc);
    if (!lp->maximize) {
      *obj = -*obj;
    }
    if (dual != NULL) {
      for (i = 0; i < m; i++) {
        double y = *tb_at(&t, m, t.unit[i]) * t.flip[i];
        dual[i] = lp->maximize ? y : -y;
      }
    }
  }
  if (keep != NULL && *res == DS_LP_OPTIMAL) {
    *keep = t;
  } else {
    tb_free(&t, context);
  }
  return DS_OK;
}

ds_status_t ds_lp_solve(const ds_lp_t *lp, double *x, double *dual,
                        double *out_objective, ds_lp_result_t *out_result,
                        ds_context_t *context) {
  ds_status_t st = lp_check(lp);
  if (st != DS_OK) {
    return st;
  }
  if ((lp->cols > 0 && x == NULL) || out_objective == NULL ||
      out_result == NULL) {
    return DS_ERR_NULL;
  }
  *out_objective = 0.0;
  return lp_solve_rows(lp->a, lp->sense, lp->b, lp->rows, lp, x, dual,
                       out_objective, out_result, NULL, context);
}

/* ---- duality ----------------------------------------------------------------
 */

ds_status_t ds_lp_dual_build(const ds_lp_t *lp, ds_lp_owned_t *d,
                             ds_context_t *context) {
  size_t m;
  size_t n;
  size_t nv = 0;
  size_t i;
  size_t j;
  size_t v;
  ds_status_t st = lp_check(lp);
  if (st != DS_OK) {
    return st;
  }
  if (d == NULL) {
    return DS_ERR_NULL;
  }
  memset(d, 0, sizeof(*d));
  d->context = context;
  m = lp->rows;
  n = lp->cols;
  /* one dual variable per row, two for an equality (free variable) */
  for (i = 0; i < m; i++) {
    nv += lp->sense[i] == DS_LP_EQ ? 2 : 1;
  }
  if (nv > 0 && n > ((size_t)-1) / nv / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  d->a = (double *)ds_context_alloc(context, (n * nv + 1) * sizeof(double));
  d->sense = (ds_lp_sense_t *)ds_context_alloc(context,
                                               (n + 1) * sizeof(ds_lp_sense_t));
  d->b = (double *)ds_context_alloc(context, (n + 1) * sizeof(double));
  d->c = (double *)ds_context_alloc(context, (nv + 1) * sizeof(double));
  d->row_of = (size_t *)ds_context_alloc(context, (nv + 1) * sizeof(size_t));
  d->sign = (double *)ds_context_alloc(context, (nv + 1) * sizeof(double));
  if (d->a == NULL || d->sense == NULL || d->b == NULL || d->c == NULL ||
      d->row_of == NULL || d->sign == NULL) {
    ds_lp_owned_destroy(d);
    return DS_ERR_ALLOC;
  }
  /* primal max: rows <= give y >= 0, >= give y <= 0 (y = -y'), = give a
   * free y (y+ - y-); dual: min b^T y, A^T y >= c. A primal min mirrors
   * it: >= give y >= 0, <= give y <= 0, dual max with A^T y <= c. */
  v = 0;
  for (i = 0; i < m; i++) {
    double s;
    if (lp->sense[i] == DS_LP_EQ) {
      d->row_of[v] = i;
      d->sign[v++] = 1.0;
      d->row_of[v] = i;
      d->sign[v++] = -1.0;
      continue;
    }
    if (lp->maximize) {
      s = lp->sense[i] == DS_LP_LE ? 1.0 : -1.0;
    } else {
      s = lp->sense[i] == DS_LP_GE ? 1.0 : -1.0;
    }
    d->row_of[v] = i;
    d->sign[v++] = s;
  }
  for (v = 0; v < nv; v++) {
    i = d->row_of[v];
    d->c[v] = d->sign[v] * lp->b[i];
    for (j = 0; j < n; j++) {
      d->a[j * nv + v] = d->sign[v] * lp->a[i * n + j];
    }
  }
  for (j = 0; j < n; j++) {
    d->b[j] = lp->c[j];
    d->sense[j] = lp->maximize ? DS_LP_GE : DS_LP_LE;
  }
  d->lp.rows = n;
  d->lp.cols = nv;
  d->lp.a = d->a;
  d->lp.sense = d->sense;
  d->lp.b = d->b;
  d->lp.c = d->c;
  d->lp.maximize = !lp->maximize;
  return DS_OK;
}

void ds_lp_owned_destroy(ds_lp_owned_t *o) {
  if (o == NULL) {
    return;
  }
  ds_context_free(o->context, o->a);
  ds_context_free(o->context, o->sense);
  ds_context_free(o->context, o->b);
  ds_context_free(o->context, o->c);
  ds_context_free(o->context, o->row_of);
  ds_context_free(o->context, o->sign);
  memset(o, 0, sizeof(*o));
}

/* ---- integer programming ----------------------------------------------------
 */

static bool lp_is_int(double v) {
  return fabs(v - floor(v + 0.5)) < 1e-6;
}

/* Gomory fractional cut from the optimal tableau, written over the
 * original variables into row/rhs (>=). False when no row qualifies. */
static bool lp_gomory(lp_tab_t *t, const lp_work_t *w,
                      const unsigned char *integer, double *row, double *rhs) {
  size_t i;
  size_t j;
  size_t r = LP_NONE;
  double best = 0.0;
  double f0;
  for (i = 0; i < t->m; i++) {
    size_t bj = t->basis[i];
    double v = *tb_at(t, i, t->nc);
    double f = v - floor(v);
    /* the most fractional integral basic variable */
    if (bj < t->n && (integer == NULL || integer[bj]) && f > 1e-6 &&
        f < 1 - 1e-6 && (r == LP_NONE || fabs(f - 0.5) < fabs(best - 0.5))) {
      r = i;
      best = f;
    }
  }
  if (r == LP_NONE) {
    return false;
  }
  f0 = best;
  for (j = 0; j < t->n; j++) {
    row[j] = 0.0;
  }
  *rhs = f0;
  /* sum over nonbasic columns of frac(a_rj) * var_j >= f0 */
  for (j = 0; j < t->nc; j++) {
    double a = *tb_at(t, r, j);
    double f = a - floor(a);
    size_t k;
    size_t ri;
    bool basic = false;
    if (f > 1.0 - 1e-9) {
      f = 0.0; /* an integer up to rounding */
    }
    for (k = 0; k < t->m && !basic; k++) {
      basic = t->basis[k] == j;
    }
    if (basic || f < 1e-9 || t->kind[j] == 3) {
      continue;
    }
    if (j < t->n) {
      row[j] += f;
      continue;
    }
    /* slack of a (flipped) <= row: s = b' - a'x; surplus: t = a'x - b' */
    ri = t->rowof[j];
    for (k = 0; k < t->n; k++) {
      double ak = t->flip[ri] * w->a[ri * w->cols + k];
      row[k] += t->kind[j] == 1 ? -f * ak : f * ak;
    }
    {
      double bk = t->flip[ri] * w->b[ri];
      *rhs -= t->kind[j] == 1 ? f * bk : -f * bk;
    }
  }
  return true;
}

ds_status_t ds_milp_solve(const ds_lp_t *lp, const unsigned char *integer,
                          ds_milp_method_t method, size_t budget, double *x,
                          double *out_objective, ds_lp_result_t *out_result,
                          ds_context_t *context) {
  lp_work_t w;
  double *row;
  double *lo;
  double *hi;
  double *xs;
  double *bound_row;
  size_t n;
  size_t top = 0;
  size_t cap;
  size_t base_rows;
  size_t j;
  double best = -HUGE_VAL; /* in maximisation terms */
  bool have = false;
  bool limited = false;
  double sgn;
  ds_status_t st = lp_check(lp);
  if (st != DS_OK) {
    return st;
  }
  if ((lp->cols > 0 && x == NULL) || out_objective == NULL ||
      out_result == NULL) {
    return DS_ERR_NULL;
  }
  if (method != DS_MILP_BRANCH_AND_BOUND && method != DS_MILP_CUTTING_PLANE) {
    return DS_ERR_INVALID;
  }
  n = lp->cols;
  sgn = lp->maximize ? 1.0 : -1.0;
  st = lw_init(&w, lp, context);
  if (st != DS_OK) {
    return st;
  }
  row = (double *)ds_context_alloc(context, (3 * n + 1) * sizeof(double));
  if (row == NULL) {
    lw_destroy(&w);
    return DS_ERR_ALLOC;
  }
  xs = row + n;
  bound_row = xs + n;
  /* cutting planes on the root problem: Gomory fractional cuts are
   * valid only when every variable (slacks included) is integral, i.e.
   * pure integer programs with integral data */
  if (method == DS_MILP_CUTTING_PLANE) {
    size_t q;
    for (j = 0; j < n && (integer == NULL || integer[j]); j++) {
    }
    if (j < n) {
      method = DS_MILP_BRANCH_AND_BOUND;
    }
    for (q = 0; q < lp->rows * n && method == DS_MILP_CUTTING_PLANE; q++) {
      if (!lp_is_int(lp->a[q])) {
        method = DS_MILP_BRANCH_AND_BOUND;
      }
    }
    for (q = 0; q < lp->rows && method == DS_MILP_CUTTING_PLANE; q++) {
      if (!lp_is_int(lp->b[q])) {
        method = DS_MILP_BRANCH_AND_BOUND;
      }
    }
  }
  if (method == DS_MILP_CUTTING_PLANE) {
    size_t cuts = 0;
    for (;;) {
      lp_tab_t t;
      ds_lp_result_t r = DS_LP_INFEASIBLE;
      double obj = 0.0;
      double rhs = 0.0;
      st = lp_solve_rows(w.a, w.sense, w.b, w.rows, lp, xs, NULL, &obj, &r, &t,
                         context);
      if (st != DS_OK) {
        goto done;
      }
      if (r != DS_LP_OPTIMAL) {
        if (cuts == 0) {
          *out_result = r;
          ds_context_free(context, row);
          lw_destroy(&w);
          return DS_OK;
        }
        /* rounding made the cuts too strong: plain branch and bound */
        w.rows = lp->rows;
        break;
      }
      for (j = 0;
           j < n && (integer != NULL && !integer[j] ? true : lp_is_int(xs[j]));
           j++) {
      }
      if (j == n) {
        size_t q;
        bool feasible = true;
        memcpy(x, xs, n * sizeof(double));
        for (j = 0; j < n; j++) {
          if (integer == NULL || integer[j]) {
            x[j] = floor(x[j] + 0.5);
          }
        }
        /* check the rounded point against the original rows */
        for (q = 0; q < lp->rows && feasible; q++) {
          double lhs = 0.0;
          for (j = 0; j < n; j++) {
            lhs += lp->a[q * n + j] * x[j];
          }
          feasible = lp->sense[q] == DS_LP_LE   ? lhs <= lp->b[q] + 1e-6
                     : lp->sense[q] == DS_LP_GE ? lhs >= lp->b[q] - 1e-6
                                                : fabs(lhs - lp->b[q]) < 1e-6;
        }
        if (!feasible) {
          tb_free(&t, context);
          w.rows = lp->rows;
          break;
        }
        *out_objective = obj;
        *out_result = DS_LP_OPTIMAL;
        tb_free(&t, context);
        ds_context_free(context, row);
        lw_destroy(&w);
        return DS_OK;
      }
      if ((budget != 0 && cuts >= budget) || cuts >= 64 * (n + 1) ||
          !lp_gomory(&t, &w, integer, row, &rhs)) {
        tb_free(&t, context);
        break; /* fall back to branch and bound on the cut problem */
      }
      tb_free(&t, context);
      st = lw_add(&w, row, DS_LP_GE, rhs);
      if (st != DS_OK) {
        goto done;
      }
      cuts++;
    }
  }
  /* depth-first branch and bound: nodes are (lo, hi) bound vectors */
  base_rows = w.rows;
  cap = 2 * n + 4;
  lo = (double *)ds_context_alloc(context, cap * 2 * n * sizeof(double) + 1);
  if (lo == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (j = 0; j < n; j++) {
    lo[j] = 0.0;
    lo[n + j] = HUGE_VAL;
  }
  top = 1;
  while (top > 0) {
    ds_lp_result_t r = DS_LP_INFEASIBLE;
    double obj = 0.0;
    size_t bj = LP_NONE;
    double frac = 0.0;
    double *node;
    top--;
    node = lo + top * 2 * n;
    hi = node + n;
    if (budget != 0 && budget-- == 1) {
      limited = true;
      break;
    }
    /* the node's bounds as extra rows */
    w.rows = base_rows;
    for (j = 0; j < n && st == DS_OK; j++) {
      if (node[j] > 0.0 || hi[j] < HUGE_VAL) {
        memset(bound_row, 0, n * sizeof(double));
        bound_row[j] = 1.0;
        if (node[j] > 0.0) {
          st = lw_add(&w, bound_row, DS_LP_GE, node[j]);
        }
        if (st == DS_OK && hi[j] < HUGE_VAL) {
          st = lw_add(&w, bound_row, DS_LP_LE, hi[j]);
        }
      }
    }
    if (st == DS_OK) {
      st = lp_solve_rows(w.a, w.sense, w.b, w.rows, lp, xs, NULL, &obj, &r,
                         NULL, context);
    }
    if (st != DS_OK) {
      ds_context_free(context, lo);
      goto done;
    }
    if (r == DS_LP_UNBOUNDED && !have) {
      /* an unbounded relaxation: report it (integer optimum may be
       * unbounded too) */
      ds_context_free(context, lo);
      *out_result = DS_LP_UNBOUNDED;
      ds_context_free(context, row);
      lw_destroy(&w);
      return DS_OK;
    }
    if (r != DS_LP_OPTIMAL || (have && sgn * obj <= best + 1e-9)) {
      continue;
    }
    for (j = 0; j < n; j++) {
      double f = xs[j] - floor(xs[j]);
      double dist = f < 0.5 ? f : 1.0 - f;
      if ((integer == NULL || integer[j]) && !lp_is_int(xs[j]) &&
          (bj == LP_NONE || dist > frac)) {
        bj = j;
        frac = dist;
      }
    }
    if (bj == LP_NONE) {
      have = true;
      best = sgn * obj;
      memcpy(x, xs, n * sizeof(double));
      for (j = 0; j < n; j++) {
        if (integer == NULL || integer[j]) {
          x[j] = floor(x[j] + 0.5);
        }
      }
      continue;
    }
    /* two children replace the node: x_j <= floor, x_j >= ceil */
    if (top + 2 > cap) {
      void *p =
          ds_context_realloc(context, lo, 2 * cap * 2 * n * sizeof(double) + 1);
      if (p == NULL) {
        ds_context_free(context, lo);
        st = DS_ERR_ALLOC;
        goto done;
      }
      lo = (double *)p;
      cap *= 2;
    }
    {
      double *a1 = lo + top * 2 * n;
      double *a2 = lo + (top + 1) * 2 * n;
      memcpy(a2, a1, 2 * n * sizeof(double));
      /* the popped node's bounds are still in place at index top */
      a1[n + bj] = floor(xs[bj]);
      a2[bj] = ceil(xs[bj]);
      top += 2;
    }
  }
  ds_context_free(context, lo);
  *out_result = have ? (limited ? DS_LP_LIMIT : DS_LP_OPTIMAL)
                     : (limited ? DS_LP_LIMIT : DS_LP_INFEASIBLE);
  if (have) {
    *out_objective = sgn * best;
  }
done:
  ds_context_free(context, row);
  lw_destroy(&w);
  return st;
}
