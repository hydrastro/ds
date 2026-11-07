#include "tsp.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define TS_NONE (~(size_t)0)

double ds_tsp_tour_length(const double *dist, size_t n, const size_t *tour) {
  size_t i;
  double len = 0.0;
  if (dist == NULL || tour == NULL || n < 2) {
    return 0.0;
  }
  for (i = 0; i < n; i++) {
    len += dist[tour[i] * n + tour[(i + 1) % n]];
  }
  return len;
}

static ds_status_t ts_held_karp(const double *d, size_t n, size_t *tour,
                                ds_context_t *context) {
  size_t m = n - 1; /* vertices 1..n-1 in the masks */
  size_t full;
  size_t mask;
  size_t j;
  size_t k;
  size_t last;
  double *dp;
  unsigned char *par;
  double best;
  double v;
  size_t pos;
  if (n > 18) {
    return DS_ERR_UNSUPPORTED;
  }
  full = ((size_t)1 << m) - 1;
  dp = (double *)ds_context_alloc(context, (full + 1) * m * sizeof(double));
  par = (unsigned char *)ds_context_alloc(context, (full + 1) * m);
  if (dp == NULL || par == NULL) {
    ds_context_free(context, dp);
    ds_context_free(context, par);
    return DS_ERR_ALLOC;
  }
  for (mask = 1; mask <= full; mask++) {
    for (j = 0; j < m; j++) {
      if (!(mask & ((size_t)1 << j))) {
        continue;
      }
      if (mask == ((size_t)1 << j)) {
        dp[mask * m + j] = d[0 * n + j + 1];
        par[mask * m + j] = (unsigned char)255;
        continue;
      }
      best = HUGE_VAL;
      par[mask * m + j] = 0;
      for (k = 0; k < m; k++) {
        if (k == j || !(mask & ((size_t)1 << k))) {
          continue;
        }
        v = dp[(mask & ~((size_t)1 << j)) * m + k] + d[(k + 1) * n + j + 1];
        if (v < best) {
          best = v;
          par[mask * m + j] = (unsigned char)k;
        }
      }
      dp[mask * m + j] = best;
    }
  }
  best = HUGE_VAL;
  last = 0;
  for (j = 0; j < m; j++) {
    v = dp[full * m + j] + d[(j + 1) * n + 0];
    if (v < best) {
      best = v;
      last = j;
    }
  }
  tour[0] = 0;
  mask = full;
  for (pos = n - 1; pos >= 1; pos--) {
    tour[pos] = last + 1;
    k = par[mask * m + last];
    mask &= ~((size_t)1 << last);
    last = k;
  }
  ds_context_free(context, dp);
  ds_context_free(context, par);
  return DS_OK;
}

static void ts_nearest(const double *d, size_t n, size_t *tour,
                       unsigned char *used) {
  size_t i;
  size_t j;
  size_t best;
  memset(used, 0, n);
  tour[0] = 0;
  used[0] = 1;
  for (i = 1; i < n; i++) {
    best = TS_NONE;
    for (j = 0; j < n; j++) {
      if (!used[j] && (best == TS_NONE ||
                       d[tour[i - 1] * n + j] < d[tour[i - 1] * n + best])) {
        best = j;
      }
    }
    tour[i] = best;
    used[best] = 1;
  }
}

/* 2-opt until no improving move (symmetric distances). */
static void ts_two_opt(const double *d, size_t n, size_t *tour) {
  bool improved = true;
  size_t i;
  size_t j;
  size_t a;
  size_t b;
  size_t c;
  size_t e;
  size_t t;
  double delta;
  if (n < 4) {
    return;
  }
  while (improved) {
    improved = false;
    for (i = 0; i + 2 < n; i++) {
      for (j = i + 2; j < n; j++) {
        if (i == 0 && j == n - 1) {
          continue;
        }
        a = tour[i];
        b = tour[i + 1];
        c = tour[j];
        e = tour[(j + 1) % n];
        delta = d[a * n + c] + d[b * n + e] - d[a * n + b] - d[c * n + e];
        if (delta < -1e-12) {
          /* reverse tour[i + 1 .. j] */
          a = i + 1;
          b = j;
          while (a < b) {
            t = tour[a];
            tour[a] = tour[b];
            tour[b] = t;
            a++;
            b--;
          }
          improved = true;
        }
      }
    }
  }
}

