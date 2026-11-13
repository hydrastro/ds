#include "rrb_vector.h"
#include "checked.h"
#include <string.h>

#define RB_E 2 /* extra nodes a level may keep over the optimum */
#define RB_W (8 * sizeof(size_t))

typedef ds_rrb_node_t node_t;
typedef const ds_rrb_config_t cfg_t;

/* ---- layout ---- */

static size_t rb_m(cfg_t *c) { return (size_t)1 << c->bits; }

static size_t rb_hdr(void) {
  return (sizeof(node_t) + DS_MAX_ALIGN - 1) / DS_MAX_ALIGN * DS_MAX_ALIGN;
}

static size_t rb_sizes_off(cfg_t *c) {
  size_t off = rb_hdr() + rb_m(c) * sizeof(node_t *);
  return (off + DS_MAX_ALIGN - 1) / DS_MAX_ALIGN * DS_MAX_ALIGN;
}

static unsigned char *rb_elems(node_t *n) {
  return (unsigned char *)n + rb_hdr();
}

static const unsigned char *rb_celems(const node_t *n) {
  return (const unsigned char *)n + rb_hdr();
}

static node_t **rb_kids(node_t *n) {
  return (node_t **)(void *)((unsigned char *)n + rb_hdr());
}

static node_t *const *rb_ckids(const node_t *n) {
  return (node_t *const *)(const void *)((const unsigned char *)n + rb_hdr());
}

static size_t *rb_table(cfg_t *c, node_t *n) {
  return (size_t *)(void *)((unsigned char *)n + rb_sizes_off(c));
}

/* elements a subtree of height h can hold: M^(h + 1), saturating */
static size_t rb_cap(cfg_t *c, unsigned h) {
  size_t shift = (size_t)c->bits * ((size_t)h + 1);
  return shift >= RB_W ? (size_t)-1 : (size_t)1 << shift;
}

/* ---- nodes ---- */

static node_t *rb_alloc(cfg_t *c, bool leaf) {
  size_t bytes = leaf ? rb_hdr() + rb_m(c) * c->elem_size
                      : rb_sizes_off(c) + rb_m(c) * sizeof(size_t);
  node_t *n = (node_t *)ds_context_alloc(c->context, bytes);
  if (n != NULL) {
    n->refs = 1;
    n->count = 0;
    n->sizes = NULL;
  }
  return n;
}

static void rb_release_node(cfg_t *c, node_t *n, unsigned h) {
  size_t i;
  if (n == NULL || --n->refs > 0) {
    return;
  }
  if (h == 0) {
    if (c->destroy_elem != NULL) {
      for (i = 0; i < n->count; i++) {
        c->destroy_elem(rb_elems(n) + i * c->elem_size, c->user);
      }
    }
  } else {
    for (i = 0; i < n->count; i++) {
      rb_release_node(c, rb_kids(n)[i], h - 1);
    }
  }
  ds_context_free(c->context, n);
}

/* copy (clone) one element into uninitialised storage */
static bool rb_put(cfg_t *c, void *dst, const void *src) {
  if (c->copy_elem != NULL) {
    return c->copy_elem(dst, src, c->user);
  }
  memcpy(dst, src, c->elem_size);
  return true;
}

/* append k elements cloned from src to leaf n; on failure the clones made
 * so far stay in n (counted), so releasing n cleans up */
static bool rb_leaf_take(cfg_t *c, node_t *n, const unsigned char *src,
                         size_t k) {
  size_t i;
  for (i = 0; i < k; i++) {
    if (!rb_put(c, rb_elems(n) + n->count * c->elem_size,
                src + i * c->elem_size)) {
      return false;
    }
    n->count++;
  }
  return true;
}

static size_t rb_size(cfg_t *c, const node_t *n, unsigned h) {
  size_t s = 0;
  while (h > 0) {
    if (n->sizes != NULL) {
      return s + n->sizes[n->count - 1];
    }
    s += (n->count - 1) * rb_cap(c, h - 1);
    n = rb_ckids(n)[n->count - 1];
    h--;
  }
  return s + n->count;
}

/* table and balance flag of an inner node at height h from its children */
static void rb_finish(cfg_t *c, node_t *n, unsigned h) {
  size_t *t = rb_table(c, n);
  size_t i;
  size_t acc = 0;
  bool balanced = true;
  for (i = 0; i < n->count; i++) {
    size_t s = rb_size(c, rb_kids(n)[i], h - 1);
    if (i + 1 < n->count && s != rb_cap(c, h - 1)) {
      balanced = false;
    }
    acc += s;
    t[i] = acc;
  }
  n->sizes = balanced ? NULL : t;
}

