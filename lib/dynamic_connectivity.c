#include "dynamic_connectivity.h"
#include <string.h>

#define DC_NONE ((size_t) - 1)
#define DC_TREE 0U
#define DC_NONTREE 1U

/* per vertex and level, as an element: whether it has incident edges of
 * that level, tree and not; as a summary: how many vertices have them */
typedef struct dc_counts {
  size_t tree;
  size_t nontree;
} dc_counts_t;

typedef struct dc_key {
  size_t a;
  size_t b;
} dc_key_t;

static void dc_summarize(void *out, const void *elem, void *user) {
  (void)user;
  memcpy(out, elem, sizeof(dc_counts_t));
}

static void dc_combine(void *out, const void *l, const void *r, void *user) {
  dc_counts_t a;
  dc_counts_t b;
  (void)user;
  memcpy(&a, l, sizeof(a));
  memcpy(&b, r, sizeof(b));
  a.tree += b.tree;
  a.nontree += b.nontree;
  memcpy(out, &a, sizeof(a));
}

static bool dc_has_tree(const void *s, void *user) {
  dc_counts_t c;
  (void)user;
  memcpy(&c, s, sizeof(c));
  return c.tree > 0;
}

static bool dc_has_nontree(const void *s, void *user) {
  dc_counts_t c;
  (void)user;
  memcpy(&c, s, sizeof(c));
  return c.nontree > 0;
}

static unsigned long dc_hash(const void *key, void *user) {
  dc_key_t k;
  unsigned long h;
  (void)user;
  memcpy(&k, key, sizeof(k));
  h = (unsigned long)k.a * 2654435761UL;
  h ^= (unsigned long)k.b + 0x9E3779B9UL + (h << 6) + (h >> 2);
  h ^= h >> 15;
  return h * 2246822519UL;
}

static bool dc_equal(const void *x, const void *y, void *user) {
  (void)user;
  return memcmp(x, y, sizeof(dc_key_t)) == 0;
}

static dc_key_t dc_ends(size_t u, size_t v) {
  dc_key_t k;
  memset(&k, 0, sizeof(k));
  k.a = u < v ? u : v;
  k.b = u < v ? v : u;
  return k;
}

static dc_key_t dc_list_key(size_t x, unsigned level, unsigned kind) {
  dc_key_t k;
  memset(&k, 0, sizeof(k));
  k.a = x;
  k.b = 2 * (size_t)level + kind;
  return k;
}

static unsigned dc_side(const ds_dynconn_t *g, size_t e, size_t x) {
  return g->edge[e].end[0] == x ? 0U : 1U;
}

/* ---- per-vertex flags in the level forests: set only when a list
 * turns empty or non-empty, so most list edits touch no forest ---- */

static void dc_flag(ds_dynconn_t *g, unsigned level, size_t x, unsigned kind,
                    size_t on) {
  dc_counts_t c;
  (void)ds_ett_get(&g->forest[level], x, &c);
  if (kind == DC_TREE) {
    c.tree = on;
  } else {
    c.nontree = on;
  }
  (void)ds_ett_set(&g->forest[level], x, &c);
}

/* ---- lists: heads stay in the map once created, so pushing never
 * allocates ---- */

static ds_status_t dc_ensure_head(ds_dynconn_t *g, size_t x, unsigned level,
                                  unsigned kind) {
  dc_key_t k = dc_list_key(x, level, kind);
  size_t none = DC_NONE;
  ds_status_t st;
  if (ds_map_get(&g->heads, &k) != NULL) {
    return DS_OK;
  }
  st = ds_map_put(&g->heads, &k, &none);
  return st == DS_EXISTS ? DS_OK : st;
}

static size_t dc_head(const ds_dynconn_t *g, size_t x, unsigned level,
                      unsigned kind) {
  dc_key_t k = dc_list_key(x, level, kind);
  const void *p = ds_map_get(&g->heads, &k);
  size_t h = DC_NONE;
  if (p != NULL) {
    memcpy(&h, p, sizeof(h));
  }
  return h;
}

static void dc_set_head(ds_dynconn_t *g, size_t x, unsigned level,
                        unsigned kind, size_t e) {
  dc_key_t k = dc_list_key(x, level, kind);
  void *p = ds_map_get(&g->heads, &k);
  if (p != NULL) {
    memcpy(p, &e, sizeof(e));
  }
}

