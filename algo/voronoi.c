#include "voronoi.h"
#include "../lib/checked.h"
#include "robust_predicates.h"
#include <math.h>
#include <string.h>

#define VO_NONE DS_VORONOI_NONE

/* ---- helpers ---- */

static bool vo_lt(const double *xy, size_t a, size_t b) {
  return xy[2 * a] < xy[2 * b] ||
         (!(xy[2 * a] > xy[2 * b]) && xy[2 * a + 1] < xy[2 * b + 1]);
}

static bool vo_same(const double *xy, size_t a, size_t b) {
  return !vo_lt(xy, a, b) && !vo_lt(xy, b, a);
}

/* indices 0 .. n-1 by (x, y), stable, bottom-up (no recursion) */
static void vo_sort(const double *xy, size_t n, size_t *idx, size_t *tmp) {
  size_t width;
  size_t i;
  for (i = 0; i < n; i++) {
    idx[i] = i;
  }
  for (width = 1; width < n; width *= 2) {
    size_t lo;
    for (lo = 0; lo < n; lo += 2 * width) {
      size_t mid = lo + width < n ? lo + width : n;
      size_t hi = mid + width < n ? mid + width : n;
      size_t a = lo, b = mid, o = lo;
      while (a < mid || b < hi) {
        if (b == hi || (a < mid && !vo_lt(xy, idx[b], idx[a]))) {
          tmp[o++] = idx[a++];
        } else {
          tmp[o++] = idx[b++];
        }
      }
    }
    memcpy(idx, tmp, n * sizeof(size_t));
    if (width > n / 2) {
      break;
    }
  }
}

/* The circumcenter of a, b, c, relative to a. The denominator is the
 * orientation determinant, taken from the exact evaluation: for a nearly
 * flat triangle it is the one quantity that cancels catastrophically in
 * floating point. */
static void vo_circumcenter(const double *a, const double *b, const double *c,
                            double *out) {
  double bx = b[0] - a[0], by = b[1] - a[1];
  double cx = c[0] - a[0], cy = c[1] - a[1];
  double d = 2.0 * ds_orient2d_exact(a, b, c);
  double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
  if (!(d < 0.0) && !(d > 0.0)) {
    out[0] = HUGE_VAL; /* flat: the vertex is at infinity */
    out[1] = HUGE_VAL;
    return;
  }
  out[0] = a[0] + (cy * b2 - by * c2) / d;
  out[1] = a[1] + (bx * c2 - cx * b2) / d;
}

/* the right-hand normal of the direction from p to q */
static void vo_right_normal(const double *p, const double *q, double *out) {
  out[0] = q[1] - p[1];
  out[1] = -(q[0] - p[0]);
}

static void *vo_array(ds_context_t *ctx, size_t count, size_t size,
                      ds_status_t *st) {
  size_t bytes;
  void *p;
  if (*st != DS_OK) {
    return NULL;
  }
  if (!ds_size_mul(count == 0 ? 1 : count, size, &bytes)) {
    *st = DS_ERR_OVERFLOW;
    return NULL;
  }
  p = ds_context_calloc(ctx, 1, bytes);
  if (p == NULL) {
    *st = DS_ERR_ALLOC;
  }
  return p;
}

void ds_voronoi_destroy(ds_voronoi_t *vd) {
  ds_context_t *ctx;
  if (vd == NULL) {
    return;
  }
  ctx = vd->context;
  ds_context_free(ctx, vd->vertex);
  ds_context_free(ctx, vd->edge);
  ds_context_free(ctx, vd->cell_start);
  ds_context_free(ctx, vd->cell_vertex);
  ds_context_free(ctx, vd->unbounded);
  ds_context_free(ctx, vd->cell_ray);
  ds_context_free(ctx, vd->twin);
  ds_context_free(ctx, vd->nbr_start);
  ds_context_free(ctx, vd->nbr);
  memset(vd, 0, sizeof(*vd));
}

/* index of v in triangle t, 3 if absent */
static size_t vo_corner(const ds_delaunay_t *dt, size_t t, size_t v) {
  size_t k;
  for (k = 0; k < 3 && dt->tri[3 * t + k] != v; k++) {
  }
  return k;
}

/* ---- the collinear case: lines between neighbours along the line ---- */

