#include "top_tree.h"
#include "checked.h"
#include <string.h>

/*
 * Vertex nodes form the compress trees: ch[0] is towards the start of the
 * preferred path, ch[1] towards its end; vp is the parent in the compress
 * tree, and a compress tree's root has rp, the rake node that holds it
 * (NULL for the tree's top path). light is the root of the vertex's rake
 * tree. sum is the path cluster of the node's compress subtree, already
 * reversed when rev is pending (rev means: swap the children below).
 *
 * Rake nodes form the rake trees: tree is the hanging compress tree, the
 * point summary covers the rake subtree, and a rake root's owner is the
 * vertex it hangs from.
 */

typedef struct tt_v {
  struct tt_v *ch[2];
  struct tt_v *vp;
  struct tt_r *rp;
  struct tt_r *light;
  unsigned char rev;
} tt_v_t;

typedef struct tt_r {
  struct tt_r *ch[2];
  struct tt_r *parent; /* also the free list's link */
  tt_v_t *owner;
  tt_v_t *tree;
} tt_r_t;

static tt_v_t *tt_vertex(const ds_top_tree_t *t, size_t i) {
  return (tt_v_t *)(void *)(t->vertices + i * t->vstride);
}

static size_t tt_index(const ds_top_tree_t *t, const tt_v_t *x) {
  return (size_t)((const unsigned char *)(const void *)x - t->vertices) /
         t->vstride;
}

static unsigned char *tt_value(const ds_top_tree_t *t, tt_v_t *x) {
  return (unsigned char *)(void *)x + t->value_at;
}

static unsigned char *tt_sum(const ds_top_tree_t *t, tt_v_t *x) {
  return (unsigned char *)(void *)x + t->sum_at;
}

static unsigned char *tt_point(const ds_top_tree_t *t, tt_r_t *r) {
  return (unsigned char *)(void *)r + t->point_at;
}

/* ---- summaries ----------------------------------------------------------- */

static void tt_update_v(ds_top_tree_t *t, tt_v_t *x) {
  unsigned char *a = t->scratch;
  unsigned char *b = a + t->ops.path_size;
  unsigned char *swap;
  t->ops.vertex(a, tt_value(t, x),
                x->light != NULL ? tt_point(t, x->light) : NULL, t->ops.user);
  if (x->ch[0] != NULL) {
    t->ops.compress(b, tt_sum(t, x->ch[0]), a, t->ops.user);
    swap = a;
    a = b;
    b = swap;
  }
  if (x->ch[1] != NULL) {
    t->ops.compress(b, a, tt_sum(t, x->ch[1]), t->ops.user);
    swap = a;
    a = b;
    b = swap;
  }
  memcpy(tt_sum(t, x), a, t->ops.path_size);
}

static void tt_update_r(ds_top_tree_t *t, tt_r_t *r) {
  unsigned char *a = t->scratch + 6 * t->ops.path_size;
  unsigned char *b = a + t->ops.point_size;
  unsigned char *swap;
  t->ops.to_point(a, tt_sum(t, r->tree), t->ops.user);
  if (r->ch[0] != NULL) {
    t->ops.rake(b, tt_point(t, r->ch[0]), a, t->ops.user);
    swap = a;
    a = b;
    b = swap;
  }
  if (r->ch[1] != NULL) {
    t->ops.rake(b, a, tt_point(t, r->ch[1]), t->ops.user);
    swap = a;
    a = b;
    b = swap;
  }
  memcpy(tt_point(t, r), a, t->ops.point_size);
}

static void tt_toggle(ds_top_tree_t *t, tt_v_t *x) {
  x->rev ^= 1;
  if (t->ops.reverse != NULL) {
    t->ops.reverse(tt_sum(t, x), t->ops.user);
  }
}

static void tt_push(ds_top_tree_t *t, tt_v_t *x) {
  if (x->rev) {
    tt_v_t *c = x->ch[0];
    x->ch[0] = x->ch[1];
    x->ch[1] = c;
    if (x->ch[0] != NULL) {
      tt_toggle(t, x->ch[0]);
    }
    if (x->ch[1] != NULL) {
      tt_toggle(t, x->ch[1]);
    }
    x->rev = 0;
  }
}

/* ---- compress trees ------------------------------------------------------ */

