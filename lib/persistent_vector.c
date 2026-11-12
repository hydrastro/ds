#include "persistent_vector.h"
#include "checked.h"
#include <string.h>

typedef ds_pvec_node_t pv_node_t;
typedef const ds_pvec_config_t pv_cfg_t;

#define PV_NONE ((size_t) - 1)

static size_t pv_header(void) {
  size_t h = sizeof(pv_node_t);
  (void)ds_size_align(h, DS_MAX_ALIGN, &h);
  return h;
}

static size_t pv_width(pv_cfg_t *cfg) { return (size_t)1 << cfg->bits; }

static pv_node_t **pv_children(pv_node_t *n) {
  return (pv_node_t **)(void *)((unsigned char *)n + pv_header());
}

static unsigned char *pv_elems(pv_node_t *n) {
  return (unsigned char *)n + pv_header();
}

static unsigned char *pv_elem(pv_cfg_t *cfg, pv_node_t *n, size_t i) {
  return pv_elems(n) + i * cfg->elem_size;
}

static size_t pv_tailoff(const ds_pvec_t *v) {
  size_t bits = v->config->bits;
  return v->size < pv_width(v->config) ? 0 : ((v->size - 1) >> bits) << bits;
}

static pv_node_t *pv_alloc(pv_cfg_t *cfg, bool leaf) {
  size_t payload = leaf ? pv_width(cfg) * cfg->elem_size
                        : pv_width(cfg) * sizeof(pv_node_t *);
  pv_node_t *n = (pv_node_t *)ds_context_alloc(
      cfg->context, pv_header() + (payload == 0 ? 1 : payload));
  if (n == NULL) {
    return NULL;
  }
  n->refs = 1;
  n->count = 0;
  if (!leaf) {
    memset(pv_children(n), 0, payload);
  }
  return n;
}

static void pv_release(pv_cfg_t *cfg, pv_node_t *n, unsigned level) {
  size_t i;
  if (n == NULL || --n->refs > 0) {
    return;
  }
  if (level == 0) {
    if (cfg->destroy_elem != NULL) {
      for (i = 0; i < n->count; i++) {
        cfg->destroy_elem(pv_elem(cfg, n, i), cfg->user);
      }
    }
  } else {
    for (i = 0; i < pv_width(cfg); i++) {
      pv_release(cfg, pv_children(n)[i], level - cfg->bits);
    }
  }
  ds_context_free(cfg->context, n);
}

static bool pv_copy_elem(pv_cfg_t *cfg, void *dst, const void *src) {
  if (cfg->copy_elem != NULL) {
    return cfg->copy_elem(dst, src, cfg->user);
  }
  memcpy(dst, src, cfg->elem_size);
  return true;
}

static void pv_destroy_elem(pv_cfg_t *cfg, void *elem) {
  if (cfg->destroy_elem != NULL) {
    cfg->destroy_elem(elem, cfg->user);
  }
}

/* new full-width leaf with the first count elements of src (slot replace,
 * if < count, taking elem instead) and then extra (if not NULL) appended */
static pv_node_t *pv_copy_leaf(pv_cfg_t *cfg, pv_node_t *src, size_t count,
                               size_t replace, const void *elem,
                               const void *extra) {
  pv_node_t *n = pv_alloc(cfg, true);
  size_t i;
  if (n == NULL) {
    return NULL;
  }
  for (i = 0; i < count; i++) {
    const void *from = i == replace ? elem : pv_elem(cfg, src, i);
    if (!pv_copy_elem(cfg, pv_elem(cfg, n, i), from)) {
      pv_release(cfg, n, 0);
      return NULL;
    }
    n->count = i + 1;
  }
  if (extra != NULL) {
    if (!pv_copy_elem(cfg, pv_elem(cfg, n, count), extra)) {
      pv_release(cfg, n, 0);
      return NULL;
    }
    n->count = count + 1;
  }
  return n;
}

