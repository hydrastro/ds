#include "quadtree.h"
#include <math.h>
#include <string.h>

#define QT_NONE (~(size_t)0)

/* ---- region trees (PR quadtree, octree, bintree) ----------------------------
 */

static double *rt_pt(const ds_regtree_t *t, size_t p) {
  return t->coord + p * t->dim;
}

ds_status_t ds_regtree_init(ds_regtree_t *t, size_t dim, ds_regtree_mode_t mode,
                            const double *lo, const double *hi, size_t bucket,
                            size_t max_depth, ds_context_t *context) {
  size_t a;
  if (t == NULL || lo == NULL || hi == NULL) {
    return DS_ERR_NULL;
  }
  if (dim == 0 || dim > DS_REGTREE_MAX_DIM || bucket == 0 ||
      ((int)mode != DS_REGTREE_ALL_AXES && mode != DS_REGTREE_BINTREE)) {
    return DS_ERR_INVALID;
  }
  for (a = 0; a < dim; a++) {
    if (!(lo[a] < hi[a])) {
      return DS_ERR_INVALID;
    }
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->mode = mode;
  t->fanout = mode == DS_REGTREE_ALL_AXES ? (size_t)1 << dim : 2;
  t->bucket = bucket;
  t->max_depth = max_depth == 0 ? 32 : (max_depth > 200 ? 200 : max_depth);
  for (a = 0; a < dim; a++) {
    t->lo[a] = lo[a];
    t->hi[a] = hi[a];
  }
  t->node_free = QT_NONE;
  t->point_free = QT_NONE;
  t->context = context;
  /* node 0: the root leaf */
  t->child = (size_t *)ds_context_alloc(context, 16 * 4 * sizeof(size_t));
  t->depth = (unsigned char *)ds_context_alloc(context, 16);
  if (t->child == NULL || t->depth == NULL) {
    ds_context_free(context, t->child);
    ds_context_free(context, t->depth);
    return DS_ERR_ALLOC;
  }
  t->parent = t->child + 16;
  t->count = t->parent + 16;
  t->head = t->count + 16;
  t->node_capacity = 16;
  t->nodes = 1;
  t->child[0] = QT_NONE;
  t->parent[0] = QT_NONE;
  t->count[0] = 0;
  t->head[0] = QT_NONE;
  t->depth[0] = 0;
  return DS_OK;
}

void ds_regtree_destroy(ds_regtree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->child);
  ds_context_free(t->context, t->depth);
  ds_context_free(t->context, t->coord);
  ds_context_free(t->context, t->value);
  ds_context_free(t->context, t->pnext);
  ds_context_free(t->context, t->stack);
  t->child = t->parent = t->count = t->head = NULL;
  t->depth = NULL;
  t->coord = NULL;
  t->value = t->pnext = t->stack = NULL;
  t->nodes = t->points = t->size = 0;
}

size_t ds_regtree_size(const ds_regtree_t *t) {
  return t == NULL ? 0 : t->size;
}

/* A block of `fanout` consecutive leaf nodes. */
static ds_status_t rt_block(ds_regtree_t *t, size_t *out) {
  size_t first;
  size_t cap;
  size_t i;
  if (t->node_free != QT_NONE) {
    first = t->node_free;
    t->node_free = t->child[first];
  } else {
    if (t->nodes + t->fanout > t->node_capacity) {
      size_t *nc;
      unsigned char *nd;
      cap = t->node_capacity;
      while (cap < t->nodes + t->fanout) {
        cap *= 2;
      }
      if (cap > ((size_t)-1) / (4 * sizeof(size_t))) {
        return DS_ERR_OVERFLOW;
      }
      nc = (size_t *)ds_context_alloc(t->context, 4 * cap * sizeof(size_t));
      nd = (unsigned char *)ds_context_alloc(t->context, cap);
      if (nc == NULL || nd == NULL) {
        ds_context_free(t->context, nc);
        ds_context_free(t->context, nd);
        return DS_ERR_ALLOC;
      }
      memcpy(nc, t->child, t->nodes * sizeof(size_t));
      memcpy(nc + cap, t->parent, t->nodes * sizeof(size_t));
      memcpy(nc + 2 * cap, t->count, t->nodes * sizeof(size_t));
      memcpy(nc + 3 * cap, t->head, t->nodes * sizeof(size_t));
      memcpy(nd, t->depth, t->nodes);
      ds_context_free(t->context, t->child);
      ds_context_free(t->context, t->depth);
      t->child = nc;
      t->parent = nc + cap;
      t->count = nc + 2 * cap;
      t->head = nc + 3 * cap;
      t->depth = nd;
      t->node_capacity = cap;
    }
    first = t->nodes;
    t->nodes += t->fanout;
  }
  for (i = 0; i < t->fanout; i++) {
    t->child[first + i] = QT_NONE;
    t->count[first + i] = 0;
    t->head[first + i] = QT_NONE;
  }
  *out = first;
  return DS_OK;
}

/* Which child of a node (with cell box lo/hi at depth dep) holds p, and
 * narrow the box to it. */