static void tt_rotate_v(ds_top_tree_t *t, tt_v_t *x) {
  tt_v_t *y = x->vp;
  tt_v_t *z = y->vp;
  unsigned d = y->ch[1] == x ? 1u : 0u;
  tt_v_t *b = x->ch[1u - d];
  y->ch[d] = b;
  if (b != NULL) {
    b->vp = y;
  }
  x->ch[1u - d] = y;
  y->vp = x;
  x->vp = z;
  if (z != NULL) {
    z->ch[z->ch[1] == y ? 1 : 0] = x;
  } else { /* x becomes the compress root: it takes over the rake link */
    x->rp = y->rp;
    y->rp = NULL;
    if (x->rp != NULL) {
      x->rp->tree = x;
    }
  }
  tt_update_v(t, y);
  tt_update_v(t, x);
}

static void tt_splay_v(ds_top_tree_t *t, tt_v_t *x) {
  size_t sp = 0;
  tt_v_t *y = x;
  t->stack[sp++] = y;
  while (y->vp != NULL) {
    y = y->vp;
    t->stack[sp++] = y;
  }
  while (sp > 0) {
    tt_push(t, (tt_v_t *)t->stack[--sp]);
  }
  while (x->vp != NULL) {
    tt_v_t *p = x->vp;
    tt_v_t *g = p->vp;
    if (g != NULL) {
      tt_rotate_v(t, (g->ch[0] == p) == (p->ch[0] == x) ? p : x);
    }
    tt_rotate_v(t, x);
  }
}

/* ---- rake trees ---------------------------------------------------------- */

static void tt_rotate_r(ds_top_tree_t *t, tt_r_t *x) {
  tt_r_t *y = x->parent;
  tt_r_t *z = y->parent;
  unsigned d = y->ch[1] == x ? 1u : 0u;
  tt_r_t *b = x->ch[1u - d];
  y->ch[d] = b;
  if (b != NULL) {
    b->parent = y;
  }
  x->ch[1u - d] = y;
  y->parent = x;
  x->parent = z;
  if (z != NULL) {
    z->ch[z->ch[1] == y ? 1 : 0] = x;
  } else { /* x becomes the rake root */
    x->owner = y->owner;
    y->owner = NULL;
    if (x->owner != NULL) {
      x->owner->light = x;
    }
  }
  tt_update_r(t, y);
  tt_update_r(t, x);
}

static void tt_splay_r(ds_top_tree_t *t, tt_r_t *x) {
  while (x->parent != NULL) {
    tt_r_t *p = x->parent;
    tt_r_t *g = p->parent;
    if (g != NULL) {
      tt_rotate_r(t, (g->ch[0] == p) == (p->ch[0] == x) ? p : x);
    }
    tt_rotate_r(t, x);
  }
}

/* r becomes the root of w's rake tree, over the old root */
static void tt_rake_add(ds_top_tree_t *t, tt_v_t *w, tt_r_t *r) {
  r->ch[0] = w->light;
  r->ch[1] = NULL;
  r->parent = NULL;
  r->owner = w;
  if (w->light != NULL) {
    w->light->parent = r;
    w->light->owner = NULL;
  }
  w->light = r;
  tt_update_r(t, r);
}

/* remove the root r of w's rake tree */
static void tt_rake_drop(ds_top_tree_t *t, tt_v_t *w, tt_r_t *r) {
  tt_r_t *a = r->ch[0];
  tt_r_t *b = r->ch[1];
  tt_r_t *root;
  if (a != NULL) {
    a->parent = NULL;
  }
  if (b != NULL) {
    b->parent = NULL;
  }
  if (a == NULL) {
    root = b;
  } else if (b == NULL) {
    root = a;
  } else { /* the largest of a's tree rises and takes b on its right */
    tt_r_t *m = a;
    while (m->ch[1] != NULL) {
      m = m->ch[1];
    }
    tt_splay_r(t, m);
    m->ch[1] = b;
    b->parent = m;
    tt_update_r(t, m);
    root = m;
  }
  if (root != NULL) {
    root->owner = w;
  }
  w->light = root;
}

