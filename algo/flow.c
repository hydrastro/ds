#include "flow.h"
#include <math.h>
#include <string.h>

#define FL_NONE DS_FLOW_NONE

/* ---- network ------------------------------------------------------------- */

ds_status_t ds_flow_network_init(ds_flow_network_t *net, size_t vertex_count,
                                 ds_context_t *context) {
  size_t i;
  if (net == NULL) {
    return DS_ERR_NULL;
  }
  memset(net, 0, sizeof *net);
  net->context = context;
  net->vertex_count = vertex_count;
  net->epsilon = 0;
  if (vertex_count > ((size_t)-1) / 4 / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  net->head = (size_t *)ds_context_alloc(
      context, (vertex_count == 0 ? 1 : vertex_count) * sizeof(size_t));
  if (net->head == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < vertex_count; i++) {
    net->head[i] = FL_NONE;
  }
  return DS_OK;
}

void ds_flow_network_destroy(ds_flow_network_t *net) {
  if (net == NULL) {
    return;
  }
  ds_context_free(net->context, net->head);
  ds_context_free(net->context, net->next);
  ds_context_free(net->context, net->to);
  ds_context_free(net->context, net->cap);
  ds_context_free(net->context, net->cost);
  ds_context_free(net->context, net->original);
  memset(net, 0, sizeof *net);
}

static ds_status_t fl_grow(ds_flow_network_t *net) {
  size_t c = net->arc_capacity == 0 ? 16 : net->arc_capacity * 2;
  size_t *nx;
  size_t *to;
  double *cap;
  double *cost;
  double *orig;
  ds_context_t *ctx = net->context;
  if (c > ((size_t)-1) / 2 / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  nx = (size_t *)ds_context_realloc(ctx, net->next, c * sizeof(size_t));
  if (nx == NULL) {
    return DS_ERR_ALLOC;
  }
  net->next = nx;
  to = (size_t *)ds_context_realloc(ctx, net->to, c * sizeof(size_t));
  if (to == NULL) {
    return DS_ERR_ALLOC;
  }
  net->to = to;
  cap = (double *)ds_context_realloc(ctx, net->cap, c * sizeof(double));
  if (cap == NULL) {
    return DS_ERR_ALLOC;
  }
  net->cap = cap;
  cost = (double *)ds_context_realloc(ctx, net->cost, c * sizeof(double));
  if (cost == NULL) {
    return DS_ERR_ALLOC;
  }
  net->cost = cost;
  orig = (double *)ds_context_realloc(ctx, net->original,
                                      (c / 2) * sizeof(double));
  if (orig == NULL) {
    return DS_ERR_ALLOC;
  }
  net->original = orig;
  net->arc_capacity = c;
  return DS_OK;
}

ds_status_t ds_flow_network_add_edge(ds_flow_network_t *net, size_t from,
                                     size_t to, double capacity, double cost,
                                     size_t *out_edge) {
  size_t a;
  ds_status_t status;
  if (from >= net->vertex_count || to >= net->vertex_count) {
    return DS_ERR_RANGE;
  }
  if (!(capacity >= 0)) {
    return DS_ERR_INVALID;
  }
  if (net->arc_count + 2 > net->arc_capacity) {
    status = fl_grow(net);
    if (status != DS_OK) {
      return status;
    }
  }
  a = net->arc_count;
  net->to[a] = to;
  net->cap[a] = capacity;
  net->cost[a] = cost;
  net->next[a] = net->head[from];
  net->head[from] = a;
  net->to[a + 1] = from;
  net->cap[a + 1] = 0;
  net->cost[a + 1] = -cost;
  net->next[a + 1] = net->head[to];
  net->head[to] = a + 1;
  net->original[a / 2] = capacity;
  net->arc_count += 2;
  if (out_edge != NULL) {
    *out_edge = a / 2;
  }
  return DS_OK;
}

size_t ds_flow_network_edge_count(const ds_flow_network_t *net) {
  return net->arc_count / 2;
}

double ds_flow_network_flow(const ds_flow_network_t *net, size_t edge) {
  return net->original[edge] - net->cap[2 * edge];
}

void ds_flow_network_reset(ds_flow_network_t *net) {
  size_t e;
  for (e = 0; e < net->arc_count / 2; e++) {
    net->cap[2 * e] = net->original[e];
    net->cap[2 * e + 1] = 0;
  }
}

static size_t fl_from(const ds_flow_network_t *net, size_t a) {
  return net->to[a ^ 1U];
}

static bool fl_open(const ds_flow_network_t *net, size_t a) {
  return net->cap[a] > net->epsilon;
}

static void fl_push(ds_flow_network_t *net, size_t a, double f) {
  net->cap[a] -= f;
  net->cap[a ^ 1U] += f;
}

static ds_status_t fl_check(const ds_flow_network_t *net, size_t s, size_t t) {
  if (net == NULL) {
    return DS_ERR_NULL;
  }
  if (s >= net->vertex_count || t >= net->vertex_count || s == t) {
    return DS_ERR_RANGE;
  }
  return DS_OK;
}

/* BFS levels from s over open arcs; level[v] = FL_NONE if unreachable. */
static bool fl_levels(const ds_flow_network_t *net, size_t s, size_t t,
                      size_t *level, size_t *queue) {
  size_t qh = 0;
  size_t qt = 0;
  size_t v;
  size_t a;
  for (v = 0; v < net->vertex_count; v++) {
    level[v] = FL_NONE;
  }
  level[s] = 0;
  queue[qt++] = s;
  while (qh < qt) {
    v = queue[qh++];
    for (a = net->head[v]; a != FL_NONE; a = net->next[a]) {
      if (fl_open(net, a) && level[net->to[a]] == FL_NONE) {
        level[net->to[a]] = level[v] + 1;
        queue[qt++] = net->to[a];
      }
    }
  }
  return level[t] != FL_NONE;
}

/* ---- Edmonds-Karp -------------------------------------------------------- */

ds_status_t ds_flow_edmonds_karp(ds_flow_network_t *net, size_t source,
                                 size_t sink, double *out_value,
                                 ds_context_t *context) {
  size_t n;
  size_t *parent;
  size_t *queue;
  size_t qh;
  size_t qt;
  size_t v;
  size_t a;
  double f;
  double total = 0;
  ds_status_t status = fl_check(net, source, sink);
  if (status != DS_OK) {
    return status;
  }
  n = net->vertex_count;
  parent = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  queue = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  if (parent == NULL || queue == NULL) {
    ds_context_free(context, parent);
    ds_context_free(context, queue);
    return DS_ERR_ALLOC;
  }
  for (;;) {
    for (v = 0; v < n; v++) {
      parent[v] = FL_NONE;
    }
    qh = 0;
    qt = 0;
    queue[qt++] = source;
    parent[source] = FL_NONE - 1; /* visited marker */
    while (qh < qt && parent[sink] == FL_NONE) {
      v = queue[qh++];
      for (a = net->head[v]; a != FL_NONE; a = net->next[a]) {
        if (fl_open(net, a) && parent[net->to[a]] == FL_NONE) {
          parent[net->to[a]] = a;
          queue[qt++] = net->to[a];
        }
      }
    }
    if (parent[sink] == FL_NONE) {
      break;
    }
    f = HUGE_VAL;
    for (v = sink; v != source; v = fl_from(net, parent[v])) {
      if (net->cap[parent[v]] < f) {
        f = net->cap[parent[v]];
      }
    }
    for (v = sink; v != source; v = fl_from(net, parent[v])) {
      fl_push(net, parent[v], f);
    }
    total += f;
  }
  ds_context_free(context, parent);
  ds_context_free(context, queue);
  if (out_value != NULL) {
    *out_value = total;
  }
  return DS_OK;
}

/* ---- Dinic --------------------------------------------------------------- */

ds_status_t ds_flow_dinic(ds_flow_network_t *net, size_t source, size_t sink,
                          double *out_value, ds_context_t *context) {
  size_t n;
  size_t *level;
  size_t *queue;
  size_t *it;
  size_t *path;
  size_t depth;
  size_t v;
  size_t a;
  size_t k;
  size_t cut;
  double f;
  double total = 0;
  ds_status_t status = fl_check(net, source, sink);
  if (status != DS_OK) {
    return status;
  }
  n = net->vertex_count;
  level = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  queue = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  it = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  path = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  if (level == NULL || queue == NULL || it == NULL || path == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  while (fl_levels(net, source, sink, level, queue)) {
    for (v = 0; v < n; v++) {
      it[v] = net->head[v];
    }
    depth = 0;
    v = source;
    for (;;) {
      if (v == sink) {
        f = HUGE_VAL;
        for (k = 0; k < depth; k++) {
          if (net->cap[path[k]] < f) {
            f = net->cap[path[k]];
          }
        }
        cut = depth;
        for (k = 0; k < depth; k++) {
          fl_push(net, path[k], f);
          if (cut == depth && !fl_open(net, path[k])) {
            cut = k;
          }
        }
        total += f;
        depth = cut; /* retreat to the tail of the first saturated arc */
        v = depth == 0 ? source : net->to[path[depth - 1]];
        continue;
      }
      a = it[v];
      while (a != FL_NONE &&
             !(fl_open(net, a) && level[net->to[a]] != FL_NONE &&
               level[net->to[a]] == level[v] + 1)) {
        a = net->next[a];
      }
      it[v] = a;
      if (a == FL_NONE) {
        if (v == source) {
          break;
        }
        level[v] = FL_NONE; /* dead end for the rest of the phase */
        depth--;
        v = depth == 0 ? source : net->to[path[depth - 1]];
        it[v] = net->next[it[v]];
        continue;
      }
      path[depth++] = a;
      v = net->to[a];
    }
  }
  status = DS_OK;
out:
  ds_context_free(context, level);
  ds_context_free(context, queue);
  ds_context_free(context, it);
  ds_context_free(context, path);
  if (status == DS_OK && out_value != NULL) {
    *out_value = total;
  }
  return status;
}

/* ---- Malhotra-Kumar-Maheshwari ------------------------------------------- */

typedef struct fl_mkm {
  ds_flow_network_t *net;
  size_t *level;
  unsigned char *alive;
  double *inpot;
  double *outpot;
  double *excess;
  size_t *queue;
  size_t *stack;
  size_t s;
  size_t t;
} fl_mkm_t;

/* Arc a (v -> w) belongs to the current layered graph. */
static bool mkm_arc(const fl_mkm_t *m, size_t a) {
  size_t v = fl_from(m->net, a);
  size_t w = m->net->to[a];
  return m->alive[v] && m->alive[w] && m->level[w] != FL_NONE &&
         m->level[v] != FL_NONE && m->level[w] == m->level[v] + 1 &&
         fl_open(m->net, a);
}

static double mkm_pot(const fl_mkm_t *m, size_t v) {
  if (v == m->s) {
    return m->outpot[v];
  }
  if (v == m->t) {
    return m->inpot[v];
  }
  return m->inpot[v] < m->outpot[v] ? m->inpot[v] : m->outpot[v];
}

/* Delete v and every vertex whose potential drops to zero as a result. */
static void mkm_delete(fl_mkm_t *m, size_t v) {
  size_t sp = 0;
  size_t b;
  size_t a;
  size_t w;
  ds_flow_network_t *net = m->net;
  m->stack[sp++] = v;
  while (sp > 0) {
    v = m->stack[--sp];
    if (!m->alive[v]) {
      continue;
    }
    for (a = net->head[v]; a != FL_NONE; a = net->next[a]) {
      if (mkm_arc(m, a)) { /* out-arc v -> w */
        w = net->to[a];
        m->inpot[w] -= net->cap[a];
        if (mkm_pot(m, w) <= net->epsilon) {
          m->stack[sp++] = w;
        }
      }
      b = a ^ 1U; /* in-arc w -> v */
      if (mkm_arc(m, b)) {
        w = net->to[a];
        m->outpot[w] -= net->cap[b];
        if (mkm_pot(m, w) <= net->epsilon) {
          m->stack[sp++] = w;
        }
      }
    }
    m->alive[v] = 0;
  }
}

/* Route `amount` from v towards t (forward) or towards s (backward). */
static void mkm_route(fl_mkm_t *m, size_t v, double amount, bool forward) {
  ds_flow_network_t *net = m->net;
  size_t qh = 0;
  size_t qt = 0;
  size_t u;
  size_t a;
  size_t arc;
  size_t w;
  size_t end = forward ? m->t : m->s;
  double x;
  m->excess[v] = amount;
  m->queue[qt++] = v;
  while (qh < qt) {
    u = m->queue[qh++];
    if (u == end) {
      m->excess[u] = 0;
      continue;
    }
    for (a = net->head[u]; a != FL_NONE && m->excess[u] > net->epsilon;
         a = net->next[a]) {
      arc = forward ? a : a ^ 1U; /* the layered arc to use */
      if (!mkm_arc(m, arc)) {
        continue;
      }
      w = net->to[a];
      x = net->cap[arc] < m->excess[u] ? net->cap[arc] : m->excess[u];
      fl_push(net, arc, x);
      if (forward) {
        m->outpot[u] -= x;
        m->inpot[w] -= x;
      } else {
        m->inpot[u] -= x;
        m->outpot[w] -= x;
      }
      if (!(m->excess[w] > 0)) {
        m->queue[qt++] = w;
      }
      m->excess[w] += x;
      m->excess[u] -= x;
    }
    m->excess[u] = 0;
  }
}

ds_status_t ds_flow_mkm(ds_flow_network_t *net, size_t source, size_t sink,
                        double *out_value, ds_context_t *context) {
  fl_mkm_t m;
  size_t n;
  size_t v;
  size_t a;
  size_t best;
  size_t qh;
  size_t qt;
  double p;
  double bp;
  double total = 0;
  unsigned char *useful;
  ds_status_t status = fl_check(net, source, sink);
  if (status != DS_OK) {
    return status;
  }
  n = net->vertex_count;
  m.net = net;
  m.s = source;
  m.t = sink;
  m.level = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  m.alive = (unsigned char *)ds_context_alloc(context, n);
  useful = (unsigned char *)ds_context_alloc(context, n);
  m.inpot = (double *)ds_context_alloc(context, n * sizeof(double));
  m.outpot = (double *)ds_context_alloc(context, n * sizeof(double));
  m.excess = (double *)ds_context_alloc(context, n * sizeof(double));
  m.queue = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  m.stack = (size_t *)ds_context_alloc(context, (2 * net->arc_count + n + 1) *
                                                    sizeof(size_t));
  if (m.level == NULL || m.alive == NULL || useful == NULL || m.inpot == NULL ||
      m.outpot == NULL || m.excess == NULL || m.queue == NULL ||
      m.stack == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  while (fl_levels(net, source, sink, m.level, m.queue)) {
    /* Keep only vertices on some shortest s-t path: backward BFS from t. */
    memset(useful, 0, n);
    useful[sink] = 1;
    qh = 0;
    qt = 0;
    m.queue[qt++] = sink;
    while (qh < qt) {
      v = m.queue[qh++];
      for (a = net->head[v]; a != FL_NONE; a = net->next[a]) {
        size_t w = net->to[a]; /* arc a ^ 1 goes w -> v */
        if (!useful[w] && fl_open(net, a ^ 1U) && m.level[w] != FL_NONE &&
            m.level[w] + 1 == m.level[v]) {
          useful[w] = 1;
          m.queue[qt++] = w;
        }
      }
    }
    for (v = 0; v < n; v++) {
      m.alive[v] = useful[v];
      m.inpot[v] = 0;
      m.outpot[v] = 0;
      m.excess[v] = 0;
    }
    for (v = 0; v < n; v++) {
      if (!m.alive[v]) {
        continue;
      }
      for (a = net->head[v]; a != FL_NONE; a = net->next[a]) {
        if (mkm_arc(&m, a)) {
          m.outpot[v] += net->cap[a];
          m.inpot[net->to[a]] += net->cap[a];
        }
      }
    }
    while (m.alive[source] && m.alive[sink]) {
      best = FL_NONE;
      bp = HUGE_VAL;
      for (v = 0; v < n; v++) {
        if (m.alive[v]) {
          p = mkm_pot(&m, v);
          if (p < bp) {
            bp = p;
            best = v;
          }
        }
      }
      if (bp > net->epsilon) {
        if (best != sink) {
          mkm_route(&m, best, bp, true);
        }
        if (best != source) {
          mkm_route(&m, best, bp, false);
        }
        total += bp;
      }
      mkm_delete(&m, best);
    }
  }
  status = DS_OK;
out:
  ds_context_free(context, m.level);
  ds_context_free(context, m.alive);
  ds_context_free(context, useful);
  ds_context_free(context, m.inpot);
  ds_context_free(context, m.outpot);
  ds_context_free(context, m.excess);
  ds_context_free(context, m.queue);
  ds_context_free(context, m.stack);
  if (status == DS_OK && out_value != NULL) {
    *out_value = total;
  }
  return status;
}

/* ---- push-relabel -------------------------------------------------------- */

ds_status_t ds_flow_push_relabel(ds_flow_network_t *net, size_t source,
                                 size_t sink, double *out_value,
                                 ds_context_t *context) {
  size_t n;
  size_t *height;
  size_t *count;
  size_t *it;
  size_t *queue; /* circular FIFO of active vertices */
  unsigned char *active;
  double *excess;
  size_t qh = 0;
  size_t qn = 0;
  size_t u;
  size_t v;
  size_t a;
  size_t h;
  size_t old;
  double x;
  ds_status_t status = fl_check(net, source, sink);
  if (status != DS_OK) {
    return status;
  }
  n = net->vertex_count;
  height = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  count = (size_t *)ds_context_calloc(context, 2 * n + 1, sizeof(size_t));
  it = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  queue = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  active = (unsigned char *)ds_context_calloc(context, n, 1);
  excess = (double *)ds_context_calloc(context, n, sizeof(double));
  if (height == NULL || count == NULL || it == NULL || queue == NULL ||
      active == NULL || excess == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  for (v = 0; v < n; v++) {
    height[v] = 0;
    it[v] = net->head[v];
  }
  height[source] = n;
  count[0] = n - 1;
  count[n] = 1;
  for (a = net->head[source]; a != FL_NONE; a = net->next[a]) {
    if (fl_open(net, a)) {
      x = net->cap[a];
      v = net->to[a];
      fl_push(net, a, x);
      excess[v] += x;
      excess[source] -= x;
      if (v != sink && v != source && !active[v]) {
        active[v] = 1;
        queue[(qh + qn++) % n] = v;
      }
    }
  }
  while (qn > 0) {
    u = queue[qh];
    qh = (qh + 1) % n;
    qn--;
    active[u] = 0;
    while (excess[u] > net->epsilon) {
      a = it[u];
      if (a == FL_NONE) {
        /* relabel, with the gap heuristic */
        old = height[u];
        h = 2 * n;
        for (a = net->head[u]; a != FL_NONE; a = net->next[a]) {
          if (fl_open(net, a) && height[net->to[a]] + 1 < h) {
            h = height[net->to[a]] + 1;
          }
        }
        count[old]--;
        if (h > 2 * n - 1) {
          h = 2 * n - 1;
        }
        height[u] = h;
        count[h]++;
        it[u] = net->head[u];
        if (count[old] == 0 && old < n) {
          for (v = 0; v < n; v++) {
            if (v != source && height[v] > old && height[v] < n) {
              count[height[v]]--;
              height[v] = n + 1;
              count[n + 1]++;
              it[v] = net->head[v];
            }
          }
        }
        if (height[u] >= 2 * n - 1) {
          break; /* cannot happen for valid preflows; guards rounding */
        }
        continue;
      }
      v = net->to[a];
      if (fl_open(net, a) && height[u] == height[v] + 1) {
        x = net->cap[a] < excess[u] ? net->cap[a] : excess[u];
        fl_push(net, a, x);
        excess[u] -= x;
        excess[v] += x;
        if (v != source && v != sink && !active[v]) {
          active[v] = 1;
          queue[(qh + qn++) % n] = v;
        }
      } else {
        it[u] = net->next[a];
      }
    }
  }
  status = DS_OK;
  if (out_value != NULL) {
    *out_value = excess[sink];
  }
out:
  ds_context_free(context, height);
  ds_context_free(context, count);
  ds_context_free(context, it);
  ds_context_free(context, queue);
  ds_context_free(context, active);
  ds_context_free(context, excess);
  return status;
}

ds_status_t ds_flow_max_flow(ds_flow_network_t *net, size_t source, size_t sink,
                             ds_flow_algorithm_t algorithm, double *out_value,
                             ds_context_t *context) {
  switch (algorithm) {
  case DS_FLOW_EDMONDS_KARP:
    return ds_flow_edmonds_karp(net, source, sink, out_value, context);
  case DS_FLOW_DINIC:
    return ds_flow_dinic(net, source, sink, out_value, context);
  case DS_FLOW_MKM:
    return ds_flow_mkm(net, source, sink, out_value, context);
  case DS_FLOW_PUSH_RELABEL:
    return ds_flow_push_relabel(net, source, sink, out_value, context);
  default:
    return DS_ERR_INVALID;
  }
}

ds_status_t ds_flow_min_cut(const ds_flow_network_t *net, size_t source,
                            unsigned char *out_side, ds_context_t *context) {
  size_t *queue;
  size_t qh = 0;
  size_t qt = 0;
  size_t v;
  size_t a;
  if (net == NULL || out_side == NULL) {
    return DS_ERR_NULL;
  }
  if (source >= net->vertex_count) {
    return DS_ERR_RANGE;
  }
  queue =
      (size_t *)ds_context_alloc(context, net->vertex_count * sizeof(size_t));
  if (queue == NULL) {
    return DS_ERR_ALLOC;
  }
  memset(out_side, 0, net->vertex_count);
  out_side[source] = 1;
  queue[qt++] = source;
  while (qh < qt) {
    v = queue[qh++];
    for (a = net->head[v]; a != FL_NONE; a = net->next[a]) {
      if (fl_open(net, a) && !out_side[net->to[a]]) {
        out_side[net->to[a]] = 1;
        queue[qt++] = net->to[a];
      }
    }
  }
  ds_context_free(context, queue);
  return DS_OK;
}

/* ---- minimum-cost flow --------------------------------------------------- */

typedef struct fl_heap {
  double *key;
  size_t *val;
  size_t size;
} fl_heap_t;

static void fl_heap_push(fl_heap_t *h, double key, size_t val) {
  size_t i = h->size++;
  size_t p;
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
}

static void fl_heap_pop(fl_heap_t *h, double *key, size_t *val) {
  double k;
  size_t v;
  size_t i = 0;
  size_t c;
  *key = h->key[0];
  *val = h->val[0];
  h->size--;
  if (h->size == 0) {
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

ds_status_t ds_flow_min_cost_flow(ds_flow_network_t *net, size_t source,
                                  size_t sink, double limit, double *out_flow,
                                  double *out_cost, ds_context_t *context) {
  size_t n;
  double *pot = NULL;
  double *dist = NULL;
  size_t *parent = NULL;
  fl_heap_t heap;
  size_t round;
  size_t v;
  size_t a;
  size_t w;
  bool changed;
  double d;
  double rc;
  double f;
  double flow = 0;
  double cost = 0;
  ds_status_t status = fl_check(net, source, sink);
  heap.key = NULL;
  heap.val = NULL;
  if (status != DS_OK) {
    return status;
  }
  n = net->vertex_count;
  pot = (double *)ds_context_alloc(context, n * sizeof(double));
  dist = (double *)ds_context_alloc(context, n * sizeof(double));
  parent = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  heap.key = (double *)ds_context_alloc(context,
                                        (net->arc_count + n) * sizeof(double));
  heap.val = (size_t *)ds_context_alloc(context,
                                        (net->arc_count + n) * sizeof(size_t));
  if (pot == NULL || dist == NULL || parent == NULL || heap.key == NULL ||
      heap.val == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  /* Bellman-Ford potentials (negative costs allowed). */
  for (v = 0; v < n; v++) {
    pot[v] = HUGE_VAL;
  }
  pot[source] = 0;
  for (round = 0; round < n; round++) {
    changed = false;
    for (v = 0; v < n; v++) {
      if (!(pot[v] < HUGE_VAL)) {
        continue;
      }
      for (a = net->head[v]; a != FL_NONE; a = net->next[a]) {
        if (fl_open(net, a) && pot[v] + net->cost[a] < pot[net->to[a]]) {
          pot[net->to[a]] = pot[v] + net->cost[a];
          changed = true;
        }
      }
    }
    if (!changed) {
      break;
    }
  }
  if (round == n) {
    status = DS_ERR_INVALID; /* negative cycle reachable from the source */
    goto out;
  }
  for (v = 0; v < n; v++) {
    if (!(pot[v] < HUGE_VAL)) {
      pot[v] = 0;
    }
  }
  while (flow < limit) {
    for (v = 0; v < n; v++) {
      dist[v] = HUGE_VAL;
      parent[v] = FL_NONE;
    }
    dist[source] = 0;
    heap.size = 0;
    fl_heap_push(&heap, 0, source);
    while (heap.size > 0) {
      fl_heap_pop(&heap, &d, &v);
      if (d > dist[v]) {
        continue;
      }
      for (a = net->head[v]; a != FL_NONE; a = net->next[a]) {
        if (!fl_open(net, a)) {
          continue;
        }
        w = net->to[a];
        rc = net->cost[a] + pot[v] - pot[w];
        if (rc < 0) {
          rc = 0; /* rounding */
        }
        if (dist[v] + rc < dist[w]) {
          dist[w] = dist[v] + rc;
          parent[w] = a;
          fl_heap_push(&heap, dist[w], w);
        }
      }
    }
    if (parent[sink] == FL_NONE) {
      break;
    }
    for (v = 0; v < n; v++) {
      if (dist[v] < HUGE_VAL) {
        pot[v] += dist[v];
      }
    }
    f = limit - flow;
    for (v = sink; v != source; v = fl_from(net, parent[v])) {
      if (net->cap[parent[v]] < f) {
        f = net->cap[parent[v]];
      }
    }
    for (v = sink; v != source; v = fl_from(net, parent[v])) {
      fl_push(net, parent[v], f);
      cost += f * net->cost[parent[v]];
    }
    flow += f;
  }
  status = DS_OK;
out:
  ds_context_free(context, pot);
  ds_context_free(context, dist);
  ds_context_free(context, parent);
  ds_context_free(context, heap.key);
  ds_context_free(context, heap.val);
  if (status == DS_OK) {
    if (out_flow != NULL) {
      *out_flow = flow;
    }
    if (out_cost != NULL) {
      *out_cost = cost;
    }
  }
  return status;
}

/* ---- connectivity -------------------------------------------------------- */

/* Unit-capacity network of the view; with split, vertex v becomes
 * v_in = v and v_out = v + n joined by an arc of capacity 1 (edge 0..n-1). */
static ds_status_t fl_from_view(const ds_gview_t *g, bool split,
                                ds_flow_network_t *net, ds_context_t *ctx) {
  size_t n = g->vertex_count;
  size_t v;
  size_t i;
  size_t deg;
  size_t to;
  double w;
  ds_status_t status = ds_flow_network_init(net, split ? 2 * n : n, ctx);
  if (status != DS_OK) {
    return status;
  }
  if (split) {
    for (v = 0; v < n && status == DS_OK; v++) {
      status = ds_flow_network_add_edge(net, v, v + n, 1, 0, NULL);
    }
  }
  for (v = 0; v < n && status == DS_OK; v++) {
    deg = g->degree(g->graph, v);
    for (i = 0; i < deg && status == DS_OK; i++) {
      g->edge(g->graph, v, i, &to, &w);
      if (to == v || to >= n) {
        continue;
      }
      status = ds_flow_network_add_edge(net, split ? v + n : v, to,
                                        split ? (double)n : 1.0, 0, NULL);
    }
  }
  if (status != DS_OK) {
    ds_flow_network_destroy(net);
  }
  return status;
}

ds_status_t ds_flow_edge_connectivity(const ds_gview_t *g, size_t *out_value,
                                      ds_context_t *context) {
  ds_flow_network_t net;
  size_t n;
  size_t v;
  size_t best;
  double f;
  ds_status_t status;
  if (g == NULL || out_value == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  if (n < 2) {
    *out_value = 0;
    return DS_OK;
  }
  status = fl_from_view(g, false, &net, context);
  if (status != DS_OK) {
    return status;
  }
  best = (size_t)-1;
  for (v = 1; v < n && best > 0; v++) {
    ds_flow_network_reset(&net);
    status = ds_flow_dinic(&net, 0, v, &f, context);
    if (status == DS_OK && (size_t)f < best) {
      best = (size_t)f;
    }
    if (status == DS_OK) {
      ds_flow_network_reset(&net);
      status = ds_flow_dinic(&net, v, 0, &f, context);
      if (status == DS_OK && (size_t)f < best) {
        best = (size_t)f;
      }
    }
    if (status != DS_OK) {
      break;
    }
  }
  ds_flow_network_destroy(&net);
  if (status == DS_OK) {
    *out_value = best;
  }
  return status;
}

static bool fl_adjacent(const ds_gview_t *g, size_t u, size_t v) {
  size_t deg = g->degree(g->graph, u);
  size_t i;
  size_t to;
  double w;
  for (i = 0; i < deg; i++) {
    g->edge(g->graph, u, i, &to, &w);
    if (to == v) {
      return true;
    }
  }
  return false;
}

/* Max number of internally vertex-disjoint s -> t paths (s, t not
 * adjacent); the split network is reset first. */
static ds_status_t fl_split_flow(ds_flow_network_t *net, size_t n, size_t s,
                                 size_t t, double *out, ds_context_t *ctx) {
  ds_flow_network_reset(net);
  return ds_flow_dinic(net, s + n, t, out, ctx);
}

ds_status_t ds_flow_vertex_connectivity(const ds_gview_t *g, size_t *out_value,
                                        ds_context_t *context) {
  ds_flow_network_t net;
  size_t n;
  size_t i;
  size_t j;
  size_t best;
  double f;
  ds_status_t status;
  if (g == NULL || out_value == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  if (n < 2) {
    *out_value = 0;
    return DS_OK;
  }
  status = fl_from_view(g, true, &net, context);
  if (status != DS_OK) {
    return status;
  }
  best = n - 1;
  /* Even's algorithm: some vertex among the first best + 1 lies outside a
   * minimum separator. */
  for (i = 0; i <= best && i < n && status == DS_OK; i++) {
    for (j = 0; j < n && status == DS_OK; j++) {
      if (j == i) {
        continue;
      }
      /* Both directions: the vertex outside the separator may be on
       * either side of it. */
      if (!fl_adjacent(g, i, j)) {
        status = fl_split_flow(&net, n, i, j, &f, context);
        if (status == DS_OK && (size_t)f < best) {
          best = (size_t)f;
        }
      }
      if (status == DS_OK && !fl_adjacent(g, j, i)) {
        status = fl_split_flow(&net, n, j, i, &f, context);
        if (status == DS_OK && (size_t)f < best) {
          best = (size_t)f;
        }
      }
    }
  }
  ds_flow_network_destroy(&net);
  if (status == DS_OK) {
    *out_value = best;
  }
  return status;
}

ds_status_t ds_flow_min_vertex_cut(const ds_gview_t *g, size_t s, size_t t,
                                   size_t *out_cut, size_t *out_count,
                                   ds_context_t *context) {
  ds_flow_network_t net;
  unsigned char *side;
  size_t n;
  size_t v;
  size_t count = 0;
  double f;
  ds_status_t status;
  if (g == NULL || out_cut == NULL || out_count == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  if (s >= n || t >= n || s == t) {
    return DS_ERR_RANGE;
  }
  if (fl_adjacent(g, s, t)) {
    return DS_ERR_INVALID;
  }
  status = fl_from_view(g, true, &net, context);
  if (status != DS_OK) {
    return status;
  }
  side = (unsigned char *)ds_context_alloc(context, 2 * n);
  if (side == NULL) {
    ds_flow_network_destroy(&net);
    return DS_ERR_ALLOC;
  }
  status = fl_split_flow(&net, n, s, t, &f, context);
  if (status == DS_OK) {
    status = ds_flow_min_cut(&net, s + n, side, context);
  }
  if (status == DS_OK) {
    for (v = 0; v < n; v++) {
      if (v != s && v != t && side[v] && !side[v + n]) {
        out_cut[count++] = v;
      }
    }
    *out_count = count;
  }
  ds_context_free(context, side);
  ds_flow_network_destroy(&net);
  return status;
}