/* Minimum weight perfect matching of the k (even) vertices in odd[]:
 * exact by DP over subsets for k <= 20, greedy otherwise. mate is indexed
 * by position in odd[]. */
static ds_status_t ts_matching(const double *d, size_t n, const size_t *odd,
                               size_t k, size_t *mate, ds_context_t *context) {
  size_t i;
  size_t j;
  if (k <= 20) {
    size_t full = ((size_t)1 << k) - 1;
    size_t mask;
    double *dp =
        (double *)ds_context_alloc(context, (full + 1) * sizeof(double));
    unsigned char *pj = (unsigned char *)ds_context_alloc(context, full + 1);
    if (dp == NULL || pj == NULL) {
      ds_context_free(context, dp);
      ds_context_free(context, pj);
      return DS_ERR_ALLOC;
    }
    dp[0] = 0.0;
    for (mask = 1; mask <= full; mask++) {
      size_t popc = 0;
      size_t x;
      dp[mask] = HUGE_VAL;
      for (x = mask; x != 0; x &= x - 1) {
        popc++;
      }
      if (popc % 2 != 0) {
        continue;
      }
      /* pair the lowest vertex with every other */
      for (i = 0; !(mask & ((size_t)1 << i)); i++) {
      }
      for (j = i + 1; j < k; j++) {
        if (mask & ((size_t)1 << j)) {
          double v = dp[mask & ~((size_t)1 << i) & ~((size_t)1 << j)] +
                     d[odd[i] * n + odd[j]];
          if (v < dp[mask]) {
            dp[mask] = v;
            pj[mask] = (unsigned char)j;
          }
        }
      }
    }
    mask = full;
    while (mask != 0) {
      for (i = 0; !(mask & ((size_t)1 << i)); i++) {
      }
      j = pj[mask];
      mate[i] = j;
      mate[j] = i;
      mask &= ~((size_t)1 << i) & ~((size_t)1 << j);
    }
    ds_context_free(context, dp);
    ds_context_free(context, pj);
    return DS_OK;
  }
  /* greedy: repeatedly the closest unmatched pair */
  for (i = 0; i < k; i++) {
    mate[i] = TS_NONE;
  }
  for (;;) {
    size_t bi = TS_NONE;
    size_t bj = TS_NONE;
    for (i = 0; i < k; i++) {
      if (mate[i] != TS_NONE) {
        continue;
      }
      for (j = i + 1; j < k; j++) {
        if (mate[j] == TS_NONE &&
            (bi == TS_NONE ||
             d[odd[i] * n + odd[j]] < d[odd[bi] * n + odd[bj]])) {
          bi = i;
          bj = j;
        }
      }
    }
    if (bi == TS_NONE) {
      break;
    }
    mate[bi] = bj;
    mate[bj] = bi;
  }
  return DS_OK;
}

