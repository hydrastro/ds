#include "geometry.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- distances ----------------------------------------------------------- */

double ds_geo_euclidean(const double *a, const double *b, size_t dim) {
  double s = 0;
  double d;
  size_t i;
  for (i = 0; i < dim; i++) {
    d = a[i] - b[i];
    s += d * d;
  }
  return sqrt(s);
}

double ds_geo_manhattan(const double *a, const double *b, size_t dim) {
  double s = 0;
  size_t i;
  for (i = 0; i < dim; i++) {
    s += fabs(a[i] - b[i]);
  }
  return s;
}

double ds_geo_chebyshev(const double *a, const double *b, size_t dim) {
  double s = 0;
  double d;
  size_t i;
  for (i = 0; i < dim; i++) {
    d = fabs(a[i] - b[i]);
    if (d > s) {
      s = d;
    }
  }
  return s;
}

double ds_geo_minkowski(const double *a, const double *b, size_t dim,
                        double p) {
  double s = 0;
  size_t i;
  if (!(p < HUGE_VAL)) {
    return ds_geo_chebyshev(a, b, dim);
  }
  if (p < 1) {
    p = 1;
  }
  for (i = 0; i < dim; i++) {
    s += pow(fabs(a[i] - b[i]), p);
  }
  return pow(s, 1.0 / p);
}

static double geo_euclid_metric(const double *a, const double *b, size_t dim,
                                void *user) {
  (void)user;
  return ds_geo_euclidean(a, b, dim);
}

static double geo_directed_hausdorff(const double *a, size_t na,
                                     const double *b, size_t nb, size_t dim,
                                     ds_geo_metric_func_t metric, void *user) {
  double worst = 0;
  double best;
  double d;
  size_t i;
  size_t j;
  for (i = 0; i < na; i++) {
    best = HUGE_VAL;
    for (j = 0; j < nb && best > worst; j++) {
      d = metric(a + i * dim, b + j * dim, dim, user);
      if (d < best) {
        best = d;
      }
    }
    if (best > worst) {
      worst = best; /* also HUGE_VAL when b is empty */
    }
  }
  return worst;
}

double ds_geo_hausdorff(const double *a, size_t na, const double *b, size_t nb,
                        size_t dim, bool directed, ds_geo_metric_func_t metric,
                        void *user) {
  double x;
  double y;
  if (metric == NULL) {
    metric = geo_euclid_metric;
  }
  x = geo_directed_hausdorff(a, na, b, nb, dim, metric, user);
  if (directed) {
    return x;
  }
  y = geo_directed_hausdorff(b, nb, a, na, dim, metric, user);
  return x > y ? x : y;
}

/* ---- predicates ---------------------------------------------------------- */

