#include "graph_view.h"
#include "../lib/indexed_heap.h"
#include <math.h>
#include <string.h>

#define G_NONE DS_GVIEW_NONE

static size_t csr_degree(const void *graph, size_t v) {
  const ds_gview_csr_t *c = (const ds_gview_csr_t *)graph;
  return c->offsets[v + 1] - c->offsets[v];
}

static void csr_edge(const void *graph, size_t v, size_t i, size_t *to,
                     double *w) {
  const ds_gview_csr_t *c = (const ds_gview_csr_t *)graph;
  size_t e = c->offsets[v] + i;
  *to = c->targets[e];
  *w = c->weights != NULL ? c->weights[e] : 1.0;
}

void ds_gview_from_csr(const ds_gview_csr_t *csr, ds_gview_t *out_view) {
  out_view->vertex_count = csr->vertex_count;
  out_view->degree = csr_degree;
  out_view->edge = csr_edge;
  out_view->graph = csr;
}

static size_t am_view_degree(const void *graph, size_t v) {
  return ds_adj_matrix_degree((const ds_adj_matrix_t *)graph, v);
}

static void am_view_edge(const void *graph, size_t v, size_t i, size_t *to,
                         double *w) {
  ds_adj_matrix_edge((const ds_adj_matrix_t *)graph, v, i, to, w);
}

void ds_gview_from_adj_matrix(const ds_adj_matrix_t *matrix,
                              ds_gview_t *out_view) {
  out_view->vertex_count = ds_adj_matrix_vertex_count(matrix);
  out_view->degree = am_view_degree;
  out_view->edge = am_view_edge;
  out_view->graph = matrix;
}

static void *g_alloc(ds_context_t *context, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > (~(size_t)0) / size) {
    return NULL;
  }
  return ds_context_alloc(context, count * size);
}

static void g_edge(const ds_gview_t *g, size_t v, size_t i, size_t *to,
                   double *w) {
  double weight = 1.0;
  g->edge(g->graph, v, i, to, &weight);
  if (w != NULL) {
    *w = weight;
  }
}

ds_status_t ds_gview_bfs(const ds_gview_t *g, size_t source, size_t *out_hops,
                         size_t *out_parent, ds_context_t *context) {
  size_t *queue;
  size_t head = 0;
  size_t tail = 0;
  size_t v;
  size_t i;
  size_t d;
  size_t to;
  if (g == NULL || out_hops == NULL) {
    return DS_ERR_NULL;
  }
  if (source >= g->vertex_count) {
    return DS_ERR_RANGE;
  }
  queue = (size_t *)g_alloc(context, g->vertex_count, sizeof(size_t));
  if (queue == NULL) {
    return DS_ERR_ALLOC;
  }
  for (v = 0; v < g->vertex_count; v++) {
    out_hops[v] = G_NONE;
    if (out_parent != NULL) {
      out_parent[v] = G_NONE;
    }
  }
  out_hops[source] = 0;
  queue[tail++] = source;
  while (head < tail) {
    v = queue[head++];
    d = g->degree(g->graph, v);
    for (i = 0; i < d; i++) {
      g_edge(g, v, i, &to, NULL);
      if (to < g->vertex_count && out_hops[to] == G_NONE) {
        out_hops[to] = out_hops[v] + 1;
        if (out_parent != NULL) {
          out_parent[to] = v;
        }
        queue[tail++] = to;
      }
    }
  }
  ds_context_free(context, queue);
  return DS_OK;
}

static int g_compare_double(const void *a, const void *b, void *user) {
  double x = *(const double *)a;
  double y = *(const double *)b;
  (void)user;
  return (x > y) - (x < y);
}

static ds_status_t g_heap(ds_indexed_heap_t *heap, size_t n,
                          ds_context_t *context) {
  ds_indexed_heap_config_t config;
  ds_indexed_heap_config_init(&config);
  config.key_size = sizeof(double);
  config.compare = g_compare_double;
  config.context = context;
  return ds_indexed_heap_init(heap, &config, n);
}

