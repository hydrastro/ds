#include "decremental_msf.h"
#include <string.h>

#define DM_NONE DS_DMSF_NONE
#define DM_TREE 0U
#define DM_NONTREE 1U

/* per vertex and level, as an element: whether it has level-i tree edges,
 * and its lightest level-i non-tree edge; as a summary over a tree of
 * F_i: how many vertices have tree edges, and the lightest such edge */
typedef struct dm_info {
  size_t tree;
  size_t edge; /* DM_NONE: none */
  double weight;
} dm_info_t;

typedef struct dm_key {
  size_t a;
  size_t b;
} dm_key_t;

/* the strict order on edges: weight, then index */
static bool dm_before(double wa, size_t a, double wb, size_t b) {
  if (a == DM_NONE) {
    return false;
  }
  if (b == DM_NONE || wa < wb) {
    return true;
  }
  return !(wb < wa) && a < b;
}

static bool dm_less(const ds_dmsf_t *g, size_t a, size_t b) {
  return dm_before(g->edge[a].weight, a, g->edge[b].weight, b);
}

static void dm_summarize(void *out, const void *elem, void *user) {
  (void)user;
  memcpy(out, elem, sizeof(dm_info_t));
}

static void dm_combine(void *out, const void *l, const void *r, void *user) {
  dm_info_t a;
  dm_info_t b;
  (void)user;
  memcpy(&a, l, sizeof(a));
  memcpy(&b, r, sizeof(b));
  a.tree += b.tree;
  if (dm_before(b.weight, b.edge, a.weight, a.edge)) {
    a.edge = b.edge;
    a.weight = b.weight;
  }
  memcpy(out, &a, sizeof(a));
}

static bool dm_has_tree(const void *s, void *user) {
  dm_info_t c;
  (void)user;
  memcpy(&c, s, sizeof(c));
  return c.tree > 0;
}

static unsigned long dm_hash(const void *key, void *user) {
  dm_key_t k;
  unsigned long h;
  (void)user;
  memcpy(&k, key, sizeof(k));
  h = (unsigned long)k.a * 2654435761UL;
  h ^= (unsigned long)k.b + 0x9E3779B9UL + (h << 6) + (h >> 2);
  h ^= h >> 15;
  return h * 2246822519UL;
}

static bool dm_equal(const void *x, const void *y, void *user) {
  (void)user;
  return memcmp(x, y, sizeof(dm_key_t)) == 0;
}

static dm_key_t dm_list_key(size_t x, unsigned level, unsigned kind) {
  dm_key_t k;
  memset(&k, 0, sizeof(k));
  k.a = x;
  k.b = 2 * (size_t)level + kind;
  return k;
}

static unsigned dm_side(const ds_dmsf_t *g, size_t e, size_t x) {
  return g->edge[e].end[0] == x ? 0U : 1U;
}

/* ---- heads: created before use and kept, so lists and heaps never
 * allocate ---- */

static ds_status_t dm_ensure_head(ds_dmsf_t *g, size_t x, unsigned level,
                                  unsigned kind) {
  dm_key_t k = dm_list_key(x, level, kind);
  size_t none = DM_NONE;
  ds_status_t st;
  if (ds_map_get(&g->heads, &k) != NULL) {
    return DS_OK;
  }
  st = ds_map_put(&g->heads, &k, &none);
  return st == DS_EXISTS ? DS_OK : st;
}

static ds_status_t dm_ensure_both(ds_dmsf_t *g, size_t e, unsigned level,
                                  unsigned kind) {
  ds_status_t st = dm_ensure_head(g, g->edge[e].end[0], level, kind);
  if (st == DS_OK) {
    st = dm_ensure_head(g, g->edge[e].end[1], level, kind);
  }
  return st;
}

static size_t dm_head(const ds_dmsf_t *g, size_t x, unsigned level,
                      unsigned kind) {
  dm_key_t k = dm_list_key(x, level, kind);
  const void *p = ds_map_get(&g->heads, &k);
  size_t h = DM_NONE;
  if (p != NULL) {
    memcpy(&h, p, sizeof(h));
  }
  return h;
}

static void dm_set_head(ds_dmsf_t *g, size_t x, unsigned level, unsigned kind,
                        size_t e) {
  dm_key_t k = dm_list_key(x, level, kind);
  void *p = ds_map_get(&g->heads, &k);
  if (p != NULL) {
    memcpy(p, &e, sizeof(e));
  }
}