static size_t rt_which(const ds_regtree_t *t, const double *p, double *lo,
                       double *hi, size_t dep) {
  size_t a;
  size_t c = 0;
  double mid;
  if (t->mode == DS_REGTREE_BINTREE) {
    a = dep % t->dim;
    mid = lo[a] + (hi[a] - lo[a]) / 2.0;
    if (p[a] >= mid) {
      lo[a] = mid;
      return 1;
    }
    hi[a] = mid;
    return 0;
  }
  for (a = 0; a < t->dim; a++) {
    mid = lo[a] + (hi[a] - lo[a]) / 2.0;
    if (p[a] >= mid) {
      c |= (size_t)1 << a;
      lo[a] = mid;
    } else {
      hi[a] = mid;
    }
  }
  return c;
}

/* Box of child c of a node with box lo/hi at depth dep (in place). */
static void rt_child_box(const ds_regtree_t *t, size_t c, double *lo,
                         double *hi, size_t dep) {
  size_t a;
  double mid;
  if (t->mode == DS_REGTREE_BINTREE) {
    a = dep % t->dim;
    mid = lo[a] + (hi[a] - lo[a]) / 2.0;
    if (c) {
      lo[a] = mid;
    } else {
      hi[a] = mid;
    }
    return;
  }
  for (a = 0; a < t->dim; a++) {
    mid = lo[a] + (hi[a] - lo[a]) / 2.0;
    if ((c >> a) & 1u) {
      lo[a] = mid;
    } else {
      hi[a] = mid;
    }
  }
}

static ds_status_t rt_point(ds_regtree_t *t, const double *p, size_t value,
                            size_t *out) {
  size_t i;
  size_t cap;
  void *q;
  if (t->point_free != QT_NONE) {
    i = t->point_free;
    t->point_free = t->pnext[i];
  } else {
    if (t->points == t->point_capacity) {
      cap = t->point_capacity < 16 ? 16 : t->point_capacity * 2;
      if (cap > ((size_t)-1) / (t->dim * sizeof(double))) {
        return DS_ERR_OVERFLOW;
      }
      q = ds_context_realloc(t->context, t->coord,
                             cap * t->dim * sizeof(double));
      if (q == NULL) {
        return DS_ERR_ALLOC;
      }
      t->coord = (double *)q;
      q = ds_context_realloc(t->context, t->value, cap * sizeof(size_t));
      if (q == NULL) {
        return DS_ERR_ALLOC;
      }
      t->value = (size_t *)q;
      q = ds_context_realloc(t->context, t->pnext, cap * sizeof(size_t));
      if (q == NULL) {
        return DS_ERR_ALLOC;
      }
      t->pnext = (size_t *)q;
      t->point_capacity = cap;
    }
    i = t->points++;
  }
  memcpy(rt_pt(t, i), p, t->dim * sizeof(double));
  t->value[i] = value;
  *out = i;
  return DS_OK;
}

ds_status_t ds_regtree_insert(ds_regtree_t *t, const double *point,
                              size_t value) {
  double lo[DS_REGTREE_MAX_DIM];
  double hi[DS_REGTREE_MAX_DIM];
  size_t v = 0;
  size_t p;
  size_t a;
  size_t blk;
  size_t q;
  size_t nq;
  size_t c;
  ds_status_t st;
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  for (a = 0; a < t->dim; a++) {
    if (!(point[a] >= t->lo[a] && point[a] <= t->hi[a])) {
      return DS_ERR_RANGE;
    }
    lo[a] = t->lo[a];
    hi[a] = t->hi[a];
  }
  st = rt_point(t, point, value, &p);
  if (st != DS_OK) {
    return st;
  }
  while (t->child[v] != QT_NONE) {
    t->count[v]++;
    v = t->child[v] + rt_which(t, point, lo, hi, t->depth[v]);
  }
  t->pnext[p] = t->head[v];
  t->head[v] = p;
  t->count[v]++;
  t->size++;
  /* split while the leaf overflows (only one child can, b + 1 points) */
  while (t->count[v] > t->bucket && t->depth[v] < t->max_depth) {
    st = rt_block(t, &blk);
    if (st != DS_OK) {
      return DS_OK; /* stays an oversized leaf */
    }
    for (c = 0; c < t->fanout; c++) {
      t->parent[blk + c] = v;
      t->depth[blk + c] = (unsigned char)(t->depth[v] + 1);
    }
    for (q = t->head[v]; q != QT_NONE; q = nq) {
      double l2[DS_REGTREE_MAX_DIM];
      double h2[DS_REGTREE_MAX_DIM];
      nq = t->pnext[q];
      memcpy(l2, lo, t->dim * sizeof(double));
      memcpy(h2, hi, t->dim * sizeof(double));
      c = blk + rt_which(t, rt_pt(t, q), l2, h2, t->depth[v]);
      t->pnext[q] = t->head[c];
      t->head[c] = q;
      t->count[c]++;
    }
    t->head[v] = QT_NONE;
    t->child[v] = blk;
    for (c = 0; c < t->fanout && t->count[blk + c] <= t->bucket; c++) {
    }
    if (c == t->fanout) {
      break;
    }
    rt_child_box(t, c, lo, hi, t->depth[v]);
    v = blk + c;
  }
  return DS_OK;
}

