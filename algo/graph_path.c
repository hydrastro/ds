#include "graph_path.h"
#include "../lib/vec.h"
#include "graph_struct.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GP_NONE DS_GVIEW_NONE

static size_t gp_target(const ds_gview_t *g, size_t v, size_t i, double *w) {
  size_t to;
  double weight = 1.0;
  g->edge(g->graph, v, i, &to, &weight);
  if (w != NULL) {
    *w = weight;
  }
  return to;
}

static void *gp_alloc(ds_context_t *ctx, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > ((size_t)-1) / size) {
    return NULL;
  }
  return ds_context_alloc(ctx, count * size);
}

/* ---- a small lazy-deletion binary heap ----------------------------------- */

typedef struct gp_heap {
  double *key;
  size_t *val;
  size_t size;
  size_t capacity;
  ds_context_t *ctx;
} gp_heap_t;

static ds_status_t gp_heap_push(gp_heap_t *h, double key, size_t val) {
  size_t i;
  size_t p;
  double *k;
  size_t *v;
  if (h->size == h->capacity) {
    size_t c = h->capacity == 0 ? 64 : h->capacity * 2;
    k = (double *)ds_context_realloc(h->ctx, h->key, c * sizeof(double));
    if (k == NULL) {
      return DS_ERR_ALLOC;
    }
    h->key = k;
    v = (size_t *)ds_context_realloc(h->ctx, h->val, c * sizeof(size_t));
    if (v == NULL) {
      return DS_ERR_ALLOC;
    }
    h->val = v;
    h->capacity = c;
  }
  i = h->size++;
  while (i > 0) {
    p = (i - 1) / 2;
    if (!(key < h->key[p])) {
      break;
    }
    h->key[i] = h->key[p];
    h->val[i] = h->val[p];
    i = p;
  }
  h->key[i] = key;
  h->val[i] = val;
  return DS_OK;
}

static void gp_heap_pop(gp_heap_t *h, double *key, size_t *val) {
  double k;
  size_t v;
  size_t i = 0;
  size_t c;
  *key = h->key[0];
  *val = h->val[0];
  if (--h->size == 0) {
    return;
  }
  k = h->key[h->size];
  v = h->val[h->size];
  for (;;) {
    c = 2 * i + 1;
    if (c >= h->size) {
      break;
    }
    if (c + 1 < h->size && h->key[c + 1] < h->key[c]) {
      c++;
    }
    if (!(h->key[c] < k)) {
      break;
    }
    h->key[i] = h->key[c];
    h->val[i] = h->val[c];
    i = c;
  }
  h->key[i] = k;
  h->val[i] = v;
}

static void gp_heap_free(gp_heap_t *h) {
  ds_context_free(h->ctx, h->key);
  ds_context_free(h->ctx, h->val);
  h->key = NULL;
  h->val = NULL;
  h->size = h->capacity = 0;
}

/*
 * Dijkstra with optional potentials (reduced weights w + pot[u] - pot[v]),
 * blocked vertices and blocked global edges. parent_edge gets global edge
 * indices when offsets is given.
 */
static ds_status_t gp_dijkstra(const ds_gview_t *g, size_t source,
                               size_t target, const double *pot,
                               const unsigned char *blocked_v,
                               const unsigned char *blocked_e,
                               const size_t *offsets, double *dist,
                               size_t *parent, size_t *parent_edge,
                               unsigned char *done, ds_context_t *ctx) {
  size_t n = g->vertex_count;
  gp_heap_t heap;
  size_t v;
  size_t w;
  size_t i;
  size_t deg;
  double d;
  double wt;
  double nd;
  ds_status_t status = DS_OK;
  heap.key = NULL;
  heap.val = NULL;
  heap.size = heap.capacity = 0;
  heap.ctx = ctx;
  for (v = 0; v < n; v++) {
    dist[v] = HUGE_VAL;
    parent[v] = GP_NONE;
    if (parent_edge != NULL) {
      parent_edge[v] = GP_NONE;
    }
    done[v] = 0;
  }
  dist[source] = 0;
  status = gp_heap_push(&heap, 0, source);
  while (status == DS_OK && heap.size > 0) {
    gp_heap_pop(&heap, &d, &v);
    if (done[v]) {
      continue;
    }
    done[v] = 1;
    if (v == target) {
      break;
    }
    deg = g->degree(g->graph, v);
    for (i = 0; i < deg; i++) {
      if (blocked_e != NULL && blocked_e[offsets[v] + i]) {
        continue;
      }
      w = gp_target(g, v, i, &wt);
      if (w >= n || done[w] || (blocked_v != NULL && blocked_v[w])) {
        continue;
      }
      if (pot != NULL) {
        wt += pot[v] - pot[w];
        if (wt < 0) {
          wt = 0; /* rounding */
        }
      }
      nd = dist[v] + wt;
      if (nd < dist[w]) {
        dist[w] = nd;
        parent[w] = v;
        if (parent_edge != NULL) {
          parent_edge[w] = offsets[v] + i;
        }
        status = gp_heap_push(&heap, nd, w);
      }
    }
  }
  gp_heap_free(&heap);
  return status;
}

/* ---- Johnson ------------------------------------------------------------- */