/* the element of x in F_level from its list and heap */
static void dm_refresh(ds_dmsf_t *g, size_t x, unsigned level) {
  dm_info_t cur;
  dm_info_t want;
  size_t root = dm_head(g, x, level, DM_NONTREE);
  memset(&want, 0, sizeof(want));
  want.tree = dm_head(g, x, level, DM_TREE) != DM_NONE ? 1 : 0;
  want.edge = root;
  want.weight = root != DM_NONE ? g->edge[root].weight : 0.0;
  (void)ds_ett_get(&g->forest[level], x, &cur);
  if (cur.tree != want.tree || cur.edge != want.edge) {
    (void)ds_ett_set(&g->forest[level], x, &want);
  }
}

/* ---- tree edge lists ---- */

static void dm_list_push(ds_dmsf_t *g, size_t x, unsigned level, size_t e) {
  size_t h = dm_head(g, x, level, DM_TREE);
  unsigned s = dm_side(g, e, x);
  g->edge[e].next[s] = h;
  g->edge[e].prev[s] = DM_NONE;
  g->edge[e].child[s] = DM_NONE;
  if (h != DM_NONE) {
    g->edge[h].prev[dm_side(g, h, x)] = e;
  }
  dm_set_head(g, x, level, DM_TREE, e);
}

static void dm_list_unlink(ds_dmsf_t *g, size_t x, unsigned level, size_t e) {
  unsigned s = dm_side(g, e, x);
  size_t p = g->edge[e].prev[s];
  size_t q = g->edge[e].next[s];
  if (p != DM_NONE) {
    g->edge[p].next[dm_side(g, p, x)] = q;
  } else {
    dm_set_head(g, x, level, DM_TREE, q);
  }
  if (q != DM_NONE) {
    g->edge[q].prev[dm_side(g, q, x)] = p;
  }
  g->edge[e].next[s] = DM_NONE;
  g->edge[e].prev[s] = DM_NONE;
}

/* ---- non-tree edge heaps: pairing heaps at each end ---- */

static size_t dm_meld(ds_dmsf_t *g, size_t x, size_t a, size_t b) {
  unsigned sa;
  unsigned sb;
  size_t c;
  if (a == DM_NONE) {
    return b;
  }
  if (b == DM_NONE) {
    return a;
  }
  if (dm_less(g, b, a)) {
    c = a;
    a = b;
    b = c;
  }
  sa = dm_side(g, a, x);
  sb = dm_side(g, b, x);
  c = g->edge[a].child[sa];
  g->edge[b].next[sb] = c;
  g->edge[b].prev[sb] = a;
  if (c != DM_NONE) {
    g->edge[c].prev[dm_side(g, c, x)] = b;
  }
  g->edge[a].child[sa] = b;
  return a;
}

/* the sibling list from c melded into one heap: pairs left to right, then
 * the pairs right to left */
static size_t dm_pairs(ds_dmsf_t *g, size_t x, size_t c) {
  size_t stack = DM_NONE;
  size_t r = DM_NONE;
  while (c != DM_NONE) {
    size_t a = c;
    unsigned s = dm_side(g, a, x);
    size_t b = g->edge[a].next[s];
    size_t ab = a;
    g->edge[a].next[s] = DM_NONE;
    g->edge[a].prev[s] = DM_NONE;
    c = DM_NONE;
    if (b != DM_NONE) {
      unsigned t = dm_side(g, b, x);
      c = g->edge[b].next[t];
      g->edge[b].next[t] = DM_NONE;
      g->edge[b].prev[t] = DM_NONE;
      ab = dm_meld(g, x, a, b);
    }
    g->edge[ab].next[dm_side(g, ab, x)] = stack;
    stack = ab;
  }
  while (stack != DM_NONE) {
    size_t t = stack;
    unsigned s = dm_side(g, t, x);
    stack = g->edge[t].next[s];
    g->edge[t].next[s] = DM_NONE;
    r = dm_meld(g, x, r, t);
  }
  return r;
}

