#include "topology_tree.h"
#include <string.h>

#define TT_NONE DS_TOPOLOGY_NONE
/* clusters an update may build, and pending entries it may file, per
 * level; Frederickson's bound is a small constant (ds_topology_tree_work
 * reports what an update used) */
#define TT_PER_LEVEL 24

static unsigned char *tt_value(const ds_topology_tree_t *t, size_t c) {
  return t->values + c * t->value_size;
}

static unsigned char *tt_spine(const ds_topology_tree_t *t, size_t c) {
  return t->spines + c * t->value_size;
}

static void tt_free_arrays(ds_topology_tree_t *t) {
  size_t l;
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
  ds_context_free(t->context, t->spines);
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
  if (vertices > (size_t)-1 / (8 * sizeof(ds_topology_cluster_t)) ||
      (value_size > 0 && vertices > (size_t)-1 / 8 / value_size)) {
    return DS_ERR_OVERFLOW;
  }
  t->vertices = vertices;
  t->value_size = value_size;
  t->combine = combine;
  t->user = user;
  t->context = context;
  t->free_list = TT_NONE;
  t->dead = TT_NONE;
  t->capacity = 2 * vertices;
  t->adj = (size_t *)ds_context_alloc(context, 3 * vertices * sizeof(size_t));
  t->degree = (unsigned char *)ds_context_calloc(context, vertices, 1);
  t->clusters = (ds_topology_cluster_t *)ds_context_alloc(
      context, t->capacity * sizeof(ds_topology_cluster_t));
  t->values = (unsigned char *)ds_context_calloc(
      context, t->capacity * value_size + 1, 1);
  t->spines = (unsigned char *)ds_context_calloc(
      context, t->capacity * value_size + 1, 1);
  if (t->adj == NULL || t->degree == NULL || t->clusters == NULL ||
      t->values == NULL || t->spines == NULL) {
    tt_free_arrays(t);
    memset(t, 0, sizeof(*t));
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
    c->fresh = false;
  }
  if (values != NULL && value_size > 0) {
    memcpy(t->values, values, vertices * value_size);
    memcpy(t->spines, values, vertices * value_size);
  }
  t->count = vertices;
  return DS_OK;
}

void ds_topology_tree_destroy(ds_topology_tree_t *t) {
  if (t == NULL) {
    return;
  }
  tt_free_arrays(t);
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

/* ---- work lists and slots ------------------------------------------------ */

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
  p = ds_context_realloc(t->context, t->spines, nc * t->value_size + 1);
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->spines = (unsigned char *)p;
  t->capacity = nc;
  return DS_OK;
}

/* a deleted cluster waits on the dead list until the update ends, so no
 * slot is reused while the update may still meet its number */
static void tt_kill(ds_topology_tree_t *t, size_t x) {
  t->clusters[x].alive = false;
  t->clusters[x].parent = t->dead;
  t->dead = x;
}

static void tt_bury(ds_topology_tree_t *t) {
  while (t->dead != TT_NONE) {
    size_t x = t->dead;
    t->dead = t->clusters[x].parent;
    t->clusters[x].parent = t->free_list;
    t->free_list = x;
  }
}

/* ---- clustering ---------------------------------------------------------- */

/* Delete the ancestors of c; every surviving child of a deleted cluster
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
      if (ch == TT_NONE || ch == prev) {
        continue;
      }
      t->clusters[ch].parent = TT_NONE;
      if (st == DS_OK) {
        st = tt_push(t, t->clusters[ch].level, ch);
      }
    }
    tt_kill(t, x);
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
    c->nbr[k] = t->adj[3 * v + k];
  }
}

/* the aggregate and the path between the boundary vertices of p from its
 * children */
