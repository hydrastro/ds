#include "topology_tree.h"
#include <string.h>

#define TT_NONE DS_TOPOLOGY_NONE

static unsigned char *tt_value(const ds_topology_tree_t *t, size_t c) {
  return t->values + c * t->value_size;
}

ds_status_t ds_topology_tree_init(ds_topology_tree_t *t, size_t vertices,
                                  size_t value_size, const void *values,
                                  ds_topology_combine_fn combine, void *user,
                                  ds_context_t *context) {
  size_t v;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (vertices == 0 || (value_size > 0 && combine == NULL)) {
    return DS_ERR_INVALID;
  }
  if (vertices > (size_t)-1 / (4 * sizeof(ds_topology_cluster_t)) ||
      (value_size > 0 && vertices > (size_t)-1 / 4 / value_size)) {
    return DS_ERR_OVERFLOW;
  }
  t->vertices = vertices;
  t->value_size = value_size;
  t->combine = combine;
  t->user = user;
  t->context = context;
  t->free_list = TT_NONE;
  t->capacity = 2 * vertices;
  t->adj = (size_t *)ds_context_alloc(context, 3 * vertices * sizeof(size_t));
  t->degree = (unsigned char *)ds_context_calloc(context, vertices, 1);
  t->clusters = (ds_topology_cluster_t *)ds_context_alloc(
      context, t->capacity * sizeof(ds_topology_cluster_t));
  t->values = (unsigned char *)ds_context_calloc(
      context, t->capacity * value_size + 1, 1);
  if (t->adj == NULL || t->degree == NULL || t->clusters == NULL ||
      t->values == NULL) {
    ds_topology_tree_destroy(t);
    return DS_ERR_ALLOC;
  }
  for (v = 0; v < vertices; v++) {
    ds_topology_cluster_t *c = &t->clusters[v];
    c->level = 0;
    c->parent = TT_NONE;
    c->child[0] = v;
    c->child[1] = TT_NONE;
    c->edges = 0;
    c->size = 1;
    c->alive = true;
  }
  if (values != NULL && value_size > 0) {
    memcpy(t->values, values, vertices * value_size);
  }
  t->count = vertices;
  return DS_OK;
}

void ds_topology_tree_destroy(ds_topology_tree_t *t) {
  size_t l;
  if (t == NULL) {
    return;
  }
  for (l = 0; l < t->levels; l++) {
    ds_context_free(t->context, t->pending[l]);
  }
  ds_context_free(t->context, (void *)t->pending);
  ds_context_free(t->context, t->pending_count);
  ds_context_free(t->context, t->pending_capacity);
  ds_context_free(t->context, t->adj);
  ds_context_free(t->context, t->degree);
  ds_context_free(t->context, t->clusters);
  ds_context_free(t->context, t->values);
  memset(t, 0, sizeof(*t));
}

static size_t tt_anc(const ds_topology_tree_t *t, size_t v, size_t level) {
  size_t c = v;
  while (t->clusters[c].level < level) {
    c = t->clusters[c].parent;
    if (c == TT_NONE) {
      return TT_NONE;
    }
  }
  return c;
}

/* make work-list slots exist for levels 0..level */
static ds_status_t tt_ensure_levels(ds_topology_tree_t *t, size_t level) {
  size_t nl, i;
  size_t **p;
  size_t *pc, *pk;
  if (level < t->levels) {
    return DS_OK;
  }
  nl = t->levels < 8 ? 16 : t->levels * 2;
  while (nl <= level) {
    nl *= 2;
  }
  /* each array keeps working at the old length until all three grew */
  p = (size_t **)ds_context_realloc(t->context, (void *)t->pending,
                                    nl * sizeof(size_t *));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->pending = p;
  pc = (size_t *)ds_context_realloc(t->context, t->pending_count,
                                    nl * sizeof(size_t));
  if (pc == NULL) {
    return DS_ERR_ALLOC;
  }
  t->pending_count = pc;
  pk = (size_t *)ds_context_realloc(t->context, t->pending_capacity,
                                    nl * sizeof(size_t));
  if (pk == NULL) {
    return DS_ERR_ALLOC;
  }
  t->pending_capacity = pk;
  for (i = t->levels; i < nl; i++) {
    t->pending[i] = NULL;
    t->pending_count[i] = 0;
    t->pending_capacity[i] = 0;
  }
  t->levels = nl;
  return DS_OK;
}