static void dm_heap_push(ds_dmsf_t *g, size_t x, unsigned level, size_t e) {
  unsigned s = dm_side(g, e, x);
  g->edge[e].child[s] = DM_NONE;
  g->edge[e].next[s] = DM_NONE;
  g->edge[e].prev[s] = DM_NONE;
  dm_set_head(g, x, level, DM_NONTREE,
              dm_meld(g, x, dm_head(g, x, level, DM_NONTREE), e));
}

static void dm_heap_remove(ds_dmsf_t *g, size_t x, unsigned level, size_t e) {
  unsigned s = dm_side(g, e, x);
  size_t root = dm_head(g, x, level, DM_NONTREE);
  size_t sub = dm_pairs(g, x, g->edge[e].child[s]);
  g->edge[e].child[s] = DM_NONE;
  if (e == root) {
    root = sub;
  } else {
    size_t p = g->edge[e].prev[s];
    size_t q = g->edge[e].next[s];
    unsigned ps = dm_side(g, p, x);
    if (g->edge[p].child[ps] == e) {
      g->edge[p].child[ps] = q;
    } else {
      g->edge[p].next[ps] = q;
    }
    if (q != DM_NONE) {
      g->edge[q].prev[dm_side(g, q, x)] = p;
    }
    root = dm_meld(g, x, root, sub);
  }
  g->edge[e].next[s] = DM_NONE;
  g->edge[e].prev[s] = DM_NONE;
  dm_set_head(g, x, level, DM_NONTREE, root);
}

/* an edge in or out of the structures of both ends at its level */
static void dm_attach(ds_dmsf_t *g, size_t e) {
  size_t k;
  for (k = 0; k < 2; k++) {
    size_t x = g->edge[e].end[k];
    if (g->edge[e].tree) {
      dm_list_push(g, x, g->edge[e].level, e);
    } else {
      dm_heap_push(g, x, g->edge[e].level, e);
    }
    dm_refresh(g, x, g->edge[e].level);
  }
}

static void dm_detach(ds_dmsf_t *g, size_t e) {
  size_t k;
  for (k = 0; k < 2; k++) {
    size_t x = g->edge[e].end[k];
    if (g->edge[e].tree) {
      dm_list_unlink(g, x, g->edge[e].level, e);
    } else {
      dm_heap_remove(g, x, g->edge[e].level, e);
    }
    dm_refresh(g, x, g->edge[e].level);
  }
}

/* ---- building ---- */

static void dm_forests_destroy(ds_dmsf_t *g) {
  unsigned i;
  for (i = 0; i < g->levels; i++) {
    if (g->forest_ready[i]) {
      ds_ett_destroy(&g->forest[i]);
      g->forest_ready[i] = 0;
    }
  }
}

/* edge indices by (weight, index): bottom-up merge sort */
static void dm_sort(const ds_dmsf_t *g, size_t *a, size_t *tmp, size_t m) {
  size_t width;
  for (width = 1; width < m; width *= 2) {
    size_t lo;
    for (lo = 0; lo < m; lo += 2 * width) {
      size_t mid = lo + width < m ? lo + width : m;
      size_t hi = lo + 2 * width < m ? lo + 2 * width : m;
      size_t i = lo, j = mid, k = lo;
      while (i < mid && j < hi) {
        tmp[k++] = dm_less(g, a[j], a[i]) ? a[j++] : a[i++];
      }
      while (i < mid) {
        tmp[k++] = a[i++];
      }
      while (j < hi) {
        tmp[k++] = a[j++];
      }
    }
    memcpy(a, tmp, m * sizeof(size_t));
  }
}

static size_t dm_find(size_t *parent, size_t x) {
  while (parent[x] != x) {
    parent[x] = parent[parent[x]];
    x = parent[x];
  }
  return x;
}

/* Kruskal over the live edges: in_forest[e] for each; NULL scratch on
 * failure */
static ds_status_t dm_kruskal(const ds_dmsf_t *g, unsigned char *in_forest) {
  size_t *order = (size_t *)ds_context_alloc(g->context, (2 * g->m + g->n + 1) *
                                                             sizeof(size_t));
  size_t *tmp;
  size_t *parent;
  size_t k = 0;
  size_t i;
  if (order == NULL) {
    return DS_ERR_ALLOC;
  }
  tmp = order + g->m;
  parent = tmp + g->m;
  for (i = 0; i < g->m; i++) {
    in_forest[i] = 0;
    if (g->edge[i].live && g->edge[i].end[0] != g->edge[i].end[1]) {
      order[k++] = i;
    }
  }
  dm_sort(g, order, tmp, k);
  for (i = 0; i < g->n; i++) {
    parent[i] = i;
  }
  for (i = 0; i < k; i++) {
    size_t e = order[i];
    size_t a = dm_find(parent, g->edge[e].end[0]);
    size_t b = dm_find(parent, g->edge[e].end[1]);
    if (a != b) {
      parent[a] = b;
      in_forest[e] = 1;
    }
  }
  ds_context_free(g->context, order);
  return DS_OK;
}