double ds_geo_orient(ds_point2_t a, ds_point2_t b, ds_point2_t c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static int geo_sign(double v) { return (v > 0) - (v < 0); }

static bool geo_on_segment(ds_point2_t a, ds_point2_t b, ds_point2_t p) {
  return (p.x >= (a.x < b.x ? a.x : b.x)) && (p.x <= (a.x > b.x ? a.x : b.x)) &&
         (p.y >= (a.y < b.y ? a.y : b.y)) && (p.y <= (a.y > b.y ? a.y : b.y));
}

bool ds_geo_segments_intersect(ds_point2_t a, ds_point2_t b, ds_point2_t c,
                               ds_point2_t d) {
  int o1 = geo_sign(ds_geo_orient(a, b, c));
  int o2 = geo_sign(ds_geo_orient(a, b, d));
  int o3 = geo_sign(ds_geo_orient(c, d, a));
  int o4 = geo_sign(ds_geo_orient(c, d, b));
  if (o1 != o2 && o3 != o4) {
    return true;
  }
  return (o1 == 0 && geo_on_segment(a, b, c)) ||
         (o2 == 0 && geo_on_segment(a, b, d)) ||
         (o3 == 0 && geo_on_segment(c, d, a)) ||
         (o4 == 0 && geo_on_segment(c, d, b));
}

double ds_geo_polygon_area2(const ds_point2_t *poly, size_t n) {
  double s = 0;
  size_t i;
  size_t j;
  for (i = 0; i < n; i++) {
    j = i + 1 == n ? 0 : i + 1;
    s += poly[i].x * poly[j].y - poly[j].x * poly[i].y;
  }
  return s;
}

int ds_geo_point_in_polygon(const ds_point2_t *poly, size_t n, ds_point2_t p) {
  long winding = 0;
  size_t i;
  size_t j;
  double o;
  for (i = 0; i < n; i++) {
    j = i + 1 == n ? 0 : i + 1;
    o = ds_geo_orient(poly[i], poly[j], p);
    if (!(o > 0 || o < 0) && geo_on_segment(poly[i], poly[j], p)) {
      return -1;
    }
    if (poly[i].y <= p.y) {
      if (poly[j].y > p.y && o > 0) {
        winding++;
      }
    } else if (poly[j].y <= p.y && o < 0) {
      winding--;
    }
  }
  return winding != 0 ? 1 : 0;
}

void ds_geo_bisector(ds_point2_t p, ds_point2_t q, double *out_a, double *out_b,
                     double *out_c) {
  *out_a = q.x - p.x;
  *out_b = q.y - p.y;
  *out_c = ((q.x * q.x + q.y * q.y) - (p.x * p.x + p.y * p.y)) / 2;
}

/* ---- convex hull --------------------------------------------------------- */

/* qsort has no user pointer, so sort index arrays with a local merge sort
 * that takes the points explicitly. */
typedef int (*geo_icmp_t)(const ds_point2_t *pts, size_t a, size_t b,
                          const void *extra);

static void geo_msort(size_t *idx, size_t *tmp, size_t n,
                      const ds_point2_t *pts, geo_icmp_t cmp,
                      const void *extra) {
  size_t width;
  size_t i;
  size_t lo;
  size_t mid;
  size_t hi;
  size_t a;
  size_t b;
  size_t k;
  for (width = 1; width < n; width *= 2) {
    for (lo = 0; lo < n; lo += 2 * width) {
      mid = lo + width < n ? lo + width : n;
      hi = lo + 2 * width < n ? lo + 2 * width : n;
      a = lo;
      b = mid;
      k = lo;
      while (a < mid && b < hi) {
        tmp[k++] = cmp(pts, idx[b], idx[a], extra) < 0 ? idx[b++] : idx[a++];
      }
      while (a < mid) {
        tmp[k++] = idx[a++];
      }
      while (b < hi) {
        tmp[k++] = idx[b++];
      }
    }
    for (i = 0; i < n; i++) {
      idx[i] = tmp[i];
    }
  }
}

static int geo_cmp_double(double x, double y) { return (x > y) - (x < y); }

static int geo_icmp_xy(const ds_point2_t *pts, size_t a, size_t b,
                       const void *extra) {
  int c = geo_cmp_double(pts[a].x, pts[b].x);
  (void)extra;
  return c != 0 ? c : geo_cmp_double(pts[a].y, pts[b].y);
}

static double geo_d2(ds_point2_t a, ds_point2_t b) {
  double dx = a.x - b.x;
  double dy = a.y - b.y;
  return dx * dx + dy * dy;
}

static int geo_icmp_angle(const ds_point2_t *pts, size_t a, size_t b,
                          const void *extra) {
  ds_point2_t o = pts[*(const size_t *)extra];
  double c = ds_geo_orient(o, pts[a], pts[b]);
  if (c > 0) {
    return -1;
  }
  if (c < 0) {
    return 1;
  }
  return geo_d2(o, pts[a]) < geo_d2(o, pts[b])
             ? -1
             : (geo_d2(o, pts[a]) > geo_d2(o, pts[b]));
}

static size_t geo_andrew(const ds_point2_t *p, const size_t *u, size_t n,
                         size_t *out) {
  size_t k = 0;
  size_t i;
  size_t t;
  for (i = 0; i < n; i++) {
    while (k >= 2 &&
           !(ds_geo_orient(p[out[k - 2]], p[out[k - 1]], p[u[i]]) > 0)) {
      k--;
    }
    out[k++] = u[i];
  }
  t = k + 1;
  for (i = n - 1; i-- > 0;) {
    while (k >= t &&
           !(ds_geo_orient(p[out[k - 2]], p[out[k - 1]], p[u[i]]) > 0)) {
      k--;
    }
    out[k++] = u[i];
  }
  return k - 1; /* the first point was appended again */
}

static size_t geo_graham(const ds_point2_t *p, size_t *u, size_t *tmp, size_t n,
                         size_t pivot_pos, size_t *out) {
  size_t pivot = u[pivot_pos];
  size_t k = 0;
  size_t i;
  u[pivot_pos] = u[0];
  u[0] = pivot;
  geo_msort(u + 1, tmp, n - 1, p, geo_icmp_angle, &pivot);
  for (i = 0; i < n; i++) {
    while (k >= 2 &&
           !(ds_geo_orient(p[out[k - 2]], p[out[k - 1]], p[u[i]]) > 0)) {
      k--;
    }
    out[k++] = u[i];
  }
  return k;
}

static size_t geo_jarvis(const ds_point2_t *p, const size_t *u, size_t n,
                         size_t pivot_pos, size_t *out) {
  size_t k = 0;
  size_t cur = pivot_pos;
  size_t next;
  size_t i;
  double o;
  do {
    out[k++] = u[cur];
    next = cur == 0 ? 1 : 0;
    for (i = 0; i < n; i++) {
      if (i == cur) {
        continue;
      }
      o = ds_geo_orient(p[u[cur]], p[u[next]], p[u[i]]);
      if (o < 0 || (!(o > 0) && geo_d2(p[u[cur]], p[u[i]]) >
                                    geo_d2(p[u[cur]], p[u[next]]))) {
        next = i;
      }
    }
    cur = next;
  } while (cur != pivot_pos && k <= n);
  return k;
}

static size_t geo_quickhull(const ds_point2_t *p, size_t *u, size_t n,
                            size_t *next, size_t *stack, size_t *out,
                            size_t *buf) {
  size_t sp = 0;
  size_t a = u[0];
  size_t b = u[n - 1]; /* leftmost and rightmost by (x, y) */
  size_t i;
  size_t cnt;
  size_t far;
  size_t start;
  size_t count;
  size_t k;
  size_t na;
  size_t nb;
  double best;
  double d;
  size_t *base;
  memset(next, 0xFF, sizeof(size_t) * n);
  /* buffer layout: the lower set, then the upper set */
  cnt = 0;
  for (i = 1; i + 1 < n; i++) {
    if (ds_geo_orient(p[b], p[a], p[u[i]]) > 0) { /* left of b -> a: below */
      buf[cnt++] = i;
    }
  }
  k = cnt;
  for (i = 1; i + 1 < n; i++) {
    if (ds_geo_orient(p[a], p[b], p[u[i]]) > 0) { /* above */
      buf[cnt++] = i;
    }
  }
  /* Positions index u (and next[]). Counter-clockwise order is a, the lower
   * chain, b, the upper chain. A task (x, y, start, count) owns the points
   * buf[start .. start + count) lying right of x -> y (outside the hull edge
   * x -> y) and inserts them between x and y. */
  next[0] = n - 1;
  next[n - 1] = 0;
  stack[sp++] = 0;
  stack[sp++] = n - 1;
  stack[sp++] = 0;
  stack[sp++] = k; /* lower chain: right of a -> b */
  stack[sp++] = n - 1;
  stack[sp++] = 0;
  stack[sp++] = k;
  stack[sp++] = cnt - k; /* upper chain: right of b -> a */
  while (sp > 0) {
    count = stack[--sp];
    start = stack[--sp];
    b = stack[--sp];
    a = stack[--sp];
    if (count == 0) {
      continue;
    }
    base = buf + start;
    far = base[0];
    best = -1;
    for (i = 0; i < count; i++) {
      d = -ds_geo_orient(p[u[a]], p[u[b]], p[u[base[i]]]);
      if (d > best) {
        best = d;
        far = base[i];
      }
    }
    next[a] = far;
    next[far] = b;
    /* partition: right of a -> far, then right of far -> b */
    na = 0;
    for (i = 0; i < count; i++) {
      if (-ds_geo_orient(p[u[a]], p[u[far]], p[u[base[i]]]) > 0) {
        size_t t = base[na];
        base[na] = base[i];
        base[i] = t;
        na++;
      }
    }
    nb = 0;
    for (i = na; i < count; i++) {
      if (-ds_geo_orient(p[u[far]], p[u[b]], p[u[base[i]]]) > 0) {
        size_t t = base[na + nb];
        base[na + nb] = base[i];
        base[i] = t;
        nb++;
      }
    }
    stack[sp++] = a;
    stack[sp++] = far;
    stack[sp++] = start;
    stack[sp++] = na;
    stack[sp++] = far;
    stack[sp++] = b;
    stack[sp++] = start + na;
    stack[sp++] = nb;
  }
  k = 0;
  i = 0;
  do {
    out[k++] = u[i];
    i = next[i];
  } while (i != 0 && k <= n);
  return k;
}

ds_status_t ds_geo_convex_hull(const ds_point2_t *points, size_t n,
                               ds_hull_algorithm_t algorithm, size_t *out_hull,
                               size_t *out_count, ds_context_t *context) {
  size_t *u;
  size_t *tmp;
  size_t *w1 = NULL;
  size_t *w2 = NULL;
  size_t m = 0;
  size_t i;
  size_t h = 0;
  size_t pivot = 0;
  size_t start;
  ds_status_t status = DS_OK;
  if ((points == NULL && n > 0) || out_hull == NULL || out_count == NULL) {
    return DS_ERR_NULL;
  }
  if ((int)algorithm < 0 || (int)algorithm > (int)DS_HULL_QUICKHULL) {
    return DS_ERR_INVALID;
  }
  if (n == 0) {
    *out_count = 0;
    return DS_OK;
  }
  u = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  tmp = (size_t *)ds_context_alloc(context, 2 * n * sizeof(size_t) + 16);
  w1 = (size_t *)ds_context_alloc(context, 8 * n * sizeof(size_t) + 64);
  w2 = (size_t *)ds_context_alloc(context, n * sizeof(size_t) + 8);
  if (u == NULL || tmp == NULL || w1 == NULL || w2 == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  for (i = 0; i < n; i++) {
    u[i] = i;
  }
  geo_msort(u, tmp, n, points, geo_icmp_xy, NULL);
  /* drop duplicate points */
  for (i = 0; i < n; i++) {
    if (m == 0 || geo_icmp_xy(points, u[m - 1], u[i], NULL) != 0) {
      u[m++] = u[i];
    }
  }
  if (m <= 2) {
    for (i = 0; i < m; i++) {
      out_hull[i] = u[i];
    }
    h = m;
  } else {
    for (i = 1; i < m; i++) {
      if (points[u[i]].y < points[u[pivot]].y ||
          (!(points[u[i]].y > points[u[pivot]].y) &&
           points[u[i]].x < points[u[pivot]].x)) {
        pivot = i;
      }
    }
    switch (algorithm) {
    case DS_HULL_GRAHAM:
      h = geo_graham(points, u, tmp, m, pivot, out_hull);
      break;
    case DS_HULL_JARVIS:
      h = geo_jarvis(points, u, m, pivot, out_hull);
      break;
    case DS_HULL_QUICKHULL:
      h = geo_quickhull(points, u, m, w2, w1, out_hull, tmp);
      break;
    case DS_HULL_ANDREW:
    default:
      h = geo_andrew(points, u, m, out_hull);
      break;
    }
  }
  /* drop collinear vertices (all-collinear inputs collapse to 2 points)
   * and rotate to start at the lowest-then-leftmost vertex */
  if (h >= 3) {
    size_t k = 0;
    size_t j;
    for (i = 0; i < h; i++) {
      size_t prv = k > 0 ? w2[k - 1] : out_hull[h - 1];
      size_t nxt = out_hull[i + 1 == h ? 0 : i + 1];
      if (ds_geo_orient(points[prv], points[out_hull[i]], points[nxt]) > 0) {
        w2[k++] = out_hull[i];
      }
    }
    if (k < 3) {
      /* collinear: the two extremes by (x, y) */
      out_hull[0] = u[0];
      out_hull[1] = u[m - 1];
      h = 2;
    } else {
      start = 0;
      for (j = 1; j < k; j++) {
        if (points[w2[j]].y < points[w2[start]].y ||
            (!(points[w2[j]].y > points[w2[start]].y) &&
             points[w2[j]].x < points[w2[start]].x)) {
          start = j;
        }
      }
      for (j = 0; j < k; j++) {
        out_hull[j] = w2[(start + j) % k];
      }
      h = k;
    }
  }
  *out_count = h;
out:
  ds_context_free(context, u);
  ds_context_free(context, tmp);
  ds_context_free(context, w1);
  ds_context_free(context, w2);
  return status;
}

/* ---- rotating calipers --------------------------------------------------- */

static double geo_area2(ds_point2_t a, ds_point2_t b, ds_point2_t c) {
  return fabs(ds_geo_orient(a, b, c));
}

double ds_geo_convex_diameter(const ds_point2_t *poly, size_t n, size_t *out_i,
                              size_t *out_j) {
  size_t i;
  size_t j = 1;
  size_t bi = 0;
  size_t bj = 0;
  double best = 0;
  double d;
  if (n < 2) {
    if (out_i != NULL) {
      *out_i = 0;
    }
    if (out_j != NULL) {
      *out_j = 0;
    }
    return 0;
  }
  if (n == 2) {
    bi = 0;
    bj = 1;
    best = geo_d2(poly[0], poly[1]);
  } else {
    for (i = 0; i < n; i++) {
      size_t i1 = (i + 1) % n;
      while (geo_area2(poly[i], poly[i1], poly[(j + 1) % n]) >
             geo_area2(poly[i], poly[i1], poly[j])) {
        j = (j + 1) % n;
      }
      d = geo_d2(poly[i], poly[j]);
      if (d > best) {
        best = d;
        bi = i;
        bj = j;
      }
      d = geo_d2(poly[i1], poly[j]);
      if (d > best) {
        best = d;
        bi = i1;
        bj = j;
      }
    }
  }
  if (out_i != NULL) {
    *out_i = bi;
  }
  if (out_j != NULL) {
    *out_j = bj;
  }
  return sqrt(best);
}

double ds_geo_convex_width(const ds_point2_t *poly, size_t n) {
  size_t i;
  size_t j = 1;
  double best = HUGE_VAL;
  double len;
  double h;
  if (n < 3) {
    return 0;
  }
  for (i = 0; i < n; i++) {
    size_t i1 = (i + 1) % n;
    while (geo_area2(poly[i], poly[i1], poly[(j + 1) % n]) >
           geo_area2(poly[i], poly[i1], poly[j])) {
      j = (j + 1) % n;
    }
    len = sqrt(geo_d2(poly[i], poly[i1]));
    h = geo_area2(poly[i], poly[i1], poly[j]) / len;
    if (h < best) {
      best = h;
    }
  }
  return best;
}

double ds_geo_min_area_rect(const ds_point2_t *poly, size_t n,
                            ds_point2_t out_corners[4]) {
  size_t i;
  size_t r = 0; /* max projection on the edge direction */
  size_t t = 0; /* max distance from the edge line */
  size_t l = 0; /* min projection */
  double best = HUGE_VAL;
  double ux;
  double uy;
  double len;
  double a;
  double proj;
  size_t k;
  if (n == 0) {
    return 0;
  }
  if (n < 3) {
    ds_point2_t lo;
    ds_point2_t hi;
    ds_geo_bounding_box(poly, n, &lo, &hi);
    out_corners[0] = lo;
    out_corners[1].x = hi.x;
    out_corners[1].y = lo.y;
    out_corners[2] = hi;
    out_corners[3].x = lo.x;
    out_corners[3].y = hi.y;
    return 0;
  }
  for (i = 0; i < n; i++) {
    size_t i1 = (i + 1) % n;
    double width;
    double height;
    ux = poly[i1].x - poly[i].x;
    uy = poly[i1].y - poly[i].y;
    len = sqrt(ux * ux + uy * uy);
    ux /= len;
    uy /= len;
#define CAL_PROJ(q)                                                            \
  ((poly[q].x - poly[i].x) * ux + (poly[q].y - poly[i].y) * uy)
#define CAL_HGT(q)                                                             \
  (-(poly[q].x - poly[i].x) * uy + (poly[q].y - poly[i].y) * ux)
    if (i == 0) {
      r = t = l = i1;
      for (k = 0; k < n; k++) {
        if (CAL_PROJ(k) > CAL_PROJ(r)) {
          r = k;
        }
        if (CAL_HGT(k) > CAL_HGT(t)) {
          t = k;
        }
        if (CAL_PROJ(k) < CAL_PROJ(l)) {
          l = k;
        }
      }
    } else {
      while (CAL_PROJ((r + 1) % n) > CAL_PROJ(r)) {
        r = (r + 1) % n;
      }
      while (CAL_HGT((t + 1) % n) > CAL_HGT(t)) {
        t = (t + 1) % n;
      }
      while (CAL_PROJ((l + 1) % n) < CAL_PROJ(l)) {
        l = (l + 1) % n;
      }
    }
    width = CAL_PROJ(r) - CAL_PROJ(l);
    height = CAL_HGT(t);
    a = width * height;
    if (a < best) {
      best = a;
      proj = CAL_PROJ(l);
      out_corners[0].x = poly[i].x + ux * proj;
      out_corners[0].y = poly[i].y + uy * proj;
      out_corners[1].x = out_corners[0].x + ux * width;
      out_corners[1].y = out_corners[0].y + uy * width;
      out_corners[2].x = out_corners[1].x - uy * height;
      out_corners[2].y = out_corners[1].y + ux * height;
      out_corners[3].x = out_corners[0].x - uy * height;
      out_corners[3].y = out_corners[0].y + ux * height;
    }
#undef CAL_PROJ
#undef CAL_HGT
  }
  return best;
}

void ds_geo_bounding_box(const ds_point2_t *points, size_t n,
                         ds_point2_t *out_min, ds_point2_t *out_max) {
  size_t i;
  if (n == 0) {
    out_min->x = out_min->y = 0;
    *out_max = *out_min;
    return;
  }
  *out_min = points[0];
  *out_max = points[0];
  for (i = 1; i < n; i++) {
    if (points[i].x < out_min->x) {
      out_min->x = points[i].x;
    }
    if (points[i].y < out_min->y) {
      out_min->y = points[i].y;
    }
    if (points[i].x > out_max->x) {
      out_max->x = points[i].x;
    }
    if (points[i].y > out_max->y) {
      out_max->y = points[i].y;
    }
  }
}

/* ---- closest pair -------------------------------------------------------- */

typedef struct geo_cp {
  const ds_point2_t *p;
  size_t *byx;
  size_t *tmp;
  size_t *strip;
  double best; /* squared */
  size_t bi;
  size_t bj;
} geo_cp_t;

static int geo_icmp_y(const ds_point2_t *pts, size_t a, size_t b,
                      const void *extra) {
  (void)extra;
  return geo_cmp_double(pts[a].y, pts[b].y);
}

/* Recursion depth is O(log n): halves are balanced. On return byx[lo..hi)
 * is sorted by y. */
static void geo_cp_rec(geo_cp_t *c, size_t lo, size_t hi) {
  size_t mid;
  double midx;
  size_t i;
  size_t j;
  size_t k;
  size_t a;
  size_t b;
  size_t ns = 0;
  double d;
  if (hi - lo <= 3) {
    for (i = lo; i < hi; i++) {
      for (j = i + 1; j < hi; j++) {
        d = geo_d2(c->p[c->byx[i]], c->p[c->byx[j]]);
        if (d < c->best) {
          c->best = d;
          c->bi = c->byx[i];
          c->bj = c->byx[j];
        }
      }
    }
    geo_msort(c->byx + lo, c->tmp, hi - lo, c->p, geo_icmp_y, NULL);
    return;
  }
  mid = lo + (hi - lo) / 2;
  midx = c->p[c->byx[mid]].x;
  geo_cp_rec(c, lo, mid);
  geo_cp_rec(c, mid, hi);
  /* merge by y */
  a = lo;
  b = mid;
  k = 0;
  while (a < mid && b < hi) {
    c->tmp[k++] =
        c->p[c->byx[b]].y < c->p[c->byx[a]].y ? c->byx[b++] : c->byx[a++];
  }
  while (a < mid) {
    c->tmp[k++] = c->byx[a++];
  }
  while (b < hi) {
    c->tmp[k++] = c->byx[b++];
  }
  memcpy(c->byx + lo, c->tmp, (hi - lo) * sizeof(size_t));
  for (i = lo; i < hi; i++) {
    d = c->p[c->byx[i]].x - midx;
    if (d * d < c->best) {
      for (j = ns; j-- > 0;) {
        double dy = c->p[c->byx[i]].y - c->p[c->strip[j]].y;
        if (dy * dy >= c->best) {
          break;
        }
        d = geo_d2(c->p[c->byx[i]], c->p[c->strip[j]]);
        if (d < c->best) {
          c->best = d;
          c->bi = c->strip[j];
          c->bj = c->byx[i];
        }
      }
      c->strip[ns++] = c->byx[i];
    }
  }
}

ds_status_t ds_geo_closest_pair(const ds_point2_t *points, size_t n,
                                double *out_distance, size_t *out_i,
                                size_t *out_j, ds_context_t *context) {
  geo_cp_t c;
  size_t i;
  if (points == NULL || out_distance == NULL) {
    return DS_ERR_NULL;
  }
  if (n < 2) {
    return DS_ERR_RANGE;
  }
  c.p = points;
  c.byx = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  c.tmp = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  c.strip = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  if (c.byx == NULL || c.tmp == NULL || c.strip == NULL) {
    ds_context_free(context, c.byx);
    ds_context_free(context, c.tmp);
    ds_context_free(context, c.strip);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    c.byx[i] = i;
  }
  geo_msort(c.byx, c.tmp, n, points, geo_icmp_xy, NULL);
  c.best = HUGE_VAL;
  c.bi = 0;
  c.bj = 1;
  geo_cp_rec(&c, 0, n);
  *out_distance = sqrt(c.best);
  if (out_i != NULL) {
    *out_i = c.bi < c.bj ? c.bi : c.bj;
  }
  if (out_j != NULL) {
    *out_j = c.bi < c.bj ? c.bj : c.bi;
  }
  ds_context_free(context, c.byx);
  ds_context_free(context, c.tmp);
  ds_context_free(context, c.strip);
  return DS_OK;
}

/* ---- polygons ------------------------------------------------------------ */

/* Clip convex polygon src (count m) by the half-plane left of a -> b. */
static size_t geo_clip(const ds_point2_t *src, size_t m, ds_point2_t a,
                       ds_point2_t b, ds_point2_t *dst) {
  size_t i;
  size_t k = 0;
  ds_point2_t p;
  ds_point2_t q;
  double sp;
  double sq;
  for (i = 0; i < m; i++) {
    p = src[i];
    q = src[(i + 1) % m];
    sp = ds_geo_orient(a, b, p);
    sq = ds_geo_orient(a, b, q);
    if (sp >= 0) {
      dst[k++] = p;
    }
    if ((sp > 0 && sq < 0) || (sp < 0 && sq > 0)) {
      double t = sp / (sp - sq);
      dst[k].x = p.x + (q.x - p.x) * t;
      dst[k].y = p.y + (q.y - p.y) * t;
      k++;
    }
  }
  return k;
}

ds_status_t ds_geo_polygon_kernel(const ds_point2_t *poly, size_t n,
                                  ds_point2_t *out, size_t capacity,
                                  size_t *out_count, ds_context_t *context) {
  ds_point2_t *a;
  ds_point2_t *b;
  ds_point2_t *t;
  ds_point2_t lo;
  ds_point2_t hi;
  size_t m = 4;
  size_t i;
  bool ccw;
  double span;
  if (poly == NULL || out == NULL || out_count == NULL) {
    return DS_ERR_NULL;
  }
  if (n < 3) {
    return DS_ERR_INVALID;
  }
  a = (ds_point2_t *)ds_context_alloc(context, (n + 8) * sizeof(ds_point2_t));
  b = (ds_point2_t *)ds_context_alloc(context, (n + 8) * sizeof(ds_point2_t));
  if (a == NULL || b == NULL) {
    ds_context_free(context, a);
    ds_context_free(context, b);
    return DS_ERR_ALLOC;
  }
  ccw = ds_geo_polygon_area2(poly, n) > 0;
  ds_geo_bounding_box(poly, n, &lo, &hi);
  span = (hi.x - lo.x) + (hi.y - lo.y) + 1;
  a[0].x = lo.x - span;
  a[0].y = lo.y - span;
  a[1].x = hi.x + span;
  a[1].y = lo.y - span;
  a[2].x = hi.x + span;
  a[2].y = hi.y + span;
  a[3].x = lo.x - span;
  a[3].y = hi.y + span;
  for (i = 0; i < n && m > 0; i++) {
    ds_point2_t p = poly[i];
    ds_point2_t q = poly[(i + 1) % n];
    m = ccw ? geo_clip(a, m, p, q, b) : geo_clip(a, m, q, p, b);
    t = a;
    a = b;
    b = t;
  }
  if (m > capacity) {
    ds_context_free(context, a);
    ds_context_free(context, b);
    return DS_ERR_OVERFLOW;
  }
  memcpy(out, a, m * sizeof(ds_point2_t));
  *out_count = m;
  ds_context_free(context, a);
  ds_context_free(context, b);
  return DS_OK;
}

static bool geo_monotone(const ds_point2_t *poly, size_t n, bool use_x) {
  size_t i;
  int first = 0;
  int last = 0;
  int s;
  size_t changes = 0;
  double d;
  for (i = 0; i < n; i++) {
    const ds_point2_t *p = &poly[i];
    const ds_point2_t *q = &poly[(i + 1) % n];
    d = use_x ? q->x - p->x : q->y - p->y;
    s = geo_sign(d);
    if (s == 0) {
      continue;
    }
    if (first == 0) {
      first = s;
    } else if (s != last) {
      changes++;
    }
    last = s;
  }
  if (first != 0 && last != first) {
    changes++; /* cyclic wrap */
  }
  return changes <= 2;
}

bool ds_geo_orthogonally_convex(const ds_point2_t *poly, size_t n) {
  size_t i;
  if (poly == NULL || n < 4) {
    return false;
  }
  for (i = 0; i < n; i++) {
    const ds_point2_t *p = &poly[i];
    const ds_point2_t *q = &poly[(i + 1) % n];
    if ((p->x < q->x || p->x > q->x) && (p->y < q->y || p->y > q->y)) {
      return false; /* not rectilinear */
    }
  }
  return geo_monotone(poly, n, true) && geo_monotone(poly, n, false);
}

/* ---- optimal convex polygon triangulation -------------------------------- */

static double geo_perimeter(const ds_point2_t *poly, size_t i, size_t j,
                            size_t k, void *user) {
  (void)user;
  return sqrt(geo_d2(poly[i], poly[j])) + sqrt(geo_d2(poly[j], poly[k])) +
         sqrt(geo_d2(poly[k], poly[i]));
}

ds_status_t ds_geo_optimal_triangulation(const ds_point2_t *poly, size_t n,
                                         ds_geo_triangle_weight_func_t weight,
                                         void *user, size_t *out_triangles,
                                         double *out_weight,
                                         ds_context_t *context) {
  double *dp;
  size_t *cut;
  size_t len;
  size_t i;
  size_t j;
  size_t k;
  size_t sp;
  size_t t = 0;
  size_t *stack;
  double c;
  if (poly == NULL || out_weight == NULL) {
    return DS_ERR_NULL;
  }
  if (n < 3) {
    *out_weight = 0;
    return DS_OK;
  }
  if (n > ((size_t)-1) / n / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  if (weight == NULL) {
    weight = geo_perimeter;
  }
  dp = (double *)ds_context_calloc(context, n * n, sizeof(double));
  cut = (size_t *)ds_context_calloc(context, n * n, sizeof(size_t));
  stack = (size_t *)ds_context_alloc(context, 2 * n * sizeof(size_t));
  if (dp == NULL || cut == NULL || stack == NULL) {
    ds_context_free(context, dp);
    ds_context_free(context, cut);
    ds_context_free(context, stack);
    return DS_ERR_ALLOC;
  }
  for (len = 2; len < n; len++) {
    for (i = 0; i + len < n; i++) {
      j = i + len;
      dp[i * n + j] = HUGE_VAL;
      for (k = i + 1; k < j; k++) {
        c = dp[i * n + k] + dp[k * n + j] + weight(poly, i, k, j, user);
        if (c < dp[i * n + j]) {
          dp[i * n + j] = c;
          cut[i * n + j] = k;
        }
      }
    }
  }
  *out_weight = dp[n - 1];
  if (out_triangles != NULL) {
    sp = 0;
    stack[sp++] = 0;
    stack[sp++] = n - 1;
    while (sp > 0) {
      j = stack[--sp];
      i = stack[--sp];
      if (j - i < 2) {
        continue;
      }
      k = cut[i * n + j];
      out_triangles[3 * t] = i;
      out_triangles[3 * t + 1] = k;
      out_triangles[3 * t + 2] = j;
      t++;
      stack[sp++] = i;
      stack[sp++] = k;
      stack[sp++] = k;
      stack[sp++] = j;
    }
  }
  ds_context_free(context, dp);
  ds_context_free(context, cut);
  ds_context_free(context, stack);
  return DS_OK;
}

/* ---- slope selection ----------------------------------------------------- */

typedef struct geo_key {
  double a;
  double negx;
} geo_key_t;

static bool geo_key_less(const geo_key_t *p, const geo_key_t *q) {
  return p->a < q->a || (!(p->a > q->a) && p->negx < q->negx);
}

/* Strict inversions of keys (merge sort), keys destroyed. */
static size_t geo_inversions(geo_key_t *k, geo_key_t *tmp, size_t n) {
  size_t width;
  size_t lo;
  size_t mid;
  size_t hi;
  size_t a;
  size_t b;
  size_t t;
  size_t inv = 0;
  for (width = 1; width < n; width *= 2) {
    for (lo = 0; lo < n; lo += 2 * width) {
      mid = lo + width < n ? lo + width : n;
      hi = lo + 2 * width < n ? lo + 2 * width : n;
      a = lo;
      b = mid;
      t = lo;
      while (a < mid && b < hi) {
        if (geo_key_less(&k[b], &k[a])) {
          inv += mid - a;
          tmp[t++] = k[b++];
        } else {
          tmp[t++] = k[a++];
        }
      }
      while (a < mid) {
        tmp[t++] = k[a++];
      }
      while (b < hi) {
        tmp[t++] = k[b++];
      }
    }
    memcpy(k, tmp, n * sizeof(geo_key_t));
  }
  return inv;
}

/* Pairs (distinct x) with slope <= s. */
static size_t geo_count_le(const ds_point2_t *p, const size_t *order, size_t n,
                           double s, geo_key_t *k, geo_key_t *tmp) {
  size_t i;
  for (i = 0; i < n; i++) {
    k[i].a = p[order[i]].y - s * p[order[i]].x;
    k[i].negx = -p[order[i]].x;
  }
  return geo_inversions(k, tmp, n);
}

ds_status_t ds_geo_slope_selection(const ds_point2_t *points, size_t n,
                                   size_t k, double *out_slope,
                                   ds_context_t *context) {
  size_t *order;
  size_t *tmp;
  geo_key_t *keys;
  geo_key_t *ktmp;
  size_t i;
  size_t total = 0;
  size_t run = 1;
  double mindx = HUGE_VAL;
  double lo;
  double hi;
  double mid;
  double ymin;
  double ymax;
  double bound;
  unsigned int iter;
  ds_status_t status = DS_OK;
  if (points == NULL || out_slope == NULL) {
    return DS_ERR_NULL;
  }
  order = (size_t *)ds_context_alloc(context, (n + 1) * sizeof(size_t));
  tmp = (size_t *)ds_context_alloc(context, (n + 1) * sizeof(size_t));
  keys = (geo_key_t *)ds_context_alloc(context, (n + 1) * sizeof(geo_key_t));
  ktmp = (geo_key_t *)ds_context_alloc(context, (n + 1) * sizeof(geo_key_t));
  if (order == NULL || tmp == NULL || keys == NULL || ktmp == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  for (i = 0; i < n; i++) {
    order[i] = i;
  }
  geo_msort(order, tmp, n, points, geo_icmp_xy, NULL);
  /* number of pairs with distinct x, the x gap and the y range */
  ymin = ymax = n > 0 ? points[0].y : 0;
  for (i = 0; i < n; i++) {
    if (points[i].y < ymin) {
      ymin = points[i].y;
    }
    if (points[i].y > ymax) {
      ymax = points[i].y;
    }
  }
  total = n * (n - (n > 0)) / 2;
  for (i = 1; i <= n; i++) {
    if (i < n && !(points[order[i]].x > points[order[i - 1]].x)) {
      run++;
    } else {
      total -= run * (run - 1) / 2;
      if (i < n && points[order[i]].x - points[order[i - 1]].x < mindx) {
        mindx = points[order[i]].x - points[order[i - 1]].x;
      }
      run = 1;
    }
  }
  if (k >= total) {
    status = DS_ERR_RANGE;
    goto out;
  }
  bound = (ymax - ymin) / mindx + 1;
  lo = -bound;
  hi = bound;
  /* invariant: count(lo) <= k < count(hi) */
  for (iter = 0; iter < 400; iter++) {
    mid = lo + (hi - lo) / 2;
    if (!(mid > lo && mid < hi) ||
        hi - lo <= 1e-15 * (fabs(lo) > fabs(hi) ? fabs(lo) : fabs(hi))) {
      break;
    }
    if (geo_count_le(points, order, n, mid, keys, ktmp) > k) {
      hi = mid;
    } else {
      lo = mid;
    }
  }
  *out_slope = hi;
out:
  ds_context_free(context, order);
  ds_context_free(context, tmp);
  ds_context_free(context, keys);
  ds_context_free(context, ktmp);
  return status;
}

/* ---- rasterizing --------------------------------------------------------- */

void ds_geo_bresenham_line(long x0, long y0, long x1, long y1,
                           ds_geo_plot_func_t plot, void *user) {
  long dx = x1 > x0 ? x1 - x0 : x0 - x1;
  long dy = y1 > y0 ? y0 - y1 : y1 - y0;
  long sx = x0 < x1 ? 1 : -1;
  long sy = y0 < y1 ? 1 : -1;
  long err = dx + dy;
  long e2;
  for (;;) {
    plot(x0, y0, user);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void ds_geo_bresenham_circle(long cx, long cy, long r, ds_geo_plot_func_t plot,
                             void *user) {
  long x = r;
  long y = 0;
  long err = 1 - r;
  if (r < 0) {
    return;
  }
  if (r == 0) {
    plot(cx, cy, user);
    return;
  }
  while (x >= y) {
    plot(cx + x, cy + y, user);
    plot(cx - x, cy + y, user);
    if (y != 0) {
      plot(cx + x, cy - y, user);
      plot(cx - x, cy - y, user);
    }
    if (x != y) {
      plot(cx + y, cy + x, user);
      plot(cx + y, cy - x, user);
      if (y != 0) {
        plot(cx - y, cy + x, user);
        plot(cx - y, cy - x, user);
      }
    }
    y++;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      x--;
      err += 2 * (y - x) + 1;
    }
  }
}

static double geo_fpart(double x) { return x - floor(x); }
static double geo_rfpart(double x) { return 1 - geo_fpart(x); }

static void geo_wu_plot(ds_geo_plot_aa_func_t plot, void *user, bool steep,
                        double x, double y, double c) {
  if (steep) {
    plot((long)y, (long)x, c, user);
  } else {
    plot((long)x, (long)y, c, user);
  }
}

void ds_geo_wu_line(double x0, double y0, double x1, double y1,
                    ds_geo_plot_aa_func_t plot, void *user) {
  bool steep = fabs(y1 - y0) > fabs(x1 - x0);
  double t;
  double dx;
  double dy;
  double gradient;
  double xend;
  double yend;
  double xgap;
  double intery;
  double xpx1;
  double xpx2;
  double x;
  if (steep) {
    t = x0;
    x0 = y0;
    y0 = t;
    t = x1;
    x1 = y1;
    y1 = t;
  }
  if (x0 > x1) {
    t = x0;
    x0 = x1;
    x1 = t;
    t = y0;
    y0 = y1;
    y1 = t;
  }
  dx = x1 - x0;
  dy = y1 - y0;
  gradient = dx > 0 ? dy / dx : 1.0;
  xend = floor(x0 + 0.5);
  yend = y0 + gradient * (xend - x0);
  xgap = geo_rfpart(x0 + 0.5);
  xpx1 = xend;
  geo_wu_plot(plot, user, steep, xpx1, floor(yend), geo_rfpart(yend) * xgap);
  geo_wu_plot(plot, user, steep, xpx1, floor(yend) + 1, geo_fpart(yend) * xgap);
  intery = yend + gradient;
  xend = floor(x1 + 0.5);
  yend = y1 + gradient * (xend - x1);
  xgap = geo_fpart(x1 + 0.5);
  xpx2 = xend;
  for (x = xpx1 + 1; x < xpx2; x += 1) {
    geo_wu_plot(plot, user, steep, x, floor(intery), geo_rfpart(intery));
    geo_wu_plot(plot, user, steep, x, floor(intery) + 1, geo_fpart(intery));
    intery += gradient;
  }
  if (xpx2 > xpx1) {
    geo_wu_plot(plot, user, steep, xpx2, floor(yend), geo_rfpart(yend) * xgap);
    geo_wu_plot(plot, user, steep, xpx2, floor(yend) + 1,
                geo_fpart(yend) * xgap);
  }
}