/* the head must exist (dc_ensure_head) */
static void dc_push(ds_dynconn_t *g, size_t x, unsigned level, unsigned kind,
                    size_t e) {
  size_t h = dc_head(g, x, level, kind);
  unsigned s = dc_side(g, e, x);
  g->edge[e].next[s] = h;
  g->edge[e].prev[s] = DC_NONE;
  if (h != DC_NONE) {
    g->edge[h].prev[dc_side(g, h, x)] = e;
  } else {
    dc_flag(g, level, x, kind, 1);
  }
  dc_set_head(g, x, level, kind, e);
}

static void dc_unlink(ds_dynconn_t *g, size_t x, unsigned level, unsigned kind,
                      size_t e) {
  unsigned s = dc_side(g, e, x);
  size_t p = g->edge[e].prev[s];
  size_t q = g->edge[e].next[s];
  if (p != DC_NONE) {
    g->edge[p].next[dc_side(g, p, x)] = q;
  } else {
    dc_set_head(g, x, level, kind, q);
    if (q == DC_NONE) {
      dc_flag(g, level, x, kind, 0);
    }
  }
  if (q != DC_NONE) {
    g->edge[q].prev[dc_side(g, q, x)] = p;
  }
}

/* an edge's membership in the lists of both ends */
static ds_status_t dc_ensure_both(ds_dynconn_t *g, size_t e, unsigned level,
                                  unsigned kind) {
  ds_status_t st = dc_ensure_head(g, g->edge[e].end[0], level, kind);
  if (st == DS_OK) {
    st = dc_ensure_head(g, g->edge[e].end[1], level, kind);
  }
  return st;
}

static void dc_push_both(ds_dynconn_t *g, size_t e, unsigned level,
                         unsigned kind) {
  dc_push(g, g->edge[e].end[0], level, kind, e);
  dc_push(g, g->edge[e].end[1], level, kind, e);
}

static void dc_unlink_both(ds_dynconn_t *g, size_t e, unsigned level,
                           unsigned kind) {
  dc_unlink(g, g->edge[e].end[0], level, kind, e);
  dc_unlink(g, g->edge[e].end[1], level, kind, e);
}

/* ---- edge records ---- */