/* forests, heads and placement of every live edge at level 0 */
static ds_status_t dm_build(ds_dmsf_t *g) {
  ds_ett_config_t c;
  dm_info_t *blank;
  unsigned char *in_forest;
  ds_status_t st = DS_OK;
  size_t e;
  size_t x;
  unsigned i;
  if (g->map_ready) {
    ds_map_destroy(&g->heads);
    g->map_ready = false;
  }
  dm_forests_destroy(g);
  st = ds_map_init(&g->heads, NULL, sizeof(dm_key_t), sizeof(size_t), dm_hash,
                   dm_equal, NULL, g->context);
  if (st != DS_OK) {
    return st;
  }
  g->map_ready = true;
  blank = (dm_info_t *)ds_context_alloc(g->context, g->n * sizeof(dm_info_t));
  in_forest = (unsigned char *)ds_context_alloc(g->context, g->m + 1);
  if (blank == NULL || in_forest == NULL) {
    ds_context_free(g->context, blank);
    ds_context_free(g->context, in_forest);
    return DS_ERR_ALLOC;
  }
  for (x = 0; x < g->n; x++) {
    memset(&blank[x], 0, sizeof(dm_info_t));
    blank[x].edge = DM_NONE;
  }
  ds_ett_config_init(&c);
  c.elem_size = sizeof(dm_info_t);
  c.summary_size = sizeof(dm_info_t);
  c.summarize = dm_summarize;
  c.combine = dm_combine;
  c.context = g->context;
  for (i = 0; i < g->levels && st == DS_OK; i++) {
    c.seed = 7919UL * (unsigned long)i + 3UL;
    st = ds_ett_init(&g->forest[i], g->n, blank, &c);
    g->forest_ready[i] = st == DS_OK ? 1 : 0;
  }
  if (st == DS_OK) {
    st = dm_kruskal(g, in_forest);
  }
  g->tree_edges = 0;
  g->weight = 0.0;
  for (e = 0; st == DS_OK && e < g->m; e++) {
    ds_dmsf_edge_t *d = &g->edge[e];
    size_t k;
    d->level = 0;
    d->tree = in_forest[e];
    for (k = 0; k < 2; k++) {
      d->next[k] = DM_NONE;
      d->prev[k] = DM_NONE;
      d->child[k] = DM_NONE;
    }
    if (!d->live || d->end[0] == d->end[1]) {
      continue; /* gone, or a loop: in no list, heap or forest */
    }
    st = dm_ensure_both(g, e, 0, d->tree ? DM_TREE : DM_NONTREE);
    if (st == DS_OK && d->tree) {
      st = ds_ett_link(&g->forest[0], d->end[0], d->end[1]);
      g->tree_edges++;
      g->weight += d->weight;
    }
    if (st == DS_OK) {
      dm_attach(g, e);
    }
  }
  ds_context_free(g->context, blank);
  ds_context_free(g->context, in_forest);
  return st;
}