static ds_status_t rt_stack(ds_regtree_t *t, size_t need) {
  void *p;
  size_t cap;
  if (need <= t->stack_capacity) {
    return DS_OK;
  }
  cap = t->stack_capacity < 64 ? 64 : t->stack_capacity;
  while (cap < need) {
    cap *= 2;
  }
  p = ds_context_realloc(t->context, t->stack, cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->stack = (size_t *)p;
  t->stack_capacity = cap;
  return DS_OK;
}

ds_status_t ds_regtree_remove(ds_regtree_t *t, const double *point,
                              size_t value) {
  double lo[DS_REGTREE_MAX_DIM];
  double hi[DS_REGTREE_MAX_DIM];
  size_t v = 0;
  size_t a;
  size_t q;
  size_t prev = QT_NONE;
  size_t u;
  size_t top;
  ds_status_t st;
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  for (a = 0; a < t->dim; a++) {
    if (!(point[a] >= t->lo[a] && point[a] <= t->hi[a])) {
      return DS_NOT_FOUND;
    }
    lo[a] = t->lo[a];
    hi[a] = t->hi[a];
  }
  while (t->child[v] != QT_NONE) {
    v = t->child[v] + rt_which(t, point, lo, hi, t->depth[v]);
  }
  for (q = t->head[v]; q != QT_NONE; prev = q, q = t->pnext[q]) {
    if (t->value[q] == value &&
        memcmp(rt_pt(t, q), point, t->dim * sizeof(double)) == 0) {
      break;
    }
  }
  if (q == QT_NONE) {
    return DS_NOT_FOUND;
  }
  if (prev == QT_NONE) {
    t->head[v] = t->pnext[q];
  } else {
    t->pnext[prev] = t->pnext[q];
  }
  t->pnext[q] = t->point_free;
  t->point_free = q;
  t->size--;
  for (u = v; u != QT_NONE; u = t->parent[u]) {
    t->count[u]--;
  }
  /* collapse the topmost ancestor that fits in one bucket */
  q = QT_NONE;
  for (u = t->parent[v]; u != QT_NONE; u = t->parent[u]) {
    if (t->count[u] <= t->bucket) {
      q = u;
    }
  }
  if (q == QT_NONE) {
    return DS_OK;
  }
  /* gather the subtree's points into q, then release its blocks */
  {
    size_t *blocks;
    size_t nb = 0;
    size_t gathered = QT_NONE;
    size_t i;
    st = rt_stack(t, t->nodes + 1);
    blocks = (size_t *)ds_context_alloc(t->context, t->nodes * sizeof(size_t));
    if (st != DS_OK || blocks == NULL) {
      ds_context_free(t->context, blocks);
      return DS_OK; /* still correct, just not collapsed */
    }
    top = 0;
    t->stack[top++] = q;
    while (top > 0) {
      size_t w = t->stack[--top];
      size_t c;
      size_t r;
      size_t nr;
      if (t->child[w] == QT_NONE) {
        for (r = t->head[w]; r != QT_NONE; r = nr) {
          nr = t->pnext[r];
          t->pnext[r] = gathered;
          gathered = r;
        }
        t->head[w] = QT_NONE;
        continue;
      }
      blocks[nb++] = t->child[w];
      for (c = 0; c < t->fanout; c++) {
        t->stack[top++] = t->child[w] + c;
      }
    }
    for (i = 0; i < nb; i++) {
      t->child[blocks[i]] = t->node_free;
      t->node_free = blocks[i];
    }
    ds_context_free(t->context, blocks);
    t->child[q] = QT_NONE;
    t->head[q] = gathered;
  }
  return DS_OK;
}

/* Frames for box traversals: node index on t->stack, its box in a
 * parallel double array (2 * dim per frame). */
typedef struct rt_frames {
  double *box;
  size_t cap;
} rt_frames_t;

static ds_status_t rt_frames_need(ds_regtree_t *t, rt_frames_t *f,
                                  size_t need) {
  void *p;
  size_t cap;
  ds_status_t st = rt_stack(t, need);
  if (st != DS_OK) {
    return st;
  }
  if (need <= f->cap) {
    return DS_OK;
  }
  cap = f->cap < 64 ? 64 : f->cap;
  while (cap < need) {
    cap *= 2;
  }
  p = ds_context_realloc(t->context, f->box, cap * 2 * t->dim * sizeof(double));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  f->box = (double *)p;
  f->cap = cap;
  return DS_OK;
}

static bool rt_inside(const ds_regtree_t *t, const double *p, const double *qlo,
                      const double *qhi) {
  size_t a;
  for (a = 0; a < t->dim; a++) {
    if (p[a] < qlo[a] || p[a] > qhi[a]) {
      return false;
    }
  }
  return true;
}

/* mode 0: visit, mode 1: count (using subtree counts) */
static ds_status_t rt_box_walk(ds_regtree_t *t, const double *qlo,
                               const double *qhi, int mode,
                               ds_spatial_visit_func_t visit, void *user,
                               size_t *count) {
  rt_frames_t f;
  size_t top = 0;
  size_t d = t->dim;
  size_t a;
  size_t c;
  ds_status_t st = DS_OK;
  f.box = NULL;
  f.cap = 0;
  *count = 0;
  st = rt_frames_need(t, &f, 1);
  if (st != DS_OK) {
    return st;
  }
  t->stack[0] = 0;
  memcpy(f.box, t->lo, d * sizeof(double));
  memcpy(f.box + d, t->hi, d * sizeof(double));
  top = 1;
  while (top > 0 && st == DS_OK) {
    size_t v = t->stack[--top];
    double *lo = f.box + top * 2 * d;
    double *hi = lo + d;
    bool disjoint = false;
    bool contained = true;
    for (a = 0; a < d; a++) {
      if (hi[a] < qlo[a] || lo[a] > qhi[a]) {
        disjoint = true;
      }
      if (lo[a] < qlo[a] || hi[a] > qhi[a]) {
        contained = false;
      }
    }
    if (disjoint || t->count[v] == 0) {
      continue;
    }
    if (mode == 1 && contained) {
      *count += t->count[v];
      continue;
    }
    if (t->child[v] == QT_NONE) {
      size_t q;
      for (q = t->head[v]; q != QT_NONE; q = t->pnext[q]) {
        if (rt_inside(t, rt_pt(t, q), qlo, qhi)) {
          (*count)++;
          if (mode == 0 && !visit(rt_pt(t, q), t->value[q], user)) {
            st = DS_STOP;
            break;
          }
        }
      }
      continue;
    }
    st = rt_frames_need(t, &f, top + t->fanout);
    if (st != DS_OK) {
      break;
    }
    lo = f.box + top * 2 * d; /* the buffer may have moved */
    {
      double plo[DS_REGTREE_MAX_DIM];
      double phi[DS_REGTREE_MAX_DIM];
      memcpy(plo, lo, d * sizeof(double));
      memcpy(phi, lo + d, d * sizeof(double));
      for (c = t->fanout; c-- > 0;) {
        double *clo = f.box + top * 2 * d;
        memcpy(clo, plo, d * sizeof(double));
        memcpy(clo + d, phi, d * sizeof(double));
        rt_child_box(t, c, clo, clo + d, t->depth[v]);
        t->stack[top++] = t->child[v] + c;
      }
    }
  }
  ds_context_free(t->context, f.box);
  return st;
}

ds_status_t ds_regtree_query(ds_regtree_t *t, const double *qlo,
                             const double *qhi, ds_spatial_visit_func_t visit,
                             void *user) {
  size_t n;
  if (t == NULL || qlo == NULL || qhi == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  return rt_box_walk(t, qlo, qhi, 0, visit, user, &n);
}

size_t ds_regtree_count(ds_regtree_t *t, const double *qlo, const double *qhi) {
  size_t n = 0;
  if (t == NULL || qlo == NULL || qhi == NULL) {
    return 0;
  }
  (void)rt_box_walk(t, qlo, qhi, 1, NULL, NULL, &n);
  return n;
}

size_t ds_regtree_leaves(const ds_regtree_t *t) {
  size_t v;
  size_t n = 0;
  size_t f;
  unsigned char *freed;
  if (t == NULL) {
    return 0;
  }
  freed = (unsigned char *)ds_context_calloc(t->context, t->nodes + 1, 1);
  if (freed == NULL) {
    return 0;
  }
  for (f = t->node_free; f != QT_NONE; f = t->child[f]) {
    for (v = 0; v < t->fanout; v++) {
      freed[f + v] = 1;
    }
  }
  for (v = 0; v < t->nodes; v++) {
    n += !freed[v] && t->child[v] == QT_NONE;
  }
  ds_context_free(t->context, freed);
  return n;
}

static double rt_mindist2(const ds_regtree_t *t, const double *q,
                          const double *lo, const double *hi) {
  size_t a;
  double s = 0.0;
  double d;
  for (a = 0; a < t->dim; a++) {
    d = q[a] < lo[a] ? lo[a] - q[a] : (q[a] > hi[a] ? q[a] - hi[a] : 0.0);
    s += d * d;
  }
  return s;
}

ds_status_t ds_regtree_nearest(ds_regtree_t *t, const double *q, size_t k,
                               double *out_points, size_t *out_values,
                               size_t *out_found) {
  rt_frames_t f;
  double *bd; /* result distances, sorted ascending */
  size_t *bi; /* result point ids */
  size_t found = 0;
  size_t top;
  size_t d;
  size_t i;
  size_t c;
  ds_status_t st;
  if (t == NULL || q == NULL || out_found == NULL ||
      (k > 0 && (out_points == NULL || out_values == NULL))) {
    return DS_ERR_NULL;
  }
  *out_found = 0;
  if (k == 0 || t->size == 0) {
    return DS_OK;
  }
  d = t->dim;
  bd = (double *)ds_context_alloc(t->context, k * sizeof(double));
  bi = (size_t *)ds_context_alloc(t->context, k * sizeof(size_t));
  if (bd == NULL || bi == NULL) {
    ds_context_free(t->context, bd);
    ds_context_free(t->context, bi);
    return DS_ERR_ALLOC;
  }
  f.box = NULL;
  f.cap = 0;
  st = rt_frames_need(t, &f, 1);
  if (st == DS_OK) {
    t->stack[0] = 0;
    memcpy(f.box, t->lo, d * sizeof(double));
    memcpy(f.box + d, t->hi, d * sizeof(double));
    top = 1;
    while (top > 0) {
      size_t v = t->stack[--top];
      double *lo = f.box + top * 2 * d;
      double md = rt_mindist2(t, q, lo, lo + d);
      if (t->count[v] == 0 || (found == k && md >= bd[k - 1])) {
        continue;
      }
      if (t->child[v] == QT_NONE) {
        size_t r;
        for (r = t->head[v]; r != QT_NONE; r = t->pnext[r]) {
          double s = 0.0;
          size_t a;
          for (a = 0; a < d; a++) {
            double dd = rt_pt(t, r)[a] - q[a];
            s += dd * dd;
          }
          if (found < k || s < bd[found - 1]) {
            /* insertion into the sorted result list */
            size_t pos = found < k ? found++ : k - 1;
            while (pos > 0 && bd[pos - 1] > s) {
              bd[pos] = bd[pos - 1];
              bi[pos] = bi[pos - 1];
              pos--;
            }
            bd[pos] = s;
            bi[pos] = r;
          }
        }
        continue;
      }
      st = rt_frames_need(t, &f, top + t->fanout);
      if (st != DS_OK) {
        break;
      }
      lo = f.box + top * 2 * d;
      {
        double plo[DS_REGTREE_MAX_DIM];
        double phi[DS_REGTREE_MAX_DIM];
        double cd[256];
        size_t order[256];
        size_t base = top;
        memcpy(plo, lo, d * sizeof(double));
        memcpy(phi, lo + d, d * sizeof(double));
        /* children farthest first on the stack: nearest popped first */
        for (c = 0; c < t->fanout; c++) {
          double *clo = f.box + (base + c) * 2 * d;
          memcpy(clo, plo, d * sizeof(double));
          memcpy(clo + d, phi, d * sizeof(double));
          rt_child_box(t, c, clo, clo + d, t->depth[v]);
          cd[c] = rt_mindist2(t, q, clo, clo + d);
          order[c] = c;
        }
        for (c = 1; c < t->fanout; c++) {
          size_t x = order[c];
          for (i = c; i > 0 && cd[order[i - 1]] < cd[x]; i--) {
            order[i] = order[i - 1];
          }
          order[i] = x;
        }
        /* rewrite frames in the sorted order */
        {
          double *save = (double *)ds_context_alloc(
              t->context, t->fanout * 2 * d * sizeof(double));
          if (save == NULL) {
            st = DS_ERR_ALLOC;
            break;
          }
          memcpy(save, f.box + base * 2 * d,
                 t->fanout * 2 * d * sizeof(double));
          for (c = 0; c < t->fanout; c++) {
            memcpy(f.box + (base + c) * 2 * d, save + order[c] * 2 * d,
                   2 * d * sizeof(double));
            t->stack[base + c] = t->child[v] + order[c];
          }
          ds_context_free(t->context, save);
        }
        top = base + t->fanout;
      }
    }
  }
  for (i = 0; i < found && st == DS_OK; i++) {
    memcpy(out_points + i * d, rt_pt(t, bi[i]), d * sizeof(double));
    out_values[i] = t->value[bi[i]];
  }
  *out_found = st == DS_OK ? found : 0;
  ds_context_free(t->context, f.box);
  ds_context_free(t->context, bd);
  ds_context_free(t->context, bi);
  return st;
}

/* ---- point quadtree
 * ---------------------------------------------------------- */

ds_status_t ds_point_quadtree_init(ds_point_quadtree_t *t,
                                   ds_context_t *context) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  t->root = QT_NONE;
  t->free_list = QT_NONE;
  t->context = context;
  return DS_OK;
}

void ds_point_quadtree_destroy(ds_point_quadtree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->x);
  ds_context_free(t->context, t->y);
  ds_context_free(t->context, t->value);
  ds_context_free(t->context, t->child);
  ds_context_free(t->context, t->stack);
  ds_point_quadtree_init(t, t->context);
}