static pv_node_t *pv_copy_inner(pv_cfg_t *cfg, pv_node_t *src) {
  pv_node_t *n = pv_alloc(cfg, false);
  size_t i;
  if (n == NULL) {
    return NULL;
  }
  n->count = src->count;
  for (i = 0; i < pv_width(cfg); i++) {
    pv_children(n)[i] = pv_children(src)[i];
    if (pv_children(n)[i] != NULL) {
      pv_children(n)[i]->refs++;
    }
  }
  return n;
}

/* chain of single-child interior nodes from level down to leaf (one new
 * reference to leaf) */
static pv_node_t *pv_new_path(pv_cfg_t *cfg, unsigned level, pv_node_t *leaf) {
  pv_node_t *n, *c;
  if (level == 0) {
    leaf->refs++;
    return leaf;
  }
  c = pv_new_path(cfg, level - cfg->bits, leaf);
  if (c == NULL) {
    return NULL;
  }
  n = pv_alloc(cfg, false);
  if (n == NULL) {
    pv_release(cfg, c, level - cfg->bits);
    return NULL;
  }
  pv_children(n)[0] = c;
  n->count = 1;
  return n;
}

/*
 * Ownership contract shared by the recursive helpers: with owned set the
 * caller hands over its reference to node, which nobody else holds, and
 * the node may be edited (the result holds that reference); otherwise
 * node is left alone and the result is a fresh reference. NULL means
 * allocation failure with nothing changed. Owned levels only edit after
 * the levels below succeeded, and every level below an unowned one is
 * unowned, so a failure never leaves a partial edit.
 */
static pv_node_t *pv_push_tail(pv_cfg_t *cfg, pv_node_t *node, unsigned level,
                               bool owned, size_t idx, pv_node_t *leaf) {
  size_t sub = (idx >> level) & (pv_width(cfg) - 1);
  pv_node_t *child = pv_children(node)[sub], *nc, *ret;
  bool child_owned = false;
  if (level == cfg->bits) {
    leaf->refs++;
    nc = leaf;
  } else if (child != NULL) {
    child_owned = owned && child->refs == 1;
    nc = pv_push_tail(cfg, child, level - cfg->bits, child_owned, idx, leaf);
  } else {
    nc = pv_new_path(cfg, level - cfg->bits, leaf);
  }
  if (nc == NULL) {
    return NULL;
  }
  if (owned) {
    ret = node;
    if (child != NULL && !child_owned) {
      pv_release(cfg, child, level - cfg->bits);
    }
  } else {
    ret = pv_copy_inner(cfg, node);
    if (ret == NULL) {
      pv_release(cfg, nc, level - cfg->bits);
      return NULL;
    }
    pv_release(cfg, pv_children(ret)[sub], level - cfg->bits);
  }
  pv_children(ret)[sub] = nc;
  if (ret->count < sub + 1) {
    ret->count = sub + 1;
  }
  return ret;
}

/* drop the leaf holding element idx (the last leaf); *out may be NULL
 * when node empties. Returns false on allocation failure. */
static bool pv_pop_tail(pv_cfg_t *cfg, pv_node_t *node, unsigned level,
                        bool owned, size_t idx, pv_node_t **out) {
  size_t sub = (idx >> level) & (pv_width(cfg) - 1);
  pv_node_t *child = pv_children(node)[sub], *nc = NULL, *ret;
  bool child_owned = false;
  if (level > cfg->bits) {
    child_owned = owned && child->refs == 1;
    if (!pv_pop_tail(cfg, child, level - cfg->bits, child_owned, idx, &nc)) {
      return false;
    }
  }
  if (nc == NULL && sub == 0) {
    /* node empties */
    if (owned) {
      if (child_owned) {
        pv_children(node)[sub] = NULL; /* consumed below */
      }
      pv_release(cfg, node, level);
    }
    *out = NULL;
    return true;
  }
  if (owned) {
    ret = node;
    if (!child_owned) {
      pv_release(cfg, child, level - cfg->bits);
    }
  } else {
    ret = pv_copy_inner(cfg, node);
    if (ret == NULL) {
      pv_release(cfg, nc, level - cfg->bits);
      return false;
    }
    pv_release(cfg, pv_children(ret)[sub], level - cfg->bits);
  }
  pv_children(ret)[sub] = nc;
  ret->count = nc != NULL ? sub + 1 : sub;
  *out = ret;
  return true;
}