static tt_r_t *tt_rake_new(ds_top_tree_t *t) {
  tt_r_t *r = (tt_r_t *)t->free_rakes;
  t->free_rakes = r->parent;
  r->ch[0] = NULL;
  r->ch[1] = NULL;
  r->parent = NULL;
  r->owner = NULL;
  r->tree = NULL;
  return r;
}

static void tt_rake_free(ds_top_tree_t *t, tt_r_t *r) {
  r->parent = (tt_r_t *)t->free_rakes;
  t->free_rakes = r;
}

/* ---- access, evert ------------------------------------------------------- */

/* the path from v's tree root to v becomes the top path, v its splay root
 * and its last vertex */
static void tt_access(ds_top_tree_t *t, tt_v_t *v) {
  tt_v_t *x = v;
  tt_v_t *prev = NULL;
  for (;;) {
    tt_v_t *c;
    tt_splay_v(t, x);
    c = x->ch[1];
    if (prev == NULL) {
      if (c != NULL) { /* the rest of the path below hangs from x */
        tt_r_t *r = tt_rake_new(t);
        c->vp = NULL;
        c->rp = r;
        r->tree = c;
        tt_rake_add(t, x, r);
      }
    } else { /* prev hangs from x through r, the root of x's rake tree */
      tt_r_t *r = prev->rp;
      prev->rp = NULL;
      if (c != NULL) { /* r holds x's old continuation instead */
        c->vp = NULL;
        c->rp = r;
        r->tree = c;
        tt_update_r(t, r);
      } else {
        tt_rake_drop(t, x, r);
        tt_rake_free(t, r);
      }
      prev->vp = x;
    }
    x->ch[1] = prev;
    tt_update_v(t, x);
    if (x->rp == NULL) {
      break;
    }
    tt_splay_r(t, x->rp);
    prev = x;
    x = x->rp->owner;
  }
  tt_splay_v(t, v);
}

static void tt_evert(ds_top_tree_t *t, tt_v_t *v) {
  tt_access(t, v);
  tt_toggle(t, v);
}

static tt_v_t *tt_root(ds_top_tree_t *t, tt_v_t *x) {
  tt_v_t *y = x;
  tt_access(t, x);
  tt_push(t, y);
  while (y->ch[0] != NULL) {
    y = y->ch[0];
    tt_push(t, y);
  }
  tt_splay_v(t, y);
  return y;
}

/* ---- the interface ------------------------------------------------------- */

