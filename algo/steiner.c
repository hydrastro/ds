#include "steiner.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define SN_NONE (~(size_t)0)

/* All-pairs shortest paths with next hops (Floyd-Warshall). */
static void sn_floyd(const double *w, size_t n, double *d, size_t *nx) {
  size_t i;
  size_t j;
  size_t k;
  for (i = 0; i < n; i++) {
    for (j = 0; j < n; j++) {
      d[i * n + j] = i == j ? 0.0 : w[i * n + j];
      nx[i * n + j] = (i == j || d[i * n + j] < HUGE_VAL) ? j : SN_NONE;
    }
  }
  for (k = 0; k < n; k++) {
    for (i = 0; i < n; i++) {
      if (!(d[i * n + k] < HUGE_VAL)) {
        continue;
      }
      for (j = 0; j < n; j++) {
        if (d[i * n + k] + d[k * n + j] < d[i * n + j]) {
          d[i * n + j] = d[i * n + k] + d[k * n + j];
          nx[i * n + j] = nx[i * n + k];
        }
      }
    }
  }
}

static void sn_mark_path(const size_t *nx, size_t n, size_t a, size_t b,
                         unsigned char *mark) {
  size_t x = a;
  size_t y;
  while (x != b && x != SN_NONE) {
    y = nx[x * n + b];
    if (y == SN_NONE) {
      return;
    }
    mark[x * n + y] = mark[y * n + x] = 1;
    x = y;
  }
}

/* From a marked edge set containing a tree on the terminals: span it,
 * prune non-terminal leaves, and write the edges. */
static ds_status_t sn_finish(const double *w, size_t n,
                             const unsigned char *mark, const size_t *term,
                             size_t k, size_t *eu, size_t *ev,
                             size_t *out_edges, double *out_length,
                             ds_context_t *context) {
  size_t *par;
  double *key;
  unsigned char *in;
  unsigned char *isterm;
  size_t *deg;
  size_t i;
  size_t j;
  size_t v;
  size_t m = 0;
  bool pruned;
  double len = 0.0;
  par = (size_t *)ds_context_alloc(context, 2 * n * sizeof(size_t));
  key = (double *)ds_context_alloc(context, n * sizeof(double));
  in = (unsigned char *)ds_context_calloc(context, 2 * n, 1);
  if (par == NULL || key == NULL || in == NULL) {
    ds_context_free(context, par);
    ds_context_free(context, key);
    ds_context_free(context, in);
    return DS_ERR_ALLOC;
  }
  deg = par + n;
  isterm = in + n;
  for (i = 0; i < k; i++) {
    isterm[term[i]] = 1;
  }
  for (i = 0; i < n; i++) {
    key[i] = HUGE_VAL;
    par[i] = SN_NONE;
    deg[i] = 0;
  }
  /* Prim over the marked edges from the first terminal */
  key[term[0]] = 0.0;
  for (;;) {
    v = SN_NONE;
    for (i = 0; i < n; i++) {
      if (!in[i] && key[i] < HUGE_VAL && (v == SN_NONE || key[i] < key[v])) {
        v = i;
      }
    }
    if (v == SN_NONE) {
      break;
    }
    in[v] = 1;
    for (j = 0; j < n; j++) {
      if (!in[j] && mark[v * n + j] && w[v * n + j] < key[j]) {
        key[j] = w[v * n + j];
        par[j] = v;
      }
    }
  }
  for (i = 0; i < k; i++) {
    if (!in[term[i]]) {
      ds_context_free(context, par);
      ds_context_free(context, key);
      ds_context_free(context, in);
      return DS_NOT_FOUND;
    }
  }
  for (i = 0; i < n; i++) {
    if (in[i] && par[i] != SN_NONE) {
      deg[i]++;
      deg[par[i]]++;
    }
  }
  /* prune non-terminal leaves (in[] = still in the tree) */
  do {
    pruned = false;
    for (i = 0; i < n; i++) {
      if (in[i] && !isterm[i] && deg[i] <= 1) {
        in[i] = 0;
        pruned = true;
        if (par[i] != SN_NONE && in[par[i]]) {
          deg[par[i]]--;
          deg[i] = 0;
          par[i] = SN_NONE;
        } else {
          /* the leaf is i's child: find it */
          for (j = 0; j < n; j++) {
            if (in[j] && par[j] == i) {
              deg[j]--;
              par[j] = SN_NONE;
            }
          }
          deg[i] = 0;
        }
      }
    }
  } while (pruned);
  for (i = 0; i < n; i++) {
    if (in[i] && par[i] != SN_NONE && in[par[i]]) {
      eu[m] = par[i];
      ev[m++] = i;
      len += w[par[i] * n + i];
    }
  }
  *out_edges = m;
  *out_length = len;
  ds_context_free(context, par);
  ds_context_free(context, key);
  ds_context_free(context, in);
  return DS_OK;
}