size_t ds_point_quadtree_size(const ds_point_quadtree_t *t) {
  return t == NULL ? 0 : t->size;
}

static size_t pq_quad(const ds_point_quadtree_t *t, size_t v, double x,
                      double y) {
  bool east = x >= t->x[v];
  bool north = y >= t->y[v];
  return north ? (east ? 0 : 1) : (east ? 3 : 2);
}

static ds_status_t pq_stack(ds_point_quadtree_t *t, size_t need) {
  void *p;
  size_t cap;
  if (need <= t->stack_capacity) {
    return DS_OK;
  }
  cap = t->stack_capacity < 64 ? 64 : t->stack_capacity;
  while (cap < need) {
    cap *= 2;
  }
  p = ds_context_realloc(t->context, t->stack, cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->stack = (size_t *)p;
  t->stack_capacity = cap;
  return DS_OK;
}

/* Link node n (coordinates set) into the tree. */
static void pq_link(ds_point_quadtree_t *t, size_t n) {
  size_t v = t->root;
  size_t c;
  t->child[4 * n] = t->child[4 * n + 1] = t->child[4 * n + 2] =
      t->child[4 * n + 3] = QT_NONE;
  if (v == QT_NONE) {
    t->root = n;
    return;
  }
  for (;;) {
    c = pq_quad(t, v, t->x[n], t->y[n]);
    if (t->child[4 * v + c] == QT_NONE) {
      t->child[4 * v + c] = n;
      return;
    }
    v = t->child[4 * v + c];
  }
}

ds_status_t ds_point_quadtree_insert(ds_point_quadtree_t *t, double x, double y,
                                     size_t value) {
  size_t n;
  size_t cap;
  void *p;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (t->free_list != QT_NONE) {
    n = t->free_list;
    t->free_list = t->child[4 * n];
  } else {
    if (t->nodes == t->capacity) {
      cap = t->capacity < 16 ? 16 : t->capacity * 2;
      if (cap > ((size_t)-1) / (4 * sizeof(size_t))) {
        return DS_ERR_OVERFLOW;
      }
      p = ds_context_realloc(t->context, t->x, cap * sizeof(double));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->x = (double *)p;
      p = ds_context_realloc(t->context, t->y, cap * sizeof(double));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->y = (double *)p;
      p = ds_context_realloc(t->context, t->value, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->value = (size_t *)p;
      p = ds_context_realloc(t->context, t->child, 4 * cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->child = (size_t *)p;
      t->capacity = cap;
    }
    n = t->nodes++;
  }
  t->x[n] = x;
  t->y[n] = y;
  t->value[n] = value;
  pq_link(t, n);
  t->size++;
  return DS_OK;
}

static bool pq_same(double a, double b) {
  return !(a < b) && !(a > b);
}

ds_status_t ds_point_quadtree_find(ds_point_quadtree_t *t, double x, double y,
                                   size_t *out_value) {
  size_t v;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  for (v = t->root; v != QT_NONE; v = t->child[4 * v + pq_quad(t, v, x, y)]) {
    if (pq_same(t->x[v], x) && pq_same(t->y[v], y)) {
      if (out_value != NULL) {
        *out_value = t->value[v];
      }
      return DS_OK;
    }
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_point_quadtree_remove(ds_point_quadtree_t *t, double x, double y,
                                     size_t value) {
  size_t v;
  size_t parent = QT_NONE;
  size_t pc = 0;
  size_t top = 0;
  size_t c;
  size_t w;
  size_t base;
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  for (v = t->root; v != QT_NONE;) {
    if (pq_same(t->x[v], x) && pq_same(t->y[v], y) && t->value[v] == value) {
      break;
    }
    parent = v;
    pc = pq_quad(t, v, x, y);
    v = t->child[4 * v + pc];
  }
  if (v == QT_NONE) {
    return DS_NOT_FOUND;
  }
  st = pq_stack(t, 2 * t->size + 4);
  if (st != DS_OK) {
    return st;
  }
  /* detach v; collect its descendants (second half of the stack) */
  if (parent == QT_NONE) {
    t->root = QT_NONE;
  } else {
    t->child[4 * parent + pc] = QT_NONE;
  }
  base = t->size + 1;
  top = 0;
  for (c = 0; c < 4; c++) {
    if (t->child[4 * v + c] != QT_NONE) {
      t->stack[top++] = t->child[4 * v + c];
    }
  }
  {
    size_t col = 0;
    while (top > 0) {
      w = t->stack[--top];
      t->stack[base + col++] = w;
      for (c = 0; c < 4; c++) {
        if (t->child[4 * w + c] != QT_NONE) {
          t->stack[top++] = t->child[4 * w + c];
        }
      }
    }
    /* reinsert them (in collection order: parents before children) */
    for (c = 0; c < col; c++) {
      pq_link(t, t->stack[base + c]);
    }
  }
  t->child[4 * v] = t->free_list;
  t->free_list = v;
  t->size--;
  return DS_OK;
}

ds_status_t ds_point_quadtree_query(ds_point_quadtree_t *t, double xlo,
                                    double ylo, double xhi, double yhi,
                                    ds_spatial_visit_func_t visit, void *user) {
  size_t top = 0;
  size_t v;
  double p[2];
  ds_status_t st;
  if (t == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root == QT_NONE) {
    return DS_OK;
  }
  st = pq_stack(t, 4 * t->size + 4);
  if (st != DS_OK) {
    return st;
  }
  t->stack[top++] = t->root;
  while (top > 0) {
    v = t->stack[--top];
    if (t->x[v] >= xlo && t->x[v] <= xhi && t->y[v] >= ylo && t->y[v] <= yhi) {
      p[0] = t->x[v];
      p[1] = t->y[v];
      if (!visit(p, t->value[v], user)) {
        return DS_STOP;
      }
    }
    if (t->child[4 * v] != QT_NONE && xhi >= t->x[v] && yhi >= t->y[v]) {
      t->stack[top++] = t->child[4 * v];
    }
    if (t->child[4 * v + 1] != QT_NONE && xlo < t->x[v] && yhi >= t->y[v]) {
      t->stack[top++] = t->child[4 * v + 1];
    }
    if (t->child[4 * v + 2] != QT_NONE && xlo < t->x[v] && ylo < t->y[v]) {
      t->stack[top++] = t->child[4 * v + 2];
    }
    if (t->child[4 * v + 3] != QT_NONE && xhi >= t->x[v] && ylo < t->y[v]) {
      t->stack[top++] = t->child[4 * v + 3];
    }
  }
  return DS_OK;
}

ds_status_t ds_point_quadtree_nearest(ds_point_quadtree_t *t, double x,
                                      double y, double *out_x, double *out_y,
                                      size_t *out_value) {
  double *box; /* per frame: xlo, ylo, xhi, yhi */
  size_t top = 0;
  size_t v;
  size_t c;
  size_t best = QT_NONE;
  double bd = HUGE_VAL;
  ds_status_t st;
  if (t == NULL || out_x == NULL || out_y == NULL || out_value == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root == QT_NONE) {
    return DS_EMPTY;
  }
  st = pq_stack(t, 4 * t->size + 4);
  if (st != DS_OK) {
    return st;
  }
  box = (double *)ds_context_alloc(t->context,
                                   (4 * t->size + 4) * 4 * sizeof(double));
  if (box == NULL) {
    return DS_ERR_ALLOC;
  }
  t->stack[top] = t->root;
  box[0] = -HUGE_VAL;
  box[1] = -HUGE_VAL;
  box[2] = HUGE_VAL;
  box[3] = HUGE_VAL;
  top++;
  while (top > 0) {
    double *b;
    double dx;
    double dy;
    double d2;
    top--;
    v = t->stack[top];
    b = box + 4 * top;
    dx = x < b[0] ? b[0] - x : (x > b[2] ? x - b[2] : 0.0);
    dy = y < b[1] ? b[1] - y : (y > b[3] ? y - b[3] : 0.0);
    if (dx * dx + dy * dy >= bd) {
      continue;
    }
    dx = t->x[v] - x;
    dy = t->y[v] - y;
    d2 = dx * dx + dy * dy;
    if (d2 < bd) {
      bd = d2;
      best = v;
    }
    {
      double xs = t->x[v];
      double ys = t->y[v];
      double cb[4][4];
      size_t near = pq_quad(t, v, x, y);
      cb[0][0] = xs;
      cb[0][1] = ys;
      cb[0][2] = b[2];
      cb[0][3] = b[3];
      cb[1][0] = b[0];
      cb[1][1] = ys;
      cb[1][2] = xs;
      cb[1][3] = b[3];
      cb[2][0] = b[0];
      cb[2][1] = b[1];
      cb[2][2] = xs;
      cb[2][3] = ys;
      cb[3][0] = xs;
      cb[3][1] = b[1];
      cb[3][2] = b[2];
      cb[3][3] = ys;
      /* the query's own quadrant last, so it is explored first */
      for (c = 0; c < 4; c++) {
        size_t q = c == 3 ? near : (c < near ? c : c + 1);
        if (t->child[4 * v + q] == QT_NONE) {
          continue;
        }
        t->stack[top] = t->child[4 * v + q];
        memcpy(box + 4 * top, cb[q], 4 * sizeof(double));
        top++;
      }
    }
  }
  *out_x = t->x[best];
  *out_y = t->y[best];
  *out_value = t->value[best];
  ds_context_free(t->context, box);
  return DS_OK;
}

/* ---- linear quadtree
 * --------------------------------------------------------- */

static unsigned long lq_spread(unsigned v) {
  unsigned long x = (unsigned long)(v & 0xFFFFu);
  x = (x | (x << 8)) & 0x00FF00FFUL;
  x = (x | (x << 4)) & 0x0F0F0F0FUL;
  x = (x | (x << 2)) & 0x33333333UL;
  x = (x | (x << 1)) & 0x55555555UL;
  return x;
}

static unsigned lq_gather(unsigned long x) {
  x &= 0x55555555UL;
  x = (x | (x >> 1)) & 0x33333333UL;
  x = (x | (x >> 2)) & 0x0F0F0F0FUL;
  x = (x | (x >> 4)) & 0x00FF00FFUL;
  x = (x | (x >> 8)) & 0x0000FFFFUL;
  return (unsigned)x;
}

static unsigned long lq_code(unsigned x, unsigned y) {
  return lq_spread(x) | (lq_spread(y) << 1);
}

ds_status_t ds_linear_quadtree_init(ds_linear_quadtree_t *t,
                                    ds_context_t *context) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  t->context = context;
  return DS_OK;
}

void ds_linear_quadtree_destroy(ds_linear_quadtree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->code);
  ds_context_free(t->context, t->value);
  t->code = NULL;
  t->value = NULL;
  t->size = t->capacity = 0;
}

size_t ds_linear_quadtree_size(const ds_linear_quadtree_t *t) {
  return t == NULL ? 0 : t->size;
}

/* First index with code >= k. */
static size_t lq_lower(const ds_linear_quadtree_t *t, unsigned long k) {
  size_t lo = 0;
  size_t hi = t->size;
  size_t mid;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    if (t->code[mid] < k) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

ds_status_t ds_linear_quadtree_insert(ds_linear_quadtree_t *t, unsigned x,
                                      unsigned y, size_t value) {
  unsigned long k;
  size_t i;
  size_t cap;
  void *p;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (x > 0xFFFFu || y > 0xFFFFu) {
    return DS_ERR_RANGE;
  }
  if (t->size == t->capacity) {
    cap = t->capacity < 16 ? 16 : t->capacity * 2;
    if (cap > ((size_t)-1) / sizeof(unsigned long)) {
      return DS_ERR_OVERFLOW;
    }
    p = ds_context_realloc(t->context, t->code, cap * sizeof(unsigned long));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    t->code = (unsigned long *)p;
    p = ds_context_realloc(t->context, t->value, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    t->value = (size_t *)p;
    t->capacity = cap;
  }
  k = lq_code(x, y);
  i = lq_lower(t, k + 1); /* after equal codes */
  memmove(t->code + i + 1, t->code + i, (t->size - i) * sizeof(unsigned long));
  memmove(t->value + i + 1, t->value + i, (t->size - i) * sizeof(size_t));
  t->code[i] = k;
  t->value[i] = value;
  t->size++;
  return DS_OK;
}

ds_status_t ds_linear_quadtree_remove(ds_linear_quadtree_t *t, unsigned x,
                                      unsigned y, size_t value) {
  unsigned long k;
  size_t i;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (x > 0xFFFFu || y > 0xFFFFu) {
    return DS_NOT_FOUND;
  }
  k = lq_code(x, y);
  for (i = lq_lower(t, k); i < t->size && t->code[i] == k; i++) {
    if (t->value[i] == value) {
      memmove(t->code + i, t->code + i + 1,
              (t->size - i - 1) * sizeof(unsigned long));
      memmove(t->value + i, t->value + i + 1,
              (t->size - i - 1) * sizeof(size_t));
      t->size--;
      return DS_OK;
    }
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_linear_quadtree_query(const ds_linear_quadtree_t *t,
                                     unsigned xlo, unsigned ylo, unsigned xhi,
                                     unsigned yhi,
                                     bool (*visit)(unsigned x, unsigned y,
                                                   size_t value, void *user),
                                     void *user) {
  /* frames: cell origin and level (side 2^level) */
  unsigned long sx[3 * 64];
  size_t top = 0;
  if (t == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (xlo > xhi || ylo > yhi || t->size == 0) {
    return DS_OK;
  }
  sx[0] = 0;
  sx[1] = 0;
  sx[2] = 16;
  top = 1;
  while (top > 0) {
    unsigned long cx;
    unsigned long cy;
    unsigned long lvl;
    unsigned long side;
    top--;
    cx = sx[3 * top];
    cy = sx[3 * top + 1];
    lvl = sx[3 * top + 2];
    side = 1UL << lvl;
    if (cx > xhi || cy > yhi || cx + side - 1 < xlo || cy + side - 1 < ylo) {
      continue;
    }
    if (cx >= xlo && cy >= ylo && cx + side - 1 <= xhi &&
        cy + side - 1 <= yhi) {
      /* the cell is a contiguous code interval */
      unsigned long k0 = lq_code((unsigned)cx, (unsigned)cy);
      unsigned long k1 = k0 + (lvl == 16 ? 0xFFFFFFFFUL : (side * side - 1));
      size_t i;
      for (i = lq_lower(t, k0); i < t->size && t->code[i] <= k1; i++) {
        if (!visit(lq_gather(t->code[i]), lq_gather(t->code[i] >> 1),
                   t->value[i], user)) {
          return DS_STOP;
        }
        if (t->code[i] == 0xFFFFFFFFUL) {
          break;
        }
      }
      continue;
    }
    /* four children, pushed in reverse Z order */
    {
      unsigned long h = side / 2;
      unsigned long q;
      for (q = 4; q-- > 0;) {
        sx[3 * top] = cx + ((q & 1UL) ? h : 0);
        sx[3 * top + 1] = cy + ((q & 2UL) ? h : 0);
        sx[3 * top + 2] = lvl - 1;
        top++;
      }
    }
  }
  return DS_OK;
}
