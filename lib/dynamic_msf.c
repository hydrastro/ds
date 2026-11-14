#include "dynamic_msf.h"
#include <stdlib.h>
#include <string.h>

#define DY_NONE DS_DYN_MSF_NONE

/* the strict order on edges: weight, then id */
static bool dy_less(const ds_dyn_msf_t *g, size_t a, size_t b) {
  double wa = g->edge[a].weight;
  double wb = g->edge[b].weight;
  if (wa < wb) {
    return true;
  }
  return !(wb < wa) && a < b;
}

static unsigned long dy_hash(const void *key, void *user) {
  size_t k;
  unsigned long h;
  (void)user;
  memcpy(&k, key, sizeof(k));
  h = (unsigned long)k * 2654435761UL;
  h ^= h >> 15;
  return h * 2246822519UL;
}

static bool dy_equal(const void *a, const void *b, void *user) {
  (void)user;
  return memcmp(a, b, sizeof(size_t)) == 0;
}

/* ---- the link-cut tree over vertices and edge nodes ---- */

static size_t *dy_tags(const ds_dyn_msf_t *g, size_t x) {
  return g->tag + x * g->slot_capacity;
}

static size_t *dy_lazies(const ds_dyn_msf_t *g, size_t x) {
  return g->lazy + x * g->slot_capacity;
}

static bool dy_is_root(const ds_dyn_msf_t *g, size_t x) {
  size_t p = g->node[x].parent;
  return p == DY_NONE || (g->node[p].ch[0] != x && g->node[p].ch[1] != x);
}

/* the heavier of two edge nodes, either may be NONE */
static size_t dy_heavier(const ds_dyn_msf_t *g, size_t a, size_t b) {
  if (a == DY_NONE) {
    return b;
  }
  if (b == DY_NONE) {
    return a;
  }
  return dy_less(g, a - g->n, b - g->n) ? b : a;
}

static void dy_pull(ds_dyn_msf_t *g, size_t x) {
  ds_dyn_msf_node_t *t = &g->node[x];
  size_t best = x >= g->n ? x : DY_NONE;
  size_t mm = t->mark;
  size_t k;
  for (k = 0; k < 2; k++) {
    size_t c = t->ch[k];
    if (c != DY_NONE) {
      best = dy_heavier(g, best, g->node[c].best);
      if (g->node[c].max_mark > mm) {
        mm = g->node[c].max_mark;
      }
    }
  }
  t->best = best;
  t->max_mark = mm;
}

static void dy_apply_rev(ds_dyn_msf_t *g, size_t x) {
  ds_dyn_msf_node_t *t = &g->node[x];
  size_t c = t->ch[0];
  t->ch[0] = t->ch[1];
  t->ch[1] = c;
  t->rev ^= 1;
}

static void dy_apply_mark(ds_dyn_msf_t *g, size_t x, size_t m) {
  g->node[x].mark = m;
  g->node[x].max_mark = m;
  g->node[x].lazy_mark = m;
}

static void dy_apply_tag(ds_dyn_msf_t *g, size_t x, size_t s, size_t v) {
  dy_tags(g, x)[s] = v;
  dy_lazies(g, x)[s] = v;
  g->node[x].lazy_tags = 1;
}

static void dy_push(ds_dyn_msf_t *g, size_t x) {
  ds_dyn_msf_node_t *t = &g->node[x];
  size_t k;
  if (t->rev) {
    for (k = 0; k < 2; k++) {
      if (t->ch[k] != DY_NONE) {
        dy_apply_rev(g, t->ch[k]);
      }
    }
    t->rev = 0;
  }
  if (t->lazy_mark != 0) {
    for (k = 0; k < 2; k++) {
      if (t->ch[k] != DY_NONE) {
        dy_apply_mark(g, t->ch[k], t->lazy_mark);
      }
    }
    t->lazy_mark = 0;
  }
  if (t->lazy_tags) {
    size_t *lz = dy_lazies(g, x);
    size_t s;
    for (s = 0; s < g->slot_capacity; s++) {
      if (lz[s] == DY_NONE) {
        continue;
      }
      for (k = 0; k < 2; k++) {
        if (t->ch[k] != DY_NONE) {
          dy_apply_tag(g, t->ch[k], s, lz[s]);
        }
      }
      lz[s] = DY_NONE;
    }
    t->lazy_tags = 0;
  }
}

static void dy_rotate(ds_dyn_msf_t *g, size_t x) {
  size_t p = g->node[x].parent;
  size_t gp = g->node[p].parent;
  size_t k = g->node[p].ch[1] == x ? 1 : 0;
  size_t b = g->node[x].ch[1 - k];
  if (!dy_is_root(g, p)) {
    if (g->node[gp].ch[0] == p) {
      g->node[gp].ch[0] = x;
    } else {
      g->node[gp].ch[1] = x;
    }
  }
  g->node[x].parent = gp;
  g->node[p].ch[k] = b;
  if (b != DY_NONE) {
    g->node[b].parent = p;
  }
  g->node[x].ch[1 - k] = p;
  g->node[p].parent = x;
  dy_pull(g, p);
  dy_pull(g, x);
}

