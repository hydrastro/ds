#include "geo_opt.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static unsigned long go_rand(unsigned long *s) {
  *s = (*s * 1103515245UL + 12345UL) & 0xFFFFFFFFUL;
  return (*s >> 8) & 0xFFFFFFUL;
}

static void go_shuffle(size_t *idx, size_t n, unsigned long seed) {
  size_t i;
  unsigned long s = seed ^ 0x5DEECE66UL;
  for (i = n; i > 1; i--) {
    size_t j = (size_t)(go_rand(&s) % (unsigned long)i);
    size_t t = idx[i - 1];
    idx[i - 1] = idx[j];
    idx[j] = t;
  }
}

static bool go_finite(const double *x, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    if (!(x[i] > -HUGE_VAL && x[i] < HUGE_VAL)) {
      return false;
    }
  }
  return true;
}

/* ---- smallest enclosing circle
 * ------------------------------------------------ */

static bool go_in(double cx, double cy, double r, const double *p) {
  double dx = p[0] - cx;
  double dy = p[1] - cy;
  return sqrt(dx * dx + dy * dy) <= r * (1.0 + 1e-12) + 1e-12;
}

static void go_circle2(const double *a, const double *b, double *cx, double *cy,
                       double *r) {
  *cx = (a[0] + b[0]) / 2.0;
  *cy = (a[1] + b[1]) / 2.0;
  *r = sqrt((a[0] - *cx) * (a[0] - *cx) + (a[1] - *cy) * (a[1] - *cy));
}

static void go_circle3(const double *a, const double *b, const double *c,
                       double *cx, double *cy, double *r) {
  double bx = b[0] - a[0];
  double by = b[1] - a[1];
  double qx = c[0] - a[0];
  double qy = c[1] - a[1];
  double d = 2.0 * (bx * qy - by * qx);
  if (!(fabs(d) > 0.0)) {
    /* collinear: the farthest pair */
    double r1;
    double r2;
    double r3;
    double x1;
    double y1;
    double x2;
    double y2;
    double x3;
    double y3;
    go_circle2(a, b, &x1, &y1, &r1);
    go_circle2(a, c, &x2, &y2, &r2);
    go_circle2(b, c, &x3, &y3, &r3);
    if (r1 >= r2 && r1 >= r3) {
      *cx = x1;
      *cy = y1;
      *r = r1;
    } else if (r2 >= r3) {
      *cx = x2;
      *cy = y2;
      *r = r2;
    } else {
      *cx = x3;
      *cy = y3;
      *r = r3;
    }
    return;
  }
  {
    double b2 = bx * bx + by * by;
    double c2 = qx * qx + qy * qy;
    double ux = (qy * b2 - by * c2) / d;
    double uy = (bx * c2 - qx * b2) / d;
    *cx = a[0] + ux;
    *cy = a[1] + uy;
    *r = sqrt(ux * ux + uy * uy);
  }
}

ds_status_t ds_geo_min_circle(const double *xy, size_t n, unsigned long seed,
                              double *out_cx, double *out_cy, double *out_r,
                              ds_context_t *context) {
  size_t *idx;
  size_t i;
  size_t j;
  size_t k;
  double cx;
  double cy;
  double r;
  if (xy == NULL || out_cx == NULL || out_cy == NULL || out_r == NULL) {
    return DS_ERR_NULL;
  }
  if (n == 0 || !go_finite(xy, 2 * n)) {
    return DS_ERR_INVALID;
  }
  idx = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  if (idx == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    idx[i] = i;
  }
  go_shuffle(idx, n, seed);
  cx = xy[2 * idx[0]];
  cy = xy[2 * idx[0] + 1];
  r = 0.0;
  for (i = 1; i < n; i++) {
    const double *p = xy + 2 * idx[i];
    if (go_in(cx, cy, r, p)) {
      continue;
    }
    cx = p[0];
    cy = p[1];
    r = 0.0;
    for (j = 0; j < i; j++) {
      const double *q = xy + 2 * idx[j];
      if (go_in(cx, cy, r, q)) {
        continue;
      }
      go_circle2(p, q, &cx, &cy, &r);
      for (k = 0; k < j; k++) {
        const double *s = xy + 2 * idx[k];
        if (!go_in(cx, cy, r, s)) {
          go_circle3(p, q, s, &cx, &cy, &r);
        }
      }
    }
  }
  ds_context_free(context, idx);
  *out_cx = cx;
  *out_cy = cy;
  *out_r = r;
  return DS_OK;
}

/* ---- prune and search: centre on a line
 * ------------------------------------------ */

