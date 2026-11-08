#include "euler_tour_tree.h"
#include "checked.h"
#include <string.h>

#define ETT_NONE ((size_t) - 1)

typedef ds_ett_node_t node_t;

typedef struct ett_key {
  size_t a;
  size_t b;
} ett_key_t;

static unsigned char *ett_elem(const ds_ett_t *t, node_t *x) {
  return (unsigned char *)(void *)x + t->elem_offset;
}

static unsigned char *ett_sum(const ds_ett_t *t, node_t *x) {
  return (unsigned char *)(void *)x + t->summary_offset;
}

static unsigned char *ett_tag(const ds_ett_t *t, node_t *x) {
  return (unsigned char *)(void *)x + t->tag_offset;
}

static node_t *ett_vertex(const ds_ett_t *t, size_t v) {
  return (node_t *)(void *)(t->vertices + v * t->node_size);
}

static size_t ett_size(const node_t *x) { return x == NULL ? 0 : x->size; }

static unsigned long ett_rand(ds_ett_t *t) {
  unsigned long x = t->rng;
  x ^= (x << 13) & 0xFFFFFFFFUL;
  x ^= x >> 17;
  x ^= (x << 5) & 0xFFFFFFFFUL;
  t->rng = x & 0xFFFFFFFFUL;
  return t->rng;
}

/* apply tag to every vertex in x's subtree (x's own data now, children
 * later) */
static void ett_apply(ds_ett_t *t, node_t *x, const void *tag) {
  const ds_ett_config_t *c = &t->config;
  if (x == NULL || c->tag_size == 0) {
    return;
  }
  if (x->vertex != ETT_NONE && c->elem_size > 0) {
    c->tag_apply_elem(ett_elem(t, x), tag, c->user);
  }
  if (c->summary_size > 0 && x->vcount > 0) {
    c->tag_apply_summary(ett_sum(t, x), tag, x->vcount, c->user);
  }
  if (x->tagged) {
    c->tag_compose(ett_tag(t, x), tag, c->user);
  } else {
    memcpy(ett_tag(t, x), tag, c->tag_size);
    x->tagged = 1;
  }
}

static void ett_push(ds_ett_t *t, node_t *x) {
  if (x != NULL && x->tagged) {
    ett_apply(t, x->left, ett_tag(t, x));
    ett_apply(t, x->right, ett_tag(t, x));
    x->tagged = 0;
  }
}

static void ett_update(ds_ett_t *t, node_t *x) {
  const ds_ett_config_t *c = &t->config;
  unsigned char *acc, *s1, *s2;
  bool have = false;
  x->size = 1 + ett_size(x->left) + ett_size(x->right);
  x->vcount = (x->vertex != ETT_NONE ? 1U : 0U) +
              (x->left != NULL ? x->left->vcount : 0U) +
              (x->right != NULL ? x->right->vcount : 0U);
  if (c->summary_size == 0) {
    return;
  }
  acc = ett_sum(t, x);
  s1 = t->scratch;
  s2 = t->scratch + c->summary_size;
  if (x->left != NULL && x->left->vcount > 0) {
    memcpy(acc, ett_sum(t, x->left), c->summary_size);
    have = true;
  }
  if (x->vertex != ETT_NONE) {
    c->summarize(s1, ett_elem(t, x), c->user);
    if (have) {
      c->combine(s2, acc, s1, c->user);
      memcpy(acc, s2, c->summary_size);
    } else {
      memcpy(acc, s1, c->summary_size);
    }
    have = true;
  }
  if (x->right != NULL && x->right->vcount > 0) {
    if (have) {
      c->combine(s2, acc, ett_sum(t, x->right), c->user);
      memcpy(acc, s2, c->summary_size);
    } else {
      memcpy(acc, ett_sum(t, x->right), c->summary_size);
    }
  }
}

/* treap merge/split: recursion follows one root-to-leaf path, expected
 * O(log n) deep */
static node_t *ett_merge(ds_ett_t *t, node_t *a, node_t *b) {
  if (a == NULL) {
    return b;
  }
  if (b == NULL) {
    return a;
  }
  if (a->priority > b->priority) {
    ett_push(t, a);
    a->right = ett_merge(t, a->right, b);
    a->right->parent = a;
    ett_update(t, a);
    return a;
  }
  ett_push(t, b);
  b->left = ett_merge(t, a, b->left);
  b->left->parent = b;
  ett_update(t, b);
  return b;
}

