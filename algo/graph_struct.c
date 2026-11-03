#include "graph_struct.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GS_NONE DS_GVIEW_NONE

static size_t gs_target(const ds_gview_t *g, size_t v, size_t i,
                        double *weight) {
  size_t to;
  double w = 1.0;
  g->edge(g->graph, v, i, &to, &w);
  if (weight != NULL) {
    *weight = w;
  }
  return to;
}

static void *gs_alloc(ds_context_t *ctx, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > ((size_t)-1) / size) {
    return NULL;
  }
  return ds_context_alloc(ctx, count * size);
}

size_t ds_gview_edge_offsets(const ds_gview_t *g, size_t *out_offsets) {
  size_t v;
  size_t total = 0;
  for (v = 0; v < g->vertex_count; v++) {
    out_offsets[v] = total;
    total += g->degree(g->graph, v);
  }
  out_offsets[g->vertex_count] = total;
  return total;
}

/* ---- DFS forest ---------------------------------------------------------- */

ds_status_t ds_gview_dfs_forest(const ds_gview_t *g, size_t *out_discovery,
                                size_t *out_finish, size_t *out_parent,
                                ds_gview_edge_class_func_t classify, void *user,
                                ds_context_t *context) {
  size_t n;
  size_t *disc;
  size_t *fin;
  size_t *it;
  size_t *stack;
  size_t *parent;
  size_t sp;
  size_t t = 0;
  size_t r;
  size_t v;
  size_t w;
  size_t idx;
  ds_gview_edge_class_t kind;
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  disc = (size_t *)gs_alloc(context, n, sizeof(size_t));
  fin = (size_t *)gs_alloc(context, n, sizeof(size_t));
  it = (size_t *)gs_alloc(context, n, sizeof(size_t));
  stack = (size_t *)gs_alloc(context, n, sizeof(size_t));
  parent = (size_t *)gs_alloc(context, n, sizeof(size_t));
  if (disc == NULL || fin == NULL || it == NULL || stack == NULL ||
      parent == NULL) {
    ds_context_free(context, disc);
    ds_context_free(context, fin);
    ds_context_free(context, it);
    ds_context_free(context, stack);
    ds_context_free(context, parent);
    return DS_ERR_ALLOC;
  }
  for (v = 0; v < n; v++) {
    disc[v] = GS_NONE;
    fin[v] = GS_NONE;
    it[v] = 0;
    parent[v] = GS_NONE;
  }
  for (r = 0; r < n; r++) {
    if (disc[r] != GS_NONE) {
      continue;
    }
    sp = 0;
    stack[sp++] = r;
    disc[r] = t++;
    while (sp > 0) {
      v = stack[sp - 1];
      if (it[v] < g->degree(g->graph, v)) {
        idx = it[v]++;
        w = gs_target(g, v, idx, NULL);
        if (w >= n) {
          continue;
        }
        if (disc[w] == GS_NONE) {
          kind = DS_GVIEW_TREE_EDGE;
          parent[w] = v;
          disc[w] = t++;
          stack[sp++] = w;
        } else if (fin[w] == GS_NONE) {
          kind = DS_GVIEW_BACK_EDGE;
        } else if (disc[v] < disc[w]) {
          kind = DS_GVIEW_FORWARD_EDGE;
        } else {
          kind = DS_GVIEW_CROSS_EDGE;
        }
        if (classify != NULL) {
          classify(v, idx, w, kind, user);
        }
      } else {
        fin[v] = t++;
        sp--;
      }
    }
  }
  if (out_discovery != NULL) {
    memcpy(out_discovery, disc, n * sizeof(size_t));
  }
  if (out_finish != NULL) {
    memcpy(out_finish, fin, n * sizeof(size_t));
  }
  if (out_parent != NULL) {
    memcpy(out_parent, parent, n * sizeof(size_t));
  }
  ds_context_free(context, disc);
  ds_context_free(context, fin);
  ds_context_free(context, it);
  ds_context_free(context, stack);
  ds_context_free(context, parent);
  return DS_OK;
}

/* ---- articulation points, bridges, biconnected components ---------------- */