static ds_status_t dc_new_edge(ds_dynconn_t *g, size_t *out) {
  size_t e;
  if (g->free_edge != DC_NONE) {
    e = g->free_edge;
    g->free_edge = g->edge[e].next[0];
  } else {
    if (g->edge_slots == g->edge_capacity) {
      size_t cap = g->edge_capacity < 8 ? 16 : 2 * g->edge_capacity;
      ds_dc_edge_t *p;
      if (cap > (size_t)-1 / sizeof(ds_dc_edge_t)) {
        return DS_ERR_OVERFLOW;
      }
      p = (ds_dc_edge_t *)ds_context_realloc(g->context, g->edge,
                                             cap * sizeof(ds_dc_edge_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      g->edge = p;
      g->edge_capacity = cap;
    }
    e = g->edge_slots++;
  }
  memset(&g->edge[e], 0, sizeof(ds_dc_edge_t));
  *out = e;
  return DS_OK;
}

static void dc_free_edge(ds_dynconn_t *g, size_t e) {
  g->edge[e].live = 0;
  g->edge[e].next[0] = g->free_edge;
  g->free_edge = e;
}

static size_t dc_find(const ds_dynconn_t *g, size_t u, size_t v) {
  dc_key_t k = dc_ends(u, v);
  const void *p = ds_map_get(&g->by_ends, &k);
  size_t e = DC_NONE;
  if (p != NULL) {
    memcpy(&e, p, sizeof(e));
  }
  return e;
}

/* ---- setup ---- */

static ds_status_t dc_forest_init(ds_dynconn_t *g, unsigned level) {
  ds_ett_config_t c;
  ds_status_t st;
  ds_ett_config_init(&c);
  c.elem_size = sizeof(dc_counts_t);
  c.summary_size = sizeof(dc_counts_t);
  c.summarize = dc_summarize;
  c.combine = dc_combine;
  c.context = g->context;
  c.seed = 7919UL * (unsigned long)level + 1UL;
  st = ds_ett_init(&g->forest[level], g->n, NULL, &c);
  g->forest_ready[level] = st == DS_OK ? 1 : 0;
  return st;
}

static void dc_forests_destroy(ds_dynconn_t *g) {
  unsigned i;
  for (i = 0; i < g->levels; i++) {
    if (g->forest_ready[i]) {
      ds_ett_destroy(&g->forest[i]);
      g->forest_ready[i] = 0;
    }
  }
}

ds_status_t ds_dc_init(ds_dynconn_t *g, size_t n, ds_context_t *context) {
  unsigned i;
  size_t m;
  ds_status_t st;
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  memset(g, 0, sizeof(*g));
  g->n = n;
  g->context = context;
  g->free_edge = DC_NONE;
  g->levels = 1; /* floor(log2 n) + 1 */
  for (m = n; m > 1; m >>= 1) {
    g->levels++;
  }
  g->forest =
      (ds_ett_t *)ds_context_alloc(context, g->levels * sizeof(ds_ett_t));
  g->forest_ready = (unsigned char *)ds_context_calloc(context, g->levels, 1);
  if (g->forest == NULL || g->forest_ready == NULL) {
    ds_context_free(context, g->forest);
    ds_context_free(context, g->forest_ready);
    memset(g, 0, sizeof(*g));
    return DS_ERR_ALLOC;
  }
  st = ds_map_init(&g->by_ends, NULL, sizeof(dc_key_t), sizeof(size_t), dc_hash,
                   dc_equal, NULL, context);
  if (st == DS_OK) {
    st = ds_map_init(&g->heads, NULL, sizeof(dc_key_t), sizeof(size_t), dc_hash,
                     dc_equal, NULL, context);
    if (st != DS_OK) {
      ds_map_destroy(&g->by_ends);
    }
  }
  if (st == DS_OK) {
    g->maps_ready = true;
    for (i = 0; i < g->levels && st == DS_OK; i++) {
      st = dc_forest_init(g, i);
    }
  }
  if (st != DS_OK) {
    ds_dc_destroy(g);
    return st;
  }
  return DS_OK;
}

void ds_dc_destroy(ds_dynconn_t *g) {
  if (g == NULL) {
    return;
  }
  if (g->forest_ready != NULL) {
    dc_forests_destroy(g);
  }
  if (g->maps_ready) {
    ds_map_destroy(&g->by_ends);
    ds_map_destroy(&g->heads);
  }
  ds_context_free(g->context, g->forest);
  ds_context_free(g->context, g->forest_ready);
  ds_context_free(g->context, g->edge);
  memset(g, 0, sizeof(*g));
}

/* ---- updates ---- */

ds_status_t ds_dc_insert(ds_dynconn_t *g, size_t u, size_t v) {
  dc_key_t k;
  size_t e;
  unsigned kind;
  ds_status_t st;
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= g->n || v >= g->n) {
    return DS_ERR_RANGE;
  }
  if (u == v) {
    return DS_ERR_INVALID;
  }
  if (g->broken) {
    return DS_ERR_STATE;
  }
  if (dc_find(g, u, v) != DC_NONE) {
    return DS_EXISTS;
  }
  st = dc_new_edge(g, &e);
  if (st != DS_OK) {
    return st;
  }
  g->edge[e].end[0] = u;
  g->edge[e].end[1] = v;
  kind = ds_ett_connected(&g->forest[0], u, v) ? DC_NONTREE : DC_TREE;
  st = dc_ensure_both(g, e, 0, kind);
  if (st == DS_OK) {
    k = dc_ends(u, v);
    st = ds_map_put(&g->by_ends, &k, &e);
  }
  if (st == DS_OK && kind == DC_TREE) {
    st = ds_ett_link(&g->forest[0], u, v);
    if (st != DS_OK) {
      (void)ds_map_remove(&g->by_ends, &k, NULL);
    }
  }
  if (st != DS_OK) {
    dc_free_edge(g, e);
    return st;
  }
  g->edge[e].live = 1;
  g->edge[e].tree = kind == DC_TREE ? 1 : 0;
  dc_push_both(g, e, 0, kind);
  g->edges++;
  if (kind == DC_TREE) {
    g->tree_edges++;
  }
  return DS_OK;
}

/* promote the level-i tree edges of x's tree in F_i to level i + 1;
 * an edge that cannot get memory stays where it is */
static void dc_promote_tree_edges(ds_dynconn_t *g, unsigned i, size_t x) {
  size_t w;
  while (ds_ett_component_find(&g->forest[i], x, dc_has_tree, NULL, &w) ==
         DS_OK) {
    size_t f = dc_head(g, w, i, DC_TREE);
    size_t a = g->edge[f].end[0];
    size_t b = g->edge[f].end[1];
    if (dc_ensure_both(g, f, i + 1, DC_TREE) != DS_OK ||
        ds_ett_link(&g->forest[i + 1], a, b) != DS_OK) {
      return;
    }
    dc_unlink_both(g, f, i, DC_TREE);
    g->edge[f].level = i + 1;
    dc_push_both(g, f, i + 1, DC_TREE);
    g->promotions++;
  }
}

/* search level i for an edge reconnecting the trees of u and v in F_i */
static ds_status_t dc_replace(ds_dynconn_t *g, unsigned i, size_t u, size_t v,
                              bool *replaced) {
  size_t x;
  size_t w;
  *replaced = false;
  x = ds_ett_component_size(&g->forest[i], u) <=
              ds_ett_component_size(&g->forest[i], v)
          ? u
          : v;
  if (i + 1 < g->levels) {
    dc_promote_tree_edges(g, i, x);
  }
  while (ds_ett_component_find(&g->forest[i], x, dc_has_nontree, NULL, &w) ==
         DS_OK) {
    size_t f = dc_head(g, w, i, DC_NONTREE);
    size_t a = g->edge[f].end[0];
    size_t b = g->edge[f].end[1];
    size_t o = a == w ? b : a;
    ds_status_t st;
    if (ds_ett_connected(&g->forest[i], o, x)) {
      /* both ends in the small tree: up one level */
      if (i + 1 >= g->levels) {
        return DS_ERR_STATE; /* the size bound rules this out */
      }
      st = dc_ensure_both(g, f, i + 1, DC_NONTREE);
      if (st != DS_OK) {
        return st;
      }
      dc_unlink_both(g, f, i, DC_NONTREE);
      g->edge[f].level = i + 1;
      dc_push_both(g, f, i + 1, DC_NONTREE);
      g->promotions++;
    } else {
      /* a replacement: into F_0 .. F_i */
      unsigned linked = 0;
      st = dc_ensure_both(g, f, i, DC_TREE);
      while (st == DS_OK && linked <= i) {
        st = ds_ett_link(&g->forest[linked], a, b);
        if (st == DS_OK) {
          linked++;
        }
      }
      if (st != DS_OK) {
        while (linked-- > 0) {
          (void)ds_ett_cut(&g->forest[linked], a, b);
        }
        return st;
      }
      dc_unlink_both(g, f, i, DC_NONTREE);
      g->edge[f].tree = 1;
      dc_push_both(g, f, i, DC_TREE);
      g->replacements++;
      *replaced = true;
      return DS_OK;
    }
  }
  return DS_OK;
}

ds_status_t ds_dc_delete(ds_dynconn_t *g, size_t u, size_t v) {
  dc_key_t k;
  size_t e;
  unsigned level;
  unsigned i;
  bool replaced = false;
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= g->n || v >= g->n) {
    return DS_ERR_RANGE;
  }
  if (g->broken) {
    return DS_ERR_STATE;
  }
  e = dc_find(g, u, v);
  if (e == DC_NONE) {
    return DS_NOT_FOUND;
  }
  k = dc_ends(u, v);
  (void)ds_map_remove(&g->by_ends, &k, NULL);
  level = g->edge[e].level;
  g->edges--;
  if (!g->edge[e].tree) {
    dc_unlink_both(g, e, level, DC_NONTREE);
    dc_free_edge(g, e);
    return DS_OK;
  }
  dc_unlink_both(g, e, level, DC_TREE);
  for (i = 0; i <= level; i++) {
    (void)ds_ett_cut(&g->forest[i], u, v);
  }
  dc_free_edge(g, e);
  g->tree_edges--;
  for (i = level + 1; i-- > 0;) {
    ds_status_t st = dc_replace(g, i, u, v, &replaced);
    if (st != DS_OK) {
      g->broken = true;
      return st;
    }
    if (replaced) {
      g->tree_edges++;
      break;
    }
  }
  return DS_OK;
}