static void ett_split_rec(ds_ett_t *t, node_t *x, size_t k, node_t **l,
                          node_t **r) {
  if (x == NULL) {
    *l = NULL;
    *r = NULL;
    return;
  }
  ett_push(t, x);
  if (ett_size(x->left) >= k) {
    ett_split_rec(t, x->left, k, l, &x->left);
    if (x->left != NULL) {
      x->left->parent = x;
    }
    ett_update(t, x);
    *r = x;
  } else {
    ett_split_rec(t, x->right, k - ett_size(x->left) - 1, &x->right, r);
    if (x->right != NULL) {
      x->right->parent = x;
    }
    ett_update(t, x);
    *l = x;
  }
}

static void ett_split(ds_ett_t *t, node_t *x, size_t k, node_t **l,
                      node_t **r) {
  ett_split_rec(t, x, k, l, r);
  if (*l != NULL) {
    (*l)->parent = NULL;
  }
  if (*r != NULL) {
    (*r)->parent = NULL;
  }
}

static node_t *ett_root(node_t *x) {
  while (x->parent != NULL) {
    x = x->parent;
  }
  return x;
}

static size_t ett_index(const node_t *x) {
  size_t i = ett_size(x->left);
  while (x->parent != NULL) {
    if (x->parent->right == x) {
      i += ett_size(x->parent->left) + 1;
    }
    x = x->parent;
  }
  return i;
}

static node_t *ett_merge_top(ds_ett_t *t, node_t *a, node_t *b) {
  node_t *r = ett_merge(t, a, b);
  if (r != NULL) {
    r->parent = NULL;
  }
  return r;
}

/* rotate v's tour so that v's own entry comes first; returns its root */
static node_t *ett_reroot(ds_ett_t *t, size_t v) {
  node_t *x = ett_vertex(t, v), *a, *b;
  ett_split(t, ett_root(x), ett_index(x), &a, &b);
  return ett_merge_top(t, b, a);
}

/* push pending actions from the root down to x, x included (so that x
 * can be recomputed from its children) */
static void ett_push_path(ds_ett_t *t, node_t *x) {
  node_t *n = ett_root(x);
  size_t i = ett_index(x);
  while (n != x) {
    ett_push(t, n);
    if (i < ett_size(n->left)) {
      n = n->left;
    } else {
      i -= ett_size(n->left) + 1;
      n = n->right;
    }
  }
  ett_push(t, x);
}

static unsigned long ett_hash(const void *key, void *user) {
  const ett_key_t *k = (const ett_key_t *)key;
  unsigned long h = (unsigned long)k->a * 2654435761UL;
  (void)user;
  h ^= (unsigned long)k->b + 0x9E3779B9UL + (h << 6) + (h >> 2);
  return h;
}

static bool ett_equal(const void *a, const void *b, void *user) {
  (void)user;
  return memcmp(a, b, sizeof(ett_key_t)) == 0;
}

static ett_key_t ett_make_key(size_t u, size_t v) {
  ett_key_t k;
  memset(&k, 0, sizeof(k));
  k.a = u < v ? u : v;
  k.b = u < v ? v : u;
  return k;
}

static unsigned char *ett_find_edge(const ds_ett_t *t, size_t u, size_t v) {
  ett_key_t k = ett_make_key(u, v);
  void *val = ds_map_get(&t->edge_map, &k);
  unsigned char *block;
  if (val == NULL) {
    return NULL;
  }
  memcpy(&block, val, sizeof(block));
  return block;
}

/* ---- public ---- */

void ds_ett_config_init(ds_ett_config_t *config) {
  if (config != NULL) {
    memset(config, 0, sizeof(*config));
  }
}

