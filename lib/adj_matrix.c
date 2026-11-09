#include "adj_matrix.h"
#include "checked.h"
#include <string.h>

ds_status_t ds_adj_matrix_init(ds_adj_matrix_t *m, size_t n, bool directed,
                               ds_context_t *context) {
  size_t cells;
  size_t bytes;
  size_t v;
  if (m == NULL) {
    return DS_ERR_NULL;
  }
  memset(m, 0, sizeof(*m));
  if (!ds_size_mul(n, n, &cells) || !ds_size_mul(cells, 3, &bytes) ||
      !ds_size_mul(bytes, sizeof(size_t), &bytes) ||
      !ds_size_mul(cells == 0 ? 1 : cells, sizeof(double), &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  m->n = n;
  m->directed = directed;
  m->context = context;
  if (cells == 0) {
    cells = 1;
  }
  m->weight = (double *)ds_context_alloc(context, cells * sizeof(double));
  m->pos = (size_t *)ds_context_alloc(context, cells * sizeof(size_t));
  m->list = (size_t *)ds_context_alloc(context, cells * sizeof(size_t));
  m->degree =
      (size_t *)ds_context_alloc(context, (n == 0 ? 1 : n) * sizeof(size_t));
  if (m->weight == NULL || m->pos == NULL || m->list == NULL ||
      m->degree == NULL) {
    ds_adj_matrix_destroy(m);
    return DS_ERR_ALLOC;
  }
  /* positions are validated against the packed list before use; they are
   * initialized anyway so that no read ever sees indeterminate memory */
  memset(m->pos, 0, cells * sizeof(size_t));
  for (v = 0; v < n; v++) {
    m->degree[v] = 0;
  }
  return DS_OK;
}

void ds_adj_matrix_destroy(ds_adj_matrix_t *m) {
  if (m == NULL) {
    return;
  }
  ds_context_free(m->context, m->weight);
  ds_context_free(m->context, m->pos);
  ds_context_free(m->context, m->list);
  ds_context_free(m->context, m->degree);
  memset(m, 0, sizeof(*m));
}

static bool am_has(const ds_adj_matrix_t *m, size_t u, size_t v) {
  size_t k = m->pos[u * m->n + v];
  return k < m->degree[u] && m->list[u * m->n + k] == v;
}

/* add or update one directed entry; true when it was new */
static bool am_put(ds_adj_matrix_t *m, size_t u, size_t v, double w) {
  bool fresh = !am_has(m, u, v);
  if (fresh) {
    size_t k = m->degree[u]++;
    m->list[u * m->n + k] = v;
    m->pos[u * m->n + v] = k;
    m->edges++;
  }
  m->weight[u * m->n + v] = w;
  return fresh;
}

static void am_drop(ds_adj_matrix_t *m, size_t u, size_t v) {
  size_t k = m->pos[u * m->n + v];
  size_t last = --m->degree[u];
  size_t moved = m->list[u * m->n + last];
  m->list[u * m->n + k] = moved;
  m->pos[u * m->n + moved] = k;
  m->edges--;
}

ds_status_t ds_adj_matrix_set(ds_adj_matrix_t *m, size_t u, size_t v,
                              double w) {
  bool fresh;
  if (m == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= m->n || v >= m->n) {
    return DS_ERR_RANGE;
  }
  fresh = am_put(m, u, v, w);
  if (!m->directed && u != v) {
    (void)am_put(m, v, u, w);
  }
  return fresh ? DS_OK : DS_EXISTS;
}

ds_status_t ds_adj_matrix_remove(ds_adj_matrix_t *m, size_t u, size_t v) {
  if (m == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= m->n || v >= m->n) {
    return DS_ERR_RANGE;
  }
  if (!am_has(m, u, v)) {
    return DS_NOT_FOUND;
  }
  am_drop(m, u, v);
  if (!m->directed && u != v) {
    am_drop(m, v, u);
  }
  return DS_OK;
}

bool ds_adj_matrix_has(const ds_adj_matrix_t *m, size_t u, size_t v) {
  return m != NULL && u < m->n && v < m->n && am_has(m, u, v);
}

ds_status_t ds_adj_matrix_weight(const ds_adj_matrix_t *m, size_t u, size_t v,
                                 double *out_weight) {
  if (m == NULL || out_weight == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= m->n || v >= m->n) {
    return DS_ERR_RANGE;
  }
  if (!am_has(m, u, v)) {
    return DS_NOT_FOUND;
  }
  *out_weight = m->weight[u * m->n + v];
  return DS_OK;
}

size_t ds_adj_matrix_vertex_count(const ds_adj_matrix_t *m) {
  return m == NULL ? 0 : m->n;
}

size_t ds_adj_matrix_entry_count(const ds_adj_matrix_t *m) {
  return m == NULL ? 0 : m->edges;
}

size_t ds_adj_matrix_degree(const ds_adj_matrix_t *m, size_t v) {
  return m == NULL || v >= m->n ? 0 : m->degree[v];
}

void ds_adj_matrix_edge(const ds_adj_matrix_t *m, size_t v, size_t index,
                        size_t *out_target, double *out_weight) {
  size_t t = m->list[v * m->n + index];
  if (out_target != NULL) {
    *out_target = t;
  }
  if (out_weight != NULL) {
    *out_weight = m->weight[v * m->n + t];
  }
}

void ds_adj_matrix_clear(ds_adj_matrix_t *m) {
  size_t v;
  if (m == NULL) {
    return;
  }
  for (v = 0; v < m->n; v++) {
    m->degree[v] = 0;
  }
  m->edges = 0;
}

void ds_adj_matrix_to_dense(const ds_adj_matrix_t *m, double absent,
                            double diagonal, double *out) {
  size_t u;
  size_t k;
  if (m == NULL || out == NULL) {
    return;
  }
  for (u = 0; u < m->n; u++) {
    for (k = 0; k < m->n; k++) {
      out[u * m->n + k] = u == k ? diagonal : absent;
    }
    for (k = 0; k < m->degree[u]; k++) {
      size_t t = m->list[u * m->n + k];
      out[u * m->n + t] = m->weight[u * m->n + t];
    }
  }
}