ds_status_t ds_geo_center_on_line(const double *xy, size_t n, double px,
                                  double py, double dx, double dy,
                                  double *out_cx, double *out_cy, double *out_r,
                                  ds_context_t *context) {
  double *t;  /* coordinate along the line */
  double *h2; /* squared distance to the line */
  double *mid;
  size_t live;
  size_t i;
  double len;
  double best_t = 0.0;
  if (xy == NULL || out_cx == NULL || out_cy == NULL || out_r == NULL) {
    return DS_ERR_NULL;
  }
  len = sqrt(dx * dx + dy * dy);
  if (n == 0 || !(len > 0.0) || !go_finite(xy, 2 * n)) {
    return DS_ERR_INVALID;
  }
  dx /= len;
  dy /= len;
  t = (double *)ds_context_alloc(context, n * sizeof(double));
  h2 = (double *)ds_context_alloc(context, n * sizeof(double));
  mid = (double *)ds_context_alloc(context, (n / 2 + 1) * sizeof(double));
  if (t == NULL || h2 == NULL || mid == NULL) {
    ds_context_free(context, t);
    ds_context_free(context, h2);
    ds_context_free(context, mid);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    double qx = xy[2 * i] - px;
    double qy = xy[2 * i + 1] - py;
    double h;
    t[i] = qx * dx + qy * dy;
    h = -qx * dy + qy * dx;
    h2[i] = h * h;
  }
  /* F(s) = max_i (s - t_i)^2 + h2_i is convex; prune and search on s,
   * keeping the interval [lo_s, hi_s] known to hold the minimum (on it the
   * surviving points still define F) */
  {
    double lo_s = -HUGE_VAL;
    double hi_s = HUGE_VAL;
    live = n;
    for (;;) {
      size_t pairs = 0;
      size_t keep = 0;
      double sm;
      double fmax = -HUGE_VAL;
      double tmin = HUGE_VAL;
      double tmax = -HUGE_VAL;
      /* pairs: drop dominated points outright, collect inner bisectors */
      for (i = 0; i + 1 < live; i += 2) {
        double a = t[i];
        double b = t[i + 1];
        size_t drop = live;
        if (!(a < b) && !(a > b)) {
          drop = h2[i] >= h2[i + 1] ? i + 1 : i;
        } else {
          double m = (h2[i + 1] - h2[i] + b * b - a * a) / (2.0 * (b - a));
          /* right of m the smaller t is farther, left of m the larger */
          if (m <= lo_s) {
            drop = a > b ? i : i + 1;
          } else if (m >= hi_s) {
            drop = a < b ? i : i + 1;
          } else {
            mid[pairs++] = m;
          }
        }
        if (drop != i) {
          t[keep] = t[i];
          h2[keep++] = h2[i];
        }
        if (drop != i + 1) {
          t[keep] = t[i + 1];
          h2[keep++] = h2[i + 1];
        }
      }
      if (live % 2 == 1) {
        t[keep] = t[live - 1];
        h2[keep++] = h2[live - 1];
      }
      if (live <= 3 || pairs == 0) {
        if (keep == live) {
          break;
        }
        live = keep;
        continue;
      }
      live = keep;
      /* the median bisector */
      {
        size_t a;
        size_t b;
        for (a = 1; a < pairs; a++) {
          double x = mid[a];
          for (b = a; b > 0 && mid[b - 1] > x; b--) {
            mid[b] = mid[b - 1];
          }
          mid[b] = x;
        }
      }
      sm = mid[pairs / 2];
      /* the farthest points at sm say which side the minimum is on */
      for (i = 0; i < live; i++) {
        double f = (sm - t[i]) * (sm - t[i]) + h2[i];
        if (f > fmax) {
          fmax = f;
        }
      }
      for (i = 0; i < live; i++) {
        double f = (sm - t[i]) * (sm - t[i]) + h2[i];
        if (f >= fmax * (1.0 - 1e-14)) {
          tmin = t[i] < tmin ? t[i] : tmin;
          tmax = t[i] > tmax ? t[i] : tmax;
        }
      }
      if (tmin > sm) {
        lo_s = sm;
      } else if (tmax < sm) {
        hi_s = sm;
      } else {
        lo_s = hi_s = sm; /* sm is the minimum */
        break;
      }
    }
    /* the rest: minimise over [lo_s, hi_s] among the parabola apexes, the
     * pairwise bisectors and the interval ends */
    {
      double bestf = HUGE_VAL;
      size_t a;
      size_t b;
      best_t = lo_s > -HUGE_VAL ? lo_s : (hi_s < HUGE_VAL ? hi_s : t[0]);
      for (a = 0; a < live + 2; a++) {
        for (b = a; b < live + 1; b++) {
          double sv;
          double f = -HUGE_VAL;
          size_t c;
          if (a == live) {
            sv = lo_s;
          } else if (a == live + 1) {
            sv = hi_s;
          } else if (b == a) {
            sv = t[a];
          } else if (b == live || (!(t[a] < t[b]) && !(t[a] > t[b]))) {
            continue;
          } else {
            sv = (h2[b] - h2[a] + t[b] * t[b] - t[a] * t[a]) /
                 (2.0 * (t[b] - t[a]));
          }
          if (!(sv >= lo_s) || !(sv <= hi_s) || !(sv > -HUGE_VAL) ||
              !(sv < HUGE_VAL)) {
            continue;
          }
          for (c = 0; c < live; c++) {
            double v = (sv - t[c]) * (sv - t[c]) + h2[c];
            f = v > f ? v : f;
          }
          if (f < bestf) {
            bestf = f;
            best_t = sv;
          }
          if (a >= live) {
            break;
          }
        }
      }
    }
  }
  /* the radius over all points */
  {
    double f = 0.0;
    for (i = 0; i < n; i++) {
      double ex = xy[2 * i] - (px + best_t * dx);
      double ey = xy[2 * i + 1] - (py + best_t * dy);
      double v = ex * ex + ey * ey;
      f = v > f ? v : f;
    }
    *out_cx = px + best_t * dx;
    *out_cy = py + best_t * dy;
    *out_r = sqrt(f);
  }
  ds_context_free(context, t);
  ds_context_free(context, h2);
  ds_context_free(context, mid);
  return DS_OK;
}