/* the child holding index *i of inner node n at height h; *i becomes the
 * index inside that child */
static size_t rb_slot(cfg_t *c, const node_t *n, unsigned h, size_t *i) {
  size_t shift = (size_t)c->bits * h;
  size_t slot = shift >= RB_W ? 0 : *i >> shift;
  if (n->sizes == NULL) {
    if (slot > 0) {
      *i -= slot << shift;
    }
    return slot;
  }
  if (slot >= n->count) {
    slot = n->count - 1;
  }
  while (n->sizes[slot] <= *i) {
    slot++;
  }
  if (slot > 0) {
    *i -= n->sizes[slot - 1];
  }
  return slot;
}

/* a path of single-child nodes of height h ending in a leaf holding elem */
static node_t *rb_chain(cfg_t *c, unsigned h, const void *elem) {
  node_t *n = rb_alloc(c, true);
  unsigned k;
  if (n == NULL) {
    return NULL;
  }
  if (!rb_leaf_take(c, n, (const unsigned char *)elem, 1)) {
    rb_release_node(c, n, 0);
    return NULL;
  }
  for (k = 1; k <= h; k++) {
    node_t *p = rb_alloc(c, false);
    if (p == NULL) {
      rb_release_node(c, n, k - 1);
      return NULL;
    }
    rb_kids(p)[0] = n;
    p->count = 1;
    rb_finish(c, p, k);
    n = p;
  }
  return n;
}

/* an inner node at height h with the children [first, first + k) of src
 * (retained); not finished */
static node_t *rb_inner_from(cfg_t *c, const node_t *src, size_t first,
                             size_t k) {
  node_t *n = rb_alloc(c, false);
  size_t i;
  if (n == NULL) {
    return NULL;
  }
  for (i = 0; i < k; i++) {
    node_t *kid = rb_ckids(src)[first + i];
    kid->refs++;
    rb_kids(n)[i] = kid;
  }
  n->count = k;
  return n;
}

/* ---- handles ---- */

static void rb_assign(ds_rrb_t *out, const ds_rrb_t *in_a, const ds_rrb_t *in_b,
                      node_t *root, unsigned height, size_t size, cfg_t *c) {
  if (out == in_a || out == in_b) {
    ds_rrb_release(out);
  }
  out->root = root;
  out->height = height;
  out->size = size;
  out->config = c;
}

/* collapse single-child roots */
static void rb_normalize(cfg_t *c, node_t **root, unsigned *height) {
  while (*root != NULL && *height > 0 && (*root)->count == 1) {
    node_t *kid = rb_kids(*root)[0];
    kid->refs++;
    rb_release_node(c, *root, *height);
    *root = kid;
    (*height)--;
  }
}

void ds_rrb_config_init(ds_rrb_config_t *config, size_t elem_size) {
  if (config == NULL) {
    return;
  }
  memset(config, 0, sizeof(*config));
  config->elem_size = elem_size;
  config->bits = 5;
}