static pv_node_t *pv_set_rec(pv_cfg_t *cfg, pv_node_t *node, unsigned level,
                             bool owned, size_t idx, const void *elem,
                             void *tmp) {
  size_t sub = (idx >> level) & (pv_width(cfg) - 1);
  pv_node_t *child, *nc, *ret;
  bool child_owned;
  if (level == 0) {
    if (owned) {
      if (tmp != NULL) {
        /* copy first so a failing copy leaves the old element */
        if (!cfg->copy_elem(tmp, elem, cfg->user)) {
          return NULL;
        }
        pv_destroy_elem(cfg, pv_elem(cfg, node, sub));
        memcpy(pv_elem(cfg, node, sub), tmp, cfg->elem_size);
      } else {
        pv_destroy_elem(cfg, pv_elem(cfg, node, sub));
        memcpy(pv_elem(cfg, node, sub), elem, cfg->elem_size);
      }
      return node;
    }
    return pv_copy_leaf(cfg, node, node->count, sub, elem, NULL);
  }
  child = pv_children(node)[sub];
  child_owned = owned && child->refs == 1;
  nc = pv_set_rec(cfg, child, level - cfg->bits, child_owned, idx, elem, tmp);
  if (nc == NULL) {
    return NULL;
  }
  if (owned) {
    if (!child_owned) {
      pv_release(cfg, child, level - cfg->bits);
    }
    pv_children(node)[sub] = nc;
    return node;
  }
  ret = pv_copy_inner(cfg, node);
  if (ret == NULL) {
    pv_release(cfg, nc, level - cfg->bits);
    return NULL;
  }
  pv_release(cfg, pv_children(ret)[sub], level - cfg->bits);
  pv_children(ret)[sub] = nc;
  return ret;
}

static pv_node_t *pv_leaf_for(const ds_pvec_t *v, size_t i) {
  pv_cfg_t *cfg = v->config;
  pv_node_t *n = v->root;
  unsigned level;
  for (level = v->shift; level > 0; level -= cfg->bits) {
    n = pv_children(n)[(i >> level) & (pv_width(cfg) - 1)];
  }
  return n;
}

/* ---- public ---- */

void ds_pvec_config_init(ds_pvec_config_t *config, size_t elem_size) {
  if (config == NULL) {
    return;
  }
  memset(config, 0, sizeof(*config));
  config->elem_size = elem_size;
  config->bits = 5;
}