/* ---- two-variable LP
 * ------------------------------------------------------------- */

ds_status_t ds_geo_lp2(const double *abc, size_t m, double cx, double cy,
                       unsigned long seed, double *out_x, double *out_y,
                       ds_context_t *context) {
  double *h;
  size_t *idx;
  size_t total;
  size_t i;
  size_t j;
  double big = 1.0;
  double x;
  double y;
  double eps;
  ds_status_t st = DS_OK;
  if ((m > 0 && abc == NULL) || out_x == NULL || out_y == NULL) {
    return DS_ERR_NULL;
  }
  if (!go_finite(abc, 3 * m) || !(cx > -HUGE_VAL && cx < HUGE_VAL) ||
      !(cy > -HUGE_VAL && cy < HUGE_VAL)) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < 3 * m; i++) {
    big = fabs(abc[i]) > big ? fabs(abc[i]) : big;
  }
  big *= 1e6;
  eps = 1e-9 * big / 1e6;
  total = m + 4;
  h = (double *)ds_context_alloc(context, 3 * total * sizeof(double));
  idx = (size_t *)ds_context_alloc(context, total * sizeof(size_t));
  if (h == NULL || idx == NULL) {
    ds_context_free(context, h);
    ds_context_free(context, idx);
    return DS_ERR_ALLOC;
  }
  /* the bounding box comes first, the rest in random order */
  h[0] = 1.0;
  h[1] = 0.0;
  h[2] = big;
  h[3] = -1.0;
  h[4] = 0.0;
  h[5] = big;
  h[6] = 0.0;
  h[7] = 1.0;
  h[8] = big;
  h[9] = 0.0;
  h[10] = -1.0;
  h[11] = big;
  if (m > 0) {
    memcpy(h + 12, abc, 3 * m * sizeof(double));
  }
  for (i = 0; i < m; i++) {
    idx[i] = i + 4;
  }
  go_shuffle(idx, m, seed);
  /* optimum over the box */
  x = cx > 0.0 ? big : (cx < 0.0 ? -big : 0.0);
  y = cy > 0.0 ? big : (cy < 0.0 ? -big : 0.0);
  for (i = 0; i < m && st == DS_OK; i++) {
    const double *c = h + 3 * idx[i];
    double nrm = sqrt(c[0] * c[0] + c[1] * c[1]);
    double px;
    double py;
    double ux;
    double uy;
    double lo = -HUGE_VAL;
    double hi = HUGE_VAL;
    double slope;
    if (c[0] * x + c[1] * y <= c[2] + eps * (nrm > 1.0 ? nrm : 1.0)) {
      continue;
    }
    if (!(nrm > 0.0)) {
      st = DS_NOT_FOUND; /* 0 <= c with c < 0 */
      break;
    }
    /* the new optimum lies on the line c: p + s u */
    px = c[0] * c[2] / (nrm * nrm);
    py = c[1] * c[2] / (nrm * nrm);
    ux = -c[1] / nrm;
    uy = c[0] / nrm;
    for (j = 0; j < 4 + i; j++) {
      const double *e = h + 3 * (j < 4 ? j : idx[j - 4]);
      double den = e[0] * ux + e[1] * uy;
      double num = e[2] - e[0] * px - e[1] * py;
      if (fabs(den) <= 1e-15 * (fabs(e[0]) + fabs(e[1]))) {
        if (num < -eps * (fabs(e[0]) + fabs(e[1]) + 1.0)) {
          lo = 1.0;
          hi = -1.0; /* parallel and violated */
        }
        continue;
      }
      if (den > 0.0) {
        double s = num / den;
        hi = s < hi ? s : hi;
      } else {
        double s = num / den;
        lo = s > lo ? s : lo;
      }
    }
    if (lo > hi + eps) {
      st = DS_NOT_FOUND;
      break;
    }
    slope = cx * ux + cy * uy;
    {
      double s = slope > 0.0 ? hi : (slope < 0.0 ? lo : (lo + hi) / 2.0);
      if (!(s > -HUGE_VAL && s < HUGE_VAL)) {
        s = lo > -HUGE_VAL ? lo : (hi < HUGE_VAL ? hi : 0.0);
      }
      x = px + s * ux;
      y = py + s * uy;
    }
  }
  ds_context_free(context, h);
  ds_context_free(context, idx);
  if (st != DS_OK) {
    return st;
  }
  *out_x = x;
  *out_y = y;
  /* a solution on the artificial box means the program is unbounded */
  if ((fabs(x) >= big * (1.0 - 1e-9) && fabs(cx) > 0.0) ||
      (fabs(y) >= big * (1.0 - 1e-9) && fabs(cy) > 0.0)) {
    return DS_ERR_RANGE;
  }
  return DS_OK;
}