ds_status_t ds_dmsf_init(ds_dmsf_t *g, size_t n, const size_t *u,
                         const size_t *v, const double *w, size_t m,
                         ds_context_t *context) {
  size_t i;
  size_t k;
  ds_status_t st;
  if (g == NULL || (m > 0 && (u == NULL || v == NULL || w == NULL))) {
    return DS_ERR_NULL;
  }
  memset(g, 0, sizeof(*g));
  if (n == 0) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < m; i++) {
    if (u[i] >= n || v[i] >= n) {
      return DS_ERR_RANGE;
    }
    if (!(w[i] >= w[i])) {
      return DS_ERR_INVALID; /* NaN */
    }
  }
  if (m > (size_t)-1 / sizeof(ds_dmsf_edge_t) / 2 ||
      n > (size_t)-1 / sizeof(dm_info_t) / 2) {
    return DS_ERR_OVERFLOW;
  }
  g->n = n;
  g->m = m;
  g->context = context;
  g->levels = 1; /* floor(log2 n) + 1 */
  for (k = n; k > 1; k >>= 1) {
    g->levels++;
  }
  g->forest =
      (ds_ett_t *)ds_context_alloc(context, g->levels * sizeof(ds_ett_t));
  g->forest_ready = (unsigned char *)ds_context_calloc(context, g->levels, 1);
  g->edge = (ds_dmsf_edge_t *)ds_context_alloc(
      context, (m + 1) * sizeof(ds_dmsf_edge_t));
  if (g->forest == NULL || g->forest_ready == NULL || g->edge == NULL) {
    ds_dmsf_destroy(g);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < m; i++) {
    memset(&g->edge[i], 0, sizeof(ds_dmsf_edge_t));
    g->edge[i].end[0] = u[i];
    g->edge[i].end[1] = v[i];
    g->edge[i].weight = w[i];
    g->edge[i].live = 1;
  }
  g->edges = m;
  st = dm_build(g);
  if (st != DS_OK) {
    ds_dmsf_destroy(g);
    return st;
  }
  return DS_OK;
}

void ds_dmsf_destroy(ds_dmsf_t *g) {
  if (g == NULL) {
    return;
  }
  if (g->forest_ready != NULL) {
    dm_forests_destroy(g);
  }
  if (g->map_ready) {
    ds_map_destroy(&g->heads);
  }
  ds_context_free(g->context, g->forest);
  ds_context_free(g->context, g->forest_ready);
  ds_context_free(g->context, g->edge);
  memset(g, 0, sizeof(*g));
}

/* ---- deletion ---- */

/* the level-i tree edges of x's tree in F_i move to level i + 1 */
static ds_status_t dm_promote_tree_edges(ds_dmsf_t *g, unsigned i, size_t x) {
  size_t w;
  while (ds_ett_component_find(&g->forest[i], x, dm_has_tree, NULL, &w) ==
         DS_OK) {
    size_t f = dm_head(g, w, i, DM_TREE);
    size_t a = g->edge[f].end[0];
    size_t b = g->edge[f].end[1];
    ds_status_t st = dm_ensure_both(g, f, i + 1, DM_TREE);
    if (st == DS_OK) {
      st = ds_ett_link(&g->forest[i + 1], a, b);
    }
    if (st != DS_OK) {
      return st;
    }
    dm_detach(g, f);
    g->edge[f].level = i + 1;
    dm_attach(g, f);
    g->promotions++;
  }
  return DS_OK;
}

/* level i's search for the lightest edge reconnecting the trees of a and
 * b in F_i */
static ds_status_t dm_replace(ds_dmsf_t *g, unsigned i, size_t a, size_t b,
                              size_t *out) {
  size_t x;
  ds_status_t st;
  *out = DM_NONE;
  x = ds_ett_component_size(&g->forest[i], a) <=
              ds_ett_component_size(&g->forest[i], b)
          ? a
          : b;
  if (i + 1 < g->levels) {
    st = dm_promote_tree_edges(g, i, x);
    if (st != DS_OK) {
      return st;
    }
  }
  for (;;) {
    dm_info_t s;
    size_t f;
    size_t p;
    size_t q;
    if (ds_ett_component_query(&g->forest[i], x, &s) != DS_OK ||
        s.edge == DM_NONE) {
      return DS_OK; /* nothing left at this level */
    }
    f = s.edge;
    p = g->edge[f].end[0];
    q = g->edge[f].end[1];
    if (ds_ett_connected(&g->forest[i], p, q)) { /* inside: up one level */
      if (i + 1 >= g->levels) {
        return DS_ERR_STATE; /* the size bound rules this out */
      }
      st = dm_ensure_both(g, f, i + 1, DM_NONTREE);
      if (st != DS_OK) {
        return st;
      }
      dm_detach(g, f);
      g->edge[f].level = i + 1;
      dm_attach(g, f);
      g->promotions++;
    } else { /* the replacement: into F_0 .. F_i */
      unsigned linked = 0;
      st = dm_ensure_both(g, f, i, DM_TREE);
      while (st == DS_OK && linked <= i) {
        st = ds_ett_link(&g->forest[linked], p, q);
        if (st == DS_OK) {
          linked++;
        }
      }
      if (st != DS_OK) {
        while (linked-- > 0) {
          (void)ds_ett_cut(&g->forest[linked], p, q);
        }
        return st;
      }
      dm_detach(g, f);
      g->edge[f].tree = 1;
      dm_attach(g, f);
      g->replacements++;
      *out = f;
      return DS_OK;
    }
  }
}

