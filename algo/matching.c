#include "matching.h"
#include <math.h>
#include <string.h>

#define MT_NONE DS_MATCHING_NONE

static size_t mt_target(const ds_gview_t *g, size_t v, size_t i) {
  size_t to;
  double w;
  g->edge(g->graph, v, i, &to, &w);
  return to;
}

/* ---- Hopcroft-Karp ------------------------------------------------------- */

ds_status_t ds_matching_hopcroft_karp(const ds_gview_t *left,
                                      size_t right_count,
                                      size_t *out_match_left,
                                      size_t *out_match_right, size_t *out_size,
                                      ds_context_t *context) {
  size_t nl;
  size_t *dist;
  size_t *queue;
  size_t *it;
  size_t *stack;
  size_t *ml = out_match_left;
  size_t *mr = out_match_right;
  size_t qh;
  size_t qt;
  size_t u;
  size_t w;
  size_t r;
  size_t i;
  size_t deg;
  size_t sp;
  size_t root;
  size_t k;
  size_t size = 0;
  bool found;
  ds_status_t status = DS_OK;
  if (left == NULL || ml == NULL || mr == NULL) {
    return DS_ERR_NULL;
  }
  nl = left->vertex_count;
  for (u = 0; u < nl; u++) {
    ml[u] = MT_NONE;
  }
  for (r = 0; r < right_count; r++) {
    mr[r] = MT_NONE;
  }
  dist = (size_t *)ds_context_alloc(context, (nl + 1) * sizeof(size_t));
  queue = (size_t *)ds_context_alloc(context, (nl + 1) * sizeof(size_t));
  it = (size_t *)ds_context_alloc(context, (nl + 1) * sizeof(size_t));
  stack = (size_t *)ds_context_alloc(context, (nl + 1) * sizeof(size_t));
  if (dist == NULL || queue == NULL || it == NULL || stack == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  for (;;) {
    qh = 0;
    qt = 0;
    for (u = 0; u < nl; u++) {
      if (ml[u] == MT_NONE) {
        dist[u] = 0;
        queue[qt++] = u;
      } else {
        dist[u] = MT_NONE;
      }
    }
    found = false;
    while (qh < qt) {
      u = queue[qh++];
      deg = left->degree(left->graph, u);
      for (i = 0; i < deg; i++) {
        r = mt_target(left, u, i);
        if (r >= right_count) {
          continue;
        }
        w = mr[r];
        if (w == MT_NONE) {
          found = true;
        } else if (dist[w] == MT_NONE) {
          dist[w] = dist[u] + 1;
          queue[qt++] = w;
        }
      }
    }
    if (!found) {
      break;
    }
    for (u = 0; u < nl; u++) {
      it[u] = 0;
    }
    for (root = 0; root < nl; root++) {
      if (ml[root] != MT_NONE) {
        continue;
      }
      sp = 0;
      stack[sp++] = root;
      while (sp > 0) {
        u = stack[sp - 1];
        deg = left->degree(left->graph, u);
        if (it[u] >= deg) {
          dist[u] = MT_NONE; /* dead end this phase */
          sp--;
          if (sp > 0) {
            it[stack[sp - 1]]++;
          }
          continue;
        }
        r = mt_target(left, u, it[u]);
        if (r >= right_count) {
          it[u]++;
          continue;
        }
        w = mr[r];
        if (w == MT_NONE) {
          /* augment along the stack */
          for (k = sp; k-- > 0;) {
            u = stack[k];
            r = mt_target(left, u, it[u]);
            ml[u] = r;
            mr[r] = u;
          }
          size++;
          break;
        }
        if (dist[w] != MT_NONE && dist[w] == dist[u] + 1) {
          stack[sp++] = w;
        } else {
          it[u]++;
        }
      }
    }
  }
  /* count (also covers a pre-existing call with partial results) */
  size = 0;
  for (u = 0; u < nl; u++) {
    size += ml[u] != MT_NONE;
  }
out:
  ds_context_free(context, dist);
  ds_context_free(context, queue);
  ds_context_free(context, it);
  ds_context_free(context, stack);
  if (status == DS_OK && out_size != NULL) {
    *out_size = size;
  }
  return status;
}

/* ---- Hungarian ----------------------------------------------------------- */

ds_status_t ds_matching_hungarian(const double *cost, size_t n, size_t m,
                                  bool maximize, size_t *out_assignment,
                                  double *out_total, ds_context_t *context) {
  double *u;
  double *v;
  double *minv;
  size_t *p;
  size_t *way;
  unsigned char *used;
  size_t i;
  size_t j;
  size_t j0;
  size_t j1;
  size_t i0;
  double delta;
  double cur;
  double total = 0;
  double sign = maximize ? -1.0 : 1.0;
  ds_status_t status = DS_OK;
  if ((cost == NULL && n > 0) || out_assignment == NULL) {
    return DS_ERR_NULL;
  }
  if (n > m) {
    return DS_ERR_INVALID;
  }
  if (n == 0) {
    if (out_total != NULL) {
      *out_total = 0;
    }
    return DS_OK;
  }
  u = (double *)ds_context_calloc(context, n + 1, sizeof(double));
  v = (double *)ds_context_calloc(context, m + 1, sizeof(double));
  minv = (double *)ds_context_alloc(context, (m + 1) * sizeof(double));
  p = (size_t *)ds_context_calloc(context, m + 1, sizeof(size_t));
  way = (size_t *)ds_context_calloc(context, m + 1, sizeof(size_t));
  used = (unsigned char *)ds_context_alloc(context, m + 1);
  if (u == NULL || v == NULL || minv == NULL || p == NULL || way == NULL ||
      used == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  /* 1-based rows/columns; column 0 is a virtual start. */
  for (i = 1; i <= n; i++) {
    p[0] = i;
    j0 = 0;
    for (j = 0; j <= m; j++) {
      minv[j] = HUGE_VAL;
      used[j] = 0;
    }
    do {
      used[j0] = 1;
      i0 = p[j0];
      delta = HUGE_VAL;
      j1 = 0;
      for (j = 1; j <= m; j++) {
        if (!used[j]) {
          cur = sign * cost[(i0 - 1) * m + (j - 1)] - u[i0] - v[j];
          if (cur < minv[j]) {
            minv[j] = cur;
            way[j] = j0;
          }
          if (minv[j] < delta) {
            delta = minv[j];
            j1 = j;
          }
        }
      }
      for (j = 0; j <= m; j++) {
        if (used[j]) {
          u[p[j]] += delta;
          v[j] -= delta;
        } else {
          minv[j] -= delta;
        }
      }
      j0 = j1;
    } while (p[j0] != 0);
    do {
      j1 = way[j0];
      p[j0] = p[j1];
      j0 = j1;
    } while (j0 != 0);
  }
  for (j = 1; j <= m; j++) {
    if (p[j] != 0) {
      out_assignment[p[j] - 1] = j - 1;
    }
  }
  for (i = 0; i < n; i++) {
    total += cost[i * m + out_assignment[i]];
  }
  if (out_total != NULL) {
    *out_total = total;
  }
out:
  ds_context_free(context, u);
  ds_context_free(context, v);
  ds_context_free(context, minv);
  ds_context_free(context, p);
  ds_context_free(context, way);
  ds_context_free(context, used);
  return status;
}

/* ---- Edmonds' blossom ---------------------------------------------------- */

typedef struct mt_blossom {
  const ds_gview_t *g;
  size_t n;
  size_t *match;
  size_t *p;
  size_t *base;
  size_t *queue;
  unsigned char *used;
  unsigned char *blossom;
  unsigned char *seen;
} mt_blossom_t;

static size_t mt_lca(mt_blossom_t *b, size_t x, size_t y) {
  memset(b->seen, 0, b->n);
  for (;;) {
    x = b->base[x];
    b->seen[x] = 1;
    if (b->match[x] == MT_NONE) {
      break;
    }
    x = b->p[b->match[x]];
  }
  for (;;) {
    y = b->base[y];
    if (b->seen[y]) {
      return y;
    }
    y = b->p[b->match[y]];
  }
}

static void mt_mark_path(mt_blossom_t *b, size_t v, size_t root, size_t child) {
  while (b->base[v] != root) {
    b->blossom[b->base[v]] = 1;
    b->blossom[b->base[b->match[v]]] = 1;
    b->p[v] = child;
    child = b->match[v];
    v = b->p[b->match[v]];
  }
}

static size_t mt_find_path(mt_blossom_t *b, size_t root) {
  size_t qh = 0;
  size_t qt = 0;
  size_t v;
  size_t to;
  size_t i;
  size_t deg;
  size_t cur;
  size_t k;
  for (i = 0; i < b->n; i++) {
    b->used[i] = 0;
    b->p[i] = MT_NONE;
    b->base[i] = i;
  }
  b->used[root] = 1;
  b->queue[qt++] = root;
  while (qh < qt) {
    v = b->queue[qh++];
    deg = b->g->degree(b->g->graph, v);
    for (i = 0; i < deg; i++) {
      to = mt_target(b->g, v, i);
      if (to >= b->n || to == v || b->base[v] == b->base[to] ||
          b->match[v] == to) {
        continue;
      }
      if (to == root ||
          (b->match[to] != MT_NONE && b->p[b->match[to]] != MT_NONE)) {
        cur = mt_lca(b, v, to);
        memset(b->blossom, 0, b->n);
        mt_mark_path(b, v, cur, to);
        mt_mark_path(b, to, cur, v);
        for (k = 0; k < b->n; k++) {
          if (b->blossom[b->base[k]]) {
            b->base[k] = cur;
            if (!b->used[k]) {
              b->used[k] = 1;
              b->queue[qt++] = k;
            }
          }
        }
      } else if (b->p[to] == MT_NONE) {
        b->p[to] = v;
        if (b->match[to] == MT_NONE) {
          return to;
        }
        b->used[b->match[to]] = 1;
        b->queue[qt++] = b->match[to];
      }
    }
  }
  return MT_NONE;
}

ds_status_t ds_matching_blossom(const ds_gview_t *g, size_t *out_mate,
                                size_t *out_size, ds_context_t *context) {
  mt_blossom_t b;
  size_t v;
  size_t x;
  size_t pv;
  size_t ppv;
  size_t i;
  size_t deg;
  size_t to;
  size_t size = 0;
  ds_status_t status = DS_OK;
  if (g == NULL || out_mate == NULL) {
    return DS_ERR_NULL;
  }
  b.g = g;
  b.n = g->vertex_count;
  b.match = out_mate;
  for (v = 0; v < b.n; v++) {
    b.match[v] = MT_NONE;
  }
  b.p = (size_t *)ds_context_alloc(context, (b.n + 1) * sizeof(size_t));
  b.base = (size_t *)ds_context_alloc(context, (b.n + 1) * sizeof(size_t));
  /* every vertex enters the queue at most once per search */
  b.queue = (size_t *)ds_context_alloc(context, (b.n + 1) * sizeof(size_t));
  b.used = (unsigned char *)ds_context_alloc(context, b.n + 1);
  b.blossom = (unsigned char *)ds_context_alloc(context, b.n + 1);
  b.seen = (unsigned char *)ds_context_alloc(context, b.n + 1);
  if (b.p == NULL || b.base == NULL || b.queue == NULL || b.used == NULL ||
      b.blossom == NULL || b.seen == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  /* greedy start */
  for (v = 0; v < b.n; v++) {
    if (b.match[v] != MT_NONE) {
      continue;
    }
    deg = g->degree(g->graph, v);
    for (i = 0; i < deg; i++) {
      to = mt_target(g, v, i);
      if (to < b.n && to != v && b.match[to] == MT_NONE) {
        b.match[v] = to;
        b.match[to] = v;
        break;
      }
    }
  }
  for (v = 0; v < b.n; v++) {
    if (b.match[v] != MT_NONE) {
      continue;
    }
    x = mt_find_path(&b, v);
    while (x != MT_NONE) {
      pv = b.p[x];
      ppv = b.match[pv];
      b.match[x] = pv;
      b.match[pv] = x;
      x = ppv;
    }
  }
  for (v = 0; v < b.n; v++) {
    size += b.match[v] != MT_NONE;
  }
  size /= 2;
out:
  ds_context_free(context, b.p);
  ds_context_free(context, b.base);
  ds_context_free(context, b.queue);
  ds_context_free(context, b.used);
  ds_context_free(context, b.blossom);
  ds_context_free(context, b.seen);
  if (status == DS_OK && out_size != NULL) {
    *out_size = size;
  }
  return status;
}

/* ---- stable marriage ----------------------------------------------------- */

ds_status_t ds_matching_stable(const size_t *proposer_pref,
                               const size_t *acceptor_pref, size_t n,
                               size_t *out_match, ds_context_t *context) {
  size_t *rank;
  size_t *next;
  size_t *partner; /* acceptor -> proposer */
  size_t *free_list;
  size_t nfree = 0;
  size_t p;
  size_t a;
  size_t i;
  if ((n > 0 && (proposer_pref == NULL || acceptor_pref == NULL)) ||
      out_match == NULL) {
    return DS_ERR_NULL;
  }
  if (n > 0 && n > ((size_t)-1) / n / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  rank = (size_t *)ds_context_alloc(context, (n * n + 1) * sizeof(size_t));
  next = (size_t *)ds_context_alloc(context, (n + 1) * sizeof(size_t));
  partner = (size_t *)ds_context_alloc(context, (n + 1) * sizeof(size_t));
  free_list = (size_t *)ds_context_alloc(context, (n + 1) * sizeof(size_t));
  if (rank == NULL || next == NULL || partner == NULL || free_list == NULL) {
    ds_context_free(context, rank);
    ds_context_free(context, next);
    ds_context_free(context, partner);
    ds_context_free(context, free_list);
    return DS_ERR_ALLOC;
  }
  for (a = 0; a < n; a++) {
    for (i = 0; i < n; i++) {
      rank[a * n + acceptor_pref[a * n + i]] = i;
    }
    partner[a] = MT_NONE;
  }
  for (p = 0; p < n; p++) {
    next[p] = 0;
    out_match[p] = MT_NONE;
    free_list[nfree++] = n - 1 - p;
  }
  while (nfree > 0) {
    p = free_list[--nfree];
    a = proposer_pref[p * n + next[p]++];
    if (partner[a] == MT_NONE) {
      partner[a] = p;
      out_match[p] = a;
    } else if (rank[a * n + p] < rank[a * n + partner[a]]) {
      out_match[partner[a]] = MT_NONE;
      free_list[nfree++] = partner[a];
      partner[a] = p;
      out_match[p] = a;
    } else {
      free_list[nfree++] = p;
    }
  }
  ds_context_free(context, rank);
  ds_context_free(context, next);
  ds_context_free(context, partner);
  ds_context_free(context, free_list);
  return DS_OK;
}