static ds_status_t vo_collinear(ds_voronoi_t *vd, const double *xy,
                                const size_t *order) {
  size_t n = vd->sites;
  size_t i;
  size_t prev = VO_NONE;
  size_t distinct = 0;
  size_t e = 0;
  size_t o = 0;
  size_t *left;
  size_t *right;
  ds_status_t st = DS_OK;
  for (i = 0; i < n; i++) {
    if (vd->twin[order[i]] == order[i]) {
      distinct++;
    }
  }
  vd->edges = distinct > 1 ? distinct - 1 : 0;
  vd->edge = (ds_voronoi_edge_t *)vo_array(vd->context, vd->edges,
                                           sizeof(ds_voronoi_edge_t), &st);
  vd->nbr = (size_t *)vo_array(vd->context, 2 * n, sizeof(size_t), &st);
  left = (size_t *)vo_array(vd->context, n, sizeof(size_t), &st);
  right = (size_t *)vo_array(vd->context, n, sizeof(size_t), &st);
  if (st == DS_OK) {
    for (i = 0; i < n; i++) {
      left[i] = VO_NONE;
      right[i] = VO_NONE;
    }
    /* along the line, consecutive distinct sites are neighbours */
    for (i = 0; i < n; i++) {
      size_t s = order[i];
      if (vd->twin[s] != s) {
        continue;
      }
      vd->unbounded[s] = true;
      if (prev != VO_NONE) {
        ds_voronoi_edge_t *ed = &vd->edge[e++];
        left[s] = prev;
        right[prev] = s;
        ed->site[0] = prev;
        ed->site[1] = s;
        ed->vertex[0] = VO_NONE;
        ed->vertex[1] = VO_NONE;
        ed->origin[0] = (xy[2 * prev] + xy[2 * s]) / 2.0;
        ed->origin[1] = (xy[2 * prev + 1] + xy[2 * s + 1]) / 2.0;
        vo_right_normal(xy + 2 * prev, xy + 2 * s, ed->dir);
      }
      prev = s;
    }
    for (i = 0; i < n; i++) {
      vd->nbr_start[i] = o;
      if (left[i] != VO_NONE) {
        vd->nbr[o++] = left[i];
      }
      if (right[i] != VO_NONE) {
        vd->nbr[o++] = right[i];
      }
    }
    vd->nbr_start[n] = o;
  }
  ds_context_free(vd->context, left);
  ds_context_free(vd->context, right);
  return st; /* cell_start stays zero: no vertices */
}

/* ---- the general case ---- */

/* site v's triangles in counter-clockwise order into cell_vertex, its
 * neighbours into nbr, from triangle t0 that contains it */
static ds_status_t vo_cell(ds_voronoi_t *vd, const ds_delaunay_t *dt,
                           const double *xy, size_t v, size_t t0, size_t deg,
                           size_t *cpos, size_t *npos) {
  size_t t = t0;
  size_t steps = 0;
  size_t k;
  size_t i;
  bool hull = false;
  /* clockwise until the hull or back to t0 */
  for (;;) {
    size_t p;
    k = vo_corner(dt, t, v);
    if (k == 3) {
      return DS_ERR_INVALID;
    }
    p = dt->adj[3 * t + (k + 2) % 3];
    if (p == VO_NONE) {
      hull = true;
      break;
    }
    if (p >= dt->triangles || ++steps > deg) {
      return DS_ERR_INVALID;
    }
    if (p == t0) {
      t = t0;
      break;
    }
    t = p;
  }
  vd->unbounded[v] = hull;
  if (hull) {
    k = vo_corner(dt, t, v);
    vo_right_normal(xy + 2 * v, xy + 2 * dt->tri[3 * t + (k + 1) % 3],
                    vd->cell_ray + 4 * v);
  }
  /* counter-clockwise, collecting */
  for (i = 0; i < deg; i++) {
    size_t next;
    k = vo_corner(dt, t, v);
    if (k == 3) {
      return DS_ERR_INVALID;
    }
    vd->cell_vertex[(*cpos)++] = t;
    vd->nbr[(*npos)++] = dt->tri[3 * t + (k + 1) % 3];
    next = dt->adj[3 * t + (k + 1) % 3];
    if (i + 1 < deg) {
      if (next == VO_NONE || next >= dt->triangles) {
        return DS_ERR_INVALID;
      }
      t = next;
    } else if (hull ? next != VO_NONE
                    : next != (vd->cell_vertex[*cpos - deg])) {
      return DS_ERR_INVALID; /* the ring does not close as counted */
    }
  }
  if (hull) {
    size_t c = dt->tri[3 * t + (k + 2) % 3];
    vd->nbr[(*npos)++] = c;
    vo_right_normal(xy + 2 * c, xy + 2 * v, vd->cell_ray + 4 * v + 2);
  }
  return DS_OK;
}