ds_status_t ds_top_tree_init(ds_top_tree_t *t, size_t n,
                             const ds_top_tree_ops_t *ops, const void *values,
                             ds_context_t *context) {
  size_t i;
  size_t bytes;
  size_t scratch;
  if (t == NULL || ops == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (ops->vertex == NULL || ops->compress == NULL || ops->rake == NULL ||
      ops->to_point == NULL || ops->path_size == 0 || ops->point_size == 0) {
    return DS_ERR_INVALID;
  }
  t->n = n;
  t->ops = *ops;
  t->context = context;
  if (!ds_size_align(sizeof(tt_v_t), DS_MAX_ALIGN, &t->value_at) ||
      !ds_size_add(t->value_at, ops->value_size, &t->sum_at) ||
      !ds_size_align(t->sum_at, DS_MAX_ALIGN, &t->sum_at) ||
      !ds_size_add(t->sum_at, ops->path_size, &t->vstride) ||
      !ds_size_align(t->vstride, DS_MAX_ALIGN, &t->vstride) ||
      !ds_size_align(sizeof(tt_r_t), DS_MAX_ALIGN, &t->point_at) ||
      !ds_size_add(t->point_at, ops->point_size, &t->rstride) ||
      !ds_size_align(t->rstride, DS_MAX_ALIGN, &t->rstride) ||
      !ds_size_mul(6, ops->path_size, &scratch) ||
      !ds_size_mul_add(2, ops->point_size, scratch, &scratch)) {
    return DS_ERR_OVERFLOW;
  }
  if (n == 0) {
    return DS_OK;
  }
  if (!ds_size_mul(n, t->vstride, &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  t->vertices = (unsigned char *)ds_context_alloc(context, bytes);
  if (!ds_size_mul(n, t->rstride, &bytes)) {
    ds_top_tree_destroy(t);
    return DS_ERR_OVERFLOW;
  }
  t->rakes = (unsigned char *)ds_context_alloc(context, bytes);
  t->stack = (void **)ds_context_alloc(context, n * sizeof(void *));
  t->scratch = (unsigned char *)ds_context_alloc(context, scratch);
  if (t->vertices == NULL || t->rakes == NULL || t->stack == NULL ||
      t->scratch == NULL) {
    ds_top_tree_destroy(t);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    tt_v_t *x = tt_vertex(t, i);
    memset(x, 0, sizeof(*x));
    if (ops->value_size > 0) {
      if (values != NULL) {
        memcpy(tt_value(t, x),
               (const unsigned char *)values + i * ops->value_size,
               ops->value_size);
      } else {
        memset(tt_value(t, x), 0, ops->value_size);
      }
    }
    tt_update_v(t, x);
  }
  t->free_rakes = NULL;
  for (i = n; i-- > 0;) {
    tt_r_t *r = (tt_r_t *)(void *)(t->rakes + i * t->rstride);
    r->parent = (tt_r_t *)t->free_rakes;
    t->free_rakes = r;
  }
  return DS_OK;
}

void ds_top_tree_destroy(ds_top_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->vertices);
  ds_context_free(t->context, t->rakes);
  ds_context_free(t->context, t->stack);
  ds_context_free(t->context, t->scratch);
  t->vertices = NULL;
  t->rakes = NULL;
  t->stack = NULL;
  t->scratch = NULL;
  t->free_rakes = NULL;
  t->n = 0;
}

bool ds_top_connected(ds_top_tree_t *t, size_t u, size_t v) {
  if (t == NULL || u >= t->n || v >= t->n) {
    return false;
  }
  if (u == v) {
    return true;
  }
  return tt_root(t, tt_vertex(t, u)) == tt_root(t, tt_vertex(t, v));
}

ds_status_t ds_top_link(ds_top_tree_t *t, size_t u, size_t v) {
  tt_v_t *x;
  tt_v_t *y;
  tt_r_t *r;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= t->n || v >= t->n) {
    return DS_ERR_RANGE;
  }
  if (ds_top_connected(t, u, v)) {
    return DS_ERR_INVALID;
  }
  x = tt_vertex(t, u);
  y = tt_vertex(t, v);
  tt_evert(t, x);
  tt_access(t, y);
  r = tt_rake_new(t);
  x->rp = r;
  r->tree = x;
  tt_rake_add(t, y, r);
  tt_update_v(t, y);
  return DS_OK;
}

/* with u everted and v accessed: whether u is v's path neighbour */
static bool tt_adjacent(ds_top_tree_t *t, tt_v_t *x, tt_v_t *y) {
  tt_v_t *c;
  tt_evert(t, x);
  tt_access(t, y);
  c = y->ch[0];
  if (c != x) {
    return false;
  }
  tt_push(t, c);
  return c->ch[0] == NULL && c->ch[1] == NULL;
}

bool ds_top_adjacent(ds_top_tree_t *t, size_t u, size_t v) {
  if (t == NULL || u >= t->n || v >= t->n || u == v ||
      !ds_top_connected(t, u, v)) {
    return false;
  }
  return tt_adjacent(t, tt_vertex(t, u), tt_vertex(t, v));
}

ds_status_t ds_top_cut(ds_top_tree_t *t, size_t u, size_t v) {
  tt_v_t *x;
  tt_v_t *y;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= t->n || v >= t->n) {
    return DS_ERR_RANGE;
  }
  if (u == v || !ds_top_connected(t, u, v)) {
    return DS_NOT_FOUND;
  }
  x = tt_vertex(t, u);
  y = tt_vertex(t, v);
  if (!tt_adjacent(t, x, y)) {
    return DS_NOT_FOUND;
  }
  y->ch[0] = NULL;
  x->vp = NULL;
  tt_update_v(t, y);
  return DS_OK;
}

ds_status_t ds_top_expose(ds_top_tree_t *t, size_t u, size_t v,
                          void *out_path) {
  tt_v_t *y;
  if (t == NULL || out_path == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= t->n || v >= t->n) {
    return DS_ERR_RANGE;
  }
  if (!ds_top_connected(t, u, v)) {
    return DS_NOT_FOUND;
  }
  y = tt_vertex(t, v);
  tt_evert(t, tt_vertex(t, u));
  tt_access(t, y);
  memcpy(out_path, tt_sum(t, y), t->ops.path_size);
  return DS_OK;
}

