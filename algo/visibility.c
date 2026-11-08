#include "visibility.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define VS_NONE DS_VISIBILITY_NONE

typedef struct vs_seg {
  double a1; /* sweep coordinate, a1 <= a2 */
  double b1; /* ray coordinate */
  double a2;
  double b2;
  size_t e1; /* output slot of (a1, b1) */
  size_t e2;
  bool along;
} vs_seg_t;

typedef struct vs {
  const vs_seg_t *seg;
  size_t *lc;
  size_t *rc;
  unsigned long *pri;
  size_t root;
} vs_t;

static double vs_y(const vs_seg_t *s, double at) {
  if (!(at > s->a1)) {
    return s->b1;
  }
  if (!(at < s->a2)) {
    return s->b2;
  }
  return s->b1 + (s->b2 - s->b1) * ((at - s->a1) / (s->a2 - s->a1));
}

static bool vs_less(const vs_t *v, size_t s, size_t t, double at) {
  double ys = vs_y(&v->seg[s], at);
  double yt = vs_y(&v->seg[t], at);
  if (ys < yt) {
    return true;
  }
  if (ys > yt) {
    return false;
  }
  return s < t;
}

/* mode 0: left gets nodes < s; mode 1: left gets nodes <= s */
static void vs_split(vs_t *v, size_t t, size_t s, double at, int mode,
                     size_t *l, size_t *r) {
  bool left;
  if (t == VS_NONE) {
    *l = *r = VS_NONE;
    return;
  }
  left = mode == 0 ? vs_less(v, t, s, at) : !vs_less(v, s, t, at);
  if (left) {
    vs_split(v, v->rc[t], s, at, mode, &v->rc[t], r);
    *l = t;
  } else {
    vs_split(v, v->lc[t], s, at, mode, l, &v->lc[t]);
    *r = t;
  }
}

static size_t vs_merge(vs_t *v, size_t a, size_t b) {
  if (a == VS_NONE) {
    return b;
  }
  if (b == VS_NONE) {
    return a;
  }
  if (v->pri[a] > v->pri[b]) {
    v->rc[a] = vs_merge(v, v->rc[a], b);
    return a;
  }
  v->lc[b] = vs_merge(v, a, v->lc[b]);
  return b;
}

static void vs_insert(vs_t *v, size_t s, double at) {
  size_t l;
  size_t r;
  v->lc[s] = v->rc[s] = VS_NONE;
  vs_split(v, v->root, s, at, 0, &l, &r);
  v->root = vs_merge(v, vs_merge(v, l, s), r);
}

static void vs_erase(vs_t *v, size_t s, double at) {
  size_t l;
  size_t m;
  size_t r;
  vs_split(v, v->root, s, at, 0, &l, &r);
  vs_split(v, r, s, at, 1, &m, &r);
  v->root = vs_merge(v, l, r);
}

/* nearest segment strictly above / below (pa, pb) in the status */
static void vs_query(const vs_t *v, double pa, double pb,
                     ds_visibility_ray_t *ray) {
  size_t t;
  for (t = v->root; t != VS_NONE;) {
    double y = vs_y(&v->seg[t], pa);
    if (y > pb) {
      if (ray->up == VS_NONE || y < ray->up_at) {
        ray->up = t;
        ray->up_at = y;
      }
      t = v->lc[t];
    } else {
      t = v->rc[t];
    }
  }
  for (t = v->root; t != VS_NONE;) {
    double y = vs_y(&v->seg[t], pa);
    if (y < pb) {
      if (ray->down == VS_NONE || y > ray->down_at) {
        ray->down = t;
        ray->down_at = y;
      }
      t = v->rc[t];
    } else {
      t = v->lc[t];
    }
  }
}

static int vs_cmp_double(const void *x, const void *y) {
  double a = *(const double *)x;
  double b = *(const double *)y;
  return a < b ? -1 : a > b ? 1 : 0;
}

static size_t vs_find(const double *xs, size_t m, double a) {
  size_t lo = 0;
  size_t hi = m;
  while (lo + 1 < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (xs[mid] > a) {
      hi = mid;
    } else {
      lo = mid;
    }
  }
  return lo;
}