static void dy_splay(ds_dyn_msf_t *g, size_t x) {
  size_t top = 0;
  size_t y = x;
  g->path[top++] = y;
  while (!dy_is_root(g, y)) {
    y = g->node[y].parent;
    g->path[top++] = y;
  }
  while (top > 0) { /* pending work from the top of the splay tree down */
    dy_push(g, g->path[--top]);
  }
  while (!dy_is_root(g, x)) {
    size_t p = g->node[x].parent;
    if (!dy_is_root(g, p)) {
      size_t gp = g->node[p].parent;
      bool same = (g->node[gp].ch[0] == p) == (g->node[p].ch[0] == x);
      dy_rotate(g, same ? p : x);
    }
    dy_rotate(g, x);
  }
}

static void dy_access(ds_dyn_msf_t *g, size_t x) {
  size_t last = DY_NONE;
  size_t y;
  for (y = x; y != DY_NONE; y = g->node[y].parent) {
    dy_splay(g, y);
    g->node[y].ch[1] = last;
    dy_pull(g, y);
    last = y;
  }
  dy_splay(g, x);
}

static void dy_make_root(ds_dyn_msf_t *g, size_t x) {
  dy_access(g, x);
  dy_apply_rev(g, x);
}

static size_t dy_find_root(ds_dyn_msf_t *g, size_t x) {
  size_t y = x;
  dy_access(g, x);
  for (;;) {
    dy_push(g, y);
    if (g->node[y].ch[0] == DY_NONE) {
      break;
    }
    y = g->node[y].ch[0];
  }
  dy_splay(g, y);
  return y;
}

static bool dy_connected(ds_dyn_msf_t *g, size_t u, size_t v) {
  return u == v || dy_find_root(g, u) == dy_find_root(g, v);
}

static void dy_link(ds_dyn_msf_t *g, size_t x, size_t y) {
  dy_make_root(g, x);
  g->node[x].parent = y;
}

static void dy_cut(ds_dyn_msf_t *g, size_t x, size_t y) {
  dy_make_root(g, x);
  dy_access(g, y);
  g->node[y].ch[0] = DY_NONE;
  g->node[x].parent = DY_NONE;
  dy_pull(g, y);
}

/* the u-v path as y's splay tree; y = v */
static void dy_expose(ds_dyn_msf_t *g, size_t u, size_t v) {
  dy_make_root(g, u);
  dy_access(g, v);
}

static void dy_link_edge(ds_dyn_msf_t *g, size_t id) {
  size_t t = g->n + id;
  dy_link(g, t, g->edge[id].end[0]);
  dy_link(g, g->edge[id].end[1], t);
}

static void dy_cut_edge(ds_dyn_msf_t *g, size_t id) {
  size_t t = g->n + id;
  dy_cut(g, g->edge[id].end[0], t);
  dy_cut(g, t, g->edge[id].end[1]);
}

/* the deepest node marked with gen on the path from the root to s */
static size_t dy_deepest_marked(ds_dyn_msf_t *g, size_t s, size_t gen) {
  size_t x = s;
  dy_access(g, s);
  for (;;) {
    size_t r;
    dy_push(g, x);
    r = g->node[x].ch[1];
    if (r != DY_NONE && g->node[r].max_mark >= gen) {
      x = r;
    } else if (g->node[x].mark >= gen) {
      break;
    } else {
      x = g->node[x].ch[0];
    }
  }
  dy_splay(g, x);
  return x;
}

static void dy_node_reset(ds_dyn_msf_t *g, size_t x) {
  ds_dyn_msf_node_t *t = &g->node[x];
  size_t s;
  t->ch[0] = DY_NONE;
  t->ch[1] = DY_NONE;
  t->parent = DY_NONE;
  t->best = x >= g->n ? x : DY_NONE;
  t->mark = 0;
  t->max_mark = 0;
  t->lazy_mark = 0;
  t->rev = 0;
  t->lazy_tags = 0;
  for (s = 0; s < g->slot_capacity; s++) {
    dy_tags(g, x)[s] = DY_NONE;
    dy_lazies(g, x)[s] = DY_NONE;
  }
}

/* ---- storage ---- */