static ds_status_t tt_level_room(ds_topology_tree_t *t, size_t level,
                                 size_t need) {
  size_t nc;
  size_t *q;
  ds_status_t st = tt_ensure_levels(t, level);
  if (st != DS_OK) {
    return st;
  }
  if (need <= t->pending_capacity[level]) {
    return DS_OK;
  }
  nc = t->pending_capacity[level] < 4 ? 8 : 2 * t->pending_capacity[level];
  while (nc < need) {
    nc *= 2;
  }
  q = (size_t *)ds_context_realloc(t->context, t->pending[level],
                                   nc * sizeof(size_t));
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  t->pending[level] = q;
  t->pending_capacity[level] = nc;
  return DS_OK;
}

static ds_status_t tt_push(ds_topology_tree_t *t, size_t level, size_t c) {
  ds_status_t st = tt_level_room(
      t, level, (level < t->levels ? t->pending_count[level] : 0) + 1);
  if (st != DS_OK) {
    return st;
  }
  t->pending[level][t->pending_count[level]++] = c;
  return DS_OK;
}

static size_t tt_height_of(const ds_topology_tree_t *t, size_t v) {
  size_t r = v;
  while (t->clusters[r].parent != TT_NONE) {
    r = t->clusters[r].parent;
  }
  return t->clusters[r].level;
}

/* work lists with `room` entries on every level the update can reach (the
 * merged tree is at most one level taller), so the common update never
 * allocates after the forest changed */