/* counting-sort items into per-event buckets */
static void vs_bucket(const size_t *key, size_t n, size_t m, size_t *off,
                      size_t *items) {
  size_t i;
  for (i = 0; i <= m; i++) {
    off[i] = 0;
  }
  for (i = 0; i < n; i++) {
    if (key[i] != VS_NONE) {
      off[key[i] + 1]++;
    }
  }
  for (i = 0; i < m; i++) {
    off[i + 1] += off[i];
  }
  for (i = 0; i < n; i++) {
    if (key[i] != VS_NONE) {
      items[off[key[i]]++] = i;
    }
  }
  for (i = m; i > 0; i--) {
    off[i] = off[i - 1];
  }
  off[0] = 0;
}

ds_status_t ds_visibility_map(const ds_visibility_segment_t *segments,
                              size_t count, ds_visibility_axis_t axis,
                              ds_visibility_ray_t *out, ds_context_t *context) {
  vs_seg_t *seg = NULL;
  double *xs = NULL;
  size_t *mem = NULL;
  unsigned long *pri = NULL;
  size_t *key;
  size_t *s_off;
  size_t *s_item;
  size_t *e_off;
  size_t *e_item;
  size_t *p_off;
  size_t *p_item;
  size_t *v_off;
  size_t *v_item;
  vs_t v;
  size_t m = 0;
  size_t i;
  size_t ev;
  unsigned long rng = 0x2545F491ul;
  ds_status_t st = DS_OK;
  if (count > 0 && (segments == NULL || out == NULL)) {
    return DS_ERR_NULL;
  }
  if (count == 0) {
    return DS_OK;
  }
  if (count > (size_t)-1 / 32) {
    return DS_ERR_OVERFLOW;
  }
  seg = (vs_seg_t *)ds_context_alloc(context, count * sizeof(vs_seg_t));
  xs = (double *)ds_context_alloc(context, 2 * count * sizeof(double));
  pri =
      (unsigned long *)ds_context_alloc(context, count * sizeof(unsigned long));
  if (seg == NULL || xs == NULL || pri == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < count; i++) {
    const ds_visibility_segment_t *g = &segments[i];
    double a1 = axis == DS_VISIBILITY_VERTICAL ? g->x1 : g->y1;
    double b1 = axis == DS_VISIBILITY_VERTICAL ? g->y1 : g->x1;
    double a2 = axis == DS_VISIBILITY_VERTICAL ? g->x2 : g->y2;
    double b2 = axis == DS_VISIBILITY_VERTICAL ? g->y2 : g->x2;
    bool swap;
    if (!(fabs(a1) < HUGE_VAL) || !(fabs(b1) < HUGE_VAL) ||
        !(fabs(a2) < HUGE_VAL) || !(fabs(b2) < HUGE_VAL) ||
        (!(a1 < a2) && !(a1 > a2) && !(b1 < b2) && !(b1 > b2))) {
      st = DS_ERR_INVALID;
      goto done;
    }
    seg[i].along = !(a1 < a2) && !(a1 > a2);
    swap = seg[i].along ? b1 > b2 : a1 > a2;
    seg[i].a1 = swap ? a2 : a1;
    seg[i].b1 = swap ? b2 : b1;
    seg[i].a2 = swap ? a1 : a2;
    seg[i].b2 = swap ? b1 : b2;
    seg[i].e1 = swap ? 2 * i + 1 : 2 * i;
    seg[i].e2 = swap ? 2 * i : 2 * i + 1;
    xs[2 * i] = a1;
    xs[2 * i + 1] = a2;
    rng = rng * 1103515245ul + 12345ul;
    pri[i] = rng ^ (rng >> 16);
  }
  qsort(xs, 2 * count, sizeof(double), vs_cmp_double);
  for (i = 0; i < 2 * count; i++) {
    if (m == 0 || xs[i] > xs[m - 1]) {
      xs[m++] = xs[i];
    }
  }
  /* m <= 2 count events: four offset tables of m + 1, five item lists */
  mem = (size_t *)ds_context_alloc(context,
                                   (4 * (m + 1) + 7 * count) * sizeof(size_t));
  if (mem == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  key = mem;
  s_off = key + 2 * count;
  s_item = s_off + m + 1;
  e_off = s_item + count;
  e_item = e_off + m + 1;
  p_off = e_item + count;
  p_item = p_off + m + 1;
  v_off = p_item + 2 * count;
  v_item = v_off + m + 1;
  for (i = 0; i < count; i++) {
    key[i] = seg[i].along ? VS_NONE : vs_find(xs, m, seg[i].a1);
  }
  vs_bucket(key, count, m, s_off, s_item);
  for (i = 0; i < count; i++) {
    key[i] = seg[i].along ? VS_NONE : vs_find(xs, m, seg[i].a2);
  }
  vs_bucket(key, count, m, e_off, e_item);
  for (i = 0; i < count; i++) {
    key[i] = seg[i].along ? vs_find(xs, m, seg[i].a1) : VS_NONE;
  }
  vs_bucket(key, count, m, v_off, v_item);
  /* along segments of one event, sorted by their lower end (they are
   * disjoint, so this also sorts the upper ends) */
  for (ev = 0; ev < m; ev++) {
    size_t a;
    for (a = v_off[ev] + 1; a < v_off[ev + 1]; a++) {
      size_t x = v_item[a];
      size_t b = a;
      while (b > v_off[ev] && seg[v_item[b - 1]].b1 > seg[x].b1) {
        v_item[b] = v_item[b - 1];
        b--;
      }
      v_item[b] = x;
    }
  }
  for (i = 0; i < count; i++) {
    key[seg[i].e1] = vs_find(xs, m, seg[i].a1);
    key[seg[i].e2] = vs_find(xs, m, seg[i].a2);
  }
  vs_bucket(key, 2 * count, m, p_off, p_item);
  for (i = 0; i < 2 * count; i++) {
    out[i].up = out[i].down = VS_NONE;
    out[i].up_at = out[i].down_at = 0.0;
  }
  v.seg = seg;
  v.lc = (size_t *)ds_context_alloc(context, 2 * count * sizeof(size_t));
  if (v.lc == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  v.rc = v.lc + count;
  v.pri = pri;
  v.root = VS_NONE;
  for (ev = 0; ev < m; ev++) {
    double pa = xs[ev];
    size_t k;
    int phase;
    for (phase = 0; phase < 2; phase++) {
      if (phase == 1) {
        double before = ev > 0 ? 0.5 * (xs[ev - 1] + pa) : pa - 1.0;
        double after = ev + 1 < m ? 0.5 * (pa + xs[ev + 1]) : pa + 1.0;
        for (k = e_off[ev]; k < e_off[ev + 1]; k++) {
          vs_erase(&v, e_item[k], before);
        }
        for (k = s_off[ev]; k < s_off[ev + 1]; k++) {
          vs_insert(&v, s_item[k], after);
        }
      }
      for (k = p_off[ev]; k < p_off[ev + 1]; k++) {
        size_t slot = p_item[k];
        size_t s = slot / 2;
        double pb = seg[s].e1 == slot ? seg[s].b1 : seg[s].b2;
        vs_query(&v, pa, pb, &out[slot]);
      }
    }
    /* along segments at this coordinate */
    for (k = p_off[ev]; k < p_off[ev + 1]; k++) {
      size_t slot = p_item[k];
      size_t s = slot / 2;
      double pb = seg[s].e1 == slot ? seg[s].b1 : seg[s].b2;
      size_t lo = v_off[ev];
      size_t hi = v_off[ev + 1];
      /* first along segment whose lower end is strictly above pb */
      while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (seg[v_item[mid]].b1 > pb) {
          hi = mid;
        } else {
          lo = mid + 1;
        }
      }
      if (lo < v_off[ev + 1]) {
        size_t t = v_item[lo];
        if (out[slot].up == VS_NONE || seg[t].b1 < out[slot].up_at) {
          out[slot].up = t;
          out[slot].up_at = seg[t].b1;
        }
      }
      /* last along segment whose upper end is strictly below pb */
      lo = v_off[ev];
      hi = v_off[ev + 1];
      while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (seg[v_item[mid]].b2 < pb) {
          lo = mid + 1;
        } else {
          hi = mid;
        }
      }
      if (lo > v_off[ev]) {
        size_t t = v_item[lo - 1];
        if (out[slot].down == VS_NONE || seg[t].b2 > out[slot].down_at) {
          out[slot].down = t;
          out[slot].down_at = seg[t].b2;
        }
      }
      if (seg[s].along) {
        if (seg[s].e1 == slot) { /* the lower end looks up along itself */
          out[slot].up = s;
          out[slot].up_at = seg[s].b2;
        } else {
          out[slot].down = s;
          out[slot].down_at = seg[s].b1;
        }
      }
    }
  }
  ds_context_free(context, v.lc);
done:
  ds_context_free(context, seg);
  ds_context_free(context, xs);
  ds_context_free(context, mem);
  ds_context_free(context, pri);
  return st;
}