/* tag arrays for slots structures per node, keeping what is there */
static ds_status_t dy_grow_slots(ds_dyn_msf_t *g, size_t want) {
  size_t cap = g->slot_capacity;
  size_t nodes = g->n + g->edge_capacity;
  size_t *tag;
  size_t *lazy;
  ds_dyn_msf_slot_t *slot;
  size_t x, s;
  if (want <= cap) {
    return DS_OK;
  }
  while (cap < want) {
    cap *= 2;
  }
  if (nodes > (size_t)-1 / cap / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  tag = (size_t *)ds_context_alloc(g->context, nodes * cap * sizeof(size_t));
  lazy = (size_t *)ds_context_alloc(g->context, nodes * cap * sizeof(size_t));
  slot = (ds_dyn_msf_slot_t *)ds_context_realloc(
      g->context, g->slot, cap * sizeof(ds_dyn_msf_slot_t));
  if (tag == NULL || lazy == NULL || slot == NULL) {
    ds_context_free(g->context, tag);
    ds_context_free(g->context, lazy);
    if (slot != NULL) {
      g->slot = slot;
    }
    return DS_ERR_ALLOC;
  }
  g->slot = slot;
  for (s = g->slot_capacity; s < cap; s++) {
    memset(&g->slot[s], 0, sizeof(ds_dyn_msf_slot_t));
  }
  for (x = 0; x < nodes; x++) {
    for (s = 0; s < cap; s++) {
      bool old = s < g->slot_capacity;
      tag[x * cap + s] = old ? g->tag[x * g->slot_capacity + s] : DY_NONE;
      lazy[x * cap + s] = old ? g->lazy[x * g->slot_capacity + s] : DY_NONE;
    }
  }
  ds_context_free(g->context, g->tag);
  ds_context_free(g->context, g->lazy);
  g->tag = tag;
  g->lazy = lazy;
  g->slot_capacity = cap;
  return DS_OK;
}

/* room for one more edge id */
static ds_status_t dy_grow_edges(ds_dyn_msf_t *g) {
  size_t cap;
  size_t nodes;
  void *p;
  size_t x;
  if (g->free_edge != DY_NONE || g->edge_slots < g->edge_capacity) {
    return DS_OK;
  }
  cap = g->edge_capacity < 8 ? 16 : 2 * g->edge_capacity;
  nodes = g->n + cap;
  if (cap > (size_t)-1 / sizeof(ds_dyn_msf_edge_t) ||
      nodes > (size_t)-1 / g->slot_capacity / sizeof(size_t) ||
      nodes > (size_t)-1 / sizeof(ds_dyn_msf_node_t)) {
    return DS_ERR_OVERFLOW;
  }
  p = ds_context_realloc(g->context, g->edge, cap * sizeof(ds_dyn_msf_edge_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  g->edge = (ds_dyn_msf_edge_t *)p;
  p = ds_context_realloc(g->context, g->node,
                         nodes * sizeof(ds_dyn_msf_node_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  g->node = (ds_dyn_msf_node_t *)p;
  p = ds_context_realloc(g->context, g->path, nodes * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  g->path = (size_t *)p;
  p = ds_context_realloc(g->context, g->tag,
                         nodes * g->slot_capacity * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  g->tag = (size_t *)p;
  p = ds_context_realloc(g->context, g->lazy,
                         nodes * g->slot_capacity * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  g->lazy = (size_t *)p;
  for (x = g->n + g->edge_capacity; x < nodes; x++) {
    dy_node_reset(g, x);
  }
  g->edge_capacity = cap;
  return DS_OK;
}

/* ---- the structures A_i ---- */

static void dy_slot_clear(ds_dyn_msf_t *g, size_t i) {
  ds_dyn_msf_slot_t *s = &g->slot[i];
  if (s->ready) {
    ds_dmsf_destroy(&s->msf);
  }
  if (s->map_ready) {
    ds_map_destroy(&s->local);
  }
  ds_context_free(g->context, s->global);
  ds_context_free(g->context, s->is_super);
  ds_context_free(g->context, s->super_local);
  memset(s, 0, sizeof(*s));
}

typedef struct dy_pair {
  size_t key;
  size_t value;
} dy_pair_t;

static int dy_pair_cmp(const void *a, const void *b) {
  const dy_pair_t *x = (const dy_pair_t *)a;
  const dy_pair_t *y = (const dy_pair_t *)b;
  if (x->key != y->key) {
    return x->key < y->key ? -1 : 1;
  }
  return x->value < y->value ? -1 : x->value > y->value ? 1 : 0;
}

typedef struct dy_local {
  double weight;
  size_t key;    /* the graph edge the weight comes from: the order */
  size_t ref;    /* graph edge id, or super id */
  size_t end[2]; /* graph vertices */
  int is_super;
} dy_local_t;

static int dy_local_cmp(const void *a, const void *b) {
  const dy_local_t *x = (const dy_local_t *)a;
  const dy_local_t *y = (const dy_local_t *)b;
  if (x->weight < y->weight) {
    return -1;
  }
  if (y->weight < x->weight) {
    return 1;
  }
  return x->key < y->key ? -1 : x->key > y->key ? 1 : 0;
}

typedef struct dy_super {
  size_t a;
  size_t b;
  int alive;
} dy_super_t;

typedef struct dy_build {
  size_t slot;
  size_t gen;
  size_t base;
  size_t root;
  dy_super_t *super;
  size_t supers;
  size_t *virt; /* virtual vertices, in order */
  size_t virts;
} dy_build_t;

static void dy_add_virtual(ds_dyn_msf_t *g, dy_build_t *b, size_t v) {
  g->local_id[v] = b->virts;
  g->virt[v] = b->gen;
  b->virt[b->virts++] = v;
}

static bool dy_is_virtual(const ds_dyn_msf_t *g, const dy_build_t *b,
                          size_t v) {
  return g->virt[v] == b->gen;
}

/* a new super edge over the F path a..b, tagged on every node of it */
static void dy_new_super(ds_dyn_msf_t *g, dy_build_t *b, size_t from,
                         size_t to) {
  size_t id = b->base + b->supers;
  b->super[b->supers].a = from;
  b->super[b->supers].b = to;
  b->super[b->supers].alive = 1;
  b->supers++;
  dy_expose(g, from, to);
  dy_apply_tag(g, to, b->slot, id);
}

/* x lies inside a super edge: cut it in two there */
static void dy_split(ds_dyn_msf_t *g, dy_build_t *b, size_t x) {
  size_t id;
  dy_super_t *p;
  dy_access(g, x);
  id = dy_tags(g, x)[b->slot];
  p = &b->super[id - b->base];
  p->alive = 0;
  dy_new_super(g, b, p->a, x);
  p = &b->super[id - b->base]; /* (the array does not move) */
  dy_new_super(g, b, x, p->b);
  dy_add_virtual(g, b, x);
}

/* A_j from scratch over the graph edges e[0..count), all non-tree edges
 * of the graph: the compressed forest joining their ends, then the
 * deletions-only structure over both */
static ds_status_t dy_build_slot(ds_dyn_msf_t *g, size_t j, const size_t *e,
                                 size_t count) {
  ds_dyn_msf_slot_t *slot = &g->slot[j];
  dy_build_t b;
  dy_pair_t *ends = NULL;
  dy_local_t *loc = NULL;
  size_t *lu = NULL, *lv = NULL;
  double *lw = NULL;
  size_t k, i, nends = 0, nloc = 0;
  ds_status_t st = DS_OK;
  memset(&b, 0, sizeof(b));
  if (count == 0) {
    return DS_OK;
  }
  b.slot = j;
  b.gen = ++g->generation;
  b.base = g->next_super;
  ends = (dy_pair_t *)ds_context_alloc(g->context, 2 * count * sizeof(*ends));
  /* per end: a new super edge, and a split into two (dead records stay) */
  b.super = (dy_super_t *)ds_context_alloc(g->context, (6 * count + 2) *
                                                           sizeof(dy_super_t));
  b.virt = (size_t *)ds_context_alloc(g->context, 4 * count * sizeof(size_t));
  loc = (dy_local_t *)ds_context_alloc(g->context,
                                       5 * count * sizeof(dy_local_t));
  lu = (size_t *)ds_context_alloc(g->context, 5 * count * sizeof(size_t));
  lv = (size_t *)ds_context_alloc(g->context, 5 * count * sizeof(size_t));
  lw = (double *)ds_context_alloc(g->context, 5 * count * sizeof(double));
  if (ends == NULL || b.super == NULL || b.virt == NULL || loc == NULL ||
      lu == NULL || lv == NULL || lw == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  /* the ends, grouped by their tree of F (roots read before any
   * rerooting) */
  for (i = 0; i < count; i++) {
    for (k = 0; k < 2; k++) {
      size_t v = g->edge[e[i]].end[k];
      if (g->stamp[v] != b.gen) {
        g->stamp[v] = b.gen;
        ends[nends].value = v;
        ends[nends].key = dy_find_root(g, v);
        nends++;
      }
    }
  }
  qsort(ends, nends, sizeof(*ends), dy_pair_cmp);
  for (i = 0; i < nends; i++) {
    size_t v = ends[i].value;
    size_t x;
    if (i == 0 || ends[i].key != ends[i - 1].key) { /* a new tree */
      b.root = v;
      dy_make_root(g, v);
      dy_access(g, v); /* v alone in its splay tree: mark just v */
      g->node[v].mark = b.gen;
      dy_pull(g, v);
      dy_add_virtual(g, &b, v);
      continue;
    }
    if (dy_is_virtual(g, &b, v)) {
      continue;
    }
    dy_access(g, v);
    if (g->node[v].mark == b.gen) { /* on the compressed tree already */
      dy_split(g, &b, v);
    } else {
      x = dy_deepest_marked(g, v, b.gen);
      if (!dy_is_virtual(g, &b, x)) {
        dy_split(g, &b, x);
      }
      dy_expose(g, v, x);
      dy_apply_mark(g, x, b.gen);
      dy_new_super(g, &b, x, v);
      dy_add_virtual(g, &b, v);
    }
    dy_make_root(g, b.root);
  }
  /* local edges: live super edges weighing their heaviest F edge, then
   * the graph edges, in the global order */
  for (i = 0; i < b.supers; i++) {
    size_t t;
    if (!b.super[i].alive) {
      continue;
    }
    dy_expose(g, b.super[i].a, b.super[i].b);
    t = g->node[b.super[i].b].best - g->n;
    loc[nloc].weight = g->edge[t].weight;
    loc[nloc].key = t;
    loc[nloc].ref = b.base + i;
    loc[nloc].end[0] = b.super[i].a;
    loc[nloc].end[1] = b.super[i].b;
    loc[nloc].is_super = 1;
    nloc++;
  }
  for (i = 0; i < count; i++) {
    loc[nloc].weight = g->edge[e[i]].weight;
    loc[nloc].key = e[i];
    loc[nloc].ref = e[i];
    loc[nloc].end[0] = g->edge[e[i]].end[0];
    loc[nloc].end[1] = g->edge[e[i]].end[1];
    loc[nloc].is_super = 0;
    nloc++;
  }
  qsort(loc, nloc, sizeof(*loc), dy_local_cmp);
  for (i = 0; i < nloc; i++) {
    lu[i] = g->local_id[loc[i].end[0]];
    lv[i] = g->local_id[loc[i].end[1]];
    lw[i] = loc[i].weight;
  }
  slot->global = (size_t *)ds_context_alloc(g->context, nloc * sizeof(size_t));
  slot->is_super = (unsigned char *)ds_context_alloc(g->context, nloc);
  slot->super_local =
      (size_t *)ds_context_alloc(g->context, (b.supers + 1) * sizeof(size_t));
  if (slot->global == NULL || slot->is_super == NULL ||
      slot->super_local == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  st = ds_map_init(&slot->local, NULL, sizeof(size_t), sizeof(size_t), dy_hash,
                   dy_equal, NULL, g->context);
  if (st != DS_OK) {
    goto done;
  }
  slot->map_ready = true;
  for (i = 0; i < b.supers; i++) {
    slot->super_local[i] = DY_NONE;
  }
  for (i = 0; i < nloc && st == DS_OK; i++) {
    slot->global[i] = loc[i].ref;
    slot->is_super[i] = (unsigned char)loc[i].is_super;
    if (loc[i].is_super) {
      slot->super_local[loc[i].ref - b.base] = i;
    } else {
      st = ds_map_put(&slot->local, &loc[i].ref, &i);
    }
  }
  if (st == DS_OK) {
    st = ds_dmsf_init(&slot->msf, b.virts, lu, lv, lw, nloc, g->context);
  }
  if (st == DS_OK) {
    slot->ready = true;
    slot->edges = nloc;
    slot->base = b.base;
    slot->supers = b.supers;
    g->rebuilds++;
  }
done:
  g->next_super = b.base + b.supers;
  ds_context_free(g->context, ends);
  ds_context_free(g->context, b.super);
  ds_context_free(g->context, b.virt);
  ds_context_free(g->context, loc);
  ds_context_free(g->context, lu);
  ds_context_free(g->context, lv);
  ds_context_free(g->context, lw);
  return st;
}

static int dy_size_cmp(const void *a, const void *b) {
  size_t x = *(const size_t *)a;
  size_t y = *(const size_t *)b;
  return x < y ? -1 : x > y ? 1 : 0;
}

/* the graph's non-tree edges among d[0..dn) go, with the non-tree edges
 * of A_0 .. A_j, into the smallest A_j that can take them all; the
 * structures below it are emptied */
static ds_status_t dy_update(ds_dyn_msf_t *g, const size_t *d, size_t dn) {
  size_t *c;
  size_t cap = dn + 1;
  size_t n = 0;
  size_t j;
  size_t i;
  ds_status_t st;
  for (j = 0; j < g->slots; j++) {
    cap += g->slot[j].edges;
  }
  c = (size_t *)ds_context_alloc(g->context, cap * sizeof(size_t));
  if (c == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < dn; i++) {
    if (g->edge[d[i]].live && !g->edge[d[i]].tree) {
      c[n++] = d[i];
    }
  }
  for (j = 0;; j++) {
    size_t u;
    if (j < g->slots && g->slot[j].ready) {
      ds_dyn_msf_slot_t *s = &g->slot[j];
      for (i = 0; i < s->edges; i++) {
        size_t f = s->global[i];
        if (!s->is_super[i] && ds_dmsf_live(&s->msf, i) &&
            !ds_dmsf_in_forest(&s->msf, i) && !g->edge[f].tree) {
          c[n++] = f;
        }
      }
    }
    qsort(c, n, sizeof(size_t), dy_size_cmp);
    u = 0;
    for (i = 0; i < n; i++) {
      if (u == 0 || c[u - 1] != c[i]) {
        c[u++] = c[i];
      }
    }
    n = u;
    if (j + 1 >= 8 * sizeof(size_t) || n <= ((size_t)1 << j)) {
      break;
    }
  }
  st = dy_grow_slots(g, j + 1);
  if (st == DS_OK) {
    for (i = 0; i <= j && i < g->slots; i++) {
      dy_slot_clear(g, i);
    }
    if (j + 1 > g->slots) {
      g->slots = j + 1;
    }
    st = dy_build_slot(g, j, c, n);
  }
  ds_context_free(g->context, c);
  return st;
}

/* ---- the operations ---- */

ds_status_t ds_dyn_msf_init(ds_dyn_msf_t *g, size_t n, ds_context_t *context) {
  size_t x;
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  memset(g, 0, sizeof(*g));
  if (n == 0) {
    return DS_ERR_INVALID;
  }
  if (n > (size_t)-1 / 64 / sizeof(ds_dyn_msf_node_t)) {
    return DS_ERR_OVERFLOW;
  }
  g->n = n;
  g->context = context;
  g->free_edge = DY_NONE;
  g->slot_capacity = 8;
  g->node = (ds_dyn_msf_node_t *)ds_context_alloc(
      context, n * sizeof(ds_dyn_msf_node_t));
  g->path = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  g->tag = (size_t *)ds_context_alloc(context, n * 8 * sizeof(size_t));
  g->lazy = (size_t *)ds_context_alloc(context, n * 8 * sizeof(size_t));
  g->slot = (ds_dyn_msf_slot_t *)ds_context_calloc(context, 8,
                                                   sizeof(ds_dyn_msf_slot_t));
  g->stamp = (size_t *)ds_context_calloc(context, n, sizeof(size_t));
  g->virt = (size_t *)ds_context_calloc(context, n, sizeof(size_t));
  g->local_id = (size_t *)ds_context_calloc(context, n, sizeof(size_t));
  if (g->node == NULL || g->path == NULL || g->tag == NULL || g->lazy == NULL ||
      g->slot == NULL || g->stamp == NULL || g->virt == NULL ||
      g->local_id == NULL) {
    ds_dyn_msf_destroy(g);
    return DS_ERR_ALLOC;
  }
  for (x = 0; x < n; x++) {
    dy_node_reset(g, x);
  }
  return DS_OK;
}

void ds_dyn_msf_destroy(ds_dyn_msf_t *g) {
  size_t i;
  if (g == NULL) {
    return;
  }
  for (i = 0; g->slot != NULL && i < g->slot_capacity; i++) {
    dy_slot_clear(g, i);
  }
  ds_context_free(g->context, g->slot);
  ds_context_free(g->context, g->edge);
  ds_context_free(g->context, g->node);
  ds_context_free(g->context, g->path);
  ds_context_free(g->context, g->tag);
  ds_context_free(g->context, g->lazy);
  ds_context_free(g->context, g->stamp);
  ds_context_free(g->context, g->virt);
  ds_context_free(g->context, g->local_id);
  memset(g, 0, sizeof(*g));
}

ds_status_t ds_dyn_msf_insert(ds_dyn_msf_t *g, size_t u, size_t v, double w,
                              size_t *out_id) {
  size_t id;
  size_t d = DY_NONE;
  ds_status_t st;
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= g->n || v >= g->n) {
    return DS_ERR_RANGE;
  }
  if (!(w >= w)) {
    return DS_ERR_INVALID; /* NaN */
  }
  if (g->broken) {
    return DS_ERR_STATE;
  }
  st = dy_grow_edges(g);
  if (st != DS_OK) {
    return st;
  }
  if (g->free_edge != DY_NONE) {
    id = g->free_edge;
    g->free_edge = g->edge[id].end[0];
  } else {
    id = g->edge_slots++;
  }
  g->edge[id].end[0] = u;
  g->edge[id].end[1] = v;
  g->edge[id].weight = w;
  g->edge[id].live = 1;
  g->edge[id].tree = 0;
  dy_node_reset(g, g->n + id);
  g->edges++;
  if (out_id != NULL) {
    *out_id = id;
  }
  if (u == v) {
    d = id;
  } else if (!dy_connected(g, u, v)) {
    dy_link_edge(g, id);
    g->edge[id].tree = 1;
    g->tree_edges++;
    g->weight += w;
  } else {
    size_t f;
    dy_expose(g, u, v);
    f = g->node[v].best - g->n;
    if (dy_less(g, id, f)) { /* the new edge takes the heaviest one's place */
      dy_cut_edge(g, f);
      g->edge[f].tree = 0;
      dy_link_edge(g, id);
      g->edge[id].tree = 1;
      g->weight += w - g->edge[f].weight;
      d = f;
    } else {
      d = id;
    }
  }
  if (d != DY_NONE) {
    st = dy_update(g, &d, 1);
    if (st != DS_OK) {
      g->broken = true;
      return st;
    }
  }
  return DS_OK;
}

ds_status_t ds_dyn_msf_delete(ds_dyn_msf_t *g, size_t id) {
  ds_dyn_msf_edge_t *e;
  size_t *r;
  size_t nr = 0;
  size_t i;
  size_t t;
  size_t best = DY_NONE;
  size_t *tags;
  bool was_tree;
  ds_status_t st = DS_OK;
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  if (g->broken) {
    return DS_ERR_STATE;
  }
  if (id >= g->edge_slots || !g->edge[id].live) {
    return DS_NOT_FOUND;
  }
  r = (size_t *)ds_context_alloc(g->context,
                                 2 * (g->slots + 1) * sizeof(size_t));
  if (r == NULL) {
    return DS_ERR_ALLOC;
  }
  e = &g->edge[id];
  t = g->n + id;
  /* out of every A_i holding it, as itself or inside a super edge */
  dy_access(g, t);
  tags = dy_tags(g, t);
  for (i = 0; i < g->slots && st == DS_OK; i++) {
    ds_dyn_msf_slot_t *s = &g->slot[i];
    size_t local = DY_NONE;
    size_t rep = DY_NONE;
    const void *p;
    if (!s->ready) {
      continue;
    }
    p = ds_map_get(&s->local, &id);
    if (p != NULL) {
      memcpy(&local, p, sizeof(local));
      (void)ds_map_remove(&s->local, &id, NULL);
    } else if (tags[i] != DY_NONE && tags[i] >= s->base &&
               tags[i] - s->base < s->supers) {
      local = s->super_local[tags[i] - s->base];
    }
    if (local == DY_NONE || !ds_dmsf_live(&s->msf, local)) {
      continue;
    }
    st = ds_dmsf_delete(&s->msf, local, &rep);
    if (st == DS_OK && rep != DY_NONE) {
      r[nr++] = s->global[rep];
    }
  }
  if (st != DS_OK) {
    ds_context_free(g->context, r);
    g->broken = true;
    return st;
  }
  was_tree = e->tree != 0;
  if (was_tree) {
    dy_cut_edge(g, id);
    e->tree = 0;
    g->tree_edges--;
    g->weight -= e->weight;
  }
  e->live = 0;
  g->edges--;
  if (was_tree) { /* the lightest reported edge that reconnects F */
    for (i = 0; i < nr; i++) {
      size_t f = r[i];
      if (g->edge[f].live && !g->edge[f].tree &&
          (best == DY_NONE || dy_less(g, f, best)) &&
          !dy_connected(g, g->edge[f].end[0], g->edge[f].end[1])) {
        best = f;
      }
    }
    if (best != DY_NONE) {
      dy_link_edge(g, best);
      g->edge[best].tree = 1;
      g->tree_edges++;
      g->weight += g->edge[best].weight;
    }
  }
  if (g->tree_edges == 0) {
    g->weight = 0.0;
  }
  st = dy_update(g, r, nr);
  ds_context_free(g->context, r);
  e->end[0] = g->free_edge;
  g->free_edge = id;
  if (st != DS_OK) {
    g->broken = true;
  }
  return st;
}

/* ---- queries ---- */

bool ds_dyn_msf_connected(ds_dyn_msf_t *g, size_t u, size_t v) {
  if (g == NULL || u >= g->n || v >= g->n || g->broken) {
    return false;
  }
  return dy_connected(g, u, v);
}

bool ds_dyn_msf_live(const ds_dyn_msf_t *g, size_t id) {
  return g != NULL && id < g->edge_slots && g->edge[id].live;
}

bool ds_dyn_msf_in_forest(const ds_dyn_msf_t *g, size_t id) {
  return ds_dyn_msf_live(g, id) && g->edge[id].tree;
}

ds_status_t ds_dyn_msf_path_max(ds_dyn_msf_t *g, size_t u, size_t v,
                                size_t *out_id) {
  if (g == NULL || out_id == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= g->n || v >= g->n) {
    return DS_ERR_RANGE;
  }
  if (g->broken) {
    return DS_ERR_STATE;
  }
  if (u == v || !dy_connected(g, u, v)) {
    return DS_NOT_FOUND;
  }
  dy_expose(g, u, v);
  *out_id = g->node[v].best - g->n;
  return DS_OK;
}

size_t ds_dyn_msf_edges(const ds_dyn_msf_t *g) {
  return g == NULL ? 0 : g->edges;
}

size_t ds_dyn_msf_forest_edges(const ds_dyn_msf_t *g) {
  return g == NULL ? 0 : g->tree_edges;
}

double ds_dyn_msf_weight(const ds_dyn_msf_t *g) {
  return g == NULL ? 0.0 : g->weight;
}

bool ds_dyn_msf_broken(const ds_dyn_msf_t *g) { return g != NULL && g->broken; }

/* Kruskal over the live edges into in_forest (one byte per id) */
static ds_status_t dy_kruskal(ds_dyn_msf_t *g, unsigned char *in_forest) {
  size_t *order;
  size_t *tmp;
  size_t *parent;
  size_t k = 0;
  size_t i;
  size_t width;
  order = (size_t *)ds_context_alloc(
      g->context, (2 * g->edge_slots + g->n + 1) * sizeof(size_t));
  if (order == NULL) {
    return DS_ERR_ALLOC;
  }
  tmp = order + g->edge_slots;
  parent = tmp + g->edge_slots;
  for (i = 0; i < g->edge_slots; i++) {
    in_forest[i] = 0;
    if (g->edge[i].live && g->edge[i].end[0] != g->edge[i].end[1]) {
      order[k++] = i;
    }
  }
  for (width = 1; width < k; width *= 2) { /* merge sort by key */
    size_t lo;
    for (lo = 0; lo < k; lo += 2 * width) {
      size_t mid = lo + width < k ? lo + width : k;
      size_t hi = lo + 2 * width < k ? lo + 2 * width : k;
      size_t a = lo, b = mid, o = lo;
      while (a < mid && b < hi) {
        tmp[o++] = dy_less(g, order[b], order[a]) ? order[b++] : order[a++];
      }
      while (a < mid) {
        tmp[o++] = order[a++];
      }
      while (b < hi) {
        tmp[o++] = order[b++];
      }
    }
    memcpy(order, tmp, k * sizeof(size_t));
  }
  for (i = 0; i < g->n; i++) {
    parent[i] = i;
  }
  for (i = 0; i < k; i++) {
    size_t a = g->edge[order[i]].end[0];
    size_t b = g->edge[order[i]].end[1];
    while (parent[a] != a) {
      parent[a] = parent[parent[a]];
      a = parent[a];
    }
    while (parent[b] != b) {
      parent[b] = parent[parent[b]];
      b = parent[b];
    }
    if (a != b) {
      parent[a] = b;
      in_forest[order[i]] = 1;
    }
  }
  ds_context_free(g->context, order);
  return DS_OK;
}

ds_status_t ds_dyn_msf_rebuild(ds_dyn_msf_t *g) {
  unsigned char *in_forest;
  size_t *rest;
  size_t nrest = 0;
  size_t i;
  ds_status_t st;
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  g->broken = true;
  in_forest = (unsigned char *)ds_context_alloc(g->context, g->edge_slots + 1);
  rest = (size_t *)ds_context_alloc(g->context,
                                    (g->edge_slots + 1) * sizeof(size_t));
  if (in_forest == NULL || rest == NULL) {
    ds_context_free(g->context, in_forest);
    ds_context_free(g->context, rest);
    return DS_ERR_ALLOC;
  }
  st = dy_kruskal(g, in_forest);
  if (st == DS_OK) {
    for (i = 0; i < g->slot_capacity; i++) {
      dy_slot_clear(g, i);
    }
    g->slots = 0;
    for (i = 0; i < g->n + g->edge_capacity; i++) {
      dy_node_reset(g, i);
    }
    g->tree_edges = 0;
    g->weight = 0.0;
    for (i = 0; i < g->edge_slots; i++) {
      g->edge[i].tree = 0;
      if (!g->edge[i].live) {
        continue;
      }
      if (in_forest[i]) {
        dy_link_edge(g, i);
        g->edge[i].tree = 1;
        g->tree_edges++;
        g->weight += g->edge[i].weight;
      } else {
        rest[nrest++] = i;
      }
    }
    st = dy_update(g, rest, nrest);
  }
  ds_context_free(g->context, in_forest);
  ds_context_free(g->context, rest);
  if (st == DS_OK) {
    g->broken = false;
  }
  return st;
}

bool ds_dyn_msf_check(ds_dyn_msf_t *g) {
  unsigned char *in_forest;
  size_t *home;
  size_t *homes;
  size_t *low_tree;
  size_t i, j;
  size_t trees = 0;
  double total = 0.0;
  double scale = 1.0;
  double diff;
  bool ok = true;
  if (g == NULL || g->broken) {
    return false;
  }
  in_forest = (unsigned char *)ds_context_alloc(g->context, g->edge_slots + 1);
  home = (size_t *)ds_context_alloc(g->context,
                                    3 * (g->edge_slots + 1) * sizeof(size_t));
  if (in_forest == NULL || home == NULL || dy_kruskal(g, in_forest) != DS_OK) {
    ds_context_free(g->context, in_forest);
    ds_context_free(g->context, home);
    return false;
  }
  homes = home + g->edge_slots + 1;
  low_tree = homes + g->edge_slots + 1;
  for (i = 0; i < g->edge_slots && ok; i++) {
    ds_dyn_msf_edge_t *e = &g->edge[i];
    home[i] = DY_NONE;
    homes[i] = 0;
    low_tree[i] = DY_NONE;
    scale += e->weight < 0 ? -e->weight : e->weight;
    if (!e->live) {
      continue;
    }
    if (e->tree != in_forest[i]) {
      ok = false; /* F is the minimum spanning forest */
    }
    if (e->tree) {
      trees++;
      total += e->weight;
      if (!dy_connected(g, e->end[0], e->end[1])) {
        ok = false;
      }
    }
  }
  diff = total - g->weight;
  if (diff < 0) {
    diff = -diff;
  }
  if (trees != g->tree_edges || diff > 1e-9 * scale) {
    ok = false;
  }
  for (j = 0; j < g->slots && ok; j++) { /* the reduction's invariant */
    ds_dyn_msf_slot_t *s = &g->slot[j];
    if (!s->ready) {
      continue;
    }
    if (!ds_dmsf_check(&s->msf)) {
      ok = false;
      break;
    }
    for (i = 0; i < s->edges; i++) {
      size_t f = s->global[i];
      if (s->is_super[i] || !ds_dmsf_live(&s->msf, i)) {
        continue;
      }
      if (!g->edge[f].live) {
        ok = false;
        break;
      }
      if (g->edge[f].tree) {
        continue;
      }
      if (ds_dmsf_in_forest(&s->msf, i)) {
        if (low_tree[f] == DY_NONE) {
          low_tree[f] = j;
        }
      } else {
        homes[f]++;
        home[f] = j;
      }
    }
  }
  for (i = 0; i < g->edge_slots && ok; i++) {
    if (!g->edge[i].live || g->edge[i].tree) {
      continue;
    }
    if (homes[i] != 1 || (low_tree[i] != DY_NONE && low_tree[i] <= home[i])) {
      ok = false;
    }
  }
  ds_context_free(g->context, in_forest);
  ds_context_free(g->context, home);
  return ok;
}