static void tt_combine_up(ds_topology_tree_t *t, size_t p) {
  const ds_topology_cluster_t *cp = &t->clusters[p];
  size_t f = cp->child[0];
  size_t g = cp->child[1];
  if (t->value_size == 0) {
    return;
  }
  if (g == TT_NONE) {
    memcpy(tt_value(t, p), tt_value(t, f), t->value_size);
    memcpy(tt_spine(t, p), tt_spine(t, f), t->value_size);
    return;
  }
  t->combine(tt_value(t, p), tt_value(t, f), tt_value(t, g), t->user);
  if (t->clusters[f].edges == 3) { /* a vertex and its leaf side */
    memcpy(tt_spine(t, p), tt_spine(t, f), t->value_size);
  } else if (t->clusters[g].edges == 3) {
    memcpy(tt_spine(t, p), tt_spine(t, g), t->value_size);
  } else if (cp->edges == 2) { /* both halves lie on the path */
    t->combine(tt_spine(t, p), tt_spine(t, f), tt_spine(t, g), t->user);
  } else {
    memcpy(tt_spine(t, p), tt_value(t, p), t->value_size);
  }
}

/* a parent at level l + 1 for f (and g, adjacent); its neighbour links
 * hold the level-l clusters across its edges until tt_link_fresh */
static ds_status_t tt_new_parent(ds_topology_tree_t *t, size_t f, size_t g) {
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
  cp->fresh = true;
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
      if (other != TT_NONE && cs->nbr[k] == other) {
        continue; /* the edge joining the two halves */
      }
      cp->inner[cp->edges] = cs->inner[k];
      cp->outer[cp->edges] = cs->outer[k];
      cp->nbr[cp->edges] = cs->nbr[k];
      cp->edges++;
    }
  }
  t->clusters[f].parent = p;
  if (g != TT_NONE) {
    t->clusters[g].parent = p;
  }
  tt_combine_up(t, p);
  t->work++;
  return tt_push(t, level + 1, p);
}

/* the level-l clusters built while level l - 1 was clustered get their
 * neighbours, and the neighbours get them back; every level-(l-1)
 * cluster has its final parent by now */
static void tt_link_fresh(ds_topology_tree_t *t, size_t l) {
  size_t i, k, m;
  for (i = 0; i < t->pending_count[l]; i++) {
    ds_topology_cluster_t *c = &t->clusters[t->pending[l][i]];
    if (!c->alive || !c->fresh || c->level != l) {
      continue;
    }
    for (k = 0; k < c->edges; k++) {
      c->nbr[k] = t->clusters[c->nbr[k]].parent;
    }
  }
  for (i = 0; i < t->pending_count[l]; i++) {
    size_t id = t->pending[l][i];
    ds_topology_cluster_t *c = &t->clusters[id];
    if (!c->alive || !c->fresh || c->level != l) {
      continue;
    }
    for (k = 0; k < c->edges; k++) {
      ds_topology_cluster_t *w = &t->clusters[c->nbr[k]];
      for (m = 0; m < w->edges; m++) {
        if (w->inner[m] == c->outer[k] && w->outer[m] == c->inner[k]) {
          w->nbr[m] = id;
        }
      }
    }
    c->fresh = false;
  }
}

