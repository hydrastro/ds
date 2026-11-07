#include "facility.h"
#include "flow.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FC_NONE (~(size_t)0)

static double fc_w(const double *weight, size_t i) {
  return weight == NULL ? 1.0 : weight[i];
}

static ds_status_t fc_check_sets(size_t universe, size_t count,
                                 const size_t *off, const size_t *elems) {
  size_t i;
  size_t k;
  if (count > 0 && (off == NULL || (off[count] > 0 && elems == NULL))) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < count; i++) {
    if (off[i] > off[i + 1]) {
      return DS_ERR_INVALID;
    }
    for (k = off[i]; k < off[i + 1]; k++) {
      if (elems[k] >= universe) {
        return DS_ERR_RANGE;
      }
    }
  }
  return DS_OK;
}

/* ---- set cover --------------------------------------------------------------
 */

ds_status_t ds_set_cover(size_t universe, size_t count, const size_t *off,
                         const size_t *elems, const double *weight,
                         ds_cover_method_t method, size_t budget,
                         unsigned char *chosen, double *out_cost,
                         ds_context_t *context) {
  size_t *cnt;  /* times each element is covered */
  size_t *eoff; /* element -> sets (CSR) */
  size_t *esets;
  size_t *fe; /* stack: element per frame */
  size_t *fp; /* next candidate position per frame */
  size_t *fs; /* set chosen at the frame or FC_NONE */
  unsigned char *cur;
  size_t i;
  size_t k;
  size_t e;
  size_t total;
  double cost = 0.0;
  double best = HUGE_VAL;
  ds_status_t st;
  if (out_cost == NULL || (count > 0 && chosen == NULL)) {
    return DS_ERR_NULL;
  }
  st = fc_check_sets(universe, count, off, elems);
  if (st != DS_OK) {
    return st;
  }
  total = count > 0 ? off[count] : 0;
  if (universe > ((size_t)-1) / (6 * sizeof(size_t)) - 1 ||
      total > ((size_t)-1) / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  cnt = (size_t *)ds_context_calloc(context, 5 * universe + 2 + total,
                                    sizeof(size_t));
  cur = (unsigned char *)ds_context_calloc(context, count + 1, 1);
  if (cnt == NULL || cur == NULL) {
    ds_context_free(context, cnt);
    ds_context_free(context, cur);
    return DS_ERR_ALLOC;
  }
  eoff = cnt + universe;
  fe = eoff + universe + 1;
  fp = fe + universe;
  fs = fp + universe;
  esets = fs + universe;
  for (i = 0; i < count; i++) {
    for (k = off[i]; k < off[i + 1]; k++) {
      eoff[elems[k] + 1]++;
    }
  }
  for (e = 0; e < universe; e++) {
    if (eoff[e + 1] == 0) {
      ds_context_free(context, cnt);
      ds_context_free(context, cur);
      return DS_NOT_FOUND; /* an element no set covers */
    }
    eoff[e + 1] += eoff[e];
    cnt[e] = eoff[e];
  }
  for (i = 0; i < count; i++) {
    for (k = off[i]; k < off[i + 1]; k++) {
      esets[cnt[elems[k]]++] = i;
    }
  }
  memset(cnt, 0, universe * sizeof(size_t));
  memset(chosen, 0, count);
  if (method == DS_COVER_GREEDY || method == DS_COVER_EXACT) {
    /* greedy: also the starting bound for the exact search */
    size_t left = universe;
    while (left > 0) {
      size_t bs = FC_NONE;
      double br = HUGE_VAL;
      for (i = 0; i < count; i++) {
        size_t fresh = 0;
        if (cur[i]) {
          continue;
        }
        for (k = off[i]; k < off[i + 1]; k++) {
          fresh += cnt[elems[k]] == 0;
        }
        if (fresh > 0 && fc_w(weight, i) / (double)fresh < br) {
          br = fc_w(weight, i) / (double)fresh;
          bs = i;
        }
      }
      cur[bs] = 1;
      cost += fc_w(weight, bs);
      for (k = off[bs]; k < off[bs + 1]; k++) {
        if (cnt[elems[k]]++ == 0) {
          left--;
        }
      }
    }
    memcpy(chosen, cur, count);
    best = cost;
  }
  if (method == DS_COVER_EXACT) {
    size_t top = 0;
    memset(cur, 0, count);
    memset(cnt, 0, universe * sizeof(size_t));
    cost = 0.0;
    st = DS_OK;
    /* frames: the first uncovered element and the set covering it */
    for (;;) {
      /* open a frame for the first uncovered element, if any */
      for (e = 0; e < universe && cnt[e] > 0; e++) {
      }
      if (e == universe) {
        if (cost < best) {
          best = cost;
          memcpy(chosen, cur, count);
        }
      } else {
        fe[top] = e;
        fp[top] = eoff[e];
        fs[top] = FC_NONE;
        top++;
      }
      /* advance the deepest frame to its next affordable candidate */
      for (;;) {
        size_t s;
        if (top == 0) {
          goto exact_done;
        }
        if (fs[top - 1] != FC_NONE) {
          s = fs[top - 1];
          cur[s] = 0;
          cost -= fc_w(weight, s);
          for (k = off[s]; k < off[s + 1]; k++) {
            cnt[elems[k]]--;
          }
          fs[top - 1] = FC_NONE;
        }
        e = fe[top - 1];
        while (fp[top - 1] < eoff[e + 1] &&
               cost + fc_w(weight, esets[fp[top - 1]]) >= best) {
          fp[top - 1]++;
        }
        if (fp[top - 1] == eoff[e + 1]) {
          top--;
          continue;
        }
        if (budget != 0 && --budget == 0) {
          st = DS_STOP;
          goto exact_done;
        }
        s = esets[fp[top - 1]++];
        fs[top - 1] = s;
        cur[s] = 1;
        cost += fc_w(weight, s);
        for (k = off[s]; k < off[s + 1]; k++) {
          cnt[elems[k]]++;
        }
        break;
      }
    }
  exact_done:;
  } else if (method != DS_COVER_GREEDY) {
    st = DS_ERR_INVALID;
  }
  ds_context_free(context, cnt);
  ds_context_free(context, cur);
  *out_cost = best;
  return st;
}

/* ---- set packing ------------------------------------------------------------
 */

ds_status_t ds_set_packing(size_t universe, size_t count, const size_t *off,
                           const size_t *elems, const double *weight,
                           ds_cover_method_t method, size_t budget,
                           unsigned char *chosen, double *out_weight,
                           ds_context_t *context) {
  unsigned char *used;
  unsigned char *cur;
  size_t *order;
  double *suffix;
  size_t i;
  size_t k;
  size_t s;
  size_t depth;
  double w = 0.0;
  double best = 0.0;
  bool ok;
  ds_status_t st;
  if (out_weight == NULL || (count > 0 && chosen == NULL)) {
    return DS_ERR_NULL;
  }
  st = fc_check_sets(universe, count, off, elems);
  if (st != DS_OK) {
    return st;
  }
  used = (unsigned char *)ds_context_calloc(context, universe + 1, 1);
  cur = (unsigned char *)ds_context_calloc(context, 2 * count + 2, 1);
  order = (size_t *)ds_context_alloc(context, (count + 1) * sizeof(size_t));
  suffix = (double *)ds_context_alloc(context, (count + 1) * sizeof(double));
  if (used == NULL || cur == NULL || order == NULL || suffix == NULL) {
    ds_context_free(context, used);
    ds_context_free(context, cur);
    ds_context_free(context, order);
    ds_context_free(context, suffix);
    return DS_ERR_ALLOC;
  }
  memset(chosen, 0, count);
  /* greedy by weight per element (also the exact search's first bound) */
  for (i = 0; i < count; i++) {
    order[i] = i;
  }
  for (i = 1; i < count; i++) {
    size_t x = order[i];
    double rx = fc_w(weight, x) / (double)(off[x + 1] - off[x] + 1);
    for (k = i; k > 0; k--) {
      size_t y = order[k - 1];
      double ry = fc_w(weight, y) / (double)(off[y + 1] - off[y] + 1);
      if (ry >= rx) {
        break;
      }
      order[k] = y;
    }
    order[k] = x;
  }
  for (i = 0; i < count; i++) {
    s = order[i];
    ok = fc_w(weight, s) > 0.0;
    for (k = off[s]; k < off[s + 1] && ok; k++) {
      ok = !used[elems[k]];
    }
    if (ok) {
      for (k = off[s]; k < off[s + 1]; k++) {
        used[elems[k]] = 1;
      }
      chosen[s] = 1;
      best += fc_w(weight, s);
    }
  }
  if (method == DS_COVER_EXACT) {
    unsigned char *dec = cur + count + 1; /* 0 untried, 1 in, 2 out */
    /* heaviest sets first (insertion sort: the search is exponential
     * anyway) */
    for (i = 0; i < count; i++) {
      size_t x = i;
      for (k = i; k > 0 && fc_w(weight, order[k - 1]) < fc_w(weight, x); k--) {
        order[k] = order[k - 1];
      }
      order[k] = x;
    }
    suffix[count] = 0.0;
    for (i = count; i-- > 0;) {
      double wi = fc_w(weight, order[i]);
      suffix[i] = suffix[i + 1] + (wi > 0.0 ? wi : 0.0);
    }
    memset(used, 0, universe);
    memset(cur, 0, count);
    memset(dec, 0, count);
    depth = 0;
    for (;;) {
      if (depth == count || w + suffix[depth] <= best) {
        if (depth == count && w > best) {
          best = w;
          memset(chosen, 0, count);
          for (i = 0; i < count; i++) {
            chosen[order[i]] = cur[i];
          }
        }
        /* backtrack to the deepest decision that can still go "out" */
        for (;;) {
          if (depth == 0) {
            goto pack_done;
          }
          depth--;
          if (dec[depth] == 1) {
            s = order[depth];
            for (k = off[s]; k < off[s + 1]; k++) {
              used[elems[k]] = 0;
            }
            w -= fc_w(weight, s);
            cur[depth] = 0;
            dec[depth] = 2;
            depth++;
            break;
          }
          dec[depth] = 0;
        }
        continue;
      }
      if (budget != 0 && --budget == 0) {
        st = DS_STOP;
        break;
      }
      s = order[depth];
      ok = fc_w(weight, s) > 0.0;
      for (k = off[s]; k < off[s + 1] && ok; k++) {
        ok = !used[elems[k]];
      }
      if (ok) {
        for (k = off[s]; k < off[s + 1]; k++) {
          used[elems[k]] = 1;
        }
        w += fc_w(weight, s);
        cur[depth] = 1;
        dec[depth] = 1;
      } else {
        cur[depth] = 0;
        dec[depth] = 2;
      }
      depth++;
    }
  pack_done:;
  } else if (method != DS_COVER_GREEDY) {
    st = DS_ERR_INVALID;
  }
  ds_context_free(context, used);
  ds_context_free(context, cur);
  ds_context_free(context, order);
  ds_context_free(context, suffix);
  *out_weight = best;
  return st;
}

/* ---- uncapacitated facility location
 * ----------------------------------------- */

static double fc_cost(const double *f, const double *c, size_t m, size_t n,
                      const unsigned char *open) {
  size_t i;
  size_t j;
  double total = 0.0;
  double b;
  bool any = false;
  for (i = 0; i < m; i++) {
    if (open[i]) {
      total += f[i];
      any = true;
    }
  }
  if (!any) {
    return HUGE_VAL;
  }
  for (j = 0; j < n; j++) {
    b = HUGE_VAL;
    for (i = 0; i < m; i++) {
      if (open[i] && c[i * n + j] < b) {
        b = c[i * n + j];
      }
    }
    total += b;
  }
  return total;
}

ds_status_t ds_facility_location(const double *f, const double *c, size_t m,
                                 size_t n, ds_facility_method_t method,
                                 unsigned char *open, size_t *assign,
                                 double *out_cost, ds_context_t *context) {
  unsigned char *cur;
  size_t i;
  size_t j;
  size_t a;
  size_t b;
  double best;
  double v;
  bool improved;
  if (f == NULL || c == NULL || open == NULL || out_cost == NULL ||
      (n > 0 && assign == NULL)) {
    return DS_ERR_NULL;
  }
  if (m == 0) {
    return DS_ERR_INVALID;
  }
  cur = (unsigned char *)ds_context_calloc(context, m, 1);
  if (cur == NULL) {
    return DS_ERR_ALLOC;
  }
  memset(open, 0, m);
  if (method == DS_FACILITY_EXACT) {
    size_t mask;
    if (m > 20) {
      ds_context_free(context, cur);
      return DS_ERR_UNSUPPORTED;
    }
    best = HUGE_VAL;
    for (mask = 1; mask < ((size_t)1 << m); mask++) {
      for (i = 0; i < m; i++) {
        cur[i] = (unsigned char)((mask >> i) & 1u);
      }
      v = fc_cost(f, c, m, n, cur);
      if (v < best) {
        best = v;
        memcpy(open, cur, m);
      }
    }
  } else if (method == DS_FACILITY_GREEDY ||
             method == DS_FACILITY_LOCAL_SEARCH) {
    /* add the facility that lowers the cost most, while any does */
    best = HUGE_VAL;
    improved = true;
    while (improved) {
      improved = false;
      a = FC_NONE;
      for (i = 0; i < m; i++) {
        if (open[i]) {
          continue;
        }
        open[i] = 1;
        v = fc_cost(f, c, m, n, open);
        open[i] = 0;
        if (v < best - 1e-12) {
          best = v;
          a = i;
        }
      }
      if (a != FC_NONE) {
        open[a] = 1;
        improved = true;
      }
    }
    if (method == DS_FACILITY_LOCAL_SEARCH) {
      improved = true;
      while (improved) {
        improved = false;
        for (a = 0; a < m && !improved; a++) {
          /* toggle a (open or close) */
          open[a] = (unsigned char)!open[a];
          v = fc_cost(f, c, m, n, open);
          if (v < best - 1e-12) {
            best = v;
            improved = true;
            break;
          }
          open[a] = (unsigned char)!open[a];
          /* swap a (open) with b (closed) */
          if (!open[a]) {
            continue;
          }
          for (b = 0; b < m && !improved; b++) {
            if (open[b]) {
              continue;
            }
            open[a] = 0;
            open[b] = 1;
            v = fc_cost(f, c, m, n, open);
            if (v < best - 1e-12) {
              best = v;
              improved = true;
            } else {
              open[a] = 1;
              open[b] = 0;
            }
          }
        }
      }
    }
  } else {
    ds_context_free(context, cur);
    return DS_ERR_INVALID;
  }
  for (j = 0; j < n; j++) {
    a = FC_NONE;
    for (i = 0; i < m; i++) {
      if (open[i] && (a == FC_NONE || c[i * n + j] < c[a * n + j])) {
        a = i;
      }
    }
    assign[j] = a;
  }
  ds_context_free(context, cur);
  *out_cost = fc_cost(f, c, m, n, open);
  return DS_OK;
}

ds_status_t ds_capacitated_facility_location(
    const double *f, const double *capacity, const double *demand,
    const double *c, size_t m, size_t n, unsigned char *open, double *flow,
    double *out_cost, ds_context_t *context) {
  ds_flow_network_t net;
  size_t mask;
  size_t i;
  size_t j;
  size_t e;
  double need = 0.0;
  double cap;
  double fixed;
  double fl = 0.0;
  double fcost = 0.0;
  double best = HUGE_VAL;
  bool found = false;
  ds_status_t st;
  if (f == NULL || capacity == NULL || demand == NULL || c == NULL ||
      open == NULL || out_cost == NULL) {
    return DS_ERR_NULL;
  }
  if (m == 0) {
    return DS_ERR_INVALID;
  }
  if (m > 16) {
    return DS_ERR_UNSUPPORTED;
  }
  for (j = 0; j < n; j++) {
    need += demand[j];
  }
  for (mask = 1; mask < ((size_t)1 << m); mask++) {
    cap = 0.0;
    fixed = 0.0;
    for (i = 0; i < m; i++) {
      if ((mask >> i) & 1u) {
        cap += capacity[i];
        fixed += f[i];
      }
    }
    if (cap < need - 1e-9 || fixed >= best) {
      continue;
    }
    /* transportation problem for this subset: source, facilities,
     * clients, sink */
    st = ds_flow_network_init(&net, m + n + 2, context);
    if (st != DS_OK) {
      return st;
    }
    for (i = 0; i < m && st == DS_OK; i++) {
      if ((mask >> i) & 1u) {
        st = ds_flow_network_add_edge(&net, 0, 1 + i, capacity[i], 0.0, NULL);
      }
    }
    for (i = 0; i < m && st == DS_OK; i++) {
      for (j = 0; j < n && st == DS_OK; j++) {
        if ((mask >> i) & 1u) {
          st = ds_flow_network_add_edge(&net, 1 + i, 1 + m + j, need,
                                        c[i * n + j], NULL);
        }
      }
    }
    for (j = 0; j < n && st == DS_OK; j++) {
      st = ds_flow_network_add_edge(&net, 1 + m + j, m + n + 1, demand[j], 0.0,
                                    NULL);
    }
    if (st == DS_OK) {
      st = ds_flow_min_cost_flow(&net, 0, m + n + 1, HUGE_VAL, &fl, &fcost,
                                 context);
    }
    if (st != DS_OK) {
      ds_flow_network_destroy(&net);
      return st;
    }
    if (fl >= need - 1e-9 && fixed + fcost < best) {
      best = fixed + fcost;
      found = true;
      for (i = 0; i < m; i++) {
        open[i] = (unsigned char)((mask >> i) & 1u);
      }
      if (flow != NULL) {
        for (i = 0; i < m * n; i++) {
          flow[i] = 0.0;
        }
        /* edges were added in order: facility edges, then pairs */
        e = 0;
        for (i = 0; i < m; i++) {
          e += (mask >> i) & 1u;
        }
        for (i = 0; i < m; i++) {
          if (!((mask >> i) & 1u)) {
            continue;
          }
          for (j = 0; j < n; j++) {
            flow[i * n + j] = ds_flow_network_flow(&net, e++);
          }
        }
      }
    }
    ds_flow_network_destroy(&net);
  }
  if (!found) {
    return DS_NOT_FOUND;
  }
  *out_cost = best;
  return DS_OK;
}

/* ---- p-center and k-clustering
 * ------------------------------------------------ */

/* Farthest-first: p centres starting from vertex 0; returns the radius. */
static double fc_gonzalez(const double *d, size_t n, size_t p, size_t *centers,
                          double *near) {
  size_t i;
  size_t k;
  size_t far;
  double r = 0.0;
  for (i = 0; i < n; i++) {
    near[i] = HUGE_VAL;
  }
  far = 0;
  for (k = 0; k < p; k++) {
    centers[k] = far;
    for (i = 0; i < n; i++) {
      if (d[far * n + i] < near[i]) {
        near[i] = d[far * n + i];
      }
    }
    far = 0;
    for (i = 1; i < n; i++) {
      if (near[i] > near[far]) {
        far = i;
      }
    }
  }
  for (i = 0; i < n; i++) {
    if (near[i] > r) {
      r = near[i];
    }
  }
  return r;
}

static int fc_cmp_d(const void *a, const void *b) {
  double x = *(const double *)a;
  double y = *(const double *)b;
  return (x > y) - (x < y);
}

/* At most p centres covering every vertex within r (DFS, iterative):
 * branch over the centres able to cover the first uncovered vertex. */
static ds_status_t fc_cover_within(const double *d, size_t n, size_t p,
                                   double r, size_t *centers, size_t *cnt,
                                   size_t *fu, size_t *fp, size_t *budget) {
  size_t top = 0;
  size_t u;
  size_t v;
  size_t i;
  memset(cnt, 0, n * sizeof(size_t));
  for (;;) {
    for (u = 0; u < n && cnt[u] > 0; u++) {
    }
    if (u == n) {
      return DS_OK;
    }
    if (top < p) {
      fu[top] = u;
      fp[top] = 0;
      centers[top] = FC_NONE;
      top++;
    }
    /* advance the deepest frame */
    for (;;) {
      if (top == 0) {
        return DS_NOT_FOUND;
      }
      if (centers[top - 1] != FC_NONE) {
        v = centers[top - 1];
        for (i = 0; i < n; i++) {
          if (d[v * n + i] <= r) {
            cnt[i]--;
          }
        }
        centers[top - 1] = FC_NONE;
      }
      u = fu[top - 1];
      while (fp[top - 1] < n && d[fp[top - 1] * n + u] > r) {
        fp[top - 1]++;
      }
      if (fp[top - 1] == n) {
        top--;
        continue;
      }
      if (*budget != 0 && --*budget == 0) {
        return DS_STOP;
      }
      v = fp[top - 1]++;
      centers[top - 1] = v;
      for (i = 0; i < n; i++) {
        if (d[v * n + i] <= r) {
          cnt[i]++;
        }
      }
      /* a frame at depth p with vertices still uncovered must backtrack */
      for (u = 0; u < n && cnt[u] > 0; u++) {
      }
      if (u == n || top < p) {
        break;
      }
    }
  }
}

ds_status_t ds_p_center(const double *dist, size_t n, size_t p,
                        ds_center_method_t method, size_t budget,
                        size_t *centers, double *out_radius,
                        ds_context_t *context) {
  double *near;
  double *radii;
  size_t *scratch;
  size_t i;
  size_t j;
  size_t lo;
  size_t hi;
  size_t mid;
  size_t nr = 0;
  ds_status_t st = DS_OK;
  if ((n > 0 && (dist == NULL || centers == NULL)) || out_radius == NULL) {
    return DS_ERR_NULL;
  }
  *out_radius = 0.0;
  if (n == 0) {
    return DS_OK;
  }
  if (p == 0 || p > n) {
    return DS_ERR_INVALID;
  }
  if (n > ((size_t)-1) / n / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  near = (double *)ds_context_alloc(context, n * sizeof(double));
  if (near == NULL) {
    return DS_ERR_ALLOC;
  }
  *out_radius = fc_gonzalez(dist, n, p, centers, near);
  ds_context_free(context, near);
  if (method == DS_CENTER_GONZALEZ) {
    return DS_OK;
  }
  if (method != DS_CENTER_EXACT) {
    return DS_ERR_INVALID;
  }
  radii = (double *)ds_context_alloc(context, (n * n + 1) * sizeof(double));
  scratch =
      (size_t *)ds_context_alloc(context, (2 * n + 2 * p + 2) * sizeof(size_t));
  if (radii == NULL || scratch == NULL) {
    ds_context_free(context, radii);
    ds_context_free(context, scratch);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    for (j = 0; j < n; j++) {
      if (dist[i * n + j] <= *out_radius) {
        radii[nr++] = dist[i * n + j];
      }
    }
  }
  qsort(radii, nr, sizeof(double), fc_cmp_d);
  /* the optimum is one of the distances not above Gonzalez's radius */
  lo = 0;
  hi = nr - 1;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    st = fc_cover_within(dist, n, p, radii[mid], scratch + 2 * n + p + 1,
                         scratch, scratch + n, scratch + 2 * n, &budget);
    if (st == DS_STOP) {
      break;
    }
    if (st == DS_OK) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  if (st != DS_STOP) {
    st = fc_cover_within(dist, n, p, radii[lo], scratch + 2 * n + p + 1,
                         scratch, scratch + n, scratch + 2 * n, &budget);
    if (st == DS_OK) {
      /* report the cover (fewer than p centres: pad with any vertex) */
      size_t used = 0;
      size_t *cs = scratch + 2 * n + p + 1;
      for (i = 0; i < p && cs[i] != FC_NONE; i++) {
        centers[used++] = cs[i];
      }
      for (i = 0; used < p && i < n; i++) {
        bool dup = false;
        for (j = 0; j < used; j++) {
          dup = dup || centers[j] == i;
        }
        if (!dup) {
          centers[used++] = i;
        }
      }
      *out_radius = radii[lo];
    } else if (st == DS_NOT_FOUND) {
      st = DS_OK; /* Gonzalez's radius was optimal already */
    }
  }
  ds_context_free(context, radii);
  ds_context_free(context, scratch);
  return st;
}

typedef struct fc_edge {
  double w;
  size_t u;
  size_t v;
} fc_edge_t;

static int fc_cmp_edge(const void *a, const void *b) {
  double x = ((const fc_edge_t *)a)->w;
  double y = ((const fc_edge_t *)b)->w;
  return (x > y) - (x < y);
}

static size_t fc_find(size_t *uf, size_t x) {
  while (uf[x] != x) {
    uf[x] = uf[uf[x]];
    x = uf[x];
  }
  return x;
}

ds_status_t ds_k_clustering(const double *dist, size_t n, size_t k,
                            ds_cluster_method_t method, size_t *label,
                            double *out_value, ds_context_t *context) {
  size_t i;
  size_t j;
  if ((n > 0 && (dist == NULL || label == NULL)) || out_value == NULL) {
    return DS_ERR_NULL;
  }
  *out_value = 0.0;
  if (n == 0) {
    return DS_OK;
  }
  if (k == 0 || k > n) {
    return DS_ERR_INVALID;
  }
  if (method == DS_CLUSTER_K_CENTER) {
    size_t *centers = (size_t *)ds_context_alloc(context, k * sizeof(size_t));
    double *near = (double *)ds_context_alloc(context, n * sizeof(double));
    if (centers == NULL || near == NULL) {
      ds_context_free(context, centers);
      ds_context_free(context, near);
      return DS_ERR_ALLOC;
    }
    *out_value = fc_gonzalez(dist, n, k, centers, near);
    for (i = 0; i < n; i++) {
      size_t b = 0;
      for (j = 1; j < k; j++) {
        if (dist[centers[j] * n + i] < dist[centers[b] * n + i]) {
          b = j;
        }
      }
      label[i] = b;
    }
    ds_context_free(context, centers);
    ds_context_free(context, near);
    return DS_OK;
  }
  if (method == DS_CLUSTER_SINGLE_LINKAGE) {
    fc_edge_t *e;
    size_t *uf;
    size_t m = 0;
    size_t comps = n;
    size_t r;
    size_t next = 0;
    if (n > ((size_t)-1) / n / sizeof(fc_edge_t)) {
      return DS_ERR_OVERFLOW;
    }
    e = (fc_edge_t *)ds_context_alloc(context,
                                      (n * n / 2 + 1) * sizeof(fc_edge_t));
    uf = (size_t *)ds_context_alloc(context, 2 * n * sizeof(size_t));
    if (e == NULL || uf == NULL) {
      ds_context_free(context, e);
      ds_context_free(context, uf);
      return DS_ERR_ALLOC;
    }
    for (i = 0; i < n; i++) {
      uf[i] = i;
      for (j = i + 1; j < n; j++) {
        e[m].w = dist[i * n + j] < dist[j * n + i] ? dist[i * n + j]
                                                   : dist[j * n + i];
        e[m].u = i;
        e[m++].v = j;
      }
    }
    qsort(e, m, sizeof(fc_edge_t), fc_cmp_edge);
    *out_value = HUGE_VAL;
    for (r = 0; r < m; r++) {
      size_t a = fc_find(uf, e[r].u);
      size_t b = fc_find(uf, e[r].v);
      if (a == b) {
        continue;
      }
      if (comps == k) {
        *out_value = e[r].w; /* the spacing */
        break;
      }
      uf[a] = b;
      comps--;
    }
    for (i = 0; i < n; i++) {
      uf[n + i] = FC_NONE;
    }
    for (i = 0; i < n; i++) {
      size_t root = fc_find(uf, i);
      if (uf[n + root] == FC_NONE) {
        uf[n + root] = next++;
      }
      label[i] = uf[n + root];
    }
    ds_context_free(context, e);
    ds_context_free(context, uf);
    return DS_OK;
  }
  return DS_ERR_INVALID;
}
