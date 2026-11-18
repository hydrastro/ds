#include <dsa/core/checked.h>
#include <dsa/structures/hypergraph.h>
#include <string.h>

typedef struct hg_edge {
  ds_vec_t members;
  size_t dense;
} hg_edge_t;
static hg_edge_t *hg_get(const ds_hypergraph_t *g, ds_hyperedge_handle_t id) {
  hg_edge_t *edge = NULL;
  void *slot = ds_map_get(&g->edges, &id);
  if (slot != NULL)
    memcpy(&edge, slot, sizeof(edge));
  return edge;
}
static int hg_compare(const void *a, const void *b, void *user) {
  size_t x, y;
  (void)user;
  memcpy(&x, a, sizeof(x));
  memcpy(&y, b, sizeof(y));
  return (x > y) - (x < y);
}
static ds_status_t hg_members(ds_hypergraph_t *g, const size_t *vertices,
                              size_t count, ds_vec_t *out) {
  size_t i, used = 0, previous = 0;
  ds_status_t st = ds_vec_init(out, sizeof(size_t), g->context);
  if (st != DS_OK)
    return st;
  if (count > 0 && vertices == NULL) {
    st = DS_ERR_NULL;
    goto fail;
  }
  st = ds_vec_append(out, vertices, count);
  if (st != DS_OK)
    goto fail;
  ds_vec_sort(out, hg_compare, NULL);
  for (i = 0; i < out->size; i++) {
    size_t v;
    memcpy(&v, out->data + i * sizeof(v), sizeof(v));
    if (v >= g->vertex_count) {
      st = DS_ERR_RANGE;
      goto fail;
    }
    if (used == 0 || previous != v) {
      memcpy(out->data + used * sizeof(v), &v, sizeof(v));
      used++;
      previous = v;
    }
  }
  out->size = used;
  return DS_OK;
fail:
  ds_vec_destroy(out);
  return st;
}
static size_t hg_vertex(const ds_vec_t *members, size_t i) {
  size_t result;
  memcpy(&result, members->data + i * sizeof(result), sizeof(result));
  return result;
}
static ds_status_t hg_reserve_incident(ds_hypergraph_t *g,
                                       const ds_vec_t *members) {
  size_t i;
  for (i = 0; i < members->size; i++) {
    ds_vec_t *inc = &g->incident[hg_vertex(members, i)];
    ds_status_t st;
    if (inc->size == (size_t)-1)
      return DS_ERR_OVERFLOW;
    st = ds_vec_reserve(inc, inc->size + 1);
    if (st != DS_OK)
      return st;
  }
  return DS_OK;
}
static void hg_append_reserved(ds_vec_t *v, ds_hyperedge_handle_t id) {
  memcpy(v->data + v->size * sizeof(id), &id, sizeof(id));
  v->size++;
}
static void hg_attach(ds_hypergraph_t *g, const ds_vec_t *members,
                      ds_hyperedge_handle_t id) {
  size_t i;
  for (i = 0; i < members->size; i++)
    hg_append_reserved(&g->incident[hg_vertex(members, i)], id);
}
static void hg_detach(ds_hypergraph_t *g, const ds_vec_t *members,
                      ds_hyperedge_handle_t id) {
  size_t i, j;
  for (i = 0; i < members->size; i++) {
    ds_vec_t *inc = &g->incident[hg_vertex(members, i)];
    for (j = 0; j < inc->size; j++) {
      ds_hyperedge_handle_t candidate;
      memcpy(&candidate, inc->data + j * sizeof(candidate), sizeof(candidate));
      if (candidate == id) {
        (void)ds_vec_swap_remove(inc, j, NULL);
        break;
      }
    }
  }
}
void ds_hypergraph_config_init(ds_hypergraph_config_t *c) {
  if (c != NULL)
    memset(c, 0, sizeof(*c));
}
ds_status_t ds_hypergraph_init(ds_hypergraph_t *g,
                               const ds_hypergraph_config_t *c) {
  ds_status_t st;
  size_t offset, bytes;
  if (g == NULL || c == NULL)
    return DS_ERR_NULL;
  if (!ds_size_align(sizeof(hg_edge_t), DS_MAX_ALIGN, &offset) ||
      !ds_size_add(offset, c->value_size, &bytes))
    return DS_ERR_OVERFLOW;
  memset(g, 0, sizeof(*g));
  g->context = c->context;
  g->value_size = c->value_size;
  g->value_offset = offset;
  g->next_id = 1;
  st = ds_vec_init(&g->edge_ids, sizeof(ds_hyperedge_handle_t), g->context);
  if (st != DS_OK)
    return st;
  st = ds_map_init(&g->edges, c->edge_map_ops, sizeof(ds_hyperedge_handle_t),
                   sizeof(hg_edge_t *), ds_map_hash_ulong, ds_map_equal_ulong,
                   NULL, g->context);
  if (st != DS_OK) {
    ds_vec_destroy(&g->edge_ids);
    return st;
  }
  st = ds_hypergraph_resize_vertices(g, c->vertex_count);
  if (st != DS_OK)
    ds_hypergraph_destroy(g);
  return st;
}
void ds_hypergraph_destroy(ds_hypergraph_t *g) {
  size_t i;
  if (g == NULL)
    return;
  for (i = 0; i < g->edge_ids.size; i++) {
    ds_hyperedge_handle_t id;
    hg_edge_t *edge;
    memcpy(&id, g->edge_ids.data + i * sizeof(id), sizeof(id));
    edge = hg_get(g, id);
    ds_vec_destroy(&edge->members);
    ds_context_free(g->context, edge);
  }
  for (i = 0; i < g->vertex_count; i++)
    ds_vec_destroy(&g->incident[i]);
  ds_context_free(g->context, g->incident);
  ds_vec_destroy(&g->edge_ids);
  ds_map_destroy(&g->edges);
  memset(g, 0, sizeof(*g));
}
ds_status_t ds_hypergraph_resize_vertices(ds_hypergraph_t *g, size_t count) {
  size_t i, bytes;
  ds_vec_t *array;
  if (g == NULL)
    return DS_ERR_NULL;
  if (count == g->vertex_count)
    return DS_OK;
  if (count < g->vertex_count) {
    for (i = count; i < g->vertex_count; i++)
      if (g->incident[i].size != 0)
        return DS_ERR_STATE;
    for (i = count; i < g->vertex_count; i++)
      ds_vec_destroy(&g->incident[i]);
    g->vertex_count = count;
    return DS_OK;
  }
  if (!ds_size_mul(count, sizeof(ds_vec_t), &bytes))
    return DS_ERR_OVERFLOW;
  array = (ds_vec_t *)ds_context_alloc(g->context, bytes);
  if (array == NULL)
    return DS_ERR_ALLOC;
  if (g->vertex_count > 0)
    memcpy(array, g->incident, g->vertex_count * sizeof(*array));
  for (i = g->vertex_count; i < count; i++) {
    ds_status_t st =
        ds_vec_init(&array[i], sizeof(ds_hyperedge_handle_t), g->context);
    if (st != DS_OK) {
      while (i > g->vertex_count)
        ds_vec_destroy(&array[--i]);
      ds_context_free(g->context, array);
      return st;
    }
  }
  ds_context_free(g->context, g->incident);
  g->incident = array;
  g->vertex_count = count;
  return DS_OK;
}
size_t ds_hypergraph_vertex_count(const ds_hypergraph_t *g) {
  return g == NULL ? 0 : g->vertex_count;
}
size_t ds_hypergraph_edge_count(const ds_hypergraph_t *g) {
  return g == NULL ? 0 : g->edge_ids.size;
}
ds_status_t ds_hypergraph_insert(ds_hypergraph_t *g, const size_t *vertices,
                                 size_t count, const void *value,
                                 ds_hyperedge_handle_t *out) {
  hg_edge_t *edge;
  ds_status_t st;
  ds_hyperedge_handle_t id;
  if (g == NULL || out == NULL)
    return DS_ERR_NULL;
  if (g->next_id == 0 || g->edge_ids.size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  edge = (hg_edge_t *)ds_context_alloc(g->context,
                                       g->value_offset + g->value_size);
  if (edge == NULL)
    return DS_ERR_ALLOC;
  if (g->value_size > 0) {
    void *dst = (unsigned char *)edge + g->value_offset;
    if (value == NULL)
      memset(dst, 0, g->value_size);
    else
      memcpy(dst, value, g->value_size);
  }
  st = hg_members(g, vertices, count, &edge->members);
  if (st != DS_OK) {
    ds_context_free(g->context, edge);
    return st;
  }
  st = hg_reserve_incident(g, &edge->members);
  if (st == DS_OK)
    st = ds_vec_reserve(&g->edge_ids, g->edge_ids.size + 1);
  id = g->next_id;
  edge->dense = g->edge_ids.size;
  if (st == DS_OK)
    st = ds_map_put(&g->edges, &id, &edge);
  if (st != DS_OK) {
    ds_vec_destroy(&edge->members);
    ds_context_free(g->context, edge);
    return st;
  }
  hg_append_reserved(&g->edge_ids, id);
  hg_attach(g, &edge->members, id);
  g->next_id++;
  *out = id;
  return DS_OK;
}
ds_status_t ds_hypergraph_set_members(ds_hypergraph_t *g,
                                      ds_hyperedge_handle_t id,
                                      const size_t *vertices, size_t count) {
  hg_edge_t *edge;
  ds_vec_t members;
  ds_status_t st;
  if (g == NULL)
    return DS_ERR_NULL;
  edge = hg_get(g, id);
  if (edge == NULL)
    return DS_NOT_FOUND;
  st = hg_members(g, vertices, count, &members);
  if (st != DS_OK)
    return st;
  st = hg_reserve_incident(g, &members);
  if (st != DS_OK) {
    ds_vec_destroy(&members);
    return st;
  }
  hg_detach(g, &edge->members, id);
  hg_attach(g, &members, id);
  ds_vec_destroy(&edge->members);
  edge->members = members;
  return DS_OK;
}
ds_status_t ds_hypergraph_remove(ds_hypergraph_t *g, ds_hyperedge_handle_t id) {
  hg_edge_t *edge;
  hg_edge_t *moved_edge = NULL;
  ds_status_t st;
  if (g == NULL)
    return DS_ERR_NULL;
  edge = hg_get(g, id);
  if (edge == NULL)
    return DS_NOT_FOUND;
  if (edge->dense + 1 < g->edge_ids.size) {
    ds_hyperedge_handle_t moved;
    memcpy(&moved, g->edge_ids.data + (g->edge_ids.size - 1) * sizeof(moved),
           sizeof(moved));
    moved_edge = hg_get(g, moved);
    if (moved_edge == NULL)
      return DS_ERR_STATE;
  }
  st = ds_map_remove(&g->edges, &id, NULL);
  if (st != DS_OK)
    return st;
  hg_detach(g, &edge->members, id);
  (void)ds_vec_swap_remove(&g->edge_ids, edge->dense, NULL);
  if (moved_edge != NULL)
    moved_edge->dense = edge->dense;
  ds_vec_destroy(&edge->members);
  ds_context_free(g->context, edge);
  return DS_OK;
}
ds_status_t ds_hypergraph_members(const ds_hypergraph_t *g,
                                  ds_hyperedge_handle_t id,
                                  const size_t **vertices, size_t *count) {
  hg_edge_t *edge;
  if (g == NULL || vertices == NULL || count == NULL)
    return DS_ERR_NULL;
  edge = hg_get(g, id);
  if (edge == NULL)
    return DS_NOT_FOUND;
  *vertices = (const size_t *)(const void *)edge->members.data;
  *count = edge->members.size;
  return DS_OK;
}
void *ds_hypergraph_value(const ds_hypergraph_t *g, ds_hyperedge_handle_t id) {
  hg_edge_t *edge;
  if (g == NULL)
    return NULL;
  edge = hg_get(g, id);
  return edge == NULL ? NULL : (unsigned char *)edge + g->value_offset;
}
ds_status_t ds_hypergraph_set_value(ds_hypergraph_t *g,
                                    ds_hyperedge_handle_t id,
                                    const void *value) {
  void *dst = ds_hypergraph_value(g, id);
  if (g == NULL)
    return DS_ERR_NULL;
  if (dst == NULL)
    return DS_NOT_FOUND;
  if (g->value_size > 0) {
    if (value == NULL)
      memset(dst, 0, g->value_size);
    else
      memmove(dst, value, g->value_size);
  }
  return DS_OK;
}
ds_status_t ds_hypergraph_edge_at(const ds_hypergraph_t *g, size_t index,
                                  ds_hyperedge_handle_t *out) {
  if (g == NULL || out == NULL)
    return DS_ERR_NULL;
  return ds_vec_get(&g->edge_ids, index, out);
}
ds_status_t ds_hypergraph_edge_index(const ds_hypergraph_t *g,
                                     ds_hyperedge_handle_t id, size_t *out) {
  hg_edge_t *edge;
  if (g == NULL || out == NULL)
    return DS_ERR_NULL;
  edge = hg_get(g, id);
  if (edge == NULL)
    return DS_NOT_FOUND;
  *out = edge->dense;
  return DS_OK;
}
ds_status_t ds_hypergraph_incident(const ds_hypergraph_t *g, size_t vertex,
                                   const ds_hyperedge_handle_t **edges,
                                   size_t *count) {
  if (g == NULL || edges == NULL || count == NULL)
    return DS_ERR_NULL;
  if (vertex >= g->vertex_count)
    return DS_ERR_RANGE;
  *edges =
      (const ds_hyperedge_handle_t *)(const void *)g->incident[vertex].data;
  *count = g->incident[vertex].size;
  return DS_OK;
}