typedef struct gs_lowlink {
  size_t *disc;
  size_t *low;
  size_t *it;
  size_t *parent;
  unsigned char *skipped; /* the edge back to the parent was skipped once */
  size_t *stack;
  size_t *estack_u; /* edge stack for biconnected components */
  size_t *estack_v;
} gs_lowlink_t;

static void gs_lowlink_free(ds_context_t *ctx, gs_lowlink_t *l) {
  ds_context_free(ctx, l->disc);
  ds_context_free(ctx, l->low);
  ds_context_free(ctx, l->it);
  ds_context_free(ctx, l->parent);
  ds_context_free(ctx, l->skipped);
  ds_context_free(ctx, l->stack);
  ds_context_free(ctx, l->estack_u);
  ds_context_free(ctx, l->estack_v);
}

static ds_status_t gs_lowlink_alloc(ds_context_t *ctx, size_t n, size_t m,
                                    bool edges, gs_lowlink_t *l) {
  memset(l, 0, sizeof *l);
  l->disc = (size_t *)gs_alloc(ctx, n, sizeof(size_t));
  l->low = (size_t *)gs_alloc(ctx, n, sizeof(size_t));
  l->it = (size_t *)gs_alloc(ctx, n, sizeof(size_t));
  l->parent = (size_t *)gs_alloc(ctx, n, sizeof(size_t));
  l->skipped = (unsigned char *)gs_alloc(ctx, n, 1);
  l->stack = (size_t *)gs_alloc(ctx, n, sizeof(size_t));
  if (edges) {
    l->estack_u = (size_t *)gs_alloc(ctx, m, sizeof(size_t));
    l->estack_v = (size_t *)gs_alloc(ctx, m, sizeof(size_t));
  }
  if (l->disc == NULL || l->low == NULL || l->it == NULL || l->parent == NULL ||
      l->skipped == NULL || l->stack == NULL ||
      (edges && (l->estack_u == NULL || l->estack_v == NULL))) {
    gs_lowlink_free(ctx, l);
    return DS_ERR_ALLOC;
  }
  return DS_OK;
}

/* One lowlink DFS serving both articulation/bridges and biconnected
 * components (when visit is non-NULL). */
static ds_status_t gs_lowlink_run(const ds_gview_t *g, unsigned char *is_cut,
                                  ds_gview_component_edge_func_t bridge,
                                  ds_gview_component_edge_func_t visit,
                                  void *user, size_t *out_bridges,
                                  size_t *out_components,
                                  ds_context_t *context) {
  size_t n = g->vertex_count;
  size_t m = 0;
  size_t v;
  size_t w;
  size_t p;
  size_t r;
  size_t sp;
  size_t ep = 0;
  size_t t = 0;
  size_t root_children;
  size_t bridges = 0;
  size_t components = 0;
  size_t eu;
  size_t ev;
  gs_lowlink_t l;
  ds_status_t status;
  for (v = 0; v < n; v++) {
    m += g->degree(g->graph, v);
  }
  status = gs_lowlink_alloc(context, n, m, visit != NULL, &l);
  if (status != DS_OK) {
    return status;
  }
  for (v = 0; v < n; v++) {
    l.disc[v] = GS_NONE;
    l.it[v] = 0;
    l.parent[v] = GS_NONE;
    l.skipped[v] = 0;
    if (is_cut != NULL) {
      is_cut[v] = 0;
    }
  }
  for (r = 0; r < n; r++) {
    if (l.disc[r] != GS_NONE) {
      continue;
    }
    root_children = 0;
    sp = 0;
    l.stack[sp++] = r;
    l.disc[r] = l.low[r] = t++;
    while (sp > 0) {
      v = l.stack[sp - 1];
      if (l.it[v] < g->degree(g->graph, v)) {
        w = gs_target(g, v, l.it[v]++, NULL);
        if (w >= n || w == v) {
          continue;
        }
        if (w == l.parent[v] && !l.skipped[v]) {
          l.skipped[v] = 1;
          continue;
        }
        if (l.disc[w] == GS_NONE) {
          l.parent[w] = v;
          l.disc[w] = l.low[w] = t++;
          l.stack[sp++] = w;
          if (v == r) {
            root_children++;
          }
          if (visit != NULL) {
            l.estack_u[ep] = v;
            l.estack_v[ep++] = w;
          }
        } else if (l.disc[w] < l.disc[v]) {
          if (l.disc[w] < l.low[v]) {
            l.low[v] = l.disc[w];
          }
          if (visit != NULL) {
            l.estack_u[ep] = v;
            l.estack_v[ep++] = w;
          }
        }
      } else {
        sp--;
        p = l.parent[v];
        if (p == GS_NONE) {
          continue;
        }
        if (l.low[v] < l.low[p]) {
          l.low[p] = l.low[v];
        }
        if (l.low[v] > l.disc[p]) {
          bridges++;
          if (bridge != NULL) {
            bridge(bridges - 1, p, v, user);
          }
        }
        if (l.low[v] >= l.disc[p]) {
          if (p != r && is_cut != NULL) {
            is_cut[p] = 1;
          }
          if (visit != NULL) {
            do {
              ep--;
              eu = l.estack_u[ep];
              ev = l.estack_v[ep];
              visit(components, eu, ev, user);
            } while (!(eu == p && ev == v));
            components++;
          }
        }
      }
    }
    if (is_cut != NULL && root_children >= 2) {
      is_cut[r] = 1;
    }
  }
  gs_lowlink_free(context, &l);
  if (out_bridges != NULL) {
    *out_bridges = bridges;
  }
  if (out_components != NULL) {
    *out_components = components;
  }
  return DS_OK;
}