ds_status_t ds_rrb_config_from_schema(ds_rrb_config_t *config,
                                      const ds_schema_t *schema) {
  ds_status_t st;
  if (config == NULL || schema == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_schema_validate(schema);
  if (st != DS_OK) {
    return st;
  }
  ds_rrb_config_init(config, schema->size);
  config->copy_elem = schema->copy;
  config->destroy_elem = schema->destroy;
  config->user = schema->user;
  return DS_OK;
}

ds_status_t ds_rrb_init(ds_rrb_t *vec, const ds_rrb_config_t *config) {
  size_t bytes;
  if (vec == NULL || config == NULL) {
    return DS_ERR_NULL;
  }
  memset(vec, 0, sizeof(*vec));
  if (config->bits < 2 || config->bits > 8 || config->elem_size == 0 ||
      (config->destroy_elem != NULL && config->copy_elem == NULL)) {
    return DS_ERR_INVALID;
  }
  if (!ds_size_mul(rb_m(config), config->elem_size, &bytes) ||
      !ds_size_add(bytes, rb_hdr(), &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  vec->config = config;
  return DS_OK;
}

void ds_rrb_copy(const ds_rrb_t *vec, ds_rrb_t *out) {
  if (vec == NULL || out == NULL) {
    return;
  }
  if (vec->root != NULL) {
    vec->root->refs++;
  }
  *out = *vec;
}

void ds_rrb_release(ds_rrb_t *vec) {
  if (vec == NULL) {
    return;
  }
  if (vec->root != NULL && vec->config != NULL) {
    rb_release_node(vec->config, vec->root, vec->height);
  }
  vec->root = NULL;
  vec->size = 0;
  vec->height = 0;
}

size_t ds_rrb_size(const ds_rrb_t *vec) { return vec == NULL ? 0 : vec->size; }

const void *ds_rrb_get(const ds_rrb_t *vec, size_t index) {
  const node_t *n;
  unsigned h;
  if (vec == NULL || index >= vec->size) {
    return NULL;
  }
  n = vec->root;
  for (h = vec->height; h > 0; h--) {
    n = rb_ckids(n)[rb_slot(vec->config, n, h, &index)];
  }
  return rb_celems(n) + index * vec->config->elem_size;
}

/* ---- set ---- */

static node_t *rb_set(cfg_t *c, const node_t *n, unsigned h, size_t i,
                      const void *elem) {
  node_t *m;
  if (h == 0) {
    m = rb_alloc(c, true);
    if (m == NULL) {
      return NULL;
    }
    if (!rb_leaf_take(c, m, rb_celems(n), i) ||
        !rb_leaf_take(c, m, (const unsigned char *)elem, 1) ||
        !rb_leaf_take(c, m, rb_celems(n) + (i + 1) * c->elem_size,
                      n->count - i - 1)) {
      rb_release_node(c, m, 0);
      return NULL;
    }
    return m;
  } else {
    size_t slot = rb_slot(c, n, h, &i);
    node_t *kid = rb_set(c, rb_ckids(n)[slot], h - 1, i, elem);
    if (kid == NULL) {
      return NULL;
    }
    m = rb_inner_from(c, n, 0, n->count);
    if (m == NULL) {
      rb_release_node(c, kid, h - 1);
      return NULL;
    }
    rb_release_node(c, rb_kids(m)[slot], h - 1);
    rb_kids(m)[slot] = kid;
    m->sizes = n->sizes == NULL ? NULL : rb_table(c, m);
    if (m->sizes != NULL) {
      memcpy(m->sizes, n->sizes, n->count * sizeof(size_t));
    }
    return m;
  }
}

ds_status_t ds_rrb_set(const ds_rrb_t *vec, size_t index, const void *elem,
                       ds_rrb_t *out) {
  node_t *root;
  if (vec == NULL || elem == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= vec->size) {
    return DS_ERR_RANGE;
  }
  root = rb_set(vec->config, vec->root, vec->height, index, elem);
  if (root == NULL) {
    return DS_ERR_ALLOC;
  }
  rb_assign(out, vec, NULL, root, vec->height, vec->size, vec->config);
  return DS_OK;
}

/* ---- push_back / pop_back ---- */

/* *out: n with elem appended, or NULL when the subtree has no room */
static ds_status_t rb_push(cfg_t *c, const node_t *n, unsigned h,
                           const void *elem, node_t **out) {
  node_t *m;
  *out = NULL;
  if (h == 0) {
    if (n->count == rb_m(c)) {
      return DS_OK;
    }
    m = rb_alloc(c, true);
    if (m == NULL) {
      return DS_ERR_ALLOC;
    }
    if (!rb_leaf_take(c, m, rb_celems(n), n->count) ||
        !rb_leaf_take(c, m, (const unsigned char *)elem, 1)) {
      rb_release_node(c, m, 0);
      return DS_ERR_ALLOC;
    }
    *out = m;
    return DS_OK;
  } else {
    node_t *kid;
    ds_status_t st = rb_push(c, rb_ckids(n)[n->count - 1], h - 1, elem, &kid);
    if (st != DS_OK) {
      return st;
    }
    if (kid == NULL) {
      if (n->count == rb_m(c)) {
        return DS_OK;
      }
      kid = rb_chain(c, h - 1, elem);
      if (kid == NULL) {
        return DS_ERR_ALLOC;
      }
      m = rb_inner_from(c, n, 0, n->count);
      if (m == NULL) {
        rb_release_node(c, kid, h - 1);
        return DS_ERR_ALLOC;
      }
      rb_kids(m)[m->count++] = kid;
    } else {
      m = rb_inner_from(c, n, 0, n->count);
      if (m == NULL) {
        rb_release_node(c, kid, h - 1);
        return DS_ERR_ALLOC;
      }
      rb_release_node(c, rb_kids(m)[m->count - 1], h - 1);
      rb_kids(m)[m->count - 1] = kid;
    }
    rb_finish(c, m, h);
    *out = m;
    return DS_OK;
  }
}

ds_status_t ds_rrb_push_back(const ds_rrb_t *vec, const void *elem,
                             ds_rrb_t *out) {
  cfg_t *c;
  node_t *root;
  unsigned height;
  ds_status_t st;
  if (vec == NULL || elem == NULL || out == NULL || vec->config == NULL) {
    return DS_ERR_NULL;
  }
  c = vec->config;
  height = vec->height;
  if (vec->root == NULL) {
    root = rb_chain(c, 0, elem);
    if (root == NULL) {
      return DS_ERR_ALLOC;
    }
  } else {
    st = rb_push(c, vec->root, vec->height, elem, &root);
    if (st != DS_OK) {
      return st;
    }
    if (root == NULL) {
      /* a full tree: a new root over it and a fresh path */
      node_t *path = rb_chain(c, vec->height, elem);
      if (path == NULL) {
        return DS_ERR_ALLOC;
      }
      root = rb_alloc(c, false);
      if (root == NULL) {
        rb_release_node(c, path, vec->height);
        return DS_ERR_ALLOC;
      }
      vec->root->refs++;
      rb_kids(root)[0] = vec->root;
      rb_kids(root)[1] = path;
      root->count = 2;
      height = vec->height + 1;
      rb_finish(c, root, height);
    }
  }
  rb_assign(out, vec, NULL, root, height, vec->size + 1, c);
  return DS_OK;
}

/* n without its last element; *out NULL when nothing is left */
static ds_status_t rb_pop(cfg_t *c, const node_t *n, unsigned h, node_t **out) {
  node_t *m;
  *out = NULL;
  if (h == 0) {
    if (n->count == 1) {
      return DS_OK;
    }
    m = rb_alloc(c, true);
    if (m == NULL) {
      return DS_ERR_ALLOC;
    }
    if (!rb_leaf_take(c, m, rb_celems(n), n->count - 1)) {
      rb_release_node(c, m, 0);
      return DS_ERR_ALLOC;
    }
    *out = m;
    return DS_OK;
  } else {
    node_t *kid;
    ds_status_t st = rb_pop(c, rb_ckids(n)[n->count - 1], h - 1, &kid);
    if (st != DS_OK) {
      return st;
    }
    if (kid == NULL && n->count == 1) {
      return DS_OK;
    }
    m = rb_inner_from(c, n, 0, n->count);
    if (m == NULL) {
      rb_release_node(c, kid, h - 1);
      return DS_ERR_ALLOC;
    }
    rb_release_node(c, rb_kids(m)[m->count - 1], h - 1);
    if (kid == NULL) {
      m->count--;
    } else {
      rb_kids(m)[m->count - 1] = kid;
    }
    rb_finish(c, m, h);
    *out = m;
    return DS_OK;
  }
}

ds_status_t ds_rrb_pop_back(const ds_rrb_t *vec, void *out_elem,
                            ds_rrb_t *out) {
  cfg_t *c;
  node_t *root;
  unsigned height;
  ds_status_t st;
  if (vec == NULL || out == NULL || vec->config == NULL) {
    return DS_ERR_NULL;
  }
  if (vec->size == 0) {
    return DS_EMPTY;
  }
  c = vec->config;
  st = rb_pop(c, vec->root, vec->height, &root);
  if (st != DS_OK) {
    return st;
  }
  if (out_elem != NULL &&
      !rb_put(c, out_elem, ds_rrb_get(vec, vec->size - 1))) {
    rb_release_node(c, root, vec->height);
    return DS_ERR_ALLOC;
  }
  height = vec->height;
  rb_normalize(c, &root, &height);
  rb_assign(out, vec, NULL, root, root == NULL ? 0 : height, vec->size - 1, c);
  return DS_OK;
}

/* ---- slicing ---- */

/* the first k elements of n (1 <= k <= size) */
static node_t *rb_take(cfg_t *c, node_t *n, unsigned h, size_t k) {
  node_t *m;
  size_t slot;
  size_t i;
  node_t *kid;
  if (k == rb_size(c, n, h)) {
    n->refs++;
    return n;
  }
  if (h == 0) {
    m = rb_alloc(c, true);
    if (m != NULL && !rb_leaf_take(c, m, rb_elems(n), k)) {
      rb_release_node(c, m, 0);
      m = NULL;
    }
    return m;
  }
  i = k - 1;
  slot = rb_slot(c, n, h, &i);
  kid = rb_take(c, rb_kids(n)[slot], h - 1, i + 1);
  if (kid == NULL) {
    return NULL;
  }
  m = rb_inner_from(c, n, 0, slot);
  if (m == NULL) {
    rb_release_node(c, kid, h - 1);
    return NULL;
  }
  rb_kids(m)[m->count++] = kid;
  rb_finish(c, m, h);
  return m;
}

/* the elements of n from k on (k < size) */
static node_t *rb_drop(cfg_t *c, node_t *n, unsigned h, size_t k) {
  node_t *m;
  size_t slot;
  size_t i;
  node_t *kid;
  if (k == 0) {
    n->refs++;
    return n;
  }
  if (h == 0) {
    m = rb_alloc(c, true);
    if (m != NULL &&
        !rb_leaf_take(c, m, rb_elems(n) + k * c->elem_size, n->count - k)) {
      rb_release_node(c, m, 0);
      m = NULL;
    }
    return m;
  }
  i = k;
  slot = rb_slot(c, n, h, &i);
  kid = rb_drop(c, rb_kids(n)[slot], h - 1, i);
  if (kid == NULL) {
    return NULL;
  }
  m = rb_alloc(c, false);
  if (m == NULL) {
    rb_release_node(c, kid, h - 1);
    return NULL;
  }
  rb_kids(m)[0] = kid;
  m->count = 1;
  for (i = slot + 1; i < n->count; i++) {
    rb_kids(n)[i]->refs++;
    rb_kids(m)[m->count++] = rb_kids(n)[i];
  }
  rb_finish(c, m, h);
  return m;
}

ds_status_t ds_rrb_slice(const ds_rrb_t *vec, size_t from, size_t to,
                         ds_rrb_t *out) {
  cfg_t *c;
  node_t *taken;
  node_t *root;
  unsigned height;
  if (vec == NULL || out == NULL || vec->config == NULL) {
    return DS_ERR_NULL;
  }
  if (from > to || to > vec->size) {
    return DS_ERR_RANGE;
  }
  c = vec->config;
  if (from == to) {
    rb_assign(out, vec, NULL, NULL, 0, 0, c);
    return DS_OK;
  }
  taken = rb_take(c, vec->root, vec->height, to);
  if (taken == NULL) {
    return DS_ERR_ALLOC;
  }
  root = rb_drop(c, taken, vec->height, from);
  rb_release_node(c, taken, vec->height);
  if (root == NULL) {
    return DS_ERR_ALLOC;
  }
  height = vec->height;
  rb_normalize(c, &root, &height);
  rb_assign(out, vec, NULL, root, height, to - from, c);
  return DS_OK;
}

/* ---- concatenation ---- */

/* the plan: new item counts for the nodes in all (in place), returning
 * their number; at most E more nodes than ceil(total / M) remain */
static size_t rb_plan(cfg_t *c, size_t *s, size_t n) {
  size_t m = rb_m(c);
  size_t total = 0;
  size_t opt;
  size_t i;
  for (i = 0; i < n; i++) {
    total += s[i];
  }
  opt = (total + m - 1) / m;
  i = 0;
  while (n > opt + RB_E) {
    size_t r;
    size_t j;
    while (s[i] > m - RB_E / 2) {
      i++;
    }
    r = s[i];
    while (r > 0) {
      size_t merged = r + s[i + 1] < m ? r + s[i + 1] : m;
      s[i] = merged;
      r = r + s[i + 1] - merged;
      i++;
    }
    for (j = i; j + 1 < n; j++) {
      s[j] = s[j + 1];
    }
    i--;
    n--;
  }
  return n;
}

/* rebuild the nodes of height h in all[0 .. n) to the item counts of the
 * plan; nodes the plan leaves alone are shared. *count gets the number
 * of nodes in out. */
static ds_status_t rb_redistribute(cfg_t *c, node_t **all, size_t n, unsigned h,
                                   const size_t *plan, size_t planned,
                                   node_t **out) {
  size_t oi = 0; /* the old node feeding the next items */
  size_t oo = 0; /* items of it already used */
  size_t p;
  for (p = 0; p < planned; p++) {
    node_t *m;
    if (oo == 0 && all[oi]->count == plan[p]) {
      all[oi]->refs++;
      out[p] = all[oi];
      oi++;
      continue;
    }
    m = rb_alloc(c, h == 0);
    if (m == NULL) {
      goto fail;
    }
    while (m->count < plan[p]) {
      size_t k = all[oi]->count - oo;
      if (k > plan[p] - m->count) {
        k = plan[p] - m->count;
      }
      if (h == 0) {
        if (!rb_leaf_take(c, m, rb_elems(all[oi]) + oo * c->elem_size, k)) {
          rb_release_node(c, m, 0);
          goto fail;
        }
      } else {
        size_t i;
        for (i = 0; i < k; i++) {
          node_t *kid = rb_kids(all[oi])[oo + i];
          kid->refs++;
          rb_kids(m)[m->count++] = kid;
        }
      }
      oo += k;
      if (oo == all[oi]->count) {
        oi++;
        oo = 0;
      }
    }
    if (h > 0) {
      rb_finish(c, m, h);
    }
    out[p] = m;
  }
  (void)n;
  return DS_OK;
fail:
  while (p-- > 0) {
    rb_release_node(c, out[p], h);
  }
  return DS_ERR_ALLOC;
}

/* the concatenation of l (height hl) and r (height hr) as a node of
 * height H + 1, H = max(hl, hr), with one or two children */
static ds_status_t rb_cat(cfg_t *c, node_t *l, unsigned hl, node_t *r,
                          unsigned hr, node_t **out) {
  size_t m = rb_m(c);
  unsigned top = hl > hr ? hl : hr;
  node_t *mid = NULL;
  node_t **all = NULL;
  node_t **fresh = NULL;
  size_t *plan = NULL;
  size_t n = 0;
  size_t planned;
  size_t i;
  node_t *res = NULL;
  ds_status_t st = DS_OK;
  *out = NULL;
  if (top == 0) {
    res = rb_alloc(c, false);
    if (res == NULL) {
      return DS_ERR_ALLOC;
    }
    if (l->count + r->count <= m) {
      node_t *leaf = rb_alloc(c, true);
      if (leaf == NULL || !rb_leaf_take(c, leaf, rb_elems(l), l->count) ||
          !rb_leaf_take(c, leaf, rb_elems(r), r->count)) {
        rb_release_node(c, leaf, 0);
        rb_release_node(c, res, 1);
        return DS_ERR_ALLOC;
      }
      rb_kids(res)[0] = leaf;
      res->count = 1;
    } else {
      l->refs++;
      r->refs++;
      rb_kids(res)[0] = l;
      rb_kids(res)[1] = r;
      res->count = 2;
    }
    rb_finish(c, res, 1);
    *out = res;
    return DS_OK;
  }
  if (hl > hr) {
    st = rb_cat(c, rb_kids(l)[l->count - 1], hl - 1, r, hr, &mid);
  } else if (hl < hr) {
    st = rb_cat(c, l, hl, rb_kids(r)[0], hr - 1, &mid);
  } else {
    st = rb_cat(c, rb_kids(l)[l->count - 1], hl - 1, rb_kids(r)[0], hr - 1,
                &mid);
  }
  if (st != DS_OK) {
    return st;
  }
  /* the children of height top - 1: l's but its last, mid's, r's but its
   * first */
  all = (node_t **)ds_context_alloc(c->context, (2 * m + 4) * sizeof(node_t *));
  fresh =
      (node_t **)ds_context_alloc(c->context, (2 * m + 4) * sizeof(node_t *));
  plan = (size_t *)ds_context_calloc(c->context, 2 * m + 4, sizeof(size_t));
  if (all == NULL || fresh == NULL || plan == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  if (hl == top) {
    for (i = 0; i + 1 < l->count; i++) {
      all[n++] = rb_kids(l)[i];
    }
  }
  for (i = 0; i < mid->count; i++) {
    all[n++] = rb_kids(mid)[i];
  }
  if (hr == top) {
    for (i = 1; i < r->count; i++) {
      all[n++] = rb_kids(r)[i];
    }
  }
  for (i = 0; i < n; i++) {
    plan[i] = all[i]->count;
  }
  planned = rb_plan(c, plan, n);
  st = rb_redistribute(c, all, n, top - 1, plan, planned, fresh);
  if (st != DS_OK) {
    goto done;
  }
  /* one or two nodes of height top under the result */
  res = rb_alloc(c, false);
  if (res == NULL) {
    st = DS_ERR_ALLOC;
    for (i = 0; i < planned; i++) {
      rb_release_node(c, fresh[i], top - 1);
    }
  } else {
    size_t k = 0;
    while (k < planned && st == DS_OK) {
      size_t take = planned - k > m ? m : planned - k;
      node_t *p = rb_alloc(c, false);
      if (p == NULL) {
        st = DS_ERR_ALLOC;
        break;
      }
      memcpy(rb_kids(p), fresh + k, take * sizeof(node_t *));
      p->count = take;
      rb_finish(c, p, top);
      rb_kids(res)[res->count++] = p;
      k += take;
    }
    if (st != DS_OK) {
      /* the fresh nodes not yet handed to a parent */
      while (k < planned) {
        rb_release_node(c, fresh[k++], top - 1);
      }
      rb_release_node(c, res, top + 1);
      res = NULL;
    } else {
      rb_finish(c, res, top + 1);
    }
  }
done:
  rb_release_node(c, mid, top);
  ds_context_free(c->context, all);
  ds_context_free(c->context, fresh);
  ds_context_free(c->context, plan);
  *out = res;
  return st;
}

ds_status_t ds_rrb_concat(const ds_rrb_t *a, const ds_rrb_t *b, ds_rrb_t *out) {
  cfg_t *c;
  node_t *res;
  node_t *root;
  unsigned height;
  ds_status_t st;
  if (a == NULL || b == NULL || out == NULL || a->config == NULL) {
    return DS_ERR_NULL;
  }
  if (b->config != a->config && b->root != NULL) {
    return DS_ERR_INVALID;
  }
  c = a->config;
  if (b->size > (size_t)-1 - a->size) {
    return DS_ERR_OVERFLOW;
  }
  if (a->root == NULL || b->root == NULL) {
    const ds_rrb_t *keep = a->root == NULL ? b : a;
    if (keep->root != NULL) {
      keep->root->refs++;
    }
    rb_assign(out, a, b, keep->root, keep->height, keep->size, c);
    return DS_OK;
  }
  st = rb_cat(c, a->root, a->height, b->root, b->height, &res);
  if (st != DS_OK) {
    return st;
  }
  height = (a->height > b->height ? a->height : b->height) + 1;
  root = res;
  rb_normalize(c, &root, &height);
  rb_assign(out, a, b, root, height, a->size + b->size, c);
  return DS_OK;
}

/* ---- built from the above ---- */

static ds_status_t rb_single(const ds_rrb_t *vec, const void *elem,
                             ds_rrb_t *one) {
  one->config = vec->config;
  one->size = 1;
  one->height = 0;
  one->root = rb_chain(vec->config, 0, elem);
  return one->root == NULL ? DS_ERR_ALLOC : DS_OK;
}

ds_status_t ds_rrb_push_front(const ds_rrb_t *vec, const void *elem,
                              ds_rrb_t *out) {
  ds_rrb_t one;
  ds_status_t st;
  if (vec == NULL || elem == NULL || out == NULL || vec->config == NULL) {
    return DS_ERR_NULL;
  }
  st = rb_single(vec, elem, &one);
  if (st == DS_OK) {
    st = ds_rrb_concat(&one, vec, out);
    ds_rrb_release(&one);
  }
  return st;
}

ds_status_t ds_rrb_split(const ds_rrb_t *vec, size_t at, ds_rrb_t *left,
                         ds_rrb_t *right) {
  ds_rrb_t l;
  ds_rrb_t r;
  ds_status_t st;
  if (vec == NULL || left == NULL || right == NULL || left == right) {
    return DS_ERR_NULL;
  }
  if (at > vec->size) {
    return DS_ERR_RANGE;
  }
  memset(&l, 0, sizeof(l));
  memset(&r, 0, sizeof(r));
  st = ds_rrb_slice(vec, 0, at, &l);
  if (st == DS_OK) {
    st = ds_rrb_slice(vec, at, vec->size, &r);
    if (st != DS_OK) {
      ds_rrb_release(&l);
    }
  }
  if (st != DS_OK) {
    return st;
  }
  if (left == vec || right == vec) {
    ds_rrb_t *in = left == vec ? left : right;
    ds_rrb_release(in);
  }
  *left = l;
  *right = r;
  return DS_OK;
}

ds_status_t ds_rrb_insert(const ds_rrb_t *vec, size_t at, const void *elem,
                          ds_rrb_t *out) {
  ds_rrb_t l;
  ds_rrb_t r;
  ds_rrb_t lx;
  ds_status_t st;
  if (vec == NULL || elem == NULL || out == NULL || vec->config == NULL) {
    return DS_ERR_NULL;
  }
  if (at > vec->size) {
    return DS_ERR_RANGE;
  }
  if (at == vec->size) {
    return ds_rrb_push_back(vec, elem, out);
  }
  memset(&l, 0, sizeof(l));
  memset(&r, 0, sizeof(r));
  memset(&lx, 0, sizeof(lx));
  st = ds_rrb_split(vec, at, &l, &r);
  if (st != DS_OK) {
    return st;
  }
  st = ds_rrb_push_back(&l, elem, &lx);
  if (st == DS_OK) {
    ds_rrb_t res;
    memset(&res, 0, sizeof(res));
    st = ds_rrb_concat(&lx, &r, &res);
    if (st == DS_OK) {
      rb_assign(out, vec, NULL, res.root, res.height, res.size, vec->config);
    }
    ds_rrb_release(&lx);
  }
  ds_rrb_release(&l);
  ds_rrb_release(&r);
  return st;
}

ds_status_t ds_rrb_remove(const ds_rrb_t *vec, size_t at, void *out_elem,
                          ds_rrb_t *out) {
  ds_rrb_t l;
  ds_rrb_t r;
  ds_rrb_t res;
  ds_status_t st;
  if (vec == NULL || out == NULL || vec->config == NULL) {
    return DS_ERR_NULL;
  }
  if (at >= vec->size) {
    return DS_ERR_RANGE;
  }
  memset(&l, 0, sizeof(l));
  memset(&r, 0, sizeof(r));
  memset(&res, 0, sizeof(res));
  st = ds_rrb_slice(vec, 0, at, &l);
  if (st == DS_OK) {
    st = ds_rrb_slice(vec, at + 1, vec->size, &r);
  }
  if (st == DS_OK) {
    st = ds_rrb_concat(&l, &r, &res);
  }
  if (st == DS_OK && out_elem != NULL &&
      !rb_put(vec->config, out_elem, ds_rrb_get(vec, at))) {
    ds_rrb_release(&res);
    st = DS_ERR_ALLOC;
  }
  ds_rrb_release(&l);
  ds_rrb_release(&r);
  if (st == DS_OK) {
    rb_assign(out, vec, NULL, res.root, res.height, res.size, vec->config);
  }
  return st;
}

/* ---- iteration and checks ---- */

void ds_rrb_iter_init(ds_rrb_iter_t *it, const ds_rrb_t *vec, size_t start) {
  if (it == NULL) {
    return;
  }
  memset(it, 0, sizeof(*it));
  it->vec = vec;
  it->index = start;
}

const void *ds_rrb_iter_next(ds_rrb_iter_t *it) {
  const ds_rrb_t *v;
  const void *p;
  if (it == NULL || it->vec == NULL || it->index >= it->vec->size) {
    return NULL;
  }
  v = it->vec;
  if (it->leaf == NULL || it->index >= it->leaf_end ||
      it->index < it->leaf_base) {
    /* find the leaf: the descent gives the offset inside it */
    const node_t *n = v->root;
    size_t i = it->index;
    unsigned h;
    for (h = v->height; h > 0; h--) {
      n = rb_ckids(n)[rb_slot(v->config, n, h, &i)];
    }
    it->leaf = rb_celems(n);
    it->leaf_base = it->index - i;
    it->leaf_end = it->leaf_base + n->count;
  }
  p = it->leaf + (it->index - it->leaf_base) * v->config->elem_size;
  it->index++;
  return p;
}

static bool rb_check_node(cfg_t *c, node_t *n, unsigned h, size_t *size) {
  size_t i;
  size_t acc = 0;
  bool balanced = true;
  if (n == NULL || n->refs == 0 || n->count == 0 || n->count > rb_m(c)) {
    return false;
  }
  if (h == 0) {
    *size = n->count;
    return n->sizes == NULL;
  }
  for (i = 0; i < n->count; i++) {
    size_t s;
    if (!rb_check_node(c, rb_kids(n)[i], h - 1, &s)) {
      return false;
    }
    if (i + 1 < n->count && s != rb_cap(c, h - 1)) {
      balanced = false;
    }
    acc += s;
    if (n->sizes != NULL && n->sizes[i] != acc) {
      return false;
    }
  }
  if (balanced != (n->sizes == NULL)) {
    return false; /* a table exactly when radix indexing would fail */
  }
  *size = acc;
  return true;
}

bool ds_rrb_check(const ds_rrb_t *vec) {
  size_t s;
  if (vec == NULL || vec->config == NULL) {
    return false;
  }
  if (vec->root == NULL) {
    return vec->size == 0 && vec->height == 0;
  }
  return rb_check_node(vec->config, vec->root, vec->height, &s) &&
         s == vec->size && (vec->height == 0 || vec->root->count > 1);
}