ds_status_t ds_voronoi_from_delaunay(ds_voronoi_t *vd, const ds_delaunay_t *dt,
                                     const double *xy, ds_context_t *context) {
  size_t n;
  size_t T;
  size_t i;
  size_t *order = NULL;
  size_t *tmp = NULL;
  size_t *incident = NULL; /* a triangle per site */
  ds_status_t st = DS_OK;
  if (vd == NULL || dt == NULL || (xy == NULL && dt->points > 0)) {
    return DS_ERR_NULL;
  }
  memset(vd, 0, sizeof(*vd));
  n = dt->points;
  T = dt->triangles;
  vd->sites = n;
  vd->context = context;
  vd->twin = (size_t *)vo_array(context, n, sizeof(size_t), &st);
  vd->unbounded = (bool *)vo_array(context, n, sizeof(bool), &st);
  vd->cell_ray = (double *)vo_array(context, 4 * n, sizeof(double), &st);
  vd->cell_start = (size_t *)vo_array(context, n + 1, sizeof(size_t), &st);
  vd->nbr_start = (size_t *)vo_array(context, n + 1, sizeof(size_t), &st);
  order = (size_t *)vo_array(context, n, sizeof(size_t), &st);
  tmp = (size_t *)vo_array(context, n, sizeof(size_t), &st);
  if (st != DS_OK) {
    goto done;
  }
  /* equal sites: each points at the first of its run in (x, y) order,
   * which the stable sort makes the smallest index */
  vo_sort(xy, n, order, tmp);
  for (i = 0; i < n; i++) {
    vd->twin[order[i]] = i > 0 && vo_same(xy, order[i - 1], order[i])
                             ? vd->twin[order[i - 1]]
                             : order[i];
  }
  if (T == 0) {
    st = vo_collinear(vd, xy, order);
    goto done;
  }
  /* vertices */
  vd->vertices = T;
  vd->vertex = (double *)vo_array(context, 2 * T, sizeof(double), &st);
  vd->cell_vertex = (size_t *)vo_array(context, 3 * T, sizeof(size_t), &st);
  vd->nbr = (size_t *)vo_array(context, 3 * T + n, sizeof(size_t), &st);
  incident = (size_t *)vo_array(context, n, sizeof(size_t), &st);
  if (st != DS_OK) {
    goto done;
  }
  for (i = 0; i < n; i++) {
    incident[i] = VO_NONE;
  }
  for (i = 0; i < T; i++) {
    size_t k;
    for (k = 0; k < 3; k++) {
      size_t v = dt->tri[3 * i + k];
      if (v >= n) {
        st = DS_ERR_INVALID;
        goto done;
      }
      vd->cell_start[v + 1]++;
      incident[v] = i;
    }
    vo_circumcenter(xy + 2 * dt->tri[3 * i], xy + 2 * dt->tri[3 * i + 1],
                    xy + 2 * dt->tri[3 * i + 2], vd->vertex + 2 * i);
  }
  for (i = 0; i < n; i++) {
    vd->cell_start[i + 1] += vd->cell_start[i];
  }
  /* cells and neighbours, site by site */
  {
    size_t cpos = 0;
    size_t npos = 0;
    for (i = 0; i < n && st == DS_OK; i++) {
      size_t deg = vd->cell_start[i + 1] - vd->cell_start[i];
      vd->nbr_start[i] = npos;
      if (deg > 0) {
        st = vo_cell(vd, dt, xy, i, incident[i], deg, &cpos, &npos);
      }
    }
    vd->nbr_start[n] = npos;
  }
  if (st != DS_OK) {
    goto done;
  }
  /* edges: one per Delaunay edge, from the triangle on its left */
  for (i = 0; i < T; i++) {
    size_t k;
    for (k = 0; k < 3; k++) {
      size_t u = dt->adj[3 * i + k];
      if (u == VO_NONE || u > i) {
        vd->edges++;
      }
    }
  }
  vd->edge = (ds_voronoi_edge_t *)vo_array(context, vd->edges,
                                           sizeof(ds_voronoi_edge_t), &st);
  if (st != DS_OK) {
    goto done;
  }
  {
    size_t e = 0;
    for (i = 0; i < T; i++) {
      size_t k;
      for (k = 0; k < 3; k++) {
        size_t u = dt->adj[3 * i + k];
        size_t a = dt->tri[3 * i + (k + 1) % 3];
        size_t b = dt->tri[3 * i + (k + 2) % 3];
        ds_voronoi_edge_t *ed;
        if (u != VO_NONE && u <= i) {
          continue;
        }
        ed = &vd->edge[e++];
        ed->site[0] = a;
        ed->site[1] = b;
        ed->vertex[0] = i;
        ed->vertex[1] = u;
        ed->origin[0] = vd->vertex[2 * i];
        ed->origin[1] = vd->vertex[2 * i + 1];
        if (u == VO_NONE) {
          vo_right_normal(xy + 2 * a, xy + 2 * b, ed->dir);
        } else {
          ed->dir[0] = vd->vertex[2 * u] - vd->vertex[2 * i];
          ed->dir[1] = vd->vertex[2 * u + 1] - vd->vertex[2 * i + 1];
        }
      }
    }
  }
done:
  ds_context_free(context, order);
  ds_context_free(context, tmp);
  ds_context_free(context, incident);
  if (st != DS_OK) {
    ds_voronoi_destroy(vd);
    vd->context = context;
  }
  return st;
}