static ds_status_t ts_christofides(const double *d, size_t n, size_t *tour,
                                   ds_context_t *context) {
  size_t *par;
  double *key;
  unsigned char *in;
  size_t *deg;
  size_t *odd;
  size_t *mate;
  size_t *eu; /* multigraph edges */
  size_t *ev;
  unsigned char *eused;
  size_t *adjoff;
  size_t *adje;
  size_t *stack;
  size_t *ptr;
  size_t m = 0;
  size_t k = 0;
  size_t i;
  size_t j;
  size_t v;
  size_t top;
  size_t cnt;
  ds_status_t st;
  par = (size_t *)ds_context_alloc(context, (16 * n + 1) * sizeof(size_t));
  key = (double *)ds_context_alloc(context, n * sizeof(double));
  in = (unsigned char *)ds_context_alloc(context, 2 * n * 2 + n);
  if (par == NULL || key == NULL || in == NULL) {
    ds_context_free(context, par);
    ds_context_free(context, key);
    ds_context_free(context, in);
    return DS_ERR_ALLOC;
  }
  deg = par + n;
  odd = deg + n;
  mate = odd + n;
  eu = mate + n; /* up to 2n edges */
  ev = eu + 2 * n;
  adjoff = ev + 2 * n;   /* n + 1 */
  adje = adjoff + n + 1; /* 4n incidences */
  stack = adje + 4 * n;  /* path stack */
  ptr = stack + 2 * n;   /* n: next incidence per vertex; ends at 16n+1 */
  eused = in + n;
  /* Prim's MST */
  for (i = 0; i < n; i++) {
    key[i] = HUGE_VAL;
    par[i] = TS_NONE;
    in[i] = 0;
    deg[i] = 0;
  }
  key[0] = 0.0;
  for (i = 0; i < n; i++) {
    v = TS_NONE;
    for (j = 0; j < n; j++) {
      if (!in[j] && (v == TS_NONE || key[j] < key[v])) {
        v = j;
      }
    }
    in[v] = 1;
    if (par[v] != TS_NONE) {
      eu[m] = par[v];
      ev[m++] = v;
      deg[v]++;
      deg[par[v]]++;
    }
    for (j = 0; j < n; j++) {
      if (!in[j] && d[v * n + j] < key[j]) {
        key[j] = d[v * n + j];
        par[j] = v;
      }
    }
  }
  for (i = 0; i < n; i++) {
    if (deg[i] % 2 != 0) {
      odd[k++] = i;
    }
  }
  st = ts_matching(d, n, odd, k, mate, context);
  if (st != DS_OK) {
    goto out;
  }
  for (i = 0; i < k; i++) {
    if (mate[i] > i) {
      eu[m] = odd[i];
      ev[m++] = odd[mate[i]];
    }
  }
  /* Euler tour (Hierholzer, iterative) and shortcutting */
  for (i = 0; i <= n; i++) {
    adjoff[i] = 0;
  }
  for (i = 0; i < m; i++) {
    adjoff[eu[i] + 1]++;
    adjoff[ev[i] + 1]++;
  }
  for (i = 0; i < n; i++) {
    adjoff[i + 1] += adjoff[i];
    ptr[i] = adjoff[i];
  }
  for (i = 0; i < m; i++) {
    adje[ptr[eu[i]]++] = i;
    adje[ptr[ev[i]]++] = i;
    eused[i] = 0;
  }
  for (i = 0; i < n; i++) {
    ptr[i] = adjoff[i];
    in[i] = 0; /* visited in the shortcut tour */
  }
  top = 0;
  cnt = 0;
  stack[top++] = 0;
  while (top > 0) {
    v = stack[top - 1];
    while (ptr[v] < adjoff[v + 1] && eused[adje[ptr[v]]]) {
      ptr[v]++;
    }
    if (ptr[v] < adjoff[v + 1]) {
      size_t e = adje[ptr[v]++];
      eused[e] = 1;
      stack[top++] = eu[e] == v ? ev[e] : eu[e];
    } else {
      top--;
      if (!in[v]) {
        in[v] = 1;
        tour[cnt++] = v;
      }
    }
  }
  /* rotate so that the tour starts at vertex 0 (three reversals) */
  {
    size_t z;
    size_t a;
    size_t b;
    size_t t;
    for (z = 0; z < cnt && tour[z] != 0; z++) {
    }
    for (i = 0; i < 3 && z != 0 && z < cnt; i++) {
      a = i == 0 ? 0 : (i == 1 ? z : 0);
      b = i == 0 ? z - 1 : cnt - 1;
      while (a < b) {
        t = tour[a];
        tour[a] = tour[b];
        tour[b] = t;
        a++;
        b--;
      }
    }
  }
out:
  ds_context_free(context, par);
  ds_context_free(context, key);
  ds_context_free(context, in);
  return st;
}