/* ---- queries ---- */

bool ds_dc_connected(const ds_dynconn_t *g, size_t u, size_t v) {
  if (g == NULL || u >= g->n || v >= g->n) {
    return false;
  }
  return u == v || ds_ett_connected(&g->forest[0], u, v);
}

bool ds_dc_has_edge(const ds_dynconn_t *g, size_t u, size_t v) {
  return g != NULL && u < g->n && v < g->n && dc_find(g, u, v) != DC_NONE;
}

bool ds_dc_is_tree_edge(const ds_dynconn_t *g, size_t u, size_t v) {
  size_t e;
  if (g == NULL || u >= g->n || v >= g->n) {
    return false;
  }
  e = dc_find(g, u, v);
  return e != DC_NONE && g->edge[e].tree;
}

size_t ds_dc_component_size(const ds_dynconn_t *g, size_t v) {
  if (g == NULL || v >= g->n) {
    return 0;
  }
  return ds_ett_component_size(&g->forest[0], v);
}

size_t ds_dc_components(const ds_dynconn_t *g) {
  return g == NULL ? 0 : g->n - g->tree_edges;
}

size_t ds_dc_edges(const ds_dynconn_t *g) { return g == NULL ? 0 : g->edges; }

bool ds_dc_broken(const ds_dynconn_t *g) { return g != NULL && g->broken; }