ds_status_t ds_ett_init(ds_ett_t *t, size_t n, const void *elems,
                        const ds_ett_config_t *config) {
  ds_ett_config_t def;
  size_t off, i, bytes;
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (config == NULL) {
    ds_ett_config_init(&def);
    config = &def;
  }
  if ((config->summary_size > 0 &&
       (config->summarize == NULL || config->combine == NULL)) ||
      (config->tag_size > 0 &&
       (config->tag_compose == NULL ||
        (config->summary_size > 0 && config->tag_apply_summary == NULL) ||
        (config->elem_size > 0 && config->tag_apply_elem == NULL)))) {
    return DS_ERR_INVALID;
  }
  t->config = *config;
  t->n = n;
  t->rng = config->seed != 0 ? (config->seed & 0xFFFFFFFFUL) : 0x2545F491UL;
  if (t->rng == 0) {
    t->rng = 0x2545F491UL;
  }
  if (!ds_size_align(sizeof(node_t), DS_MAX_ALIGN, &t->elem_offset) ||
      !ds_size_add(t->elem_offset, config->elem_size, &off) ||
      !ds_size_align(off, DS_MAX_ALIGN, &t->summary_offset) ||
      !ds_size_add(t->summary_offset, config->summary_size, &off) ||
      !ds_size_align(off, DS_MAX_ALIGN, &t->tag_offset) ||
      !ds_size_add(t->tag_offset, config->tag_size, &off) ||
      !ds_size_align(off, DS_MAX_ALIGN, &t->node_size) ||
      !ds_size_mul(n == 0 ? 1 : n, t->node_size, &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  t->vertices = (unsigned char *)ds_context_calloc(config->context, 1, bytes);
  t->scratch = (unsigned char *)ds_context_alloc(
      config->context,
      config->summary_size == 0 ? 1 : 2 * config->summary_size);
  if (t->vertices == NULL || t->scratch == NULL) {
    ds_context_free(config->context, t->vertices);
    ds_context_free(config->context, t->scratch);
    t->vertices = NULL;
    t->scratch = NULL;
    return DS_ERR_ALLOC;
  }
  st = ds_map_init(&t->edge_map, config->map_ops, sizeof(ett_key_t),
                   sizeof(unsigned char *), ett_hash, ett_equal, NULL,
                   config->context);
  if (st != DS_OK) {
    ds_context_free(config->context, t->vertices);
    ds_context_free(config->context, t->scratch);
    t->vertices = NULL;
    t->scratch = NULL;
    return st;
  }
  for (i = 0; i < n; i++) {
    node_t *x = ett_vertex(t, i);
    x->priority = ett_rand(t);
    x->vertex = i;
    if (elems != NULL && config->elem_size > 0) {
      memcpy(ett_elem(t, x),
             (const unsigned char *)elems + i * config->elem_size,
             config->elem_size);
    }
    ett_update(t, x);
  }
  return DS_OK;
}

void ds_ett_destroy(ds_ett_t *t) {
  size_t cursor = 0;
  const void *key;
  void *val;
  if (t == NULL || t->vertices == NULL) {
    return;
  }
  while (ds_map_next(&t->edge_map, &cursor, &key, &val)) {
    unsigned char *block;
    memcpy(&block, val, sizeof(block));
    ds_context_free(t->config.context, block);
  }
  ds_map_destroy(&t->edge_map);
  ds_context_free(t->config.context, t->vertices);
  ds_context_free(t->config.context, t->scratch);
  memset(t, 0, sizeof(*t));
}

bool ds_ett_connected(const ds_ett_t *t, size_t u, size_t v) {
  if (t == NULL || u >= t->n || v >= t->n) {
    return false;
  }
  return ett_root(ett_vertex(t, u)) == ett_root(ett_vertex(t, v));
}

bool ds_ett_has_edge(const ds_ett_t *t, size_t u, size_t v) {
  if (t == NULL || u >= t->n || v >= t->n) {
    return false;
  }
  return ett_find_edge(t, u, v) != NULL;
}

ds_status_t ds_ett_link(ds_ett_t *t, size_t u, size_t v) {
  unsigned char *block;
  node_t *a, *b, *U, *V;
  ett_key_t k;
  size_t bytes;
  ds_status_t st;
  int i;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= t->n || v >= t->n) {
    return DS_ERR_RANGE;
  }
  if (u == v || ds_ett_connected(t, u, v)) {
    return DS_ERR_STATE;
  }
  if (!ds_size_mul(2, t->node_size, &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  block = (unsigned char *)ds_context_calloc(t->config.context, 1, bytes);
  if (block == NULL) {
    return DS_ERR_ALLOC;
  }
  k = ett_make_key(u, v);
  st = ds_map_put(&t->edge_map, &k, &block);
  if (DS_FAILED(st)) {
    ds_context_free(t->config.context, block);
    return st;
  }
  for (i = 0; i < 2; i++) {
    node_t *x = (node_t *)(void *)(block + (size_t)i * t->node_size);
    x->priority = ett_rand(t);
    x->vertex = ETT_NONE;
    ett_update(t, x);
  }
  a = (node_t *)(void *)block;
  b = (node_t *)(void *)(block + t->node_size);
  U = ett_reroot(t, u);
  V = ett_reroot(t, v);
  ett_merge_top(t, ett_merge(t, ett_merge(t, U, a), V), b);
  t->edges++;
  return DS_OK;
}

/* split the tour around edge (u, v): *outer gets the rest of the tour,
 * *inner the part strictly between the arcs; arcs detached */
static void ett_cut_tour(ds_ett_t *t, node_t *a, node_t *b, node_t **outer,
                         node_t **inner) {
  node_t *A, *rest, *arc, *B, *C;
  size_t ia, ib;
  ia = ett_index(a);
  ib = ett_index(b);
  if (ia > ib) {
    size_t s = ia;
    ia = ib;
    ib = s;
  }
  ett_split(t, ett_root(a), ia, &A, &rest);
  ett_split(t, rest, 1, &arc, &rest);
  ett_split(t, rest, ib - ia - 1, &B, &rest);
  ett_split(t, rest, 1, &arc, &C);
  *inner = B;
  *outer = ett_merge_top(t, A, C);
}

ds_status_t ds_ett_cut(ds_ett_t *t, size_t u, size_t v) {
  unsigned char *block;
  node_t *outer, *inner;
  ett_key_t k;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= t->n || v >= t->n) {
    return DS_ERR_RANGE;
  }
  block = ett_find_edge(t, u, v);
  if (block == NULL) {
    return DS_NOT_FOUND;
  }
  ett_cut_tour(t, (node_t *)(void *)block,
               (node_t *)(void *)(block + t->node_size), &outer, &inner);
  k = ett_make_key(u, v);
  (void)ds_map_remove(&t->edge_map, &k, NULL);
  ds_context_free(t->config.context, block);
  t->edges--;
  return DS_OK;
}

size_t ds_ett_edge_count(const ds_ett_t *t) { return t == NULL ? 0 : t->edges; }

size_t ds_ett_component_count(const ds_ett_t *t) {
  return t == NULL ? 0 : t->n - t->edges;
}

size_t ds_ett_component_size(const ds_ett_t *t, size_t v) {
  if (t == NULL || v >= t->n) {
    return 0;
  }
  return ett_root(ett_vertex(t, v))->vcount;
}

ds_status_t ds_ett_component_query(ds_ett_t *t, size_t v, void *out_summary) {
  if (t == NULL || out_summary == NULL) {
    return DS_ERR_NULL;
  }
  if (v >= t->n) {
    return DS_ERR_RANGE;
  }
  if (t->config.summary_size == 0) {
    return DS_ERR_UNSUPPORTED;
  }
  memcpy(out_summary, ett_sum(t, ett_root(ett_vertex(t, v))),
         t->config.summary_size);
  return DS_OK;
}

ds_status_t ds_ett_component_apply(ds_ett_t *t, size_t v, const void *tag) {
  if (t == NULL || tag == NULL) {
    return DS_ERR_NULL;
  }
  if (v >= t->n) {
    return DS_ERR_RANGE;
  }
  if (t->config.tag_size == 0) {
    return DS_ERR_UNSUPPORTED;
  }
  ett_apply(t, ett_root(ett_vertex(t, v)), tag);
  return DS_OK;
}

/* expose v's side of edge (v, p) as a separate treap, run the action and
 * put the tour back together */
static ds_status_t ett_side(ds_ett_t *t, size_t v, size_t p, size_t *out_size,
                            void *out_summary, const void *tag) {
  unsigned char *block;
  node_t *A, *rest, *arc1, *B, *arc2, *C;
  size_t ia, ib;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (v >= t->n || p >= t->n) {
    return DS_ERR_RANGE;
  }
  block = ett_find_edge(t, v, p);
  if (block == NULL) {
    return DS_NOT_FOUND;
  }
  (void)ett_reroot(t, p);
  ia = ett_index((node_t *)(void *)block);
  ib = ett_index((node_t *)(void *)(block + t->node_size));
  if (ia > ib) {
    size_t s = ia;
    ia = ib;
    ib = s;
  }
  ett_split(t, ett_root(ett_vertex(t, p)), ia, &A, &rest);
  ett_split(t, rest, 1, &arc1, &rest);
  ett_split(t, rest, ib - ia - 1, &B, &rest);
  ett_split(t, rest, 1, &arc2, &C);
  if (out_size != NULL) {
    *out_size = B != NULL ? B->vcount : 0;
  }
  if (out_summary != NULL && B != NULL) {
    memcpy(out_summary, ett_sum(t, B), t->config.summary_size);
  }
  if (tag != NULL) {
    ett_apply(t, B, tag);
  }
  ett_merge_top(t, ett_merge(t, ett_merge(t, ett_merge(t, A, arc1), B), arc2),
                C);
  return DS_OK;
}

ds_status_t ds_ett_subtree_size(ds_ett_t *t, size_t v, size_t p,
                                size_t *out_size) {
  if (out_size == NULL) {
    return DS_ERR_NULL;
  }
  return ett_side(t, v, p, out_size, NULL, NULL);
}

ds_status_t ds_ett_subtree_query(ds_ett_t *t, size_t v, size_t p,
                                 void *out_summary) {
  if (t == NULL || out_summary == NULL) {
    return DS_ERR_NULL;
  }
  if (t->config.summary_size == 0) {
    return DS_ERR_UNSUPPORTED;
  }
  return ett_side(t, v, p, NULL, out_summary, NULL);
}

ds_status_t ds_ett_subtree_apply(ds_ett_t *t, size_t v, size_t p,
                                 const void *tag) {
  if (t == NULL || tag == NULL) {
    return DS_ERR_NULL;
  }
  if (t->config.tag_size == 0) {
    return DS_ERR_UNSUPPORTED;
  }
  return ett_side(t, v, p, NULL, NULL, tag);
}

ds_status_t ds_ett_get(ds_ett_t *t, size_t v, void *out_elem) {
  node_t *x;
  if (t == NULL || out_elem == NULL) {
    return DS_ERR_NULL;
  }
  if (v >= t->n) {
    return DS_ERR_RANGE;
  }
  x = ett_vertex(t, v);
  ett_push_path(t, x);
  memcpy(out_elem, ett_elem(t, x), t->config.elem_size);
  return DS_OK;
}

ds_status_t ds_ett_set(ds_ett_t *t, size_t v, const void *elem) {
  node_t *x;
  if (t == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (v >= t->n) {
    return DS_ERR_RANGE;
  }
  x = ett_vertex(t, v);
  ett_push_path(t, x);
  memcpy(ett_elem(t, x), elem, t->config.elem_size);
  for (; x != NULL; x = x->parent) {
    ett_update(t, x);
  }
  return DS_OK;
}

ds_status_t ds_ett_visit_component(ds_ett_t *t, size_t v,
                                   ds_ett_visit_func_t visit, void *user) {
  node_t *x;
  if (t == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (v >= t->n) {
    return DS_ERR_RANGE;
  }
  x = ett_root(ett_vertex(t, v));
  ett_push(t, x);
  while (x->left != NULL) {
    x = x->left;
    ett_push(t, x);
  }
  for (;;) {
    if (x->vertex != ETT_NONE && !visit(x->vertex, ett_elem(t, x), user)) {
      return DS_STOP;
    }
    if (x->right != NULL) {
      x = x->right;
      ett_push(t, x);
      while (x->left != NULL) {
        x = x->left;
        ett_push(t, x);
      }
    } else {
      while (x->parent != NULL && x->parent->right == x) {
        x = x->parent;
      }
      if (x->parent == NULL) {
        return DS_OK;
      }
      x = x->parent;
    }
  }
}