static size_t sn_popc(unsigned long x) {
  size_t c = 0;
  while (x != 0) {
    x &= x - 1;
    c++;
  }
  return c;
}

ds_status_t ds_steiner_tree(const double *w, size_t n, const size_t *term,
                            size_t k, ds_steiner_method_t method, size_t *eu,
                            size_t *ev, size_t *out_edges, double *out_length,
                            ds_context_t *context) {
  double *d;
  size_t *nx;
  unsigned char *mark;
  size_t i;
  size_t j;
  ds_status_t st = DS_OK;
  if (w == NULL || term == NULL || out_edges == NULL || out_length == NULL ||
      (k > 1 && (eu == NULL || ev == NULL))) {
    return DS_ERR_NULL;
  }
  *out_edges = 0;
  *out_length = 0.0;
  if (k <= 1 || n == 0) {
    return k == 0 || (k == 1 && term[0] < n) ? DS_OK : DS_ERR_RANGE;
  }
  for (i = 0; i < k; i++) {
    if (term[i] >= n) {
      return DS_ERR_RANGE;
    }
  }
  if (n > ((size_t)-1) / n / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  d = (double *)ds_context_alloc(context, n * n * sizeof(double));
  nx = (size_t *)ds_context_alloc(context, n * n * sizeof(size_t));
  mark = (unsigned char *)ds_context_calloc(context, n * n, 1);
  if (d == NULL || nx == NULL || mark == NULL) {
    ds_context_free(context, d);
    ds_context_free(context, nx);
    ds_context_free(context, mark);
    return DS_ERR_ALLOC;
  }
  sn_floyd(w, n, d, nx);
  for (i = 1; i < k; i++) {
    if (!(d[term[0] * n + term[i]] < HUGE_VAL)) {
      st = DS_NOT_FOUND;
      goto out;
    }
  }
  if (method == DS_STEINER_MST) {
    /* Prim on the terminals' metric closure, expanded into paths */
    size_t *tp = (size_t *)ds_context_alloc(context, k * sizeof(size_t));
    double *tk = (double *)ds_context_alloc(context, k * sizeof(double));
    unsigned char *tin = (unsigned char *)ds_context_calloc(context, k, 1);
    if (tp == NULL || tk == NULL || tin == NULL) {
      ds_context_free(context, tp);
      ds_context_free(context, tk);
      ds_context_free(context, tin);
      st = DS_ERR_ALLOC;
      goto out;
    }
    for (i = 0; i < k; i++) {
      tk[i] = HUGE_VAL;
      tp[i] = SN_NONE;
    }
    tk[0] = 0.0;
    for (i = 0; i < k; i++) {
      size_t v = SN_NONE;
      for (j = 0; j < k; j++) {
        if (!tin[j] && (v == SN_NONE || tk[j] < tk[v])) {
          v = j;
        }
      }
      tin[v] = 1;
      if (tp[v] != SN_NONE) {
        sn_mark_path(nx, n, term[tp[v]], term[v], mark);
      }
      for (j = 0; j < k; j++) {
        double c = d[term[v] * n + term[j]];
        if (!tin[j] && c < tk[j]) {
          tk[j] = c;
          tp[j] = v;
        }
      }
    }
    ds_context_free(context, tp);
    ds_context_free(context, tk);
    ds_context_free(context, tin);
  } else if (method == DS_STEINER_DREYFUS_WAGNER) {
    unsigned long full;
    unsigned long mask;
    unsigned long sub;
    double *dp;
    double *tmp;
    unsigned long *split;
    size_t *from;
    size_t cells;
    size_t best_v = 0;
    size_t *stk;
    size_t top;
    if (k > 16) {
      st = DS_ERR_UNSUPPORTED;
      goto out;
    }
    full = (1UL << k) - 1UL;
    cells = ((size_t)full + 1) * n;
    if (cells > ((size_t)1 << 21)) {
      st = DS_ERR_UNSUPPORTED;
      goto out;
    }
    dp = (double *)ds_context_alloc(context, cells * sizeof(double));
    tmp = (double *)ds_context_alloc(context, n * sizeof(double));
    split = (unsigned long *)ds_context_alloc(context,
                                              cells * sizeof(unsigned long));
    from = (size_t *)ds_context_alloc(context, cells * sizeof(size_t));
    stk = (size_t *)ds_context_alloc(context,
                                     3 * (4 * k + 2 * n + 8) * sizeof(size_t));
    if (dp == NULL || tmp == NULL || split == NULL || from == NULL ||
        stk == NULL) {
      ds_context_free(context, dp);
      ds_context_free(context, tmp);
      ds_context_free(context, split);
      ds_context_free(context, from);
      ds_context_free(context, stk);
      st = DS_ERR_ALLOC;
      goto out;
    }
    for (i = 0; i < cells; i++) {
      dp[i] = HUGE_VAL;
      split[i] = 0;
      from[i] = SN_NONE;
    }
    for (i = 0; i < k; i++) {
      for (j = 0; j < n; j++) {
        dp[(1UL << i) * n + j] = d[term[i] * n + j];
      }
    }
    for (mask = 1; mask <= full; mask++) {
      if (sn_popc(mask) < 2) {
        continue;
      }
      for (j = 0; j < n; j++) {
        double best = HUGE_VAL;
        unsigned long bs = 0;
        for (sub = (mask - 1) & mask; sub > 0; sub = (sub - 1) & mask) {
          if (sub < (mask ^ sub)) {
            continue; /* each split once */
          }
          if (dp[sub * n + j] + dp[(mask ^ sub) * n + j] < best) {
            best = dp[sub * n + j] + dp[(mask ^ sub) * n + j];
            bs = sub;
          }
        }
        dp[mask * n + j] = best;
        split[mask * n + j] = bs;
      }
      /* one relaxation step suffices over a metric closure */
      for (j = 0; j < n; j++) {
        tmp[j] = dp[mask * n + j];
      }
      for (j = 0; j < n; j++) {
        size_t u;
        for (u = 0; u < n; u++) {
          if (u != j && tmp[u] + d[u * n + j] < dp[mask * n + j]) {
            dp[mask * n + j] = tmp[u] + d[u * n + j];
            from[mask * n + j] = u;
          }
        }
      }
    }
    for (j = 1; j < n; j++) {
      if (dp[full * n + j] < dp[full * n + best_v]) {
        best_v = j;
      }
    }
    /* reconstruct: items (mask, vertex, use the relaxation step) */
    top = 0;
    stk[top++] = (size_t)full;
    stk[top++] = best_v;
    stk[top++] = 1;
    while (top > 0) {
      size_t use = stk[--top];
      size_t v = stk[--top];
      unsigned long msk = (unsigned long)stk[--top];
      size_t u = from[msk * n + v];
      if (use && u != SN_NONE) {
        sn_mark_path(nx, n, u, v, mark);
        stk[top++] = (size_t)msk;
        stk[top++] = u;
        stk[top++] = 0;
      } else if (sn_popc(msk) == 1) {
        for (i = 0; !(msk & (1UL << i)); i++) {
        }
        sn_mark_path(nx, n, term[i], v, mark);
      } else {
        unsigned long s = split[msk * n + v];
        stk[top++] = (size_t)s;
        stk[top++] = v;
        stk[top++] = 1;
        stk[top++] = (size_t)(msk ^ s);
        stk[top++] = v;
        stk[top++] = 1;
      }
    }
    ds_context_free(context, dp);
    ds_context_free(context, tmp);
    ds_context_free(context, split);
    ds_context_free(context, from);
    ds_context_free(context, stk);
  } else {
    st = DS_ERR_INVALID;
    goto out;
  }
  st = sn_finish(w, n, mark, term, k, eu, ev, out_edges, out_length, context);
out:
  ds_context_free(context, d);
  ds_context_free(context, nx);
  ds_context_free(context, mark);
  return st;
}

/* ---- rectilinear
 * -------------------------------------------------------------- */

static int sn_cmp_d(const void *a, const void *b) {
  double x = *(const double *)a;
  double y = *(const double *)b;
  return (x > y) - (x < y);
}

static size_t sn_unique(double *v, size_t n) {
  size_t i;
  size_t m = 0;
  qsort(v, n, sizeof(double), sn_cmp_d);
  for (i = 0; i < n; i++) {
    if (m == 0 || v[i] > v[m - 1]) {
      v[m++] = v[i];
    }
  }
  return m;
}

static size_t sn_index(const double *v, size_t n, double x) {
  size_t lo = 0;
  size_t hi = n;
  while (lo + 1 < hi) {
    size_t mid = (lo + hi) / 2;
    if (v[mid] <= x) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return lo;
}

ds_status_t ds_rectilinear_steiner(const double *xy, size_t k, double *out_xy,
                                   size_t *out_points, size_t *eu, size_t *ev,
                                   size_t *out_edges, double *out_length,
                                   ds_context_t *context) {
  double xs[12];
  double ys[12];
  size_t nx;
  size_t ny;
  size_t n;
  size_t i;
  size_t j;
  size_t term[12];
  double *w;
  size_t *te;
  size_t *tv;
  size_t m;
  size_t *id;
  size_t *deg;
  double len;
  size_t pts;
  ds_status_t st;
  if (xy == NULL || out_xy == NULL || out_points == NULL || out_edges == NULL ||
      out_length == NULL || (k > 1 && (eu == NULL || ev == NULL))) {
    return DS_ERR_NULL;
  }
  *out_edges = 0;
  *out_length = 0.0;
  for (i = 0; i < 2 * k; i++) {
    out_xy[i] = xy[i];
  }
  *out_points = k;
  if (k <= 1) {
    return DS_OK;
  }
  if (k > 12) {
    return DS_ERR_UNSUPPORTED;
  }
  for (i = 0; i < k; i++) {
    xs[i] = xy[2 * i];
    ys[i] = xy[2 * i + 1];
  }
  nx = sn_unique(xs, k);
  ny = sn_unique(ys, k);
  n = nx * ny;
  w = (double *)ds_context_alloc(context, n * n * sizeof(double));
  te = (size_t *)ds_context_alloc(context, (4 * n + 4) * sizeof(size_t));
  if (w == NULL || te == NULL) {
    ds_context_free(context, w);
    ds_context_free(context, te);
    return DS_ERR_ALLOC;
  }
  tv = te + n;
  id = tv + n;
  deg = id + n;
  /* the Hanan grid: node (a, b) = a * ny + b */
  for (i = 0; i < n * n; i++) {
    w[i] = HUGE_VAL;
  }
  for (i = 0; i < nx; i++) {
    for (j = 0; j < ny; j++) {
      size_t v = i * ny + j;
      if (i + 1 < nx) {
        w[v * n + v + ny] = w[(v + ny) * n + v] = xs[i + 1] - xs[i];
      }
      if (j + 1 < ny) {
        w[v * n + v + 1] = w[(v + 1) * n + v] = ys[j + 1] - ys[j];
      }
    }
  }
  for (i = 0; i < k; i++) {
    term[i] =
        sn_index(xs, nx, xy[2 * i]) * ny + sn_index(ys, ny, xy[2 * i + 1]);
  }
  st = ds_steiner_tree(w, n, term, k, DS_STEINER_DREYFUS_WAGNER, te, tv, &m,
                       &len, context);
  if (st == DS_OK) {
    /* nodes of the tree: inputs keep their index, others become Steiner
     * points; degree-2 Steiner points are contracted */
    bool changed = true;
    for (i = 0; i < n; i++) {
      id[i] = SN_NONE;
      deg[i] = 0;
    }
    for (i = 0; i < m; i++) {
      deg[te[i]]++;
      deg[tv[i]]++;
    }
    for (i = 0; i < k; i++) {
      deg[term[i]] += n; /* never contracted */
    }
    while (changed) {
      changed = false;
      for (i = 0; i < n; i++) {
        size_t a = SN_NONE;
        size_t b = SN_NONE;
        size_t ea = 0;
        size_t eb = 0;
        if (deg[i] != 2) {
          continue;
        }
        for (j = 0; j < m; j++) {
          if (te[j] == i || tv[j] == i) {
            if (a == SN_NONE) {
              a = te[j] == i ? tv[j] : te[j];
              ea = j;
            } else {
              b = te[j] == i ? tv[j] : te[j];
              eb = j;
            }
          }
        }
        /* join a and b directly; drop the second edge */
        te[ea] = a;
        tv[ea] = b;
        te[eb] = te[m - 1];
        tv[eb] = tv[m - 1];
        m--;
        deg[i] = 0;
        changed = true;
      }
    }
    for (i = 0; i < k; i++) {
      if (id[term[i]] == SN_NONE) {
        id[term[i]] = i;
      }
    }
    pts = k;
    len = 0.0;
    for (i = 0; i < m; i++) {
      size_t ends[2];
      size_t z;
      ends[0] = te[i];
      ends[1] = tv[i];
      for (z = 0; z < 2; z++) {
        if (id[ends[z]] == SN_NONE) {
          id[ends[z]] = pts;
          out_xy[2 * pts] = xs[ends[z] / ny];
          out_xy[2 * pts + 1] = ys[ends[z] % ny];
          pts++;
        }
      }
      eu[i] = id[te[i]];
      ev[i] = id[tv[i]];
      len += fabs(out_xy[2 * eu[i]] - out_xy[2 * ev[i]]) +
             fabs(out_xy[2 * eu[i] + 1] - out_xy[2 * ev[i] + 1]);
    }
    /* duplicate input points hang on their twin with zero length */
    for (i = 0; i < k; i++) {
      if (id[term[i]] != i) {
        eu[m] = id[term[i]];
        ev[m++] = i;
      }
    }
    *out_points = pts;
    *out_edges = m;
    *out_length = len;
  }
  ds_context_free(context, w);
  ds_context_free(context, te);
  return st;
}

/* ---- Euclidean heuristic
 * ------------------------------------------------------- */

static double sn_dist(const double *p, size_t a, size_t b) {
  double dx = p[2 * a] - p[2 * b];
  double dy = p[2 * a + 1] - p[2 * b + 1];
  return sqrt(dx * dx + dy * dy);
}

/* Fermat point of three points by Weiszfeld's iteration. */
static void sn_fermat(const double *p, size_t a, size_t b, size_t c, double *fx,
                      double *fy) {
  double x = (p[2 * a] + p[2 * b] + p[2 * c]) / 3.0;
  double y = (p[2 * a + 1] + p[2 * b + 1] + p[2 * c + 1]) / 3.0;
  size_t it;
  size_t q;
  size_t idx[3];
  idx[0] = a;
  idx[1] = b;
  idx[2] = c;
  for (it = 0; it < 200; it++) {
    double nxs = 0.0;
    double nys = 0.0;
    double den = 0.0;
    for (q = 0; q < 3; q++) {
      double dx = x - p[2 * idx[q]];
      double dy = y - p[2 * idx[q] + 1];
      double dd = sqrt(dx * dx + dy * dy);
      if (dd < 1e-12) {
        *fx = p[2 * idx[q]];
        *fy = p[2 * idx[q] + 1];
        return;
      }
      nxs += p[2 * idx[q]] / dd;
      nys += p[2 * idx[q] + 1] / dd;
      den += 1.0 / dd;
    }
    x = nxs / den;
    y = nys / den;
  }
  *fx = x;
  *fy = y;
}

ds_status_t ds_euclidean_steiner(const double *xy, size_t k, double *out_xy,
                                 size_t *out_points, size_t *eu, size_t *ev,
                                 size_t *out_edges, double *out_length,
                                 ds_context_t *context) {
  double *key;
  size_t *par;
  unsigned char *in;
  size_t pts = k;
  size_t m = 0;
  size_t i;
  size_t j;
  size_t v;
  double len = 0.0;
  if (xy == NULL || out_xy == NULL || out_points == NULL || out_edges == NULL ||
      out_length == NULL || (k > 1 && (eu == NULL || ev == NULL))) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < 2 * k; i++) {
    out_xy[i] = xy[i];
  }
  *out_points = k;
  *out_edges = 0;
  *out_length = 0.0;
  if (k <= 1) {
    return DS_OK;
  }
  key = (double *)ds_context_alloc(context, k * sizeof(double));
  par = (size_t *)ds_context_alloc(context, k * sizeof(size_t));
  in = (unsigned char *)ds_context_calloc(context, k, 1);
  if (key == NULL || par == NULL || in == NULL) {
    ds_context_free(context, key);
    ds_context_free(context, par);
    ds_context_free(context, in);
    return DS_ERR_ALLOC;
  }
  /* Euclidean MST (Prim) */
  for (i = 0; i < k; i++) {
    key[i] = HUGE_VAL;
    par[i] = SN_NONE;
  }
  key[0] = 0.0;
  for (i = 0; i < k; i++) {
    v = SN_NONE;
    for (j = 0; j < k; j++) {
      if (!in[j] && (v == SN_NONE || key[j] < key[v])) {
        v = j;
      }
    }
    in[v] = 1;
    if (par[v] != SN_NONE) {
      eu[m] = par[v];
      ev[m++] = v;
    }
    for (j = 0; j < k; j++) {
      if (!in[j] && sn_dist(out_xy, v, j) < key[j]) {
        key[j] = sn_dist(out_xy, v, j);
        par[j] = v;
      }
    }
  }
  ds_context_free(context, key);
  ds_context_free(context, par);
  ds_context_free(context, in);
  /* insert Fermat points while some pair of edges meeting at a vertex
   * gains (angle below 120 degrees) */
  while (pts < 2 * k - 2) {
    double best_gain = 1e-9;
    size_t be1 = SN_NONE;
    size_t be2 = SN_NONE;
    size_t bv = SN_NONE;
    double bx = 0.0;
    double by = 0.0;
    size_t e1;
    size_t e2;
    for (e1 = 0; e1 < m; e1++) {
      for (e2 = e1 + 1; e2 < m; e2++) {
        size_t c;
        size_t a;
        size_t b;
        double fx;
        double fy;
        double gain;
        if (eu[e1] == eu[e2] || eu[e1] == ev[e2]) {
          c = eu[e1];
        } else if (ev[e1] == eu[e2] || ev[e1] == ev[e2]) {
          c = ev[e1];
        } else {
          continue;
        }
        a = eu[e1] == c ? ev[e1] : eu[e1];
        b = eu[e2] == c ? ev[e2] : eu[e2];
        sn_fermat(out_xy, c, a, b, &fx, &fy);
        out_xy[2 * pts] = fx;
        out_xy[2 * pts + 1] = fy;
        gain = sn_dist(out_xy, c, a) + sn_dist(out_xy, c, b) -
               sn_dist(out_xy, pts, a) - sn_dist(out_xy, pts, b) -
               sn_dist(out_xy, pts, c);
        if (gain > best_gain) {
          best_gain = gain;
          be1 = e1;
          be2 = e2;
          bv = c;
          bx = fx;
          by = fy;
        }
      }
    }
    if (be1 == SN_NONE) {
      break;
    }
    /* edges (c,a), (c,b) become (s,a), (s,b) plus (s,c) */
    out_xy[2 * pts] = bx;
    out_xy[2 * pts + 1] = by;
    if (eu[be1] == bv) {
      eu[be1] = pts;
    } else {
      ev[be1] = pts;
    }
    if (eu[be2] == bv) {
      eu[be2] = pts;
    } else {
      ev[be2] = pts;
    }
    eu[m] = pts;
    ev[m++] = bv;
    pts++;
  }
  for (i = 0; i < m; i++) {
    len += sn_dist(out_xy, eu[i], ev[i]);
  }
  *out_points = pts;
  *out_edges = m;
  *out_length = len;
  return DS_OK;
}