ds_status_t ds_dc_rebuild(ds_dynconn_t *g) {
  ds_map_t heads;
  size_t e;
  unsigned i;
  ds_status_t st;
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  g->broken = true;
  /* fresh lists and forests; the edges keep their records */
  st = ds_map_init(&heads, NULL, sizeof(dc_key_t), sizeof(size_t), dc_hash,
                   dc_equal, NULL, g->context);
  if (st != DS_OK) {
    return st;
  }
  ds_map_destroy(&g->heads);
  g->heads = heads;
  dc_forests_destroy(g);
  for (i = 0; i < g->levels; i++) {
    st = dc_forest_init(g, i);
    if (st != DS_OK) {
      return st;
    }
  }
  g->tree_edges = 0;
  for (e = 0; e < g->edge_slots; e++) {
    unsigned kind;
    size_t a;
    size_t b;
    if (!g->edge[e].live) {
      continue;
    }
    a = g->edge[e].end[0];
    b = g->edge[e].end[1];
    g->edge[e].level = 0;
    kind = ds_ett_connected(&g->forest[0], a, b) ? DC_NONTREE : DC_TREE;
    st = dc_ensure_both(g, e, 0, kind);
    if (st == DS_OK && kind == DC_TREE) {
      st = ds_ett_link(&g->forest[0], a, b);
    }
    if (st != DS_OK) {
      return st;
    }
    g->edge[e].tree = kind == DC_TREE ? 1 : 0;
    dc_push_both(g, e, 0, kind);
    if (kind == DC_TREE) {
      g->tree_edges++;
    }
  }
  g->broken = false;
  return DS_OK;
}

/* ---- invariants ---- */

bool ds_dc_check(ds_dynconn_t *g) {
  size_t e;
  size_t x;
  unsigned i;
  size_t trees = 0;
  if (g == NULL || g->broken) {
    return false;
  }
  for (e = 0; e < g->edge_slots; e++) {
    ds_dc_edge_t *d = &g->edge[e];
    if (!d->live) {
      continue;
    }
    if (d->level >= g->levels || dc_find(g, d->end[0], d->end[1]) != e) {
      return false;
    }
    for (i = 0; i < g->levels; i++) {
      bool in = ds_ett_has_edge(&g->forest[i], d->end[0], d->end[1]);
      if (in != (d->tree && i <= d->level)) {
        return false; /* tree edges sit in F_0 .. F_level, nowhere else */
      }
    }
    /* a non-tree edge's ends are connected at its level */
    if (!d->tree &&
        !ds_ett_connected(&g->forest[d->level], d->end[0], d->end[1])) {
      return false;
    }
    trees += d->tree;
  }
  if (trees != g->tree_edges) {
    return false;
  }
  for (i = 0; i < g->levels; i++) {
    for (x = 0; x < g->n; x++) {
      unsigned kind;
      dc_counts_t c;
      if (ds_ett_component_size(&g->forest[i], x) > (g->n >> i)) {
        return false; /* trees of F_i have at most n / 2^i vertices */
      }
      (void)ds_ett_get(&g->forest[i], x, &c);
      for (kind = 0; kind < 2; kind++) {
        size_t f = dc_head(g, x, i, kind);
        size_t len = 0;
        size_t prev = DC_NONE;
        while (f != DC_NONE) {
          unsigned s;
          if (f >= g->edge_slots || !g->edge[f].live || g->edge[f].level != i ||
              (unsigned)(g->edge[f].tree ? DC_TREE : DC_NONTREE) != kind ||
              (g->edge[f].end[0] != x && g->edge[f].end[1] != x) ||
              len > g->edge_slots) {
            return false;
          }
          s = dc_side(g, f, x);
          if (g->edge[f].prev[s] != prev) {
            return false;
          }
          prev = f;
          f = g->edge[f].next[s];
          len++;
        }
        if ((len > 0 ? 1U : 0U) != (kind == DC_TREE ? c.tree : c.nontree)) {
          return false; /* the flag says whether the list is empty */
        }
      }
    }
  }
  return true;
}