ds_status_t ds_pvec_config_from_schema(ds_pvec_config_t *config,
                                       const ds_schema_t *schema) {
  ds_status_t st;
  if (config == NULL || schema == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_schema_validate(schema);
  if (st != DS_OK) {
    return st;
  }
  ds_pvec_config_init(config, schema->size);
  config->copy_elem = schema->copy;
  config->destroy_elem = schema->destroy;
  config->user = schema->user;
  return DS_OK;
}

ds_status_t ds_pvec_init(ds_pvec_t *vec, const ds_pvec_config_t *config) {
  size_t bytes;
  if (vec == NULL || config == NULL) {
    return DS_ERR_NULL;
  }
  if (config->bits == 0 || config->bits > 8) {
    return DS_ERR_INVALID;
  }
  /* versions share leaves by copying them: an element that owns a
   * resource (destroy_elem) must be duplicable, or two leaves would both
   * destroy it */
  if (config->destroy_elem != NULL && config->copy_elem == NULL) {
    return DS_ERR_INVALID;
  }
  if (!ds_size_mul(pv_width(config), config->elem_size, &bytes) ||
      !ds_size_add(bytes, pv_header(), &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  vec->root = NULL;
  vec->tail = NULL;
  vec->size = 0;
  vec->shift = config->bits;
  vec->config = config;
  return DS_OK;
}

void ds_pvec_copy(const ds_pvec_t *vec, ds_pvec_t *out) {
  if (vec == NULL || out == NULL || vec == out) {
    return;
  }
  *out = *vec;
  if (vec->root != NULL) {
    vec->root->refs++;
  }
  if (vec->tail != NULL) {
    vec->tail->refs++;
  }
}

void ds_pvec_release(ds_pvec_t *vec) {
  if (vec == NULL || vec->config == NULL) {
    return;
  }
  pv_release(vec->config, vec->root, vec->shift);
  pv_release(vec->config, vec->tail, 0);
  vec->root = NULL;
  vec->tail = NULL;
  vec->size = 0;
  vec->shift = vec->config->bits;
}

size_t ds_pvec_size(const ds_pvec_t *vec) {
  return vec == NULL ? 0 : vec->size;
}

const void *ds_pvec_get(const ds_pvec_t *vec, size_t index) {
  size_t off;
  if (vec == NULL || index >= vec->size) {
    return NULL;
  }
  off = pv_tailoff(vec);
  if (index >= off) {
    return pv_elem(vec->config, vec->tail, index - off);
  }
  return pv_elem(vec->config, pv_leaf_for(vec, index),
                 index & (pv_width(vec->config) - 1));
}

ds_status_t ds_pvec_push_back(const ds_pvec_t *vec, const void *elem,
                              ds_pvec_t *out) {
  pv_cfg_t *cfg;
  ds_pvec_t r;
  size_t n, tc;
  bool owned;
  if (vec == NULL || elem == NULL || out == NULL || vec->config == NULL) {
    return DS_ERR_NULL;
  }
  cfg = vec->config;
  owned = out == vec;
  n = vec->size;
  tc = n - pv_tailoff(vec);
  r = *vec;
  if (n == 0 || tc < pv_width(cfg)) {
    /* room in the tail */
    if (owned && vec->tail != NULL && vec->tail->refs == 1) {
      if (!pv_copy_elem(cfg, pv_elem(cfg, vec->tail, tc), elem)) {
        return DS_ERR_ALLOC;
      }
      vec->tail->count = tc + 1;
      out->size = n + 1;
      return DS_OK;
    }
    r.tail = pv_copy_leaf(cfg, vec->tail, n == 0 ? 0 : tc, PV_NONE, NULL, elem);
    if (r.tail == NULL) {
      return DS_ERR_ALLOC;
    }
    r.size = n + 1;
    if (owned) {
      pv_release(cfg, vec->tail, 0);
    } else if (r.root != NULL) {
      r.root->refs++;
    }
    *out = r;
    return DS_OK;
  }
  /* full tail: move it into the trie and start a new one */
  r.tail = pv_copy_leaf(cfg, NULL, 0, PV_NONE, NULL, elem);
  if (r.tail == NULL) {
    return DS_ERR_ALLOC;
  }
  if (vec->root == NULL) {
    r.root = pv_alloc(cfg, false);
    if (r.root == NULL) {
      pv_release(cfg, r.tail, 0);
      return DS_ERR_ALLOC;
    }
    pv_children(r.root)[0] = vec->tail;
    vec->tail->refs++;
    r.root->count = 1;
    r.shift = cfg->bits;
    if (owned) {
      pv_release(cfg, vec->tail, 0);
    }
  } else if ((n >> cfg->bits) > ((size_t)1 << vec->shift)) {
    /* root overflow: grow a level */
    pv_node_t *path = pv_new_path(cfg, vec->shift, vec->tail);
    if (path == NULL) {
      pv_release(cfg, r.tail, 0);
      return DS_ERR_ALLOC;
    }
    r.root = pv_alloc(cfg, false);
    if (r.root == NULL) {
      pv_release(cfg, path, vec->shift);
      pv_release(cfg, r.tail, 0);
      return DS_ERR_ALLOC;
    }
    pv_children(r.root)[0] = vec->root;
    pv_children(r.root)[1] = path;
    vec->root->refs++;
    r.root->count = 2;
    r.shift = vec->shift + cfg->bits;
    if (owned) {
      pv_release(cfg, vec->root, vec->shift);
      pv_release(cfg, vec->tail, 0);
    }
  } else {
    bool root_owned = owned && vec->root->refs == 1;
    r.root =
        pv_push_tail(cfg, vec->root, vec->shift, root_owned, n - 1, vec->tail);
    if (r.root == NULL) {
      pv_release(cfg, r.tail, 0);
      return DS_ERR_ALLOC;
    }
    if (owned) {
      if (!root_owned) {
        pv_release(cfg, vec->root, vec->shift);
      }
      pv_release(cfg, vec->tail, 0);
    }
  }
  r.size = n + 1;
  *out = r;
  return DS_OK;
}

ds_status_t ds_pvec_pop_back(const ds_pvec_t *vec, void *out_elem,
                             ds_pvec_t *out) {
  pv_cfg_t *cfg;
  ds_pvec_t r;
  size_t n, tc;
  bool owned;
  if (vec == NULL || out == NULL || vec->config == NULL) {
    return DS_ERR_NULL;
  }
  cfg = vec->config;
  n = vec->size;
  if (n == 0) {
    return DS_EMPTY;
  }
  owned = out == vec;
  tc = n - pv_tailoff(vec);
  r = *vec;
  if (n == 1 || tc > 1) {
    pv_node_t *nt = NULL;
    if (out_elem != NULL &&
        !pv_copy_elem(cfg, out_elem, pv_elem(cfg, vec->tail, tc - 1))) {
      return DS_ERR_ALLOC;
    }
    if (n == 1) {
      r.tail = NULL;
    } else if (owned && vec->tail->refs == 1) {
      pv_destroy_elem(cfg, pv_elem(cfg, vec->tail, tc - 1));
      vec->tail->count = tc - 1;
      out->size = n - 1;
      return DS_OK;
    } else {
      nt = pv_copy_leaf(cfg, vec->tail, tc - 1, PV_NONE, NULL, NULL);
      if (nt == NULL) {
        if (out_elem != NULL) {
          pv_destroy_elem(cfg, out_elem);
        }
        return DS_ERR_ALLOC;
      }
      r.tail = nt;
    }
    r.size = n - 1;
    if (owned) {
      pv_release(cfg, vec->tail, 0);
    } else if (r.root != NULL) {
      r.root->refs++;
    }
    *out = r;
    return DS_OK;
  }
  /* the tail empties: the trie's last leaf becomes the tail */
  {
    pv_node_t *leaf = pv_leaf_for(vec, n - 2), *nr;
    bool root_owned = owned && vec->root->refs == 1;
    if (out_elem != NULL &&
        !pv_copy_elem(cfg, out_elem, pv_elem(cfg, vec->tail, 0))) {
      return DS_ERR_ALLOC;
    }
    leaf->refs++; /* the new tail's reference */
    if (!pv_pop_tail(cfg, vec->root, vec->shift, root_owned, n - 2, &nr)) {
      leaf->refs--;
      if (out_elem != NULL) {
        pv_destroy_elem(cfg, out_elem);
      }
      return DS_ERR_ALLOC;
    }
    r.shift = vec->shift;
    if (nr != NULL && r.shift > cfg->bits && nr->count == 1) {
      pv_node_t *c = pv_children(nr)[0];
      c->refs++;
      pv_release(cfg, nr, r.shift);
      nr = c;
      r.shift -= cfg->bits;
    }
    if (nr == NULL) {
      r.shift = cfg->bits;
    }
    r.root = nr;
    r.tail = leaf;
    r.size = n - 1;
    if (owned) {
      if (!root_owned) {
        pv_release(cfg, vec->root, vec->shift);
      }
      pv_release(cfg, vec->tail, 0);
    }
    *out = r;
    return DS_OK;
  }
}

ds_status_t ds_pvec_set(const ds_pvec_t *vec, size_t index, const void *elem,
                        ds_pvec_t *out) {
  pv_cfg_t *cfg;
  ds_pvec_t r;
  size_t off;
  bool owned;
  void *tmp = NULL;
  if (vec == NULL || elem == NULL || out == NULL || vec->config == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= vec->size) {
    return DS_ERR_RANGE;
  }
  cfg = vec->config;
  owned = out == vec;
  off = pv_tailoff(vec);
  r = *vec;
  if (owned && cfg->copy_elem != NULL) {
    tmp = ds_context_alloc(cfg->context,
                           cfg->elem_size == 0 ? 1 : cfg->elem_size);
    if (tmp == NULL) {
      return DS_ERR_ALLOC;
    }
  }
  if (index >= off) {
    if (owned && vec->tail->refs == 1) {
      if (tmp != NULL) {
        if (!cfg->copy_elem(tmp, elem, cfg->user)) {
          ds_context_free(cfg->context, tmp);
          return DS_ERR_ALLOC;
        }
        pv_destroy_elem(cfg, pv_elem(cfg, vec->tail, index - off));
        memcpy(pv_elem(cfg, vec->tail, index - off), tmp, cfg->elem_size);
      } else {
        pv_destroy_elem(cfg, pv_elem(cfg, vec->tail, index - off));
        memcpy(pv_elem(cfg, vec->tail, index - off), elem, cfg->elem_size);
      }
      ds_context_free(cfg->context, tmp);
      return DS_OK;
    }
    r.tail =
        pv_copy_leaf(cfg, vec->tail, vec->size - off, index - off, elem, NULL);
    ds_context_free(cfg->context, tmp);
    if (r.tail == NULL) {
      return DS_ERR_ALLOC;
    }
    if (owned) {
      pv_release(cfg, vec->tail, 0);
    } else if (r.root != NULL) {
      r.root->refs++;
    }
    *out = r;
    return DS_OK;
  }
  {
    bool root_owned = owned && vec->root->refs == 1;
    r.root =
        pv_set_rec(cfg, vec->root, vec->shift, root_owned, index, elem, tmp);
    ds_context_free(cfg->context, tmp);
    if (r.root == NULL) {
      return DS_ERR_ALLOC;
    }
    if (owned) {
      if (!root_owned) {
        pv_release(cfg, vec->root, vec->shift);
      }
    } else {
      r.tail->refs++;
    }
    *out = r;
    return DS_OK;
  }
}

ds_status_t ds_pvec_truncate(const ds_pvec_t *vec, size_t n, ds_pvec_t *out) {
  ds_pvec_t work;
  ds_status_t st;
  if (vec == NULL || out == NULL || vec->config == NULL) {
    return DS_ERR_NULL;
  }
  if (n > vec->size) {
    return DS_ERR_RANGE;
  }
  /* work on a second handle so a failure leaves vec untouched; its first
   * pops copy paths, later ones edit the copies in place */
  ds_pvec_copy(vec, &work);
  while (work.size > n) {
    st = ds_pvec_pop_back(&work, NULL, &work);
    if (st != DS_OK) {
      ds_pvec_release(&work);
      return st;
    }
  }
  if (vec == out) {
    ds_pvec_release(out);
  }
  *out = work;
  return DS_OK;
}

void ds_pvec_iter_init(ds_pvec_iter_t *it, const ds_pvec_t *vec, size_t start) {
  if (it == NULL) {
    return;
  }
  it->vec = vec;
  it->index = start;
  it->leaf = NULL;
  it->leaf_base = 0;
}

const void *ds_pvec_iter_next(ds_pvec_iter_t *it) {
  const ds_pvec_t *v;
  size_t w;
  if (it == NULL || it->vec == NULL || it->index >= it->vec->size) {
    return NULL;
  }
  v = it->vec;
  w = pv_width(v->config);
  if (it->leaf == NULL || it->index < it->leaf_base ||
      it->index - it->leaf_base >= w) {
    size_t off = pv_tailoff(v);
    if (it->index >= off) {
      it->leaf = pv_elems(v->tail);
      it->leaf_base = off;
    } else {
      it->leaf = pv_elems(pv_leaf_for(v, it->index));
      it->leaf_base = it->index & ~(w - 1);
    }
  }
  return it->leaf + (it->index++ - it->leaf_base) * v->config->elem_size;
}