ds_status_t ds_top_subtree(ds_top_tree_t *t, size_t v, size_t root,
                           void *out_path) {
  tt_v_t *y;
  if (t == NULL || out_path == NULL) {
    return DS_ERR_NULL;
  }
  if (v >= t->n || root >= t->n) {
    return DS_ERR_RANGE;
  }
  if (!ds_top_connected(t, v, root)) {
    return DS_NOT_FOUND;
  }
  y = tt_vertex(t, v);
  tt_evert(t, tt_vertex(t, root));
  tt_access(t, y);
  /* below v hangs only its rake tree: v's path neighbours lie above */
  t->ops.vertex(out_path, tt_value(t, y),
                y->light != NULL ? tt_point(t, y->light) : NULL, t->ops.user);
  return DS_OK;
}

const void *ds_top_value(const ds_top_tree_t *t, size_t v) {
  if (t == NULL || v >= t->n) {
    return NULL;
  }
  return tt_value(t, tt_vertex(t, v));
}

void ds_top_set_value(ds_top_tree_t *t, size_t v, const void *value) {
  tt_v_t *x;
  if (t == NULL || v >= t->n || t->ops.value_size == 0) {
    return;
  }
  x = tt_vertex(t, v);
  tt_access(t, x);
  memcpy(tt_value(t, x), value, t->ops.value_size);
  tt_update_v(t, x);
}

void ds_top_evert(ds_top_tree_t *t, size_t v) {
  if (t != NULL && v < t->n) {
    tt_evert(t, tt_vertex(t, v));
  }
}

size_t ds_top_find_root(ds_top_tree_t *t, size_t v) {
  if (t == NULL || v >= t->n) {
    return (size_t)-1;
  }
  return tt_index(t, tt_root(t, tt_vertex(t, v)));
}

ds_status_t ds_top_path_search(ds_top_tree_t *t, size_t u, size_t v,
                               ds_top_choice_fn choose, void *user,
                               size_t *out_vertex) {
  tt_v_t *x;
  tt_v_t *last = NULL;
  unsigned char *acc;    /* the path before x's compress subtree */
  unsigned char *before; /* the path before x */
  unsigned char *here;   /* x with what hangs from it */
  unsigned char *tmp;
  bool have_acc = false;
  if (t == NULL || choose == NULL || out_vertex == NULL) {
    return DS_ERR_NULL;
  }
  if (u >= t->n || v >= t->n) {
    return DS_ERR_RANGE;
  }
  if (!ds_top_connected(t, u, v)) {
    return DS_NOT_FOUND;
  }
  acc = t->scratch + 2 * t->ops.path_size;
  before = acc + t->ops.path_size;
  here = before + t->ops.path_size;
  tmp = here + t->ops.path_size;
  x = tt_vertex(t, v);
  tt_evert(t, tt_vertex(t, u));
  tt_access(t, x);
  while (x != NULL) {
    const void *b = NULL;
    int c;
    tt_push(t, x);
    if (x->ch[0] != NULL) {
      if (have_acc) {
        t->ops.compress(before, acc, tt_sum(t, x->ch[0]), t->ops.user);
      } else {
        memcpy(before, tt_sum(t, x->ch[0]), t->ops.path_size);
      }
      b = before;
    } else if (have_acc) {
      b = acc;
    }
    t->ops.vertex(here, tt_value(t, x),
                  x->light != NULL ? tt_point(t, x->light) : NULL, t->ops.user);
    c = choose(b, here, user);
    last = x;
    if (c == 0) {
      *out_vertex = tt_index(t, x);
      tt_splay_v(t, x);
      return DS_OK;
    }
    if (c < 0) {
      x = x->ch[0];
    } else {
      if (b != NULL) {
        t->ops.compress(tmp, b, here, t->ops.user);
        memcpy(acc, tmp, t->ops.path_size);
      } else {
        memcpy(acc, here, t->ops.path_size);
      }
      have_acc = true;
      x = x->ch[1];
    }
  }
  tt_splay_v(t, last);
  return DS_NOT_FOUND;
}