ds_status_t ds_dmsf_delete(ds_dmsf_t *g, size_t e, size_t *out_replacement) {
  ds_dmsf_edge_t *d;
  size_t a;
  size_t b;
  size_t f = DM_NONE;
  unsigned level;
  unsigned i;
  if (out_replacement != NULL) {
    *out_replacement = DM_NONE;
  }
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  if (e >= g->m) {
    return DS_ERR_RANGE;
  }
  if (g->broken) {
    return DS_ERR_STATE;
  }
  d = &g->edge[e];
  if (!d->live) {
    return DS_NOT_FOUND;
  }
  a = d->end[0];
  b = d->end[1];
  level = d->level;
  g->edges--;
  if (a == b) {
    d->live = 0;
    return DS_OK;
  }
  dm_detach(g, e);
  d->live = 0;
  if (!d->tree) {
    return DS_OK;
  }
  for (i = 0; i <= level; i++) {
    (void)ds_ett_cut(&g->forest[i], a, b);
  }
  d->tree = 0;
  g->tree_edges--;
  g->weight -= d->weight;
  for (i = level + 1; i-- > 0;) {
    ds_status_t st = dm_replace(g, i, a, b, &f);
    if (st != DS_OK) {
      g->broken = true;
      return st;
    }
    if (f != DM_NONE) {
      g->tree_edges++;
      g->weight += g->edge[f].weight;
      break;
    }
  }
  if (g->tree_edges == 0) {
    g->weight = 0.0; /* no drift left over */
  }
  if (out_replacement != NULL) {
    *out_replacement = f;
  }
  return DS_OK;
}

/* ---- queries ---- */

bool ds_dmsf_connected(const ds_dmsf_t *g, size_t u, size_t v) {
  if (g == NULL || u >= g->n || v >= g->n || g->broken) {
    return false;
  }
  return u == v || ds_ett_connected(&g->forest[0], u, v);
}

size_t ds_dmsf_component_size(const ds_dmsf_t *g, size_t v) {
  if (g == NULL || v >= g->n || g->broken) {
    return 0;
  }
  return ds_ett_component_size(&g->forest[0], v);
}

bool ds_dmsf_live(const ds_dmsf_t *g, size_t e) {
  return g != NULL && e < g->m && g->edge[e].live;
}

bool ds_dmsf_in_forest(const ds_dmsf_t *g, size_t e) {
  return g != NULL && e < g->m && g->edge[e].live && g->edge[e].tree;
}

unsigned ds_dmsf_level(const ds_dmsf_t *g, size_t e) {
  return g == NULL || e >= g->m ? 0U : g->edge[e].level;
}

size_t ds_dmsf_edges(const ds_dmsf_t *g) { return g == NULL ? 0 : g->edges; }

size_t ds_dmsf_forest_edges(const ds_dmsf_t *g) {
  return g == NULL ? 0 : g->tree_edges;
}

double ds_dmsf_weight(const ds_dmsf_t *g) {
  return g == NULL ? 0.0 : g->weight;
}

bool ds_dmsf_broken(const ds_dmsf_t *g) { return g != NULL && g->broken; }

ds_status_t ds_dmsf_rebuild(ds_dmsf_t *g) {
  ds_status_t st;
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  g->broken = true;
  st = dm_build(g);
  if (st == DS_OK) {
    g->broken = false;
  }
  return st;
}

/* ---- invariants ---- */