ds_status_t ds_gview_dijkstra(const ds_gview_t *g, size_t source,
                              double *out_dist, size_t *out_parent,
                              ds_context_t *context) {
  ds_indexed_heap_t heap;
  ds_status_t status;
  size_t v;
  size_t i;
  size_t d;
  size_t to;
  double w;
  double cand;
  double dv;
  if (g == NULL || out_dist == NULL) {
    return DS_ERR_NULL;
  }
  if (source >= g->vertex_count) {
    return DS_ERR_RANGE;
  }
  status = g_heap(&heap, g->vertex_count, context);
  if (status != DS_OK) {
    return status;
  }
  for (v = 0; v < g->vertex_count; v++) {
    out_dist[v] = HUGE_VAL;
    if (out_parent != NULL) {
      out_parent[v] = G_NONE;
    }
  }
  out_dist[source] = 0.0;
  ds_indexed_heap_push(&heap, source, &out_dist[source]);
  while (ds_indexed_heap_pop(&heap, &v, &dv) == DS_OK) {
    d = g->degree(g->graph, v);
    for (i = 0; i < d; i++) {
      g_edge(g, v, i, &to, &w);
      if (w < 0) {
        ds_indexed_heap_destroy(&heap);
        return DS_ERR_INVALID; /* Dijkstra needs non-negative weights */
      }
      cand = dv + w;
      if (to < g->vertex_count && cand < out_dist[to]) {
        out_dist[to] = cand;
        if (out_parent != NULL) {
          out_parent[to] = v;
        }
        ds_indexed_heap_push_or_update(&heap, to, &cand);
      }
    }
  }
  ds_indexed_heap_destroy(&heap);
  return DS_OK;
}

ds_status_t ds_gview_bellman_ford(const ds_gview_t *g, size_t source,
                                  double *out_dist, size_t *out_parent,
                                  bool *out_negative_cycle) {
  size_t round;
  size_t v;
  size_t i;
  size_t d;
  size_t to;
  double w;
  bool changed = true;
  if (g == NULL || out_dist == NULL) {
    return DS_ERR_NULL;
  }
  if (source >= g->vertex_count) {
    return DS_ERR_RANGE;
  }
  for (v = 0; v < g->vertex_count; v++) {
    out_dist[v] = HUGE_VAL;
    if (out_parent != NULL) {
      out_parent[v] = G_NONE;
    }
  }
  out_dist[source] = 0.0;
  /* n - 1 relaxation rounds; one extra round detects negative cycles. */
  for (round = 0; round < g->vertex_count && changed; round++) {
    changed = false;
    for (v = 0; v < g->vertex_count; v++) {
      if (out_dist[v] >= HUGE_VAL) {
        continue;
      }
      d = g->degree(g->graph, v);
      for (i = 0; i < d; i++) {
        g_edge(g, v, i, &to, &w);
        if (to < g->vertex_count && out_dist[v] + w < out_dist[to]) {
          out_dist[to] = out_dist[v] + w;
          if (out_parent != NULL) {
            out_parent[to] = v;
          }
          changed = true;
        }
      }
    }
  }
  if (out_negative_cycle != NULL) {
    *out_negative_cycle = changed && round == g->vertex_count;
  }
  return DS_OK;
}