static ds_status_t tt_reserve_pending(ds_topology_tree_t *t, size_t u, size_t v,
                                      size_t room) {
  size_t hu = tt_height_of(t, u), hv = tt_height_of(t, v), l;
  size_t top = (hu > hv ? hu : hv) + 4;
  for (l = 0; l <= top; l++) {
    ds_status_t st = tt_level_room(t, l, room);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}

static ds_status_t tt_reserve_clusters(ds_topology_tree_t *t, size_t extra) {
  size_t nc;
  void *p;
  if (t->count + extra <= t->capacity) {
    return DS_OK;
  }
  nc = t->capacity * 2;
  while (nc < t->count + extra) {
    nc *= 2;
  }
  p = ds_context_realloc(t->context, t->clusters,
                         nc * sizeof(ds_topology_cluster_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->clusters = (ds_topology_cluster_t *)p;
  p = ds_context_realloc(t->context, t->values, nc * t->value_size + 1);
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->values = (unsigned char *)p;
  t->capacity = nc;
  return DS_OK;
}

/* Remove the ancestors of c; every surviving child of a removed cluster
 * becomes an orphan to re-cluster at its level. */
static ds_status_t tt_drop_ancestors(ds_topology_tree_t *t, size_t c) {
  size_t prev = c;
  size_t x = t->clusters[c].parent;
  ds_status_t st = DS_OK;
  t->clusters[c].parent = TT_NONE;
  while (x != TT_NONE) {
    ds_topology_cluster_t *cx = &t->clusters[x];
    size_t k;
    size_t next = cx->parent;
    for (k = 0; k < 2; k++) {
      size_t ch = cx->child[k];
      if (ch == TT_NONE) {
        continue;
      }
      t->clusters[ch].parent = TT_NONE;
      if (ch != prev && st == DS_OK) {
        st = tt_push(t, t->clusters[ch].level, ch);
      }
    }
    cx->alive = false;
    cx->parent = t->free_list;
    t->free_list = x;
    prev = x;
    x = next;
  }
  return st;
}

/* external edges of the level-0 cluster of v from the adjacency */
static void tt_vertex_edges(ds_topology_tree_t *t, size_t v) {
  ds_topology_cluster_t *c = &t->clusters[v];
  size_t k;
  c->edges = t->degree[v];
  for (k = 0; k < t->degree[v]; k++) {
    c->inner[k] = v;
    c->outer[k] = t->adj[3 * v + k];
  }
}

static ds_status_t tt_new_parent(ds_topology_tree_t *t, size_t f, size_t g,
                                 size_t *out) {
  size_t p;
  size_t level = t->clusters[f].level;
  size_t k;
  size_t s;
  size_t parts[2];
  ds_topology_cluster_t *cp;
  ds_status_t st;
  if (t->free_list != TT_NONE) {
    p = t->free_list;
    t->free_list = t->clusters[p].parent;
  } else {
    st = tt_reserve_clusters(t, 1);
    if (st != DS_OK) {
      return st;
    }
    p = t->count++;
  }
  cp = &t->clusters[p];
  cp->level = level + 1;
  cp->parent = TT_NONE;
  cp->child[0] = f;
  cp->child[1] = g;
  cp->edges = 0;
  cp->size = t->clusters[f].size + (g != TT_NONE ? t->clusters[g].size : 0);
  cp->alive = true;
  parts[0] = f;
  parts[1] = g;
  for (s = 0; s < 2; s++) {
    const ds_topology_cluster_t *cs;
    size_t other = parts[1 - s];
    if (parts[s] == TT_NONE) {
      continue;
    }
    cs = &t->clusters[parts[s]];
    for (k = 0; k < cs->edges; k++) {
      if (other != TT_NONE && tt_anc(t, cs->outer[k], level) == other) {
        continue; /* the edge joining the two halves */
      }
      cp->inner[cp->edges] = cs->inner[k];
      cp->outer[cp->edges] = cs->outer[k];
      cp->edges++;
    }
  }
  t->clusters[f].parent = p;
  if (g != TT_NONE) {
    t->clusters[g].parent = p;
  }
  if (t->value_size > 0) {
    if (g != TT_NONE) {
      t->combine(tt_value(t, p), tt_value(t, f), tt_value(t, g), t->user);
    } else {
      memcpy(tt_value(t, p), tt_value(t, f), t->value_size);
    }
  }
  *out = p;
  return DS_OK;
}

/* re-cluster every pending orphan, level by level */
static ds_status_t tt_process(ds_topology_tree_t *t) {
  size_t l;
  ds_status_t st = DS_OK;
  for (l = 0; l < t->levels && st == DS_OK; l++) {
    size_t i;
    for (i = 0; st == DS_OK && i < t->pending_count[l]; i++) {
      size_t f = t->pending[l][i];
      ds_topology_cluster_t *cf = &t->clusters[f];
      size_t partner = TT_NONE;
      size_t single = TT_NONE;
      size_t k;
      size_t p = TT_NONE;
      if (!cf->alive || cf->parent != TT_NONE || cf->edges == 0) {
        continue; /* already placed, gone, or a root */
      }
      for (k = 0; k < cf->edges; k++) {
        size_t g = tt_anc(t, cf->outer[k], l);
        const ds_topology_cluster_t *cg;
        if (g == TT_NONE || g == f) {
          continue;
        }
        cg = &t->clusters[g];
        if (cf->edges + cg->edges > 4) {
          continue;
        }
        if (cg->parent == TT_NONE) {
          partner = g;
          break;
        }
        if (single == TT_NONE && t->clusters[cg->parent].child[1] == TT_NONE) {
          single = g;
        }
      }
      if (partner == TT_NONE && single != TT_NONE) {
        st = tt_drop_ancestors(t, single);
        partner = single;
      }
      if (st == DS_OK) {
        st = tt_new_parent(t, f, partner, &p);
      }
      if (st == DS_OK) {
        st = tt_push(t, l + 1, p);
      }
    }
    if (l < t->levels) {
      t->pending_count[l] = 0;
    }
  }
  return st;
}

static bool tt_has_edge(const ds_topology_tree_t *t, size_t u, size_t v) {
  size_t k;
  for (k = 0; k < t->degree[u]; k++) {
    if (t->adj[3 * u + k] == v) {
      return true;
    }
  }
  return false;
}

size_t ds_topology_tree_root(const ds_topology_tree_t *t, size_t v) {
  size_t c;
  if (t == NULL || v >= t->vertices || t->broken) {
    return TT_NONE;
  }
  c = v;
  while (t->clusters[c].parent != TT_NONE) {
    c = t->clusters[c].parent;
  }
  return c;
}

bool ds_topology_tree_connected(const ds_topology_tree_t *t, size_t u,
                                size_t v) {
  size_t a = ds_topology_tree_root(t, u);
  return a != TT_NONE && a == ds_topology_tree_root(t, v);
}

static ds_status_t tt_update(ds_topology_tree_t *t, size_t u, size_t v) {
  ds_status_t st;
  tt_vertex_edges(t, u);
  tt_vertex_edges(t, v);
  st = tt_drop_ancestors(t, u);
  if (st == DS_OK) {
    st = tt_drop_ancestors(t, v);
  }
  if (st == DS_OK) {
    st = tt_push(t, 0, u);
  }
  if (st == DS_OK) {
    st = tt_push(t, 0, v);
  }
  if (st == DS_OK) {
    st = tt_process(t);
  }
  return st;
}

/* re-cluster after the adjacency changed; a failure here marks the tree
 * broken (see the header's failure contract) */
static ds_status_t tt_commit(ds_topology_tree_t *t, size_t u, size_t v) {
  ds_status_t st = tt_update(t, u, v);
  if (st != DS_OK) {
    t->broken = 1;
  }
  return st;
}

ds_status_t ds_topology_tree_rebuild(ds_topology_tree_t *t) {
  size_t v, l;
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  /* drop every cluster above level 0 and start again from the vertices */
  st = tt_level_room(t, 0, t->vertices);
  if (st != DS_OK) {
    return st;
  }
  for (l = 0; l < t->levels; l++) {
    t->pending_count[l] = 0;
  }
  t->count = t->vertices;
  t->free_list = TT_NONE;
  for (v = 0; v < t->vertices; v++) {
    t->clusters[v].parent = TT_NONE;
    tt_vertex_edges(t, v);
    t->pending[0][t->pending_count[0]++] = v;
  }
  t->broken = 1;
  st = tt_process(t);
  if (st == DS_OK) {
    t->broken = 0;
  }
  return st;
}

ds_status_t ds_topology_tree_link(ds_topology_tree_t *t, size_t u, size_t v) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= t->vertices || v >= t->vertices) {
    return DS_ERR_RANGE;
  }
  if (t->broken || u == v || t->degree[u] >= 3 || t->degree[v] >= 3 ||
      ds_topology_tree_connected(t, u, v)) {
    return DS_ERR_STATE;
  }
  /* reserve cluster slots and work lists before changing the forest */
  if (tt_reserve_clusters(t, 8 * t->levels + 64) != DS_OK ||
      tt_reserve_pending(t, u, v, 32) != DS_OK) {
    return DS_ERR_ALLOC;
  }
  t->adj[3 * u + t->degree[u]++] = v;
  t->adj[3 * v + t->degree[v]++] = u;
  return tt_commit(t, u, v);
}

ds_status_t ds_topology_tree_cut(ds_topology_tree_t *t, size_t u, size_t v) {
  size_t k;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= t->vertices || v >= t->vertices) {
    return DS_ERR_RANGE;
  }
  if (t->broken || !tt_has_edge(t, u, v)) {
    return DS_ERR_STATE;
  }
  if (tt_reserve_clusters(t, 8 * t->levels + 64) != DS_OK ||
      tt_reserve_pending(t, u, v, 32) != DS_OK) {
    return DS_ERR_ALLOC;
  }
  for (k = 0; t->adj[3 * u + k] != v; k++) {
  }
  t->adj[3 * u + k] = t->adj[3 * u + --t->degree[u]];
  for (k = 0; t->adj[3 * v + k] != u; k++) {
  }
  t->adj[3 * v + k] = t->adj[3 * v + --t->degree[v]];
  return tt_commit(t, u, v);
}