ds_status_t ds_voronoi_build(ds_voronoi_t *vd, const double *xy, size_t n,
                             ds_context_t *context) {
  ds_delaunay_t dt;
  ds_status_t st;
  if (vd == NULL) {
    return DS_ERR_NULL;
  }
  memset(vd, 0, sizeof(*vd));
  st = ds_delaunay_build(&dt, xy, n, context);
  if (st != DS_OK) {
    return st;
  }
  st = ds_voronoi_from_delaunay(vd, &dt, xy, context);
  ds_delaunay_destroy(&dt);
  return st;
}

/* ---- clipping ---- */

/* keep the part of polygon src (m vertices) where f <= 0, into dst */
static size_t vo_clip(const double *src, size_t m, double *dst, const double *s,
                      const double *t) {
  double nx = t[0] - s[0], ny = t[1] - s[1];
  double mx = (s[0] + t[0]) / 2.0, my = (s[1] + t[1]) / 2.0;
  size_t i;
  size_t o = 0;
  for (i = 0; i < m; i++) {
    const double *p = src + 2 * i;
    const double *q = src + 2 * ((i + 1) % m);
    double fp = nx * (p[0] - mx) + ny * (p[1] - my);
    double fq = nx * (q[0] - mx) + ny * (q[1] - my);
    if (fp <= 0.0) {
      dst[2 * o] = p[0];
      dst[2 * o + 1] = p[1];
      o++;
    }
    if ((fp <= 0.0) != (fq <= 0.0)) {
      double r = fp / (fp - fq);
      dst[2 * o] = p[0] + (q[0] - p[0]) * r;
      dst[2 * o + 1] = p[1] + (q[1] - p[1]) * r;
      o++;
    }
  }
  return o;
}

ds_status_t ds_voronoi_cell_clip(const ds_voronoi_t *vd, const double *xy,
                                 size_t s, const double box[4], double *out,
                                 size_t cap, size_t *count) {
  size_t deg;
  size_t need;
  size_t m = 4;
  size_t j;
  double *work;
  double *src;
  double *dst;
  if (vd == NULL || xy == NULL || box == NULL || count == NULL) {
    return DS_ERR_NULL;
  }
  *count = 0;
  if (s >= vd->sites) {
    return DS_ERR_RANGE;
  }
  if (!(box[0] < box[2]) || !(box[1] < box[3]) ||
      !(box[2] - box[0] < HUGE_VAL) || !(box[3] - box[1] < HUGE_VAL)) {
    return DS_ERR_INVALID; /* empty, inverted or not finite */
  }
  if (vd->twin[s] != s) {
    return DS_OK; /* a repeated site: the cell is its twin's */
  }
  deg = vd->nbr_start[s + 1] - vd->nbr_start[s];
  need = 4 + deg; /* each cut adds at most one vertex */
  if (cap < need || out == NULL) {
    *count = need;
    return out == NULL ? DS_ERR_NULL : DS_ERR_RANGE;
  }
  work = (double *)ds_context_alloc(vd->context, 2 * need * sizeof(double));
  if (work == NULL) {
    return DS_ERR_ALLOC;
  }
  /* an even number of cuts must end in out */
  src = deg % 2 == 0 ? out : work;
  dst = deg % 2 == 0 ? work : out;
  src[0] = box[0];
  src[1] = box[1];
  src[2] = box[2];
  src[3] = box[1];
  src[4] = box[2];
  src[5] = box[3];
  src[6] = box[0];
  src[7] = box[3];
  for (j = 0; j < deg && m > 0; j++) {
    double *swap;
    m = vo_clip(src, m, dst, xy + 2 * s,
                xy + 2 * vd->nbr[vd->nbr_start[s] + j]);
    swap = src;
    src = dst;
    dst = swap;
  }
  if (src != out && m > 0) {
    memcpy(out, src, 2 * m * sizeof(double));
  }
  ds_context_free(vd->context, work);
  *count = m;
  return DS_OK;
}