ds_status_t ds_gview_johnson(const ds_gview_t *g, double *out_dist,
                             ds_context_t *context) {
  size_t n;
  double *h;
  double *dist;
  size_t *parent;
  unsigned char *done;
  size_t round;
  size_t v;
  size_t w;
  size_t i;
  size_t s;
  size_t deg;
  double wt;
  bool changed = true;
  ds_status_t status = DS_OK;
  if (g == NULL || out_dist == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  h = (double *)ds_context_calloc(context, n + 1, sizeof(double));
  dist = (double *)gp_alloc(context, n, sizeof(double));
  parent = (size_t *)gp_alloc(context, n, sizeof(size_t));
  done = (unsigned char *)gp_alloc(context, n, 1);
  if (h == NULL || dist == NULL || parent == NULL || done == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  /* Bellman-Ford from a virtual source joined to every vertex by 0. */
  for (round = 0; round <= n && changed; round++) {
    changed = false;
    for (v = 0; v < n; v++) {
      deg = g->degree(g->graph, v);
      for (i = 0; i < deg; i++) {
        w = gp_target(g, v, i, &wt);
        if (w < n && h[v] + wt < h[w]) {
          h[w] = h[v] + wt;
          changed = true;
        }
      }
    }
  }
  if (changed) {
    status = DS_ERR_INVALID;
    goto out;
  }
  for (s = 0; s < n && status == DS_OK; s++) {
    status = gp_dijkstra(g, s, GP_NONE, h, NULL, NULL, NULL, dist, parent, NULL,
                         done, context);
    for (v = 0; v < n; v++) {
      out_dist[s * n + v] =
          dist[v] < HUGE_VAL ? dist[v] - h[s] + h[v] : HUGE_VAL;
    }
  }
out:
  ds_context_free(context, h);
  ds_context_free(context, dist);
  ds_context_free(context, parent);
  ds_context_free(context, done);
  return status;
}

/* ---- DAG paths ----------------------------------------------------------- */

ds_status_t ds_gview_dag_shortest(const ds_gview_t *g, size_t source,
                                  bool longest, double *out_dist,
                                  size_t *out_parent, ds_context_t *context) {
  size_t n;
  size_t *order;
  size_t k;
  size_t v;
  size_t w;
  size_t i;
  size_t deg;
  double wt;
  double nd;
  double unreached = longest ? -HUGE_VAL : HUGE_VAL;
  ds_status_t status;
  if (g == NULL || out_dist == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  if (source >= n) {
    return DS_ERR_RANGE;
  }
  order = (size_t *)gp_alloc(context, n, sizeof(size_t));
  if (order == NULL) {
    return DS_ERR_ALLOC;
  }
  status = ds_gview_topological_sort(g, order, context);
  if (status != DS_OK) {
    ds_context_free(context, order);
    return status == DS_ERR_ALLOC ? status : DS_ERR_INVALID;
  }
  for (v = 0; v < n; v++) {
    out_dist[v] = unreached;
    if (out_parent != NULL) {
      out_parent[v] = GP_NONE;
    }
  }
  out_dist[source] = 0;
  for (k = 0; k < n; k++) {
    v = order[k];
    if (longest ? !(out_dist[v] > -HUGE_VAL) : !(out_dist[v] < HUGE_VAL)) {
      continue;
    }
    deg = g->degree(g->graph, v);
    for (i = 0; i < deg; i++) {
      w = gp_target(g, v, i, &wt);
      if (w >= n) {
        continue;
      }
      nd = out_dist[v] + wt;
      if (longest ? nd > out_dist[w] : nd < out_dist[w]) {
        out_dist[w] = nd;
        if (out_parent != NULL) {
          out_parent[w] = v;
        }
      }
    }
  }
  ds_context_free(context, order);
  return DS_OK;
}

/* ---- best-first / A* ----------------------------------------------------- */

ds_status_t ds_gview_best_first(const ds_gview_t *g, size_t source,
                                size_t target, ds_gview_heuristic_func_t h,
                                void *user, double g_weight, double *out_dist,
                                size_t *out_parent, size_t *out_expanded,
                                ds_context_t *context) {
  size_t n;
  unsigned char *closed;
  gp_heap_t heap;
  size_t v;
  size_t w;
  size_t i;
  size_t deg;
  size_t expanded = 0;
  double key;
  double wt;
  double nd;
  bool reached = false;
  ds_status_t status;
  if (g == NULL || out_dist == NULL || out_parent == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  if (source >= n || (target != GP_NONE && target >= n)) {
    return DS_ERR_RANGE;
  }
  closed = (unsigned char *)ds_context_calloc(context, n + 1, 1);
  if (closed == NULL) {
    return DS_ERR_ALLOC;
  }
  heap.key = NULL;
  heap.val = NULL;
  heap.size = heap.capacity = 0;
  heap.ctx = context;
  for (v = 0; v < n; v++) {
    out_dist[v] = HUGE_VAL;
    out_parent[v] = GP_NONE;
  }
  out_dist[source] = 0;
  status = gp_heap_push(&heap, h != NULL ? h(source, user) : 0, source);
  while (status == DS_OK && heap.size > 0) {
    gp_heap_pop(&heap, &key, &v);
    if (closed[v]) {
      continue;
    }
    closed[v] = 1;
    expanded++;
    if (v == target) {
      reached = true;
      break;
    }
    deg = g->degree(g->graph, v);
    for (i = 0; i < deg && status == DS_OK; i++) {
      w = gp_target(g, v, i, &wt);
      if (w >= n) {
        continue;
      }
      nd = out_dist[v] + wt;
      if (nd < out_dist[w]) {
        out_dist[w] = nd;
        out_parent[w] = v;
        closed[w] = 0; /* reopen: correct for inconsistent heuristics */
        status = gp_heap_push(&heap,
                              g_weight * nd + (h != NULL ? h(w, user) : 0), w);
      }
    }
  }
  gp_heap_free(&heap);
  ds_context_free(context, closed);
  if (out_expanded != NULL) {
    *out_expanded = expanded;
  }
  if (status != DS_OK) {
    return status;
  }
  return target == GP_NONE || reached ? DS_OK : DS_NOT_FOUND;
}

/* ---- Yen's k shortest paths ---------------------------------------------- */

typedef struct gp_path {
  size_t start;  /* first vertex in the vertex pool */
  size_t length; /* vertices */
  double cost;
} gp_path_t;

typedef struct gp_paths {
  ds_vec_t meta;     /* gp_path_t */
  ds_vec_t vertices; /* size_t */
  ds_vec_t edges;    /* size_t, length - 1 per path, same start */
} gp_paths_t;

static ds_status_t gp_paths_init(gp_paths_t *p, ds_context_t *ctx) {
  ds_status_t s = ds_vec_init(&p->meta, sizeof(gp_path_t), ctx);
  if (s == DS_OK) {
    s = ds_vec_init(&p->vertices, sizeof(size_t), ctx);
  }
  if (s == DS_OK) {
    s = ds_vec_init(&p->edges, sizeof(size_t), ctx);
  }
  return s;
}

static void gp_paths_free(gp_paths_t *p) {
  ds_vec_destroy(&p->meta);
  ds_vec_destroy(&p->vertices);
  ds_vec_destroy(&p->edges);
}

static const gp_path_t *gp_meta(const gp_paths_t *p, size_t i) {
  return (const gp_path_t *)(const void *)(p->meta.data +
                                           i * sizeof(gp_path_t));
}

static size_t *gp_verts(const gp_paths_t *p, size_t start) {
  return (size_t *)(void *)(p->vertices.data + start * sizeof(size_t));
}

static size_t *gp_edges(const gp_paths_t *p, size_t start) {
  return (size_t *)(void *)(p->edges.data + start * sizeof(size_t));
}

/* Append a path given as root (vertices/edges of length rl) followed by a
 * spur (vertices/edges). Edges pool is kept aligned with vertices. */
static ds_status_t gp_paths_add(gp_paths_t *p, const size_t *rv,
                                const size_t *re, size_t rl, const size_t *sv,
                                const size_t *se, size_t sl, double cost) {
  gp_path_t meta;
  size_t zero = 0;
  ds_status_t s;
  meta.start = p->vertices.size;
  meta.length = rl + sl;
  meta.cost = cost;
  s = ds_vec_append(&p->vertices, rv, rl);
  if (s == DS_OK) {
    s = ds_vec_append(&p->vertices, sv, sl);
  }
  if (s == DS_OK && rl > 0) {
    s = ds_vec_append(&p->edges, re, rl);
  }
  if (s == DS_OK && sl > 1) {
    s = ds_vec_append(&p->edges, se, sl - 1);
  }
  if (s == DS_OK) {
    s = ds_vec_push(&p->edges, &zero); /* pad: edges pool = vertices pool */
  }
  if (s == DS_OK) {
    s = ds_vec_push(&p->meta, &meta);
  }
  return s;
}

static bool gp_same_edges(const gp_paths_t *a, size_t i, const gp_paths_t *b,
                          size_t j) {
  const gp_path_t *x = gp_meta(a, i);
  const gp_path_t *y = gp_meta(b, j);
  if (x->length != y->length) {
    return false;
  }
  return memcmp(gp_edges(a, x->start), gp_edges(b, y->start),
                (x->length - 1) * sizeof(size_t)) == 0 &&
         memcmp(gp_verts(a, x->start), gp_verts(b, y->start),
                x->length * sizeof(size_t)) == 0;
}

ds_status_t ds_gview_k_shortest(const ds_gview_t *g, size_t source,
                                size_t target, size_t k,
                                ds_gview_path_func_t visit, void *user,
                                size_t *out_found, ds_context_t *context) {
  size_t n;
  size_t m;
  size_t *offsets = NULL;
  double *dist = NULL;
  size_t *parent = NULL;
  size_t *pedge = NULL;
  unsigned char *done = NULL;
  unsigned char *bv = NULL;
  unsigned char *be = NULL;
  size_t *sv = NULL;
  size_t *se = NULL;
  double *ecost = NULL;
  gp_paths_t A;
  gp_paths_t B;
  size_t found = 0;
  size_t i;
  size_t j;
  size_t q;
  size_t v;
  size_t sl;
  size_t best;
  double rootcost;
  bool dup;
  ds_status_t status;
  if (g == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  if (source >= n || target >= n) {
    return DS_ERR_RANGE;
  }
  status = gp_paths_init(&A, context);
  if (status == DS_OK) {
    status = gp_paths_init(&B, context);
  }
  offsets = (size_t *)gp_alloc(context, n + 1, sizeof(size_t));
  if (status != DS_OK || offsets == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  m = ds_gview_edge_offsets(g, offsets);
  dist = (double *)gp_alloc(context, n, sizeof(double));
  parent = (size_t *)gp_alloc(context, n, sizeof(size_t));
  pedge = (size_t *)gp_alloc(context, n, sizeof(size_t));
  done = (unsigned char *)gp_alloc(context, n, 1);
  bv = (unsigned char *)ds_context_calloc(context, n + 1, 1);
  be = (unsigned char *)ds_context_calloc(context, m + 1, 1);
  sv = (size_t *)gp_alloc(context, n + 1, sizeof(size_t));
  se = (size_t *)gp_alloc(context, n + 1, sizeof(size_t));
  ecost = (double *)gp_alloc(context, m, sizeof(double));
  if (dist == NULL || parent == NULL || pedge == NULL || done == NULL ||
      bv == NULL || be == NULL || sv == NULL || se == NULL || ecost == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  for (v = 0; v < n; v++) {
    for (i = 0; i < g->degree(g->graph, v); i++) {
      (void)gp_target(g, v, i, &ecost[offsets[v] + i]);
    }
  }
  if (k == 0) {
    goto out;
  }

#define GP_SPUR(from)                                                          \
  do {                                                                         \
    status = gp_dijkstra(g, (from), target, NULL, bv, be, offsets, dist,       \
                         parent, pedge, done, context);                        \
    sl = 0;                                                                    \
    if (status == DS_OK && dist[target] < HUGE_VAL) {                          \
      for (v = target; v != GP_NONE; v = parent[v]) {                          \
        sl++;                                                                  \
      }                                                                        \
      q = sl;                                                                  \
      for (v = target; v != GP_NONE; v = parent[v]) {                          \
        q--;                                                                   \
        sv[q] = v;                                                             \
        if (q > 0) {                                                           \
          se[q - 1] = pedge[v];                                                \
        }                                                                      \
      }                                                                        \
    }                                                                          \
  } while (0)

  GP_SPUR(source);
  if (status != DS_OK || sl == 0) {
    goto out;
  }
  status = gp_paths_add(&A, NULL, NULL, 0, sv, se, sl, dist[target]);
  found = 1;
  while (status == DS_OK && found < k) {
    const gp_path_t *prev = gp_meta(&A, found - 1);
    size_t prev_start = prev->start;
    size_t prev_len = prev->length;
    rootcost = 0;
    for (i = 0; i + 1 < prev_len && status == DS_OK; i++) {
      size_t spur = gp_verts(&A, prev_start)[i];
      memset(bv, 0, n);
      memset(be, 0, m);
      for (j = 0; j < i; j++) {
        bv[gp_verts(&A, prev_start)[j]] = 1;
      }
      /* block the next edge of every accepted path sharing this root */
      for (j = 0; j < found; j++) {
        const gp_path_t *p = gp_meta(&A, j);
        if (p->length > i + 1 &&
            memcmp(gp_verts(&A, p->start), gp_verts(&A, prev_start),
                   (i + 1) * sizeof(size_t)) == 0 &&
            (i == 0 || memcmp(gp_edges(&A, p->start), gp_edges(&A, prev_start),
                              i * sizeof(size_t)) == 0)) {
          be[gp_edges(&A, p->start)[i]] = 1;
        }
      }
      GP_SPUR(spur);
      if (status == DS_OK && sl > 0) {
        status =
            gp_paths_add(&B, gp_verts(&A, prev_start), gp_edges(&A, prev_start),
                         i, sv, se, sl, rootcost + dist[target]);
        if (status == DS_OK) {
          /* drop duplicates of accepted or pending paths */
          dup = false;
          for (j = 0; j < found && !dup; j++) {
            dup = gp_same_edges(&B, B.meta.size - 1, &A, j);
          }
          for (j = 0; j + 1 < B.meta.size && !dup; j++) {
            dup = gp_same_edges(&B, B.meta.size - 1, &B, j);
          }
          if (dup) {
            B.meta.size--;
          }
        }
      }
      rootcost += ecost[gp_edges(&A, prev_start)[i]];
    }
    if (status != DS_OK || B.meta.size == 0) {
      break;
    }
    best = 0;
    for (j = 1; j < B.meta.size; j++) {
      if (gp_meta(&B, j)->cost < gp_meta(&B, best)->cost) {
        best = j;
      }
    }
    {
      const gp_path_t *bp = gp_meta(&B, best);
      status = gp_paths_add(&A, NULL, NULL, 0, gp_verts(&B, bp->start),
                            gp_edges(&B, bp->start), bp->length, bp->cost);
      memcpy(B.meta.data + best * sizeof(gp_path_t),
             B.meta.data + (B.meta.size - 1) * sizeof(gp_path_t),
             sizeof(gp_path_t));
      B.meta.size--;
    }
    found++;
  }
#undef GP_SPUR
  if (status == DS_OK) {
    for (j = 0; j < found && status == DS_OK; j++) {
      const gp_path_t *p = gp_meta(&A, j);
      status = visit(gp_verts(&A, p->start), p->length, p->cost, user);
    }
  }
out:
  if (out_found != NULL) {
    *out_found = found;
  }
  gp_paths_free(&A);
  gp_paths_free(&B);
  ds_context_free(context, offsets);
  ds_context_free(context, dist);
  ds_context_free(context, parent);
  ds_context_free(context, pedge);
  ds_context_free(context, done);
  ds_context_free(context, bv);
  ds_context_free(context, be);
  ds_context_free(context, sv);
  ds_context_free(context, se);
  ds_context_free(context, ecost);
  return status == DS_STOP ? DS_OK : status;
}

/* ---- all simple paths ---------------------------------------------------- */

ds_status_t ds_gview_simple_paths(const ds_gview_t *g, size_t source,
                                  size_t target, ds_gview_path_func_t visit,
                                  void *user, ds_context_t *context) {
  size_t n;
  size_t *stack;
  size_t *it;
  double *cost;
  unsigned char *on;
  size_t sp;
  size_t v;
  size_t w;
  double wt;
  ds_status_t status = DS_OK;
  if (g == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  if (source >= n || target >= n) {
    return DS_ERR_RANGE;
  }
  stack = (size_t *)gp_alloc(context, n, sizeof(size_t));
  it = (size_t *)gp_alloc(context, n, sizeof(size_t));
  cost = (double *)gp_alloc(context, n, sizeof(double));
  on = (unsigned char *)ds_context_calloc(context, n + 1, 1);
  if (stack == NULL || it == NULL || cost == NULL || on == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  sp = 0;
  stack[sp] = source;
  it[sp] = 0;
  cost[sp] = 0;
  sp++;
  on[source] = 1;
  if (source == target) {
    status = visit(stack, 1, 0, user);
    goto out;
  }
  while (sp > 0) {
    v = stack[sp - 1];
    if (it[sp - 1] >= g->degree(g->graph, v)) {
      on[v] = 0;
      sp--;
      continue;
    }
    w = gp_target(g, v, it[sp - 1]++, &wt);
    if (w >= n || on[w]) {
      continue;
    }
    if (w == target) {
      stack[sp] = w;
      status = visit(stack, sp + 1, cost[sp - 1] + wt, user);
      if (status != DS_OK) {
        break;
      }
      continue;
    }
    stack[sp] = w;
    it[sp] = 0;
    cost[sp] = cost[sp - 1] + wt;
    on[w] = 1;
    sp++;
  }
out:
  ds_context_free(context, stack);
  ds_context_free(context, it);
  ds_context_free(context, cost);
  ds_context_free(context, on);
  return status == DS_STOP ? DS_OK : status;
}

/* ---- minimum spanning arborescence (Chu-Liu/Edmonds) --------------------- */

typedef struct gp_arc {
  size_t from;
  size_t to;
  double w;
  size_t prev; /* index of this arc at the previous level */
} gp_arc_t;

typedef struct gp_level {
  gp_arc_t *arcs;
  size_t arc_count;
  size_t n;
  size_t root;
  size_t *in;    /* chosen in-arc per vertex (this level) */
  size_t *id;    /* vertex -> vertex of the next level */
  size_t *cycle; /* vertex -> cycle number, GP_NONE if none */
} gp_level_t;

ds_status_t ds_gview_min_arborescence(const ds_gview_t *g, size_t root,
                                      size_t *out_parent_edge,
                                      double *out_total,
                                      ds_context_t *context) {
  size_t n0;
  size_t m0;
  size_t *offsets = NULL;
  gp_level_t *levels = NULL;
  size_t depth = 0;
  size_t cap_levels;
  size_t *sel = NULL;           /* selected arcs at the current level */
  unsigned char *chosen = NULL; /* per arc of a level */
  size_t *mark = NULL;
  size_t v;
  size_t e;
  size_t i;
  size_t nn;
  size_t cycles;
  size_t x;
  size_t sel_count;
  double total = 0;
  ds_status_t status = DS_OK;
  gp_level_t *L;
  gp_level_t *P;

  if (g == NULL || out_parent_edge == NULL) {
    return DS_ERR_NULL;
  }
  n0 = g->vertex_count;
  if (root >= n0) {
    return DS_ERR_RANGE;
  }
  offsets = (size_t *)gp_alloc(context, n0 + 1, sizeof(size_t));
  if (offsets == NULL) {
    return DS_ERR_ALLOC;
  }
  m0 = ds_gview_edge_offsets(g, offsets);
  cap_levels = n0 + 1;
  levels =
      (gp_level_t *)ds_context_calloc(context, cap_levels, sizeof(gp_level_t));
  mark = (size_t *)gp_alloc(context, n0, sizeof(size_t));
  if (levels == NULL || mark == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  L = &levels[0];
  L->n = n0;
  L->root = root;
  L->arcs = (gp_arc_t *)gp_alloc(context, m0, sizeof(gp_arc_t));
  if (L->arcs == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  for (v = 0; v < n0; v++) {
    for (i = 0; i < g->degree(g->graph, v); i++) {
      e = offsets[v] + i;
      L->arcs[e].from = v;
      L->arcs[e].to = gp_target(g, v, i, &L->arcs[e].w);
      L->arcs[e].prev = e;
      if (L->arcs[e].to >= n0) {
        L->arcs[e].to = v; /* ignore out-of-range targets as self-loops */
      }
    }
  }
  L->arc_count = m0;

  for (;;) {
    L = &levels[depth];
    L->in = (size_t *)gp_alloc(context, L->n, sizeof(size_t));
    L->id = (size_t *)gp_alloc(context, L->n, sizeof(size_t));
    L->cycle = (size_t *)gp_alloc(context, L->n, sizeof(size_t));
    if (L->in == NULL || L->id == NULL || L->cycle == NULL) {
      status = DS_ERR_ALLOC;
      goto out;
    }
    for (v = 0; v < L->n; v++) {
      L->in[v] = GP_NONE;
      L->id[v] = GP_NONE;
      L->cycle[v] = GP_NONE;
      mark[v] = GP_NONE;
    }
    for (e = 0; e < L->arc_count; e++) {
      gp_arc_t *a = &L->arcs[e];
      if (a->from != a->to && a->to != L->root &&
          (L->in[a->to] == GP_NONE || a->w < L->arcs[L->in[a->to]].w)) {
        L->in[a->to] = e;
      }
    }
    for (v = 0; v < L->n; v++) {
      if (v != L->root && L->in[v] == GP_NONE) {
        status = DS_NOT_FOUND;
        goto out;
      }
    }
    /* find cycles of the chosen in-arcs */
    cycles = 0;
    nn = 0;
    for (v = 0; v < L->n; v++) {
      x = v;
      while (x != L->root && mark[x] == GP_NONE && L->id[x] == GP_NONE) {
        mark[x] = v;
        x = L->arcs[L->in[x]].from;
      }
      if (x != L->root && mark[x] == v && L->id[x] == GP_NONE) {
        size_t y = x;
        do {
          L->id[y] = nn;
          L->cycle[y] = cycles;
          y = L->arcs[L->in[y]].from;
        } while (y != x);
        nn++;
        cycles++;
      }
    }
    if (cycles == 0) {
      break;
    }
    for (v = 0; v < L->n; v++) {
      if (L->id[v] == GP_NONE) {
        L->id[v] = nn++;
      }
    }
    if (depth + 1 >= cap_levels) {
      status = DS_ERR_INTERNAL;
      goto out;
    }
    P = &levels[depth + 1];
    P->n = nn;
    P->root = L->id[L->root];
    P->arcs = (gp_arc_t *)gp_alloc(context, L->arc_count, sizeof(gp_arc_t));
    if (P->arcs == NULL) {
      status = DS_ERR_ALLOC;
      goto out;
    }
    P->arc_count = 0;
    for (e = 0; e < L->arc_count; e++) {
      gp_arc_t *a = &L->arcs[e];
      size_t fu = L->id[a->from];
      size_t tv = L->id[a->to];
      if (fu == tv) {
        continue;
      }
      P->arcs[P->arc_count].from = fu;
      P->arcs[P->arc_count].to = tv;
      P->arcs[P->arc_count].w =
          a->to == L->root ? a->w : a->w - L->arcs[L->in[a->to]].w;
      P->arcs[P->arc_count].prev = e;
      P->arc_count++;
    }
    depth++;
  }

  /* expand from the top level down */
  sel = (size_t *)gp_alloc(context, n0, sizeof(size_t));
  chosen = (unsigned char *)gp_alloc(context, m0 + 1, 1);
  if (sel == NULL || chosen == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  L = &levels[depth];
  sel_count = 0;
  for (v = 0; v < L->n; v++) {
    if (v != L->root) {
      sel[sel_count++] = L->in[v];
    }
  }
  while (depth > 0) {
    P = &levels[depth];     /* where sel lives */
    L = &levels[depth - 1]; /* the level being reconstructed */
    memset(chosen, 0, L->arc_count);
    for (i = 0; i < sel_count; i++) {
      size_t le = P->arcs[sel[i]].prev;
      chosen[le] = 1;
      x = L->arcs[le].to;
      if (L->cycle[x] != GP_NONE) {
        mark[L->cycle[x]] = x; /* the cycle is entered at x */
      }
    }
    for (v = 0; v < L->n; v++) {
      if (L->cycle[v] != GP_NONE && mark[L->cycle[v]] != v) {
        chosen[L->in[v]] = 1;
      }
    }
    sel_count = 0;
    for (e = 0; e < L->arc_count; e++) {
      if (chosen[e]) {
        sel[sel_count++] = e;
      }
    }
    depth--;
  }
  for (v = 0; v < n0; v++) {
    out_parent_edge[v] = GP_NONE;
  }
  for (i = 0; i < sel_count; i++) {
    e = sel[i];
    out_parent_edge[levels[0].arcs[e].to] = e;
    total += levels[0].arcs[e].w;
  }
  if (out_total != NULL) {
    *out_total = total;
  }
out:
  if (levels != NULL) {
    for (i = 0; i < cap_levels; i++) {
      ds_context_free(context, levels[i].arcs);
      ds_context_free(context, levels[i].in);
      ds_context_free(context, levels[i].id);
      ds_context_free(context, levels[i].cycle);
    }
  }
  ds_context_free(context, levels);
  ds_context_free(context, offsets);
  ds_context_free(context, mark);
  ds_context_free(context, sel);
  ds_context_free(context, chosen);
  return status;
}

/* ---- minimum spanning forests -------------------------------------------- */

static size_t gp_find(size_t *parent, size_t x) {
  while (parent[x] != x) {
    parent[x] = parent[parent[x]];
    x = parent[x];
  }
  return x;
}

typedef struct gp_wedge {
  double w;
  size_t index;
} gp_wedge_t;

static int gp_wedge_cmp(const void *a, const void *b) {
  const gp_wedge_t *x = (const gp_wedge_t *)a;
  const gp_wedge_t *y = (const gp_wedge_t *)b;
  if (x->w < y->w) {
    return -1;
  }
  if (x->w > y->w) {
    return 1;
  }
  return x->index < y->index ? -1 : (x->index > y->index);
}

ds_status_t ds_kruskal(size_t n, const size_t *u, const size_t *v,
                       const double *w, size_t m, unsigned char *out_in_forest,
                       double *out_total, ds_context_t *context) {
  size_t *parent;
  gp_wedge_t *order;
  size_t i;
  size_t a;
  size_t b;
  double total = 0;
  if (out_in_forest == NULL ||
      (m > 0 && (u == NULL || v == NULL || w == NULL))) {
    return DS_ERR_NULL;
  }
  parent = (size_t *)gp_alloc(context, n, sizeof(size_t));
  order = (gp_wedge_t *)gp_alloc(context, m, sizeof(gp_wedge_t));
  if (parent == NULL || order == NULL) {
    ds_context_free(context, parent);
    ds_context_free(context, order);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    parent[i] = i;
  }
  for (i = 0; i < m; i++) {
    if (u[i] >= n || v[i] >= n) {
      ds_context_free(context, parent);
      ds_context_free(context, order);
      return DS_ERR_RANGE;
    }
    order[i].w = w[i];
    order[i].index = i;
    out_in_forest[i] = 0;
  }
  qsort(order, m, sizeof(gp_wedge_t), gp_wedge_cmp);
  for (i = 0; i < m; i++) {
    a = gp_find(parent, u[order[i].index]);
    b = gp_find(parent, v[order[i].index]);
    if (a != b) {
      parent[a] = b;
      out_in_forest[order[i].index] = 1;
      total += order[i].w;
    }
  }
  ds_context_free(context, parent);
  ds_context_free(context, order);
  if (out_total != NULL) {
    *out_total = total;
  }
  return DS_OK;
}

/* Strict total order on edges: weight, then index (breaks ties so that
 * Boruvka never closes a cycle). */
static bool gp_lighter(const double *w, size_t a, size_t b) {
  if (w[a] < w[b]) {
    return true;
  }
  if (w[a] > w[b]) {
    return false;
  }
  return a < b;
}

ds_status_t ds_boruvka(size_t n, const size_t *u, const size_t *v,
                       const double *w, size_t m, unsigned char *out_in_forest,
                       double *out_total, ds_context_t *context) {
  size_t *parent;
  size_t *cheapest;
  size_t i;
  size_t a;
  size_t b;
  size_t e;
  bool progress = true;
  double total = 0;
  if (out_in_forest == NULL ||
      (m > 0 && (u == NULL || v == NULL || w == NULL))) {
    return DS_ERR_NULL;
  }
  parent = (size_t *)gp_alloc(context, n, sizeof(size_t));
  cheapest = (size_t *)gp_alloc(context, n, sizeof(size_t));
  if (parent == NULL || cheapest == NULL) {
    ds_context_free(context, parent);
    ds_context_free(context, cheapest);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    parent[i] = i;
  }
  for (i = 0; i < m; i++) {
    if (u[i] >= n || v[i] >= n) {
      ds_context_free(context, parent);
      ds_context_free(context, cheapest);
      return DS_ERR_RANGE;
    }
    out_in_forest[i] = 0;
  }
  while (progress) {
    progress = false;
    for (i = 0; i < n; i++) {
      cheapest[i] = GP_NONE;
    }
    for (e = 0; e < m; e++) {
      a = gp_find(parent, u[e]);
      b = gp_find(parent, v[e]);
      if (a == b) {
        continue;
      }
      if (cheapest[a] == GP_NONE || gp_lighter(w, e, cheapest[a])) {
        cheapest[a] = e;
      }
      if (cheapest[b] == GP_NONE || gp_lighter(w, e, cheapest[b])) {
        cheapest[b] = e;
      }
    }
    for (i = 0; i < n; i++) {
      e = cheapest[i];
      if (e == GP_NONE) {
        continue;
      }
      a = gp_find(parent, u[e]);
      b = gp_find(parent, v[e]);
      if (a != b) {
        parent[a] = b;
        out_in_forest[e] = 1;
        total += w[e];
        progress = true;
      }
    }
  }
  ds_context_free(context, parent);
  ds_context_free(context, cheapest);
  if (out_total != NULL) {
    *out_total = total;
  }
  return DS_OK;
}

/* ---- Chinese postman ----------------------------------------------------- */

typedef struct gp_ugraph {
  size_t n;
  const size_t *u;
  const size_t *v;
  const double *w;
  size_t *off;
  size_t *adj; /* edge ids, both endpoints */
} gp_ugraph_t;

static size_t gp_udeg(const void *graph, size_t x) {
  const gp_ugraph_t *ug = (const gp_ugraph_t *)graph;
  return ug->off[x + 1] - ug->off[x];
}

static void gp_uedge(const void *graph, size_t x, size_t i, size_t *to,
                     double *wt) {
  const gp_ugraph_t *ug = (const gp_ugraph_t *)graph;
  size_t e = ug->adj[ug->off[x] + i];
  *to = ug->u[e] == x ? ug->v[e] : ug->u[e];
  *wt = ug->w[e];
}

ds_status_t ds_chinese_postman(size_t n, const size_t *u, const size_t *v,
                               const double *w, size_t m, size_t start,
                               size_t *out_route, size_t capacity,
                               size_t *out_route_length, double *out_cost,
                               ds_context_t *context) {
  gp_ugraph_t ug;
  ds_gview_t view;
  size_t *fill = NULL;
  size_t odd[DS_CHINESE_POSTMAN_MAX_ODD];
  size_t k = 0;
  size_t i;
  size_t j;
  size_t x;
  double *pd = NULL;   /* k x k pair distances */
  size_t *ppar = NULL; /* k x n parent edges (view-local edge index) */
  double *dp = NULL;
  size_t *choice = NULL;
  double *dist = NULL;
  size_t *parent = NULL;
  size_t *pedge = NULL;
  unsigned char *done = NULL;
  size_t *eu = NULL;
  size_t *ev = NULL;
  size_t mm;
  size_t full;
  size_t mask;
  double total = 0;
  ds_status_t status = DS_OK;
  if (out_route_length == NULL || out_cost == NULL ||
      (m > 0 && (u == NULL || v == NULL || w == NULL))) {
    return DS_ERR_NULL;
  }
  if (n == 0 || start >= n) {
    return DS_ERR_RANGE;
  }
  ug.n = n;
  ug.u = u;
  ug.v = v;
  ug.w = w;
  ug.off = (size_t *)ds_context_calloc(context, n + 1, sizeof(size_t));
  ug.adj = (size_t *)gp_alloc(context, 2 * m, sizeof(size_t));
  fill = (size_t *)gp_alloc(context, n, sizeof(size_t));
  if (ug.off == NULL || ug.adj == NULL || fill == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  for (i = 0; i < m; i++) {
    if (u[i] >= n || v[i] >= n || !(w[i] >= 0)) {
      status = u[i] >= n || v[i] >= n ? DS_ERR_RANGE : DS_ERR_INVALID;
      goto out;
    }
    ug.off[u[i] + 1]++;
    ug.off[v[i] + 1]++;
    total += w[i];
  }
  for (x = 0; x < n; x++) {
    ug.off[x + 1] += ug.off[x];
    fill[x] = ug.off[x];
  }
  for (i = 0; i < m; i++) {
    ug.adj[fill[u[i]]++] = i;
    ug.adj[fill[v[i]]++] = i;
  }
  for (x = 0; x < n; x++) {
    if ((ug.off[x + 1] - ug.off[x]) & 1U) {
      if (k == DS_CHINESE_POSTMAN_MAX_ODD) {
        status = DS_ERR_UNSUPPORTED;
        goto out;
      }
      odd[k++] = x;
    }
  }
  view.vertex_count = n;
  view.degree = gp_udeg;
  view.edge = gp_uedge;
  view.graph = &ug;
  dist = (double *)gp_alloc(context, n, sizeof(double));
  parent = (size_t *)gp_alloc(context, n, sizeof(size_t));
  pedge = (size_t *)gp_alloc(context, n, sizeof(size_t));
  done = (unsigned char *)gp_alloc(context, n, 1);
  pd = (double *)gp_alloc(context, k * k, sizeof(double));
  ppar = (size_t *)gp_alloc(context, k * n, sizeof(size_t));
  full = ((size_t)1 << k) - 1;
  dp = (double *)gp_alloc(context, full + 1, sizeof(double));
  choice = (size_t *)gp_alloc(context, full + 1, sizeof(size_t));
  if (dist == NULL || parent == NULL || pedge == NULL || done == NULL ||
      pd == NULL || ppar == NULL || dp == NULL || choice == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  for (i = 0; i < k && status == DS_OK; i++) {
    status = gp_dijkstra(&view, odd[i], GP_NONE, NULL, NULL, NULL, ug.off, dist,
                         parent, pedge, done, context);
    for (j = 0; j < k; j++) {
      pd[i * k + j] = dist[odd[j]];
    }
    for (x = 0; x < n; x++) {
      ppar[i * n + x] = pedge[x];
    }
  }
  if (status != DS_OK) {
    goto out;
  }
  /* pair the odd vertices: dp over subsets, lowest vertex first */
  dp[0] = 0;
  for (mask = 1; mask <= full; mask++) {
    size_t lo = 0;
    dp[mask] = HUGE_VAL;
    choice[mask] = GP_NONE;
    {
      size_t bits = 0;
      size_t t = mask;
      while (t) {
        bits += t & 1U;
        t >>= 1;
      }
      if (bits & 1U) {
        continue;
      }
    }
    while (!((mask >> lo) & 1U)) {
      lo++;
    }
    for (j = lo + 1; j < k; j++) {
      size_t rest;
      double c;
      if (!((mask >> j) & 1U)) {
        continue;
      }
      rest = mask & ~(((size_t)1 << lo) | ((size_t)1 << j));
      c = dp[rest] + pd[lo * k + j];
      if (c < dp[mask]) {
        dp[mask] = c;
        choice[mask] = j;
      }
    }
  }
  if (k > 0 && !(dp[full] < HUGE_VAL)) {
    status = DS_NOT_FOUND; /* odd vertices in different components */
    goto out;
  }
  *out_cost = total + (k > 0 ? dp[full] : 0);
  /* augmented edge list: original edges plus the pairing paths */
  mm = m;
  eu = (size_t *)gp_alloc(context, m + k / 2 * n + 1, sizeof(size_t));
  ev = (size_t *)gp_alloc(context, m + k / 2 * n + 1, sizeof(size_t));
  if (eu == NULL || ev == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  memcpy(eu, u, m * sizeof(size_t));
  memcpy(ev, v, m * sizeof(size_t));
  mask = full;
  while (k > 0 && mask != 0) {
    size_t lo = 0;
    size_t hi;
    while (!((mask >> lo) & 1U)) {
      lo++;
    }
    hi = choice[mask];
    /* walk back from odd[hi] to odd[lo] along lo's shortest path tree */
    x = odd[hi];
    while (x != odd[lo]) {
      size_t ge = ppar[lo * n + x]; /* global (view) edge index */
      size_t e = ug.adj[ge];        /* original edge id */
      eu[mm] = u[e];
      ev[mm] = v[e];
      mm++;
      x = u[e] == x ? v[e] : u[e];
    }
    mask &= ~(((size_t)1 << lo) | ((size_t)1 << hi));
  }
  *out_route_length = mm + 1;
  if (out_route != NULL) {
    if (capacity < mm + 1) {
      status = DS_ERR_OVERFLOW;
      goto out;
    }
    status = ds_euler_undirected(n, eu, ev, mm, start, out_route,
                                 out_route_length, context);
    if (status == DS_OK && mm > 0 && (ug.off[start + 1] == ug.off[start])) {
      status = DS_NOT_FOUND; /* start not on any edge */
    }
  }
out:
  ds_context_free(context, ug.off);
  ds_context_free(context, ug.adj);
  ds_context_free(context, fill);
  ds_context_free(context, pd);
  ds_context_free(context, ppar);
  ds_context_free(context, dp);
  ds_context_free(context, choice);
  ds_context_free(context, dist);
  ds_context_free(context, parent);
  ds_context_free(context, pedge);
  ds_context_free(context, done);
  ds_context_free(context, eu);
  ds_context_free(context, ev);
  return status;
}