size_t ds_topology_tree_component_size(const ds_topology_tree_t *t, size_t v) {
  size_t r = ds_topology_tree_root(t, v);
  return r == TT_NONE ? 0 : t->clusters[r].size;
}

ds_status_t ds_topology_tree_component_value(const ds_topology_tree_t *t,
                                             size_t v, void *out) {
  size_t r;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (t->broken) {
    return DS_ERR_STATE;
  }
  r = ds_topology_tree_root(t, v);
  if (r == TT_NONE) {
    return DS_ERR_RANGE;
  }
  if (t->value_size > 0) {
    memcpy(out, tt_value(t, r), t->value_size);
  }
  return DS_OK;
}

ds_status_t ds_topology_tree_set_value(ds_topology_tree_t *t, size_t v,
                                       const void *value) {
  size_t x;
  if (t == NULL || (t->value_size > 0 && value == NULL)) {
    return DS_ERR_NULL;
  }
  if (v >= t->vertices) {
    return DS_ERR_RANGE;
  }
  if (t->value_size == 0) {
    return DS_OK;
  }
  memcpy(tt_value(t, v), value, t->value_size);
  if (t->broken) {
    return DS_OK; /* ancestors are recomputed by the rebuild */
  }
  for (x = t->clusters[v].parent; x != TT_NONE; x = t->clusters[x].parent) {
    const ds_topology_cluster_t *c = &t->clusters[x];
    if (c->child[1] != TT_NONE) {
      t->combine(tt_value(t, x), tt_value(t, c->child[0]),
                 tt_value(t, c->child[1]), t->user);
    } else {
      memcpy(tt_value(t, x), tt_value(t, c->child[0]), t->value_size);
    }
  }
  return DS_OK;
}

size_t ds_topology_tree_height(const ds_topology_tree_t *t, size_t v) {
  size_t r = ds_topology_tree_root(t, v);
  return r == TT_NONE ? 0 : t->clusters[r].level;
}

size_t ds_topology_tree_cluster(const ds_topology_tree_t *t, size_t v,
                                size_t level) {
  if (t == NULL || v >= t->vertices || t->broken) {
    return TT_NONE;
  }
  return tt_anc(t, v, level);
}