/* ---- Fermat-Weber point
 * ------------------------------------------------------------ */

ds_status_t ds_geo_fermat_weber(const double *xy, const double *weights,
                                size_t n, size_t iterations, double tolerance,
                                double *out_x, double *out_y) {
  double x = 0.0;
  double y = 0.0;
  double wsum = 0.0;
  size_t it;
  size_t i;
  if (xy == NULL || out_x == NULL || out_y == NULL) {
    return DS_ERR_NULL;
  }
  if (n == 0 || !go_finite(xy, 2 * n) ||
      (weights != NULL && !go_finite(weights, n))) {
    return DS_ERR_INVALID;
  }
  if (iterations == 0) {
    iterations = 1000;
  }
  if (!(tolerance > 0.0)) {
    tolerance = 1e-12;
  }
  for (i = 0; i < n; i++) {
    double w = weights != NULL ? weights[i] : 1.0;
    if (w < 0.0) {
      return DS_ERR_INVALID;
    }
    x += w * xy[2 * i];
    y += w * xy[2 * i + 1];
    wsum += w;
  }
  if (!(wsum > 0.0)) {
    return DS_ERR_INVALID;
  }
  x /= wsum;
  y /= wsum;
  for (it = 0; it < iterations; it++) {
    double nx = 0.0;
    double ny = 0.0;
    double den = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    double wat = 0.0; /* weight sitting exactly at (x, y) */
    double tx;
    double ty;
    double step;
    for (i = 0; i < n; i++) {
      double w = weights != NULL ? weights[i] : 1.0;
      double ex = xy[2 * i] - x;
      double ey = xy[2 * i + 1] - y;
      double d = sqrt(ex * ex + ey * ey);
      if (!(d > 1e-300)) {
        wat += w;
        continue;
      }
      nx += w * xy[2 * i] / d;
      ny += w * xy[2 * i + 1] / d;
      den += w / d;
      rx += w * ex / d;
      ry += w * ey / d;
    }
    if (!(den > 0.0)) {
      break; /* every weight at one point */
    }
    tx = nx / den;
    ty = ny / den;
    if (wat > 0.0) {
      /* Vardi-Zhang: stay put when the pull does not beat the weight */
      double r = sqrt(rx * rx + ry * ry);
      double beta;
      if (r <= wat) {
        break;
      }
      beta = wat / r;
      tx = (1.0 - beta) * tx + beta * x;
      ty = (1.0 - beta) * ty + beta * y;
    }
    step = sqrt((tx - x) * (tx - x) + (ty - y) * (ty - y));
    x = tx;
    y = ty;
    if (step <= tolerance * (1.0 + fabs(x) + fabs(y))) {
      break;
    }
  }
  *out_x = x;
  *out_y = y;
  return DS_OK;
}