ds_status_t ds_tsp(const double *dist, size_t n, ds_tsp_method_t method,
                   size_t *tour, double *out_length, ds_context_t *context) {
  unsigned char *used;
  ds_status_t st = DS_OK;
  if ((n > 0 && (dist == NULL || tour == NULL)) || out_length == NULL) {
    return DS_ERR_NULL;
  }
  *out_length = 0.0;
  if (n == 0) {
    return DS_OK;
  }
  if (n > ((size_t)-1) / (16 * sizeof(size_t))) {
    return DS_ERR_OVERFLOW;
  }
  if (n <= 2) {
    tour[0] = 0;
    if (n == 2) {
      tour[1] = 1;
    }
    *out_length = ds_tsp_tour_length(dist, n, tour);
    return DS_OK;
  }
  switch (method) {
  case DS_TSP_HELD_KARP:
    st = ts_held_karp(dist, n, tour, context);
    break;
  case DS_TSP_NEAREST_NEIGHBOR:
  case DS_TSP_TWO_OPT:
    used = (unsigned char *)ds_context_alloc(context, n);
    if (used == NULL) {
      return DS_ERR_ALLOC;
    }
    ts_nearest(dist, n, tour, used);
    ds_context_free(context, used);
    if (method == DS_TSP_TWO_OPT) {
      ts_two_opt(dist, n, tour);
    }
    break;
  case DS_TSP_CHRISTOFIDES:
  case DS_TSP_CHRISTOFIDES_TWO_OPT:
    st = ts_christofides(dist, n, tour, context);
    if (st == DS_OK && method == DS_TSP_CHRISTOFIDES_TWO_OPT) {
      ts_two_opt(dist, n, tour);
    }
    break;
  default:
    return DS_ERR_INVALID;
  }
  if (st == DS_OK) {
    *out_length = ds_tsp_tour_length(dist, n, tour);
  }
  return st;
}