ds_status_t ds_gview_topological_sort(const ds_gview_t *g, size_t *out_order,
                                      ds_context_t *context) {
  size_t *indegree;
  size_t n;
  size_t head = 0;
  size_t tail = 0;
  size_t v;
  size_t i;
  size_t d;
  size_t to;
  if (g == NULL || out_order == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  indegree = (size_t *)g_alloc(context, n, sizeof(size_t));
  if (indegree == NULL) {
    return DS_ERR_ALLOC;
  }
  memset(indegree, 0, (n > 0 ? n : 1) * sizeof(size_t));
  for (v = 0; v < n; v++) {
    d = g->degree(g->graph, v);
    for (i = 0; i < d; i++) {
      g_edge(g, v, i, &to, NULL);
      if (to < n) {
        indegree[to]++;
      }
    }
  }
  /* Kahn's algorithm, using out_order itself as the queue. */
  for (v = 0; v < n; v++) {
    if (indegree[v] == 0) {
      out_order[tail++] = v;
    }
  }
  while (head < tail) {
    v = out_order[head++];
    d = g->degree(g->graph, v);
    for (i = 0; i < d; i++) {
      g_edge(g, v, i, &to, NULL);
      if (to < n && --indegree[to] == 0) {
        out_order[tail++] = to;
      }
    }
  }
  ds_context_free(context, indegree);
  return tail == n ? DS_OK : DS_ERR_STATE; /* DS_ERR_STATE: a cycle */
}

ds_status_t ds_gview_scc(const ds_gview_t *g, size_t *out_component,
                         size_t *out_count, ds_context_t *context) {
  size_t n;
  size_t *index;
  size_t *low;
  size_t *stack;
  size_t *frame_v;
  size_t *frame_i;
  unsigned char *on_stack;
  size_t counter = 0;
  size_t sp = 0;
  size_t fp = 0;
  size_t components = 0;
  size_t s;
  size_t v;
  size_t w;
  size_t u;
  if (g == NULL || out_component == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  index = (size_t *)g_alloc(context, n, sizeof(size_t));
  low = (size_t *)g_alloc(context, n, sizeof(size_t));
  stack = (size_t *)g_alloc(context, n, sizeof(size_t));
  frame_v = (size_t *)g_alloc(context, n, sizeof(size_t));
  frame_i = (size_t *)g_alloc(context, n, sizeof(size_t));
  on_stack = (unsigned char *)g_alloc(context, n, 1);
  if (index == NULL || low == NULL || stack == NULL || frame_v == NULL ||
      frame_i == NULL || on_stack == NULL) {
    ds_context_free(context, index);
    ds_context_free(context, low);
    ds_context_free(context, stack);
    ds_context_free(context, frame_v);
    ds_context_free(context, frame_i);
    ds_context_free(context, on_stack);
    return DS_ERR_ALLOC;
  }
  for (v = 0; v < n; v++) {
    index[v] = G_NONE;
    on_stack[v] = 0;
  }
  /* Tarjan's algorithm with an explicit call stack (no recursion). */
  for (s = 0; s < n; s++) {
    if (index[s] != G_NONE) {
      continue;
    }
    index[s] = low[s] = counter++;
    stack[sp++] = s;
    on_stack[s] = 1;
    frame_v[fp] = s;
    frame_i[fp++] = 0;
    while (fp > 0) {
      v = frame_v[fp - 1];
      if (frame_i[fp - 1] < g->degree(g->graph, v)) {
        g_edge(g, v, frame_i[fp - 1]++, &w, NULL);
        if (w >= n) {
          continue;
        }
        if (index[w] == G_NONE) {
          index[w] = low[w] = counter++;
          stack[sp++] = w;
          on_stack[w] = 1;
          frame_v[fp] = w;
          frame_i[fp++] = 0;
        } else if (on_stack[w] && index[w] < low[v]) {
          low[v] = index[w];
        }
        continue;
      }
      fp--;
      if (low[v] == index[v]) {
        do {
          w = stack[--sp];
          on_stack[w] = 0;
          out_component[w] = components;
        } while (w != v);
        components++;
      }
      if (fp > 0) {
        u = frame_v[fp - 1];
        if (low[v] < low[u]) {
          low[u] = low[v];
        }
      }
    }
  }
  if (out_count != NULL) {
    *out_count = components;
  }
  ds_context_free(context, index);
  ds_context_free(context, low);
  ds_context_free(context, stack);
  ds_context_free(context, frame_v);
  ds_context_free(context, frame_i);
  ds_context_free(context, on_stack);
  return DS_OK;
}

ds_status_t ds_gview_bipartite(const ds_gview_t *g, unsigned char *out_side,
                               bool *out_bipartite, ds_context_t *context) {
  size_t *queue;
  size_t n;
  size_t s;
  size_t v;
  size_t i;
  size_t d;
  size_t to;
  size_t head;
  size_t tail;
  unsigned char *seen;
  bool ok = true;
  if (g == NULL || out_side == NULL || out_bipartite == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  queue = (size_t *)g_alloc(context, n, sizeof(size_t));
  seen = (unsigned char *)g_alloc(context, n, 1);
  if (queue == NULL || seen == NULL) {
    ds_context_free(context, queue);
    ds_context_free(context, seen);
    return DS_ERR_ALLOC;
  }
  memset(seen, 0, n > 0 ? n : 1);
  for (s = 0; s < n; s++) {
    if (seen[s]) {
      continue;
    }
    seen[s] = 1;
    out_side[s] = 0;
    head = 0;
    tail = 0;
    queue[tail++] = s;
    while (head < tail) {
      v = queue[head++];
      d = g->degree(g->graph, v);
      for (i = 0; i < d; i++) {
        g_edge(g, v, i, &to, NULL);
        if (to >= n) {
          continue;
        }
        if (!seen[to]) {
          seen[to] = 1;
          out_side[to] = (unsigned char)(out_side[v] ^ 1U);
          queue[tail++] = to;
        } else if (out_side[to] == out_side[v]) {
          ok = false;
        }
      }
    }
  }
  *out_bipartite = ok;
  ds_context_free(context, queue);
  ds_context_free(context, seen);
  return DS_OK;
}

ds_status_t ds_gview_prim(const ds_gview_t *g, size_t *out_parent,
                          double *out_total, ds_context_t *context) {
  ds_indexed_heap_t heap;
  ds_status_t status;
  unsigned char *done;
  double *best;
  double total = 0.0;
  double key;
  double w;
  size_t n;
  size_t s;
  size_t v;
  size_t i;
  size_t d;
  size_t to;
  if (g == NULL || out_parent == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  status = g_heap(&heap, n, context);
  if (status != DS_OK) {
    return status;
  }
  done = (unsigned char *)g_alloc(context, n, 1);
  best = (double *)g_alloc(context, n, sizeof(double));
  if (done == NULL || best == NULL) {
    ds_context_free(context, done);
    ds_context_free(context, best);
    ds_indexed_heap_destroy(&heap);
    return DS_ERR_ALLOC;
  }
  for (v = 0; v < n; v++) {
    done[v] = 0;
    best[v] = HUGE_VAL;
    out_parent[v] = G_NONE;
  }
  /* One tree per connected component (a minimum spanning forest). */
  for (s = 0; s < n; s++) {
    if (done[s]) {
      continue;
    }
    best[s] = 0.0;
    ds_indexed_heap_push(&heap, s, &best[s]);
    while (ds_indexed_heap_pop(&heap, &v, &key) == DS_OK) {
      done[v] = 1;
      total += key;
      d = g->degree(g->graph, v);
      for (i = 0; i < d; i++) {
        g_edge(g, v, i, &to, &w);
        if (to < n && !done[to] && w < best[to]) {
          best[to] = w;
          out_parent[to] = v;
          ds_indexed_heap_push_or_update(&heap, to, &w);
        }
      }
    }
  }
  if (out_total != NULL) {
    *out_total = total;
  }
  ds_context_free(context, done);
  ds_context_free(context, best);
  ds_indexed_heap_destroy(&heap);
  return DS_OK;
}

void ds_gview_floyd_warshall(double *dist, size_t n, size_t *out_next) {
  size_t i;
  size_t j;
  size_t k;
  double via;
  if (dist == NULL) {
    return;
  }
  if (out_next != NULL) {
    for (i = 0; i < n; i++) {
      for (j = 0; j < n; j++) {
        out_next[i * n + j] =
            i == j ? i : (dist[i * n + j] < HUGE_VAL ? j : G_NONE);
      }
    }
  }
  for (k = 0; k < n; k++) {
    for (i = 0; i < n; i++) {
      if (dist[i * n + k] >= HUGE_VAL) {
        continue;
      }
      for (j = 0; j < n; j++) {
        via = dist[i * n + k] + dist[k * n + j];
        if (via < dist[i * n + j]) {
          dist[i * n + j] = via;
          if (out_next != NULL) {
            out_next[i * n + j] = out_next[i * n + k];
          }
        }
      }
    }
  }
}

size_t ds_gview_path(const size_t *parent, size_t target, size_t *out_path,
                     size_t capacity) {
  size_t length = 0;
  size_t v = target;
  size_t i;
  size_t tmp;
  while (v != G_NONE) {
    if (out_path != NULL && length < capacity) {
      out_path[length] = v;
    }
    length++;
    v = parent[v];
  }
  if (out_path != NULL && length <= capacity) {
    for (i = 0; i < length / 2; i++) {
      tmp = out_path[i];
      out_path[i] = out_path[length - 1 - i];
      out_path[length - 1 - i] = tmp;
    }
  }
  return length;
}