/* re-cluster every pending orphan, level by level */
static ds_status_t tt_process(ds_topology_tree_t *t) {
  size_t l;
  ds_status_t st = DS_OK;
  for (l = 0; l < t->levels && st == DS_OK; l++) {
    size_t i;
    if (l > 0) {
      tt_link_fresh(t, l);
    }
    for (i = 0; st == DS_OK && i < t->pending_count[l]; i++) {
      size_t f = t->pending[l][i];
      const ds_topology_cluster_t *cf = &t->clusters[f];
      size_t partner = TT_NONE;
      size_t k;
      if (!cf->alive || cf->level != l || cf->parent != TT_NONE ||
          cf->edges == 0) {
        continue; /* gone, placed already, or a root */
      }
      for (k = 0; k < cf->edges; k++) { /* a free neighbour */
        size_t g = cf->nbr[k];
        const ds_topology_cluster_t *cg = &t->clusters[g];
        if (cg->alive && cg->parent == TT_NONE && cf->edges + cg->edges <= 4) {
          partner = g;
          break;
        }
      }
      for (k = 0; partner == TT_NONE && k < cf->edges; k++) {
        /* maximality: a neighbour alone in its parent joins f */
        size_t g = cf->nbr[k];
        const ds_topology_cluster_t *cg = &t->clusters[g];
        if (cg->alive && cf->edges + cg->edges <= 4 && cg->parent != TT_NONE &&
            t->clusters[cg->parent].child[1] == TT_NONE) {
          st = tt_drop_ancestors(t, g);
          partner = g;
        }
      }
      if (st == DS_OK) {
        st = tt_new_parent(t, f, partner);
      }
    }
    t->pending_count[l] = 0;
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

static size_t tt_lg(size_t n) {
  size_t l = 0;
  while (n > 1) {
    n >>= 1;
    l++;
  }
  return l + 1;
}

/* room for the re-clustering of an update joining or splitting the trees
 * of u and v: a level of clusters shrinks by a constant factor (at least a
 * sixth), so 4 lg n + 8 levels cover any tree of n vertices */
static ds_status_t tt_reserve(ds_topology_tree_t *t, size_t u, size_t v) {
  size_t n = ds_topology_tree_component_size(t, u);
  size_t top, l;
  ds_status_t st;
  if (ds_topology_tree_root(t, u) != ds_topology_tree_root(t, v)) {
    n += ds_topology_tree_component_size(t, v);
  }
  top = 4 * tt_lg(n) + 8;
  st = tt_reserve_clusters(t, TT_PER_LEVEL * (top + 1));
  for (l = 0; st == DS_OK && l <= top; l++) {
    st = tt_level_room(t, l, TT_PER_LEVEL);
  }
  return st;
}

static ds_status_t tt_update(ds_topology_tree_t *t, size_t u, size_t v) {
  ds_status_t st;
  t->work = 0;
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
  tt_bury(t);
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
  t->dead = TT_NONE;
  t->work = 0;
  for (v = 0; v < t->vertices; v++) {
    t->clusters[v].parent = TT_NONE;
    t->clusters[v].alive = true;
    t->clusters[v].fresh = false;
    tt_vertex_edges(t, v);
    if (t->value_size > 0) {
      memcpy(tt_spine(t, v), tt_value(t, v), t->value_size);
    }
    t->pending[0][t->pending_count[0]++] = v;
  }
  t->broken = 1;
  st = tt_process(t);
  tt_bury(t);
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
  if (tt_reserve(t, u, v) != DS_OK) { /* before changing the forest */
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
  if (tt_reserve(t, u, v) != DS_OK) {
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

/* the index of c's edge leading into the cluster d of its level */
static size_t tt_edge_to(const ds_topology_tree_t *t, size_t c, size_t d) {
  const ds_topology_cluster_t *x = &t->clusters[c];
  size_t k;
  for (k = 0; k < x->edges && x->nbr[k] != d; k++) {
  }
  return k;
}

/* from the aggregates of the paths from a vertex in c to c's boundary
 * vertices (one per external edge, in d), those for c's parent */
static void tt_climb(const ds_topology_tree_t *t, size_t c, unsigned char *d,
                     unsigned char *tmp) {
  const ds_topology_cluster_t *p = &t->clusters[t->clusters[c].parent];
  size_t vs = t->value_size;
  size_t s, k, j = 0;
  size_t other = p->child[0] == c ? p->child[1] : p->child[0];
  size_t to_other;
  if (other == TT_NONE) {
    return; /* one child: the same edges in the same order */
  }
  to_other = tt_edge_to(t, c, other);
  for (s = 0; s < 2; s++) {
    size_t x = p->child[s];
    const ds_topology_cluster_t *cx = &t->clusters[x];
    for (k = 0; k < cx->edges; k++) {
      if (cx->nbr[k] == p->child[1 - s]) {
        continue;
      }
      if (x == c) {
        memcpy(tmp + j * vs, d + k * vs, vs);
      } else { /* across into the sibling, then along its path */
        t->combine(tmp + j * vs, d + to_other * vs, tt_spine(t, other),
                   t->user);
      }
      j++;
    }
  }
  memcpy(d, tmp, 3 * vs);
}

ds_status_t ds_topology_tree_path_value(const ds_topology_tree_t *t, size_t u,
                                        size_t v, void *out) {
  unsigned char *buf;
  unsigned char *du, *dv, *tmp;
  size_t a = u, b = v, k, vs;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (t->broken) {
    return DS_ERR_STATE;
  }
  if (u >= t->vertices || v >= t->vertices) {
    return DS_ERR_RANGE;
  }
  if (!ds_topology_tree_connected(t, u, v)) {
    return DS_NOT_FOUND;
  }
  vs = t->value_size;
  if (vs == 0) {
    return DS_OK;
  }
  if (u == v) {
    memcpy(out, tt_value(t, u), vs);
    return DS_OK;
  }
  buf = (unsigned char *)ds_context_alloc(t->context, 9 * vs);
  if (buf == NULL) {
    return DS_ERR_ALLOC;
  }
  du = buf;
  dv = buf + 3 * vs;
  tmp = buf + 6 * vs;
  for (k = 0; k < 3; k++) {
    memcpy(du + k * vs, tt_value(t, u), vs);
    memcpy(dv + k * vs, tt_value(t, v), vs);
  }
  /* climb both chains together until their clusters are siblings */
  while (t->clusters[a].parent != t->clusters[b].parent) {
    tt_climb(t, a, du, tmp);
    tt_climb(t, b, dv, tmp);
    a = t->clusters[a].parent;
    b = t->clusters[b].parent;
  }
  t->combine(out, du + tt_edge_to(t, a, b) * vs, dv + tt_edge_to(t, b, a) * vs,
             t->user);
  ds_context_free(t->context, buf);
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
  memcpy(tt_spine(t, v), value, t->value_size);
  if (t->broken) {
    return DS_OK; /* ancestors are recomputed by the rebuild */
  }
  for (x = t->clusters[v].parent; x != TT_NONE; x = t->clusters[x].parent) {
    tt_combine_up(t, x);
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

size_t ds_topology_tree_work(const ds_topology_tree_t *t) {
  return t == NULL ? 0 : t->work;
}

/* ---- checking ------------------------------------------------------------ */

static bool tt_check_cluster(const ds_topology_tree_t *t, size_t c,
                             unsigned char *scratch) {
  const ds_topology_cluster_t *x = &t->clusters[c];
  size_t k, m, s, j = 0;
  if (x->fresh || x->edges > 3 || (x->edges == 0) != (x->parent == TT_NONE)) {
    return false;
  }
  if (x->level == 0) {
    if (c >= t->vertices || x->child[0] != c || x->size != 1 ||
        x->edges != t->degree[c]) {
      return false;
    }
    for (k = 0; k < x->edges; k++) {
      if (x->inner[k] != c || x->outer[k] != t->adj[3 * c + k] ||
          x->nbr[k] != x->outer[k]) {
        return false;
      }
    }
    if (t->value_size > 0 &&
        memcmp(tt_spine(t, c), tt_value(t, c), t->value_size) != 0) {
      return false;
    }
  } else {
    size_t f = x->child[0], g = x->child[1];
    const ds_topology_cluster_t *cf, *cg;
    if (c < t->vertices || f >= t->count || !t->clusters[f].alive ||
        t->clusters[f].parent != c || t->clusters[f].level + 1 != x->level) {
      return false;
    }
    cf = &t->clusters[f];
    cg = NULL;
    if (g != TT_NONE) {
      if (g >= t->count || !t->clusters[g].alive ||
          t->clusters[g].parent != c || t->clusters[g].level + 1 != x->level) {
        return false;
      }
      cg = &t->clusters[g];
      /* adjacent, and allowed to combine */
      if (cf->edges + cg->edges > 4 || tt_edge_to(t, f, g) == cf->edges ||
          tt_edge_to(t, g, f) == cg->edges) {
        return false;
      }
    }
    if (x->size != cf->size + (cg != NULL ? cg->size : 0)) {
      return false;
    }
    for (s = 0; s < 2; s++) { /* the edges are the children's, minus one */
      size_t ch = x->child[s];
      const ds_topology_cluster_t *cc;
      if (ch == TT_NONE) {
        continue;
      }
      cc = &t->clusters[ch];
      for (k = 0; k < cc->edges; k++) {
        if (cc->nbr[k] == x->child[1 - s]) {
          continue;
        }
        if (j >= x->edges || x->inner[j] != cc->inner[k] ||
            x->outer[j] != cc->outer[k]) {
          return false;
        }
        j++;
      }
    }
    if (j != x->edges) {
      return false;
    }
    if (t->value_size > 0) { /* the aggregates, recomputed */
      if (g == TT_NONE) {
        if (memcmp(tt_value(t, c), tt_value(t, f), t->value_size) != 0 ||
            memcmp(tt_spine(t, c), tt_spine(t, f), t->value_size) != 0) {
          return false;
        }
      } else {
        t->combine(scratch, tt_value(t, f), tt_value(t, g), t->user);
        if (memcmp(scratch, tt_value(t, c), t->value_size) != 0) {
          return false;
        }
        if (x->edges == 2 || x->edges == 3) {
          if (cf->edges == 3) {
            memcpy(scratch, tt_spine(t, f), t->value_size);
          } else if (cg->edges == 3) {
            memcpy(scratch, tt_spine(t, g), t->value_size);
          } else {
            t->combine(scratch, tt_spine(t, f), tt_spine(t, g), t->user);
          }
          if (memcmp(scratch, tt_spine(t, c), t->value_size) != 0) {
            return false;
          }
        }
      }
    }
  }
  if (x->edges == 3 && x->size != 1) {
    return false; /* degree 3: a single vertex */
  }
  for (k = 0; k < x->edges; k++) { /* neighbour links, both ways */
    size_t w = tt_anc(t, x->outer[k], x->level);
    const ds_topology_cluster_t *cw;
    if (w == TT_NONE || w != x->nbr[k] || w == c) {
      return false;
    }
    cw = &t->clusters[w];
    for (m = 0; m < cw->edges; m++) {
      if (cw->inner[m] == x->outer[k] && cw->outer[m] == x->inner[k]) {
        break;
      }
    }
    if (m == cw->edges || cw->nbr[m] != c) {
      return false;
    }
    /* maximality: two lone neighbours that could have combined */
    if (x->parent != TT_NONE && t->clusters[x->parent].child[1] == TT_NONE &&
        cw->parent != TT_NONE && t->clusters[cw->parent].child[1] == TT_NONE &&
        x->edges + cw->edges <= 4) {
      return false;
    }
  }
  if (x->parent != TT_NONE &&
      (x->parent >= t->count || !t->clusters[x->parent].alive ||
       t->clusters[x->parent].level != x->level + 1)) {
    return false;
  }
  return true;
}

ds_status_t ds_topology_tree_check(const ds_topology_tree_t *t,
                                   bool *out_valid) {
  unsigned char *scratch;
  size_t c;
  bool ok = true;
  if (t == NULL || out_valid == NULL) {
    return DS_ERR_NULL;
  }
  *out_valid = false;
  if (t->broken) {
    return DS_OK;
  }
  scratch = (unsigned char *)ds_context_alloc(t->context, t->value_size + 1);
  if (scratch == NULL) {
    return DS_ERR_ALLOC;
  }
  for (c = 0; c < t->count && ok; c++) {
    if (t->clusters[c].alive) {
      ok = tt_check_cluster(t, c, scratch);
    } else if (c < t->vertices) {
      ok = false;
    }
  }
  ds_context_free(t->context, scratch);
  *out_valid = ok;
  return DS_OK;
}