ds_status_t ds_tsp_euclidean(const double *xy, size_t n, ds_tsp_method_t method,
                             size_t *tour, double *out_length,
                             ds_context_t *context) {
  double *d;
  size_t i;
  size_t j;
  ds_status_t st;
  if ((n > 0 && (xy == NULL || tour == NULL)) || out_length == NULL) {
    return DS_ERR_NULL;
  }
  if (n > 0 && n > ((size_t)-1) / n / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  d = (double *)ds_context_alloc(context, (n > 0 ? n * n : 1) * sizeof(double));
  if (d == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    for (j = 0; j < n; j++) {
      double dx = xy[2 * i] - xy[2 * j];
      double dy = xy[2 * i + 1] - xy[2 * j + 1];
      d[i * n + j] = sqrt(dx * dx + dy * dy);
    }
  }
  st = ds_tsp(d, n, method, tour, out_length, context);
  ds_context_free(context, d);
  return st;
}

static int ts_cmp_double(const void *a, const void *b) {
  double x = *(const double *)a;
  double y = *(const double *)b;
  return (x > y) - (x < y);
}

/* Hamiltonian cycle using only edges <= t (DP over subsets). */
static bool ts_ham_le(const double *d, size_t n, double t, size_t *tour,
                      unsigned long *dp) {
  unsigned long full = (1UL << n) - 1UL;
  unsigned long mask;
  unsigned long ends;
  size_t u;
  size_t v;
  size_t i;
  size_t pos;
  memset(dp, 0, ((size_t)full + 1) * sizeof(unsigned long));
  dp[1] = 1UL;
  for (mask = 1; mask <= full; mask += 2) {
    if (dp[mask] == 0) {
      continue;
    }
    for (u = 0; u < n; u++) {
      if (!(dp[mask] & (1UL << u))) {
        continue;
      }
      for (v = 1; v < n; v++) {
        if (!(mask & (1UL << v)) && d[u * n + v] <= t) {
          dp[mask | (1UL << v)] |= 1UL << v;
        }
      }
    }
  }
  ends = 0;
  for (v = 0; v < n; v++) {
    if ((dp[full] & (1UL << v)) && d[v * n] <= t) {
      ends |= 1UL << v;
    }
  }
  if (ends == 0) {
    return false;
  }
  if (tour != NULL) {
    for (v = 0; !(ends & (1UL << v)); v++) {
    }
    mask = full;
    for (pos = n; pos-- > 1;) {
      tour[pos] = v;
      mask &= ~(1UL << v);
      for (i = 0; i < n; i++) {
        if ((dp[mask] & (1UL << i)) && d[i * n + v] <= t) {
          break;
        }
      }
      v = i;
    }
    tour[0] = 0;
  }
  return true;
}

ds_status_t ds_tsp_bottleneck(const double *dist, size_t n, size_t *tour,
                              double *out_max_edge, ds_context_t *context) {
  double *w;
  unsigned long *dp;
  size_t m = 0;
  size_t i;
  size_t j;
  size_t lo;
  size_t hi;
  size_t mid;
  if ((n > 0 && (dist == NULL || tour == NULL)) || out_max_edge == NULL) {
    return DS_ERR_NULL;
  }
  *out_max_edge = 0.0;
  if (n <= 1) {
    if (n == 1) {
      tour[0] = 0;
    }
    return DS_OK;
  }
  if (n > 20) {
    return DS_ERR_UNSUPPORTED;
  }
  w = (double *)ds_context_alloc(context, n * n * sizeof(double));
  dp = (unsigned long *)ds_context_alloc(context, ((size_t)1 << n) *
                                                      sizeof(unsigned long));
  if (w == NULL || dp == NULL) {
    ds_context_free(context, w);
    ds_context_free(context, dp);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    for (j = 0; j < n; j++) {
      if (i != j) {
        w[m++] = dist[i * n + j];
      }
    }
  }
  qsort(w, m, sizeof(double), ts_cmp_double);
  lo = 0;
  hi = m - 1;
  if (!ts_ham_le(dist, n, w[hi], NULL, dp)) {
    ds_context_free(context, w);
    ds_context_free(context, dp);
    return DS_NOT_FOUND;
  }
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    if (ts_ham_le(dist, n, w[mid], NULL, dp)) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  (void)ts_ham_le(dist, n, w[lo], tour, dp);
  *out_max_edge = w[lo];
  ds_context_free(context, w);
  ds_context_free(context, dp);
  return DS_OK;
}

typedef struct ts_saving {
  double s;
  size_t i;
  size_t j;
} ts_saving_t;

static int ts_cmp_saving(const void *a, const void *b) {
  double x = ((const ts_saving_t *)a)->s;
  double y = ((const ts_saving_t *)b)->s;
  return (x < y) - (x > y); /* descending */
}

ds_status_t ds_vrp_savings(const double *dist, size_t n, const double *demand,
                           double capacity, size_t *order, size_t *route_start,
                           size_t *out_routes, double *out_length,
                           ds_context_t *context) {
  ts_saving_t *sv;
  size_t *route; /* route id per customer */
  size_t *head;  /* per route: first and last customer, load */
  size_t *tail;
  double *load;
  size_t *nxt;
  size_t *prv;
  size_t ns = 0;
  size_t i;
  size_t j;
  size_t r;
  size_t routes = 0;
  size_t pos = 0;
  double len = 0.0;
  if ((n > 0 && (dist == NULL || demand == NULL)) || out_routes == NULL ||
      out_length == NULL || (n > 1 && (order == NULL || route_start == NULL))) {
    return DS_ERR_NULL;
  }
  *out_routes = 0;
  *out_length = 0.0;
  if (n <= 1) {
    if (route_start != NULL) {
      route_start[0] = 0;
    }
    return DS_OK;
  }
  for (i = 1; i < n; i++) {
    if (demand[i] > capacity || demand[i] < 0.0) {
      return DS_ERR_INVALID;
    }
  }
  if (n > ((size_t)-1) / n / sizeof(ts_saving_t)) {
    return DS_ERR_OVERFLOW;
  }
  sv = (ts_saving_t *)ds_context_alloc(context, n * n * sizeof(ts_saving_t));
  route = (size_t *)ds_context_alloc(context, 5 * n * sizeof(size_t));
  load = (double *)ds_context_alloc(context, n * sizeof(double));
  if (sv == NULL || route == NULL || load == NULL) {
    ds_context_free(context, sv);
    ds_context_free(context, route);
    ds_context_free(context, load);
    return DS_ERR_ALLOC;
  }
  head = route + n;
  tail = head + n;
  nxt = tail + n;
  prv = nxt + n;
  /* one route per customer to start with */
  for (i = 1; i < n; i++) {
    route[i] = i;
    head[i] = tail[i] = i;
    load[i] = demand[i];
    nxt[i] = prv[i] = TS_NONE;
  }
  for (i = 1; i < n; i++) {
    for (j = i + 1; j < n; j++) {
      sv[ns].s = dist[i * n] + dist[j] - dist[i * n + j];
      sv[ns].i = i;
      sv[ns++].j = j;
    }
  }
  qsort(sv, ns, sizeof(ts_saving_t), ts_cmp_saving);
  for (r = 0; r < ns; r++) {
    size_t a = sv[r].i;
    size_t b = sv[r].j;
    size_t ra = route[a];
    size_t rb = route[b];
    size_t x;
    if (sv[r].s <= 0.0 || ra == rb || load[ra] + load[rb] > capacity) {
      continue;
    }
    /* a and b must be route ends; orient so that a is a tail and b a
     * head, reversing a route when needed */
    if (tail[ra] != a && head[ra] == a) {
      /* reverse route ra */
      size_t v = head[ra];
      while (v != TS_NONE) {
        size_t t2 = nxt[v];
        nxt[v] = prv[v];
        prv[v] = t2;
        v = t2;
      }
      x = head[ra];
      head[ra] = tail[ra];
      tail[ra] = x;
    }
    if (head[rb] != b && tail[rb] == b) {
      size_t v = head[rb];
      while (v != TS_NONE) {
        size_t t2 = nxt[v];
        nxt[v] = prv[v];
        prv[v] = t2;
        v = t2;
      }
      x = head[rb];
      head[rb] = tail[rb];
      tail[rb] = x;
    }
    if (tail[ra] != a || head[rb] != b) {
      continue; /* an interior customer */
    }
    nxt[a] = b;
    prv[b] = a;
    tail[ra] = tail[rb];
    load[ra] += load[rb];
    for (x = b; x != TS_NONE; x = nxt[x]) {
      route[x] = ra;
    }
  }
  /* every route starts at the one customer without a predecessor */
  route_start[0] = 0;
  for (i = 1; i < n; i++) {
    size_t v;
    if (prv[i] != TS_NONE) {
      continue;
    }
    v = i;
    len += dist[v];
    while (v != TS_NONE) {
      order[pos++] = v;
      len += nxt[v] != TS_NONE ? dist[v * n + nxt[v]] : dist[v * n];
      v = nxt[v];
    }
    route_start[++routes] = pos;
  }
  ds_context_free(context, sv);
  ds_context_free(context, route);
  ds_context_free(context, load);
  *out_routes = routes;
  *out_length = len;
  return DS_OK;
}