ds_status_t ds_gview_articulation(const ds_gview_t *g,
                                  unsigned char *out_is_cut,
                                  ds_gview_component_edge_func_t bridge,
                                  void *user, size_t *out_bridges,
                                  ds_context_t *context) {
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  return gs_lowlink_run(g, out_is_cut, bridge, NULL, user, out_bridges, NULL,
                        context);
}

ds_status_t ds_gview_biconnected(const ds_gview_t *g,
                                 ds_gview_component_edge_func_t visit,
                                 void *user, size_t *out_count,
                                 ds_context_t *context) {
  if (g == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  return gs_lowlink_run(g, NULL, NULL, visit, user, NULL, out_count, context);
}

/* ---- Euler --------------------------------------------------------------- */

ds_status_t ds_gview_euler_directed(const ds_gview_t *g, size_t start,
                                    size_t *out_path, size_t *out_length,
                                    ds_context_t *context) {
  size_t n;
  size_t m = 0;
  size_t *indeg;
  size_t *it;
  size_t *stack;
  size_t sp;
  size_t len = 0;
  size_t v;
  size_t w;
  size_t deg;
  size_t plus = 0;
  size_t minus = 0;
  size_t s = GS_NONE;
  ds_status_t status = DS_OK;
  if (g == NULL || out_path == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  if (n == 0) {
    *out_length = 0;
    return DS_OK;
  }
  indeg = (size_t *)ds_context_calloc(context, n, sizeof(size_t));
  it = (size_t *)gs_alloc(context, n, sizeof(size_t));
  if (indeg == NULL || it == NULL) {
    ds_context_free(context, indeg);
    ds_context_free(context, it);
    return DS_ERR_ALLOC;
  }
  for (v = 0; v < n; v++) {
    deg = g->degree(g->graph, v);
    m += deg;
    for (it[v] = 0; it[v] < deg; it[v]++) {
      w = gs_target(g, v, it[v], NULL);
      if (w >= n) {
        status = DS_ERR_RANGE;
      } else {
        indeg[w]++;
      }
    }
    it[v] = 0;
  }
  for (v = 0; v < n && status == DS_OK; v++) {
    deg = g->degree(g->graph, v);
    if (deg == indeg[v] + 1) {
      plus++;
      s = v;
    } else if (indeg[v] == deg + 1) {
      minus++;
    } else if (deg != indeg[v]) {
      status = DS_NOT_FOUND;
    }
  }
  if (status == DS_OK && (plus > 1 || minus > 1 || plus != minus)) {
    status = DS_NOT_FOUND;
  }
  if (status == DS_OK) {
    if (start != GS_NONE) {
      if (start >= n || (plus == 1 && start != s)) {
        status = DS_NOT_FOUND;
      }
      s = start;
    } else if (s == GS_NONE) {
      for (v = 0; v < n && s == GS_NONE; v++) {
        if (g->degree(g->graph, v) > 0) {
          s = v;
        }
      }
      if (s == GS_NONE) {
        s = 0;
      }
    }
  }
  stack = NULL;
  if (status == DS_OK) {
    stack = (size_t *)gs_alloc(context, m + 1, sizeof(size_t));
    if (stack == NULL) {
      status = DS_ERR_ALLOC;
    }
  }
  if (status == DS_OK) {
    sp = 0;
    stack[sp++] = s;
    while (sp > 0) {
      v = stack[sp - 1];
      if (it[v] < g->degree(g->graph, v)) {
        stack[sp++] = gs_target(g, v, it[v]++, NULL);
      } else {
        out_path[len++] = v;
        sp--;
      }
    }
    if (len != m + 1) {
      status = DS_NOT_FOUND; /* edges in another component */
    } else {
      /* reverse into forward order */
      for (v = 0; v < len / 2; v++) {
        w = out_path[v];
        out_path[v] = out_path[len - 1 - v];
        out_path[len - 1 - v] = w;
      }
      *out_length = len;
    }
  }
  ds_context_free(context, indeg);
  ds_context_free(context, it);
  ds_context_free(context, stack);
  return status;
}

ds_status_t ds_euler_undirected(size_t n, const size_t *u, const size_t *v,
                                size_t m, size_t start, size_t *out_path,
                                size_t *out_length, ds_context_t *context) {
  size_t *off;
  size_t *adj_edge;
  size_t *fill;
  size_t *it;
  size_t *stack;
  unsigned char *used;
  size_t i;
  size_t x;
  size_t e;
  size_t sp;
  size_t len = 0;
  size_t odd = 0;
  size_t s = GS_NONE;
  ds_status_t status = DS_OK;
  if (out_path == NULL || out_length == NULL ||
      (m > 0 && (u == NULL || v == NULL))) {
    return DS_ERR_NULL;
  }
  if (n == 0) {
    *out_length = 0;
    return DS_OK;
  }
  off = (size_t *)ds_context_calloc(context, n + 1, sizeof(size_t));
  fill = (size_t *)gs_alloc(context, n, sizeof(size_t));
  it = (size_t *)gs_alloc(context, n, sizeof(size_t));
  adj_edge = (size_t *)gs_alloc(context, 2 * m, sizeof(size_t));
  stack = (size_t *)gs_alloc(context, m + 1, sizeof(size_t));
  used = (unsigned char *)ds_context_calloc(context, m + 1, 1);
  if (off == NULL || fill == NULL || it == NULL || adj_edge == NULL ||
      stack == NULL || used == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  for (i = 0; i < m; i++) {
    if (u[i] >= n || v[i] >= n) {
      status = DS_ERR_RANGE;
      goto out;
    }
    off[u[i] + 1]++;
    off[v[i] + 1]++;
  }
  for (x = 0; x < n; x++) {
    off[x + 1] += off[x];
    fill[x] = off[x];
    it[x] = off[x];
  }
  for (i = 0; i < m; i++) {
    adj_edge[fill[u[i]]++] = i;
    adj_edge[fill[v[i]]++] = i; /* a self-loop appears twice */
  }
  for (x = 0; x < n; x++) {
    if ((off[x + 1] - off[x]) & 1U) {
      odd++;
      if (s == GS_NONE) {
        s = x;
      }
    }
  }
  if (odd != 0 && odd != 2) {
    status = DS_NOT_FOUND;
    goto out;
  }
  if (start != GS_NONE) {
    if (start >= n || (odd == 2 && ((off[start + 1] - off[start]) & 1U) == 0)) {
      status = DS_NOT_FOUND;
      goto out;
    }
    s = start;
  } else if (s == GS_NONE) {
    for (x = 0; x < n && s == GS_NONE; x++) {
      if (off[x + 1] > off[x]) {
        s = x;
      }
    }
    if (s == GS_NONE) {
      s = 0;
    }
  }
  sp = 0;
  stack[sp++] = s;
  while (sp > 0) {
    x = stack[sp - 1];
    while (it[x] < off[x + 1] && used[adj_edge[it[x]]]) {
      it[x]++;
    }
    if (it[x] < off[x + 1]) {
      e = adj_edge[it[x]++];
      used[e] = 1;
      stack[sp++] = u[e] == x ? v[e] : u[e];
    } else {
      out_path[len++] = x;
      sp--;
    }
  }
  if (len != m + 1) {
    status = DS_NOT_FOUND;
  } else {
    *out_length = len;
  }
out:
  ds_context_free(context, off);
  ds_context_free(context, fill);
  ds_context_free(context, it);
  ds_context_free(context, adj_edge);
  ds_context_free(context, stack);
  ds_context_free(context, used);
  return status;
}

/* ---- condensation -------------------------------------------------------- */

ds_status_t ds_gview_condensation(const ds_gview_t *g, size_t *out_component,
                                  size_t *out_count, size_t *out_offsets,
                                  size_t *out_targets, size_t capacity,
                                  size_t *out_edges, ds_context_t *context) {
  size_t n;
  size_t k;
  size_t *comp;
  size_t *start;
  size_t *members;
  size_t *indeg;
  size_t *order;
  size_t *rank;
  size_t *mark;
  size_t qh;
  size_t qt;
  size_t c;
  size_t d;
  size_t v;
  size_t i;
  size_t j;
  size_t deg;
  size_t w;
  size_t edges = 0;
  ds_status_t status;
  if (g == NULL || out_component == NULL || out_count == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  comp = out_component;
  status = ds_gview_scc(g, comp, &k, context);
  if (status != DS_OK) {
    return status;
  }
  start = (size_t *)ds_context_calloc(context, k + 2, sizeof(size_t));
  members = (size_t *)gs_alloc(context, n, sizeof(size_t));
  indeg = (size_t *)ds_context_calloc(context, k + 1, sizeof(size_t));
  order = (size_t *)gs_alloc(context, k, sizeof(size_t));
  rank = (size_t *)gs_alloc(context, k, sizeof(size_t));
  mark = (size_t *)gs_alloc(context, k, sizeof(size_t));
  if (start == NULL || members == NULL || indeg == NULL || order == NULL ||
      rank == NULL || mark == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  for (v = 0; v < n; v++) {
    start[comp[v] + 1]++;
  }
  for (c = 0; c < k; c++) {
    start[c + 1] += start[c];
    mark[c] = GS_NONE;
  }
  {
    size_t *fill = indeg; /* reuse as a cursor, reset below */
    for (c = 0; c < k; c++) {
      fill[c] = start[c];
    }
    for (v = 0; v < n; v++) {
      members[fill[comp[v]]++] = v;
    }
    for (c = 0; c < k; c++) {
      fill[c] = 0;
    }
  }
  /* in-degrees of the (deduplicated) component DAG */
  for (c = 0; c < k; c++) {
    for (i = start[c]; i < start[c + 1]; i++) {
      v = members[i];
      deg = g->degree(g->graph, v);
      for (j = 0; j < deg; j++) {
        w = gs_target(g, v, j, NULL);
        if (w < n && (d = comp[w]) != c && mark[d] != c) {
          mark[d] = c;
          indeg[d]++;
        }
      }
    }
  }
  /* Kahn order of the components */
  qh = 0;
  qt = 0;
  for (c = 0; c < k; c++) {
    mark[c] = GS_NONE;
    if (indeg[c] == 0) {
      order[qt++] = c;
    }
  }
  while (qh < qt) {
    c = order[qh++];
    for (i = start[c]; i < start[c + 1]; i++) {
      v = members[i];
      deg = g->degree(g->graph, v);
      for (j = 0; j < deg; j++) {
        w = gs_target(g, v, j, NULL);
        if (w < n && (d = comp[w]) != c && mark[d] != c) {
          mark[d] = c;
          if (--indeg[d] == 0) {
            order[qt++] = d;
          }
        }
      }
    }
  }
  for (i = 0; i < k; i++) {
    rank[order[i]] = i;
    mark[i] = GS_NONE;
  }
  /* write the DAG in the new numbering */
  for (i = 0; i < k; i++) {
    c = order[i];
    if (out_offsets != NULL) {
      out_offsets[i] = edges;
    }
    for (j = start[c]; j < start[c + 1]; j++) {
      size_t e;
      v = members[j];
      deg = g->degree(g->graph, v);
      for (e = 0; e < deg; e++) {
        w = gs_target(g, v, e, NULL);
        if (w < n && (d = comp[w]) != c && mark[d] != c) {
          mark[d] = c;
          if (out_targets != NULL) {
            if (edges >= capacity) {
              status = DS_ERR_OVERFLOW;
              goto out;
            }
            out_targets[edges] = rank[d];
          }
          edges++;
        }
      }
    }
  }
  if (out_offsets != NULL) {
    out_offsets[k] = edges;
  }
  for (v = 0; v < n; v++) {
    comp[v] = rank[comp[v]];
  }
  *out_count = k;
  if (out_edges != NULL) {
    *out_edges = edges;
  }
  status = DS_OK;
out:
  ds_context_free(context, start);
  ds_context_free(context, members);
  ds_context_free(context, indeg);
  ds_context_free(context, order);
  ds_context_free(context, rank);
  ds_context_free(context, mark);
  return status;
}

/* ---- transitive closure and reduction ------------------------------------ */

ds_status_t ds_gview_transitive_closure(const ds_gview_t *g,
                                        unsigned char *out_reach,
                                        ds_context_t *context) {
  size_t n;
  size_t *queue;
  size_t s;
  size_t qh;
  size_t qt;
  size_t v;
  size_t w;
  size_t i;
  size_t deg;
  unsigned char *row;
  if (g == NULL || out_reach == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  if (n > 0 && n > ((size_t)-1) / n) {
    return DS_ERR_OVERFLOW;
  }
  queue = (size_t *)gs_alloc(context, n, sizeof(size_t));
  if (queue == NULL) {
    return DS_ERR_ALLOC;
  }
  memset(out_reach, 0, n * n);
  for (s = 0; s < n; s++) {
    row = out_reach + s * n;
    qh = 0;
    qt = 0;
    row[s] = 1;
    queue[qt++] = s;
    while (qh < qt) {
      v = queue[qh++];
      deg = g->degree(g->graph, v);
      for (i = 0; i < deg; i++) {
        w = gs_target(g, v, i, NULL);
        if (w < n && !row[w]) {
          row[w] = 1;
          queue[qt++] = w;
        }
      }
    }
  }
  ds_context_free(context, queue);
  return DS_OK;
}

typedef struct gs_succ {
  size_t pos;  /* topological position of the target */
  size_t edge; /* global edge index */
  size_t to;
} gs_succ_t;

static int gs_succ_cmp(const void *a, const void *b) {
  const gs_succ_t *x = (const gs_succ_t *)a;
  const gs_succ_t *y = (const gs_succ_t *)b;
  if (x->pos != y->pos) {
    return x->pos < y->pos ? -1 : 1;
  }
  return x->edge < y->edge ? -1 : (x->edge > y->edge);
}

ds_status_t ds_gview_transitive_reduction(const ds_gview_t *g,
                                          unsigned char *out_keep,
                                          ds_context_t *context) {
  size_t n;
  size_t words;
  size_t *order = NULL;
  size_t *pos = NULL;
  size_t *offsets = NULL;
  unsigned long *reach = NULL;
  gs_succ_t *succ = NULL;
  size_t m;
  size_t i;
  size_t j;
  size_t k;
  size_t v;
  size_t w;
  size_t deg;
  size_t maxdeg = 0;
  unsigned long *rv;
  unsigned long *rw;
  const size_t bits = sizeof(unsigned long) * 8;
  ds_status_t status;
  if (g == NULL || out_keep == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  words = (n + bits - 1) / bits;
  if (words > 0 && n > ((size_t)-1) / words / sizeof(unsigned long)) {
    return DS_ERR_OVERFLOW;
  }
  order = (size_t *)gs_alloc(context, n, sizeof(size_t));
  pos = (size_t *)gs_alloc(context, n, sizeof(size_t));
  offsets = (size_t *)gs_alloc(context, n + 1, sizeof(size_t));
  reach = (unsigned long *)ds_context_calloc(context, n * words + 1,
                                             sizeof(unsigned long));
  if (order == NULL || pos == NULL || offsets == NULL || reach == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  status = ds_gview_topological_sort(g, order, context);
  if (status != DS_OK) {
    status = status == DS_ERR_ALLOC ? status : DS_ERR_INVALID;
    goto out;
  }
  m = ds_gview_edge_offsets(g, offsets);
  for (v = 0; v < n; v++) {
    pos[order[v]] = v;
    deg = g->degree(g->graph, v);
    if (deg > maxdeg) {
      maxdeg = deg;
    }
  }
  memset(out_keep, 0, m);
  succ = (gs_succ_t *)gs_alloc(context, maxdeg, sizeof(gs_succ_t));
  if (succ == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  for (i = n; i-- > 0;) {
    v = order[i];
    rv = reach + v * words;
    deg = g->degree(g->graph, v);
    k = 0;
    for (j = 0; j < deg; j++) {
      w = gs_target(g, v, j, NULL);
      if (w >= n) {
        continue;
      }
      succ[k].pos = pos[w];
      succ[k].edge = offsets[v] + j;
      succ[k].to = w;
      k++;
    }
    qsort(succ, k, sizeof(gs_succ_t), gs_succ_cmp);
    for (j = 0; j < k; j++) {
      w = succ[j].to;
      if ((rv[w / bits] >> (w % bits)) & 1UL) {
        continue; /* already reachable through an earlier successor */
      }
      out_keep[succ[j].edge] = 1;
      rw = reach + w * words;
      {
        size_t q;
        for (q = 0; q < words; q++) {
          rv[q] |= rw[q];
        }
      }
      rv[w / bits] |= 1UL << (w % bits);
    }
  }
  status = DS_OK;
out:
  ds_context_free(context, order);
  ds_context_free(context, pos);
  ds_context_free(context, offsets);
  ds_context_free(context, reach);
  ds_context_free(context, succ);
  return status;
}

/* ---- feedback edge set --------------------------------------------------- */

static size_t gs_find(size_t *parent, size_t x) {
  while (parent[x] != x) {
    parent[x] = parent[parent[x]];
    x = parent[x];
  }
  return x;
}

ds_status_t ds_feedback_edge_set(size_t n, const size_t *u, const size_t *v,
                                 size_t m, unsigned char *out_in_set,
                                 size_t *out_count, ds_context_t *context) {
  size_t *parent;
  size_t i;
  size_t a;
  size_t b;
  size_t count = 0;
  if (out_in_set == NULL || (m > 0 && (u == NULL || v == NULL))) {
    return DS_ERR_NULL;
  }
  parent = (size_t *)gs_alloc(context, n, sizeof(size_t));
  if (parent == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    parent[i] = i;
  }
  for (i = 0; i < m; i++) {
    if (u[i] >= n || v[i] >= n) {
      ds_context_free(context, parent);
      return DS_ERR_RANGE;
    }
    a = gs_find(parent, u[i]);
    b = gs_find(parent, v[i]);
    if (a == b) {
      out_in_set[i] = 1;
      count++;
    } else {
      out_in_set[i] = 0;
      parent[a] = b;
    }
  }
  ds_context_free(context, parent);
  if (out_count != NULL) {
    *out_count = count;
  }
  return DS_OK;
}

/* ---- trees --------------------------------------------------------------- */

/* Distances from s over a tree (iterative DFS); returns the farthest. */
static size_t gs_far(const ds_gview_t *g, size_t s, double *dist, size_t *stack,
                     size_t *parent) {
  size_t n = g->vertex_count;
  size_t sp = 0;
  size_t v;
  size_t w;
  size_t i;
  size_t deg;
  size_t best = s;
  double wt;
  for (v = 0; v < n; v++) {
    dist[v] = -1;
  }
  dist[s] = 0;
  parent[s] = GS_NONE;
  stack[sp++] = s;
  while (sp > 0) {
    v = stack[--sp];
    if (dist[v] > dist[best]) {
      best = v;
    }
    deg = g->degree(g->graph, v);
    for (i = 0; i < deg; i++) {
      w = gs_target(g, v, i, &wt);
      if (w < n && dist[w] < 0) {
        dist[w] = dist[v] + wt;
        parent[w] = v;
        stack[sp++] = w;
      }
    }
  }
  return best;
}

ds_status_t ds_gview_tree_diameter(const ds_gview_t *g, double *out_length,
                                   size_t *out_a, size_t *out_b,
                                   ds_context_t *context) {
  size_t n;
  double *dist;
  size_t *stack;
  size_t *parent;
  size_t a;
  size_t b;
  if (g == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  if (n == 0) {
    *out_length = 0;
    return DS_EMPTY;
  }
  dist = (double *)gs_alloc(context, n, sizeof(double));
  stack = (size_t *)gs_alloc(context, n, sizeof(size_t));
  parent = (size_t *)gs_alloc(context, n, sizeof(size_t));
  if (dist == NULL || stack == NULL || parent == NULL) {
    ds_context_free(context, dist);
    ds_context_free(context, stack);
    ds_context_free(context, parent);
    return DS_ERR_ALLOC;
  }
  a = gs_far(g, 0, dist, stack, parent);
  b = gs_far(g, a, dist, stack, parent);
  *out_length = dist[b];
  if (out_a != NULL) {
    *out_a = a;
  }
  if (out_b != NULL) {
    *out_b = b;
  }
  ds_context_free(context, dist);
  ds_context_free(context, stack);
  ds_context_free(context, parent);
  return DS_OK;
}

ds_status_t ds_gview_tree_centroid(const ds_gview_t *g, size_t *out_centroids,
                                   size_t *out_count, ds_context_t *context) {
  size_t n;
  size_t *order;
  size_t *parent;
  size_t *size;
  size_t *heaviest;
  size_t sp = 0;
  size_t cnt = 0;
  size_t v;
  size_t w;
  size_t i;
  size_t deg;
  size_t k = 0;
  size_t worst;
  if (g == NULL || out_centroids == NULL || out_count == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  if (n == 0) {
    *out_count = 0;
    return DS_EMPTY;
  }
  order = (size_t *)gs_alloc(context, n, sizeof(size_t));
  parent = (size_t *)gs_alloc(context, n, sizeof(size_t));
  size = (size_t *)gs_alloc(context, n, sizeof(size_t));
  heaviest = (size_t *)ds_context_calloc(context, n, sizeof(size_t));
  if (order == NULL || parent == NULL || size == NULL || heaviest == NULL) {
    ds_context_free(context, order);
    ds_context_free(context, parent);
    ds_context_free(context, size);
    ds_context_free(context, heaviest);
    return DS_ERR_ALLOC;
  }
  for (v = 0; v < n; v++) {
    parent[v] = GS_NONE;
    size[v] = 1;
  }
  /* preorder via an explicit stack kept in `order` */
  order[k++] = 0;
  parent[0] = 0;
  while (sp < k) {
    v = order[sp++];
    deg = g->degree(g->graph, v);
    for (i = 0; i < deg; i++) {
      w = gs_target(g, v, i, NULL);
      if (w < n && parent[w] == GS_NONE) {
        parent[w] = v;
        order[k++] = w;
      }
    }
  }
  parent[0] = GS_NONE;
  for (i = k; i-- > 1;) {
    v = order[i];
    size[parent[v]] += size[v];
    if (size[v] > heaviest[parent[v]]) {
      heaviest[parent[v]] = size[v];
    }
  }
  for (i = 0; i < k; i++) {
    v = order[i];
    worst = heaviest[v];
    if (k - size[v] > worst) {
      worst = k - size[v];
    }
    if (2 * worst <= k && cnt < 2) {
      out_centroids[cnt++] = v;
    }
  }
  *out_count = cnt;
  ds_context_free(context, order);
  ds_context_free(context, parent);
  ds_context_free(context, size);
  ds_context_free(context, heaviest);
  return DS_OK;
}