/* the heap at (x, level): order, links, members; its size to *count */
static bool dm_check_heap(ds_dmsf_t *g, size_t x, unsigned level, size_t *stack,
                          size_t *count) {
  size_t root = dm_head(g, x, level, DM_NONTREE);
  size_t top = 0;
  *count = 0;
  if (root == DM_NONE) {
    return true;
  }
  if (g->edge[root].prev[dm_side(g, root, x)] != DM_NONE ||
      g->edge[root].next[dm_side(g, root, x)] != DM_NONE) {
    return false;
  }
  stack[top++] = root;
  while (top > 0) {
    size_t e = stack[--top];
    unsigned s = dm_side(g, e, x);
    size_t c;
    size_t prev = e;
    if (++*count > g->m || !g->edge[e].live || g->edge[e].tree ||
        g->edge[e].level != level ||
        (g->edge[e].end[0] != x && g->edge[e].end[1] != x)) {
      return false;
    }
    for (c = g->edge[e].child[s]; c != DM_NONE;) {
      unsigned t = dm_side(g, c, x);
      if (g->edge[c].prev[t] != prev || dm_less(g, c, e) || top >= g->m) {
        return false;
      }
      stack[top++] = c;
      prev = c;
      c = g->edge[c].next[t];
    }
  }
  return true;
}

bool ds_dmsf_check(ds_dmsf_t *g) {
  unsigned char *in_forest;
  size_t *stack;
  size_t e;
  size_t x;
  unsigned i;
  size_t trees = 0;
  size_t ends = 0;
  size_t entries = 0;
  double total = 0.0;
  double scale = 1.0;
  double diff;
  bool ok = true;
  if (g == NULL || g->broken) {
    return false;
  }
  in_forest = (unsigned char *)ds_context_alloc(g->context, g->m + 1);
  stack = (size_t *)ds_context_alloc(g->context, (g->m + 1) * sizeof(size_t));
  if (in_forest == NULL || stack == NULL || dm_kruskal(g, in_forest) != DS_OK) {
    ds_context_free(g->context, in_forest);
    ds_context_free(g->context, stack);
    return false;
  }
  for (e = 0; e < g->m && ok; e++) {
    ds_dmsf_edge_t *d = &g->edge[e];
    scale += d->weight < 0 ? -d->weight : d->weight;
    if (!d->live) {
      continue;
    }
    if (d->level >= g->levels || d->tree != in_forest[e]) {
      ok = false; /* F is the minimum spanning forest */
      break;
    }
    if (d->end[0] == d->end[1]) {
      continue;
    }
    if (d->tree) {
      for (i = 0; i < g->levels; i++) {
        if (ds_ett_has_edge(&g->forest[i], d->end[0], d->end[1]) !=
            (i <= d->level)) {
          ok = false;
        }
      }
      trees++;
      total += d->weight;
    } else if (!ds_ett_connected(&g->forest[d->level], d->end[0], d->end[1])) {
      ok = false; /* invariant (i) */
    }
    ends += 2;
  }
  diff = total - g->weight;
  if (diff < 0) {
    diff = -diff;
  }
  /* the running sum drifts by rounding: a loose bound */
  if (trees != g->tree_edges || diff > 1e-9 * scale) {
    ok = false;
  }
  for (i = 0; i < g->levels && ok; i++) {
    for (x = 0; x < g->n && ok; x++) {
      dm_info_t c;
      size_t f = dm_head(g, x, i, DM_TREE);
      size_t prev = DM_NONE;
      size_t len = 0;
      size_t count = 0;
      if (ds_ett_component_size(&g->forest[i], x) > (g->n >> i)) {
        ok = false; /* trees of F_i have at most n / 2^i vertices */
        break;
      }
      while (f != DM_NONE && ok) {
        unsigned s;
        if (f >= g->m || !g->edge[f].live || !g->edge[f].tree ||
            g->edge[f].level != i ||
            (g->edge[f].end[0] != x && g->edge[f].end[1] != x) || len > g->m) {
          ok = false;
          break;
        }
        s = dm_side(g, f, x);
        if (g->edge[f].prev[s] != prev) {
          ok = false;
        }
        prev = f;
        f = g->edge[f].next[s];
        len++;
      }
      if (!ok || !dm_check_heap(g, x, i, stack, &count)) {
        ok = false;
        break;
      }
      entries += len + count;
      (void)ds_ett_get(&g->forest[i], x, &c);
      if (c.tree != (len > 0 ? 1U : 0U) ||
          c.edge != dm_head(g, x, i, DM_NONTREE)) {
        ok = false; /* the elements follow the lists and heaps */
      }
    }
  }
  if (ok && entries != ends) {
    ok = false; /* every live edge sits at both its ends, once */
  }
  ds_context_free(g->context, in_forest);
  ds_context_free(g->context, stack);
  return ok;
}
