#include "graph_conn.h"
#include <string.h>

#define GC_NONE DS_GVIEW_NONE

ds_status_t ds_greverse_build(ds_greverse_t *r, const ds_gview_t *g,
                              ds_context_t *context) {
  size_t n;
  size_t m = 0;
  size_t v;
  size_t i;
  size_t d;
  size_t t;
  double w;
  size_t *cur;
  if (r == NULL || g == NULL || g->degree == NULL || g->edge == NULL) {
    return DS_ERR_NULL;
  }
  memset(r, 0, sizeof(*r));
  r->context = context;
  n = g->vertex_count;
  for (v = 0; v < n; v++) {
    d = g->degree(g->graph, v);
    if (d > ((size_t)-1) - m) {
      return DS_ERR_OVERFLOW;
    }
    m += d;
  }
  if (n > ((size_t)-1) / sizeof(size_t) / 2 - 1 ||
      m > ((size_t)-1) / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  r->offsets = (size_t *)ds_context_calloc(context, 2 * n + 2, sizeof(size_t));
  r->targets =
      (size_t *)ds_context_alloc(context, (m > 0 ? m : 1) * sizeof(size_t));
  r->weights =
      (double *)ds_context_alloc(context, (m > 0 ? m : 1) * sizeof(double));
  if (r->offsets == NULL || r->targets == NULL || r->weights == NULL) {
    ds_greverse_destroy(r);
    return DS_ERR_ALLOC;
  }
  cur = r->offsets + n + 1;
  for (v = 0; v < n; v++) {
    d = g->degree(g->graph, v);
    for (i = 0; i < d; i++) {
      g->edge(g->graph, v, i, &t, &w);
      if (t >= n) {
        ds_greverse_destroy(r);
        return DS_ERR_RANGE;
      }
      r->offsets[t + 1]++;
    }
  }
  for (v = 0; v < n; v++) {
    r->offsets[v + 1] += r->offsets[v];
    cur[v] = r->offsets[v];
  }
  for (v = 0; v < n; v++) {
    d = g->degree(g->graph, v);
    for (i = 0; i < d; i++) {
      g->edge(g->graph, v, i, &t, &w);
      r->targets[cur[t]] = v;
      r->weights[cur[t]++] = w;
    }
  }
  r->csr.vertex_count = n;
  r->csr.offsets = r->offsets;
  r->csr.targets = r->targets;
  r->csr.weights = r->weights;
  ds_gview_from_csr(&r->csr, &r->view);
  return DS_OK;
}

void ds_greverse_destroy(ds_greverse_t *r) {
  if (r == NULL) {
    return;
  }
  ds_context_free(r->context, r->offsets);
  ds_context_free(r->context, r->targets);
  ds_context_free(r->context, r->weights);
  r->offsets = r->targets = NULL;
  r->weights = NULL;
}

ds_status_t ds_gview_single_destination(const ds_gview_t *g, size_t target,
                                        double *dist, size_t *next_hop,
                                        ds_context_t *context) {
  ds_greverse_t r;
  ds_status_t st;
  if (g == NULL || dist == NULL) {
    return DS_ERR_NULL;
  }
  if (target >= g->vertex_count) {
    return DS_ERR_RANGE;
  }
  st = ds_greverse_build(&r, g, context);
  if (st != DS_OK) {
    return st;
  }
  /* parents in the reverse tree are the next hops in the original graph */
  st = ds_gview_dijkstra(&r.view, target, dist, next_hop, context);
  ds_greverse_destroy(&r);
  return st;
}

/* Articulation points of the graph without vertex `gone` (or none). */
typedef struct gc_art {
  const ds_gview_t *g;
  size_t gone;
  size_t *disc;
  size_t *low;
  size_t *stack; /* vertex */
  size_t *edge;  /* next edge index per stacked vertex */
  size_t *parent;
  unsigned char *art;
} gc_art_t;

/* Iterative Tarjan low-link from root; returns vertices reached. */
static size_t gc_dfs(gc_art_t *a, size_t root, size_t *timer) {
  size_t top = 0;
  size_t v;
  size_t t;
  size_t d;
  size_t root_children = 0;
  size_t reached = 1;
  double w;
  a->disc[root] = a->low[root] = (*timer)++;
  a->parent[root] = GC_NONE;
  a->stack[top] = root;
  a->edge[top++] = 0;
  while (top > 0) {
    v = a->stack[top - 1];
    d = a->g->degree(a->g->graph, v);
    if (a->edge[top - 1] < d) {
      a->g->edge(a->g->graph, v, a->edge[top - 1]++, &t, &w);
      if (t == a->gone || t == v) {
        continue;
      }
      if (a->disc[t] == GC_NONE) {
        a->disc[t] = a->low[t] = (*timer)++;
        a->parent[t] = v;
        a->stack[top] = t;
        a->edge[top++] = 0;
        reached++;
        if (v == root) {
          root_children++;
        }
      } else if (t != a->parent[v] && a->disc[t] < a->low[v]) {
        a->low[v] = a->disc[t];
      }
    } else {
      top--;
      if (top > 0) {
        size_t p = a->stack[top - 1];
        if (a->low[v] < a->low[p]) {
          a->low[p] = a->low[v];
        }
        if (p != root && a->low[v] >= a->disc[p]) {
          a->art[p] = 1;
        }
      }
    }
  }
  if (root_children > 1) {
    a->art[root] = 1;
  }
  return reached;
}

/* Fills a->art for G - gone; *components counts the components. */
static void gc_articulation(gc_art_t *a, size_t n, size_t *components) {
  size_t v;
  size_t timer = 0;
  for (v = 0; v < n; v++) {
    a->disc[v] = GC_NONE;
    a->art[v] = 0;
  }
  *components = 0;
  for (v = 0; v < n; v++) {
    if (v != a->gone && a->disc[v] == GC_NONE) {
      (void)gc_dfs(a, v, &timer);
      (*components)++;
    }
  }
}

static ds_status_t gc_setup(gc_art_t *a, const ds_gview_t *g, void **block,
                            ds_context_t *context) {
  size_t n = g->vertex_count;
  if (n > ((size_t)-1) / (6 * sizeof(size_t))) {
    return DS_ERR_OVERFLOW;
  }
  *block =
      ds_context_alloc(context, (n > 0 ? n : 1) * (5 * sizeof(size_t) + 1));
  if (*block == NULL) {
    return DS_ERR_ALLOC;
  }
  a->g = g;
  a->disc = (size_t *)*block;
  a->low = a->disc + n;
  a->stack = a->low + n;
  a->edge = a->stack + n;
  a->parent = a->edge + n;
  a->art = (unsigned char *)(a->parent + n);
  return DS_OK;
}

ds_status_t ds_gview_separation_pairs(
    const ds_gview_t *g, bool (*visit)(size_t u, size_t v, void *user),
    void *user, size_t *out_count, ds_context_t *context) {
  gc_art_t a;
  void *block;
  size_t n;
  size_t v;
  size_t u;
  size_t comps;
  size_t count = 0;
  ds_status_t st;
  if (g == NULL || g->degree == NULL || g->edge == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  st = gc_setup(&a, g, &block, context);
  if (st != DS_OK) {
    return st;
  }
  for (v = 0; v < n && st == DS_OK; v++) {
    a.gone = v;
    gc_articulation(&a, n, &comps);
    for (u = v + 1; u < n; u++) {
      if (a.art[u]) {
        count++;
        if (visit != NULL && !visit(v, u, user)) {
          st = DS_STOP;
          break;
        }
      }
    }
  }
  ds_context_free(context, block);
  if (out_count != NULL) {
    *out_count = count;
  }
  return st;
}

ds_status_t ds_gview_is_triconnected(const ds_gview_t *g, bool *out_result,
                                     ds_context_t *context) {
  gc_art_t a;
  void *block;
  size_t n;
  size_t v;
  size_t u;
  size_t comps;
  ds_status_t st;
  if (g == NULL || out_result == NULL || g->degree == NULL || g->edge == NULL) {
    return DS_ERR_NULL;
  }
  *out_result = false;
  n = g->vertex_count;
  if (n < 4) {
    return DS_OK;
  }
  st = gc_setup(&a, g, &block, context);
  if (st != DS_OK) {
    return st;
  }
  /* connected and without articulation points */
  a.gone = GC_NONE;
  gc_articulation(&a, n, &comps);
  *out_result = comps == 1;
  for (u = 0; u < n && *out_result; u++) {
    if (a.art[u]) {
      *out_result = false;
    }
  }
  /* and no separation pair */
  for (v = 0; v < n && *out_result; v++) {
    a.gone = v;
    gc_articulation(&a, n, &comps);
    for (u = 0; u < n; u++) {
      if (u != v && a.art[u]) {
        *out_result = false;
        break;
      }
    }
  }
  ds_context_free(context, block);
  return DS_OK;
}
