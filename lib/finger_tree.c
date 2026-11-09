#include "finger_tree.h"
#include "checked.h"
#include <string.h>

/*
 * Items are the elements (kind 0) and the 2-3 nodes built from them
 * (kind 2 or 3); a tree is empty (NULL), a single item, or deep: a
 * prefix and a suffix digit of 1-4 items around a middle tree of nodes.
 * Every constructor retains what it stores and every helper returns
 * fresh references (or reports failure, having released its
 * temporaries), so arguments are never consumed. Recursion follows the
 * nesting depth, which is O(log n).
 */

typedef const ds_ftree_config_t cfg_t;

typedef struct fi {
  size_t refs;
  int kind;
  struct fi *c[3];
} fi_t;

typedef struct ds_ftree_tree {
  size_t refs;
  int deep;
  fi_t *pr[4];
  fi_t *sf[4];
  size_t npr;
  size_t nsf;
  struct ds_ftree_tree *mid;
} ft_t;

#define FT_LEVELS 130 /* scratch levels for split and lookup */

static size_t ft_align(size_t x) {
  size_t r = x;
  (void)ds_size_align(x, DS_MAX_ALIGN, &r);
  return r;
}

static size_t fi_moff(void) { return ft_align(sizeof(fi_t)); }

static size_t fi_eoff(cfg_t *c) {
  return ft_align(fi_moff() + c->measure_size);
}

static size_t ft_moff(void) { return ft_align(sizeof(ft_t)); }

static unsigned char *fi_m(fi_t *x) { return (unsigned char *)x + fi_moff(); }

static unsigned char *fi_e(cfg_t *c, fi_t *x) {
  return (unsigned char *)x + fi_eoff(c);
}

static const void *ft_m(cfg_t *c, ft_t *t) {
  return t == NULL ? c->identity
                   : (const void *)((unsigned char *)t + ft_moff());
}

static unsigned char *ft_mw(ft_t *t) { return (unsigned char *)t + ft_moff(); }

static void fi_release(cfg_t *c, fi_t *x) {
  int i;
  if (x == NULL || --x->refs > 0) {
    return;
  }
  if (x->kind == 0) {
    if (c->destroy_elem != NULL) {
      c->destroy_elem(fi_e(c, x), c->user);
    }
  } else {
    for (i = 0; i < x->kind; i++) {
      fi_release(c, x->c[i]);
    }
  }
  ds_context_free(c->context, x);
}

static void ft_release(cfg_t *c, ft_t *t) {
  size_t i;
  if (t == NULL || --t->refs > 0) {
    return;
  }
  for (i = 0; i < t->npr; i++) {
    fi_release(c, t->pr[i]);
  }
  for (i = 0; i < t->nsf; i++) {
    fi_release(c, t->sf[i]);
  }
  ft_release(c, t->mid);
  ds_context_free(c->context, t);
}

static fi_t *fi_elem(cfg_t *c, const void *elem) {
  fi_t *x = (fi_t *)ds_context_alloc(c->context, fi_eoff(c) + c->elem_size);
  if (x == NULL) {
    return NULL;
  }
  x->refs = 1;
  x->kind = 0;
  x->c[0] = x->c[1] = x->c[2] = NULL;
  memcpy(fi_e(c, x), elem, c->elem_size);
  c->measure(fi_m(x), elem, c->user);
  return x;
}

static fi_t *fi_node(cfg_t *c, fi_t *a, fi_t *b, fi_t *d) {
  fi_t *x = (fi_t *)ds_context_alloc(c->context, fi_eoff(c));
  if (x == NULL) {
    return NULL;
  }
  x->refs = 1;
  x->kind = d != NULL ? 3 : 2;
  x->c[0] = a;
  x->c[1] = b;
  x->c[2] = d;
  a->refs++;
  b->refs++;
  memcpy(fi_m(x), fi_m(a), c->measure_size);
  c->combine(fi_m(x), fi_m(x), fi_m(b), c->user);
  if (d != NULL) {
    d->refs++;
    c->combine(fi_m(x), fi_m(x), fi_m(d), c->user);
  }
  return x;
}

/* deep tree (npr, nsf in 1..4) or, with nsf == 0, a single item */
static ft_t *ft_make(cfg_t *c, fi_t *const *pr, size_t npr, ft_t *mid,
                     fi_t *const *sf, size_t nsf) {
  ft_t *t = (ft_t *)ds_context_alloc(c->context, ft_moff() + c->measure_size);
  size_t i;
  if (t == NULL) {
    return NULL;
  }
  t->refs = 1;
  t->deep = nsf > 0;
  t->npr = npr;
  t->nsf = nsf;
  t->mid = mid;
  if (mid != NULL) {
    mid->refs++;
  }
  memcpy(ft_mw(t), c->identity, c->measure_size);
  for (i = 0; i < npr; i++) {
    t->pr[i] = pr[i];
    pr[i]->refs++;
    c->combine(ft_mw(t), ft_mw(t), fi_m(pr[i]), c->user);
  }
  if (mid != NULL) {
    c->combine(ft_mw(t), ft_mw(t), ft_m(c, mid), c->user);
  }
  for (i = 0; i < nsf; i++) {
    t->sf[i] = sf[i];
    sf[i]->refs++;
    c->combine(ft_mw(t), ft_mw(t), fi_m(sf[i]), c->user);
  }
  return t;
}

/* tree of up to 5 items */
static bool ft_from_items(cfg_t *c, fi_t *const *items, size_t n, ft_t **out) {
  if (n == 0) {
    *out = NULL;
    return true;
  }
  *out = n == 1 ? ft_make(c, items, 1, NULL, NULL, 0)
                : ft_make(c, items, 1, NULL, items + 1, n - 1);
  return *out != NULL;
}

static bool ft_push_front(cfg_t *c, ft_t *t, fi_t *x, ft_t **out) {
  fi_t *items[2];
  if (t == NULL) {
    *out = ft_make(c, &x, 1, NULL, NULL, 0);
  } else if (!t->deep) {
    *out = ft_make(c, &x, 1, NULL, t->pr, 1);
  } else if (t->npr < 4) {
    fi_t *pr[4];
    size_t i;
    pr[0] = x;
    for (i = 0; i < t->npr; i++) {
      pr[i + 1] = t->pr[i];
    }
    *out = ft_make(c, pr, t->npr + 1, t->mid, t->sf, t->nsf);
  } else {
    ft_t *m;
    fi_t *node = fi_node(c, t->pr[1], t->pr[2], t->pr[3]);
    if (node == NULL) {
      return false;
    }
    if (!ft_push_front(c, t->mid, node, &m)) {
      fi_release(c, node);
      return false;
    }
    fi_release(c, node);
    items[0] = x;
    items[1] = t->pr[0];
    *out = ft_make(c, items, 2, m, t->sf, t->nsf);
    ft_release(c, m);
  }
  return *out != NULL;
}

static bool ft_push_back(cfg_t *c, ft_t *t, fi_t *x, ft_t **out) {
  fi_t *items[2];
  if (t == NULL) {
    *out = ft_make(c, &x, 1, NULL, NULL, 0);
  } else if (!t->deep) {
    *out = ft_make(c, t->pr, 1, NULL, &x, 1);
  } else if (t->nsf < 4) {
    fi_t *sf[4];
    size_t i;
    for (i = 0; i < t->nsf; i++) {
      sf[i] = t->sf[i];
    }
    sf[t->nsf] = x;
    *out = ft_make(c, t->pr, t->npr, t->mid, sf, t->nsf + 1);
  } else {
    ft_t *m;
    fi_t *node = fi_node(c, t->sf[0], t->sf[1], t->sf[2]);
    if (node == NULL) {
      return false;
    }
    if (!ft_push_back(c, t->mid, node, &m)) {
      fi_release(c, node);
      return false;
    }
    fi_release(c, node);
    items[0] = t->sf[3];
    items[1] = x;
    *out = ft_make(c, t->pr, t->npr, m, items, 2);
    ft_release(c, m);
  }
  return *out != NULL;
}

static bool ft_view_l(cfg_t *c, ft_t *t, fi_t **head, ft_t **rest);
static bool ft_view_r(cfg_t *c, ft_t *t, ft_t **rest, fi_t **last);

/* tree from a possibly empty prefix, a middle and a suffix */
static bool ft_deep_l(cfg_t *c, fi_t *const *pr, size_t npr, ft_t *mid,
                      fi_t *const *sf, size_t nsf, ft_t **out) {
  fi_t *h;
  ft_t *m;
  if (npr > 0) {
    *out = ft_make(c, pr, npr, mid, sf, nsf);
    return *out != NULL;
  }
  if (mid == NULL) {
    return ft_from_items(c, sf, nsf, out);
  }
  if (!ft_view_l(c, mid, &h, &m)) {
    return false;
  }
  *out = ft_make(c, h->c, (size_t)h->kind, m, sf, nsf);
  fi_release(c, h);
  ft_release(c, m);
  return *out != NULL;
}

static bool ft_deep_r(cfg_t *c, fi_t *const *pr, size_t npr, ft_t *mid,
                      fi_t *const *sf, size_t nsf, ft_t **out) {
  fi_t *l;
  ft_t *m;
  if (nsf > 0) {
    *out = ft_make(c, pr, npr, mid, sf, nsf);
    return *out != NULL;
  }
  if (mid == NULL) {
    return ft_from_items(c, pr, npr, out);
  }
  if (!ft_view_r(c, mid, &m, &l)) {
    return false;
  }
  *out = ft_make(c, pr, npr, m, l->c, (size_t)l->kind);
  fi_release(c, l);
  ft_release(c, m);
  return *out != NULL;
}

static bool ft_view_l(cfg_t *c, ft_t *t, fi_t **head, ft_t **rest) {
  bool ok;
  if (!t->deep) {
    *rest = NULL;
    ok = true;
  } else {
    ok = ft_deep_l(c, t->pr + 1, t->npr - 1, t->mid, t->sf, t->nsf, rest);
  }
  if (ok) {
    *head = t->pr[0];
    (*head)->refs++;
  }
  return ok;
}

static bool ft_view_r(cfg_t *c, ft_t *t, ft_t **rest, fi_t **last) {
  bool ok;
  if (!t->deep) {
    *rest = NULL;
    *last = t->pr[0];
    t->pr[0]->refs++;
    return true;
  }
  ok = ft_deep_r(c, t->pr, t->npr, t->mid, t->sf, t->nsf - 1, rest);
  if (ok) {
    *last = t->sf[t->nsf - 1];
    (*last)->refs++;
  }
  return ok;
}

/* group 2..12 items into 2-3 nodes (fresh references in out) */
static bool ft_nodes(cfg_t *c, fi_t *const *items, size_t n, fi_t **out,
                     size_t *k) {
  size_t i = 0, j;
  *k = 0;
  while (n - i > 0) {
    size_t left = n - i, take = left == 2 || left == 4 ? 2 : 3;
    out[*k] =
        fi_node(c, items[i], items[i + 1], take == 3 ? items[i + 2] : NULL);
    if (out[*k] == NULL) {
      for (j = 0; j < *k; j++) {
        fi_release(c, out[j]);
      }
      return false;
    }
    (*k)++;
    i += take;
  }
  return true;
}

static bool ft_app3(cfg_t *c, ft_t *a, fi_t *const *ts, size_t nts, ft_t *b,
                    ft_t **out) {
  size_t i;
  if (a == NULL || b == NULL) {
    ft_t *acc = a != NULL ? a : b, *next;
    if (acc != NULL) {
      acc->refs++;
    }
    for (i = 0; i < nts; i++) {
      bool ok = a == NULL ? ft_push_front(c, acc, ts[nts - 1 - i], &next)
                          : ft_push_back(c, acc, ts[i], &next);
      ft_release(c, acc);
      if (!ok) {
        return false;
      }
      acc = next;
    }
    *out = acc;
    return true;
  }
  if (!a->deep) {
    ft_t *r;
    bool ok;
    if (!ft_app3(c, NULL, ts, nts, b, &r)) {
      return false;
    }
    ok = ft_push_front(c, r, a->pr[0], out);
    ft_release(c, r);
    return ok;
  }
  if (!b->deep) {
    ft_t *r;
    bool ok;
    if (!ft_app3(c, a, ts, nts, NULL, &r)) {
      return false;
    }
    ok = ft_push_back(c, r, b->pr[0], out);
    ft_release(c, r);
    return ok;
  }
  {
    fi_t *list[12], *ns[4] = {NULL, NULL, NULL, NULL};
    size_t n = 0, k;
    ft_t *m;
    bool ok;
    for (i = 0; i < a->nsf; i++) {
      list[n++] = a->sf[i];
    }
    for (i = 0; i < nts; i++) {
      list[n++] = ts[i];
    }
    for (i = 0; i < b->npr; i++) {
      list[n++] = b->pr[i];
    }
    if (!ft_nodes(c, list, n, ns, &k)) {
      return false;
    }
    ok = ft_app3(c, a->mid, ns, k, b->mid, &m);
    for (i = 0; i < k; i++) {
      fi_release(c, ns[i]);
    }
    if (!ok) {
      return false;
    }
    *out = ft_make(c, a->pr, a->npr, m, b->sf, b->nsf);
    ft_release(c, m);
    return *out != NULL;
  }
}

/* ---- split and lookup ---- */

typedef struct ft_split_ctx {
  cfg_t *c;
  ds_ftree_pred_t pred;
  void *user;
  unsigned char *scratch; /* 3 measures per level */
} ft_split_ctx_t;

/* first k whose prefix measure (acc combined with items[0..k]) satisfies
 * pred, n - 1 if none; acc ends as the measure before items[k] */
static size_t ft_split_digit(ft_split_ctx_t *s, unsigned char *acc,
                             unsigned char *tmp, fi_t *const *items, size_t n) {
  size_t k;
  for (k = 0; k + 1 < n; k++) {
    memcpy(tmp, acc, s->c->measure_size);
    s->c->combine(tmp, tmp, fi_m(items[k]), s->c->user);
    if (s->pred(tmp, s->user)) {
      return k;
    }
    memcpy(acc, tmp, s->c->measure_size);
  }
  return n - 1;
}

static void ft_accum(cfg_t *c, unsigned char *acc, fi_t *const *items,
                     size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    c->combine(acc, acc, fi_m(items[i]), c->user);
  }
}

/* t non-empty, pred(acc . measure(t)) true; acc unchanged */
static bool ft_split_tree(ft_split_ctx_t *s, const unsigned char *acc, ft_t *t,
                          unsigned level, ft_t **l, fi_t **x, ft_t **r) {
  cfg_t *c = s->c;
  size_t ms = c->measure_size, k;
  unsigned char *v1 = s->scratch + 3 * level * ms, *v2 = v1 + ms,
                *tmp = v2 + ms;
  bool ok;
  if (!t->deep) {
    *l = NULL;
    *r = NULL;
    *x = t->pr[0];
    (*x)->refs++;
    return true;
  }
  memcpy(v1, acc, ms);
  ft_accum(c, v1, t->pr, t->npr);
  if (s->pred(v1, s->user)) {
    memcpy(v1, acc, ms);
    k = ft_split_digit(s, v1, tmp, t->pr, t->npr);
    if (!ft_from_items(c, t->pr, k, l)) {
      return false;
    }
    if (!ft_deep_l(c, t->pr + k + 1, t->npr - k - 1, t->mid, t->sf, t->nsf,
                   r)) {
      ft_release(c, *l);
      return false;
    }
    *x = t->pr[k];
    (*x)->refs++;
    return true;
  }
  memcpy(v2, v1, ms);
  if (t->mid != NULL) {
    c->combine(v2, v2, ft_m(c, t->mid), c->user);
  }
  if (t->mid != NULL && s->pred(v2, s->user)) {
    ft_t *ml, *mr;
    fi_t *xs;
    if (level + 1 >= FT_LEVELS ||
        !ft_split_tree(s, v1, t->mid, level + 1, &ml, &xs, &mr)) {
      return false;
    }
    /* v1 becomes the measure before xs's children */
    c->combine(v1, v1, ft_m(c, ml), c->user);
    k = ft_split_digit(s, v1, tmp, xs->c, (size_t)xs->kind);
    ok = ft_deep_r(c, t->pr, t->npr, ml, xs->c, k, l);
    if (ok) {
      ok = ft_deep_l(c, xs->c + k + 1, (size_t)xs->kind - k - 1, mr, t->sf,
                     t->nsf, r);
      if (!ok) {
        ft_release(c, *l);
      }
    }
    if (ok) {
      *x = xs->c[k];
      (*x)->refs++;
    }
    ft_release(c, ml);
    ft_release(c, mr);
    fi_release(c, xs);
    return ok;
  }
  k = ft_split_digit(s, v2, tmp, t->sf, t->nsf);
  if (!ft_deep_r(c, t->pr, t->npr, t->mid, t->sf, k, l)) {
    return false;
  }
  if (!ft_from_items(c, t->sf + k + 1, t->nsf - k - 1, r)) {
    ft_release(c, *l);
    return false;
  }
  *x = t->sf[k];
  (*x)->refs++;
  return true;
}

static fi_t *ft_lookup_item(ft_split_ctx_t *s, unsigned char *acc,
                            unsigned char *tmp, fi_t *x) {
  while (x->kind != 0) {
    size_t k = ft_split_digit(s, acc, tmp, x->c, (size_t)x->kind);
    x = x->c[k];
  }
  return x;
}

static fi_t *ft_lookup_tree(ft_split_ctx_t *s, unsigned char *acc, ft_t *t,
                            unsigned level) {
  cfg_t *c = s->c;
  size_t ms = c->measure_size, k;
  unsigned char *v1 = s->scratch + 3 * level * ms, *tmp = v1 + ms;
  if (!t->deep) {
    return ft_lookup_item(s, acc, tmp, t->pr[0]);
  }
  memcpy(v1, acc, ms);
  ft_accum(c, v1, t->pr, t->npr);
  if (s->pred(v1, s->user)) {
    k = ft_split_digit(s, acc, tmp, t->pr, t->npr);
    return ft_lookup_item(s, acc, tmp, t->pr[k]);
  }
  memcpy(acc, v1, ms);
  if (t->mid != NULL) {
    memcpy(v1, acc, ms);
    c->combine(v1, v1, ft_m(c, t->mid), c->user);
    if (s->pred(v1, s->user)) {
      return level + 1 < FT_LEVELS ? ft_lookup_tree(s, acc, t->mid, level + 1)
                                   : NULL;
    }
    memcpy(acc, v1, ms);
  }
  k = ft_split_digit(s, acc, tmp, t->sf, t->nsf);
  return ft_lookup_item(s, acc, tmp, t->sf[k]);
}

/* ---- public ---- */

ds_status_t ds_ftree_init(ds_ftree_t *t, const ds_ftree_config_t *config) {
  size_t off, bytes;
  if (t == NULL || config == NULL || config->measure == NULL ||
      config->combine == NULL || config->identity == NULL) {
    return DS_ERR_NULL;
  }
  /* validate every size the helpers compute later, once: item header +
   * measure + padding + element, tree header + measure, split scratch */
  if (!ds_size_align(sizeof(fi_t), DS_MAX_ALIGN, &off) ||
      !ds_size_add(off, config->measure_size, &off) ||
      !ds_size_align(off, DS_MAX_ALIGN, &off) ||
      !ds_size_add(off, config->elem_size, &bytes) ||
      !ds_size_align(sizeof(ft_t), DS_MAX_ALIGN, &off) ||
      !ds_size_add(off, config->measure_size, &bytes) ||
      !ds_size_mul(3 * FT_LEVELS, config->measure_size, &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  t->root = NULL;
  t->config = config;
  return DS_OK;
}

void ds_ftree_copy(const ds_ftree_t *t, ds_ftree_t *out) {
  if (t == NULL || out == NULL || t == out) {
    return;
  }
  *out = *t;
  if (t->root != NULL) {
    t->root->refs++;
  }
}

void ds_ftree_release(ds_ftree_t *t) {
  if (t == NULL || t->config == NULL) {
    return;
  }
  ft_release(t->config, t->root);
  t->root = NULL;
}

bool ds_ftree_is_empty(const ds_ftree_t *t) {
  return t == NULL || t->root == NULL;
}

void ds_ftree_measure(const ds_ftree_t *t, void *out) {
  if (t == NULL || out == NULL || t->config == NULL) {
    return;
  }
  memcpy(out, ft_m(t->config, t->root), t->config->measure_size);
}

const void *ds_ftree_front(const ds_ftree_t *t) {
  if (t == NULL || t->root == NULL) {
    return NULL;
  }
  return fi_e(t->config, t->root->pr[0]);
}

const void *ds_ftree_back(const ds_ftree_t *t) {
  ft_t *r;
  if (t == NULL || t->root == NULL) {
    return NULL;
  }
  r = t->root;
  return fi_e(t->config, r->deep ? r->sf[r->nsf - 1] : r->pr[0]);
}

/* install a new root into out (releasing out's old root when out is one
 * of the inputs) */
static void ft_install(ds_ftree_t *out, const ds_ftree_t *in, ft_t *root) {
  cfg_t *c = in->config;
  if (out == in) {
    ft_release(c, out->root);
  }
  out->root = root;
  out->config = c;
}

static ds_status_t ft_push(const ds_ftree_t *t, const void *elem,
                           ds_ftree_t *out, bool front) {
  cfg_t *c;
  fi_t *x;
  ft_t *r;
  bool ok;
  if (t == NULL || elem == NULL || out == NULL || t->config == NULL) {
    return DS_ERR_NULL;
  }
  c = t->config;
  x = fi_elem(c, elem);
  if (x == NULL) {
    return DS_ERR_ALLOC;
  }
  ok = front ? ft_push_front(c, t->root, x, &r)
             : ft_push_back(c, t->root, x, &r);
  if (!ok) {
    /* the caller keeps its element: free the box without destroying */
    ds_context_free(c->context, x);
    return DS_ERR_ALLOC;
  }
  x->refs--; /* now held by the tree only */
  ft_install(out, t, r);
  return DS_OK;
}

ds_status_t ds_ftree_push_front(const ds_ftree_t *t, const void *elem,
                                ds_ftree_t *out) {
  return ft_push(t, elem, out, true);
}

ds_status_t ds_ftree_push_back(const ds_ftree_t *t, const void *elem,
                               ds_ftree_t *out) {
  return ft_push(t, elem, out, false);
}

static ds_status_t ft_pop(const ds_ftree_t *t, void *out_elem, ds_ftree_t *out,
                          bool front) {
  cfg_t *c;
  fi_t *x;
  ft_t *r;
  bool ok;
  if (t == NULL || out == NULL || t->config == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root == NULL) {
    return DS_EMPTY;
  }
  c = t->config;
  if (out_elem != NULL && c->destroy_elem != NULL && c->copy_elem == NULL) {
    return DS_ERR_UNSUPPORTED; /* an owned element cannot be byte-copied */
  }
  ok = front ? ft_view_l(c, t->root, &x, &r) : ft_view_r(c, t->root, &r, &x);
  if (!ok) {
    return DS_ERR_ALLOC;
  }
  if (out_elem != NULL) {
    /* other versions may still hold the element: hand out a clone, or
     * plain bytes when nothing is owned */
    bool copied = true;
    if (c->copy_elem != NULL) {
      copied = c->copy_elem(out_elem, fi_e(c, x), c->user);
    } else {
      memcpy(out_elem, fi_e(c, x), c->elem_size);
    }
    if (!copied) {
      fi_release(c, x);
      ft_release(c, r);
      return DS_ERR_ALLOC;
    }
  }
  fi_release(c, x);
  ft_install(out, t, r);
  return DS_OK;
}

ds_status_t ds_ftree_pop_front(const ds_ftree_t *t, void *out_elem,
                               ds_ftree_t *out) {
  return ft_pop(t, out_elem, out, true);
}

ds_status_t ds_ftree_pop_back(const ds_ftree_t *t, void *out_elem,
                              ds_ftree_t *out) {
  return ft_pop(t, out_elem, out, false);
}

ds_status_t ds_ftree_concat(const ds_ftree_t *a, const ds_ftree_t *b,
                            ds_ftree_t *out) {
  ft_t *r, *ra, *rb;
  if (a == NULL || b == NULL || out == NULL || a->config == NULL) {
    return DS_ERR_NULL;
  }
  if (a->config != b->config) {
    return DS_ERR_INVALID;
  }
  ra = a->root;
  rb = b->root;
  if (!ft_app3(a->config, ra, NULL, 0, rb, &r)) {
    return DS_ERR_ALLOC;
  }
  if (out == a || out == b) {
    ft_release(a->config, out->root); /* the handle's old reference */
  }
  out->root = r;
  out->config = a->config;
  return DS_OK;
}

static unsigned char *ft_scratch(cfg_t *c) {
  size_t bytes;
  if (!ds_size_mul(3 * FT_LEVELS, c->measure_size == 0 ? 1 : c->measure_size,
                   &bytes)) {
    return NULL;
  }
  return (unsigned char *)ds_context_alloc(c->context, bytes);
}

ds_status_t ds_ftree_split(const ds_ftree_t *t, ds_ftree_pred_t pred,
                           void *user, ds_ftree_t *out_left,
                           ds_ftree_t *out_right) {
  cfg_t *c;
  ft_split_ctx_t s;
  ft_t *l, *r, *rr, *root;
  fi_t *x;
  bool ok;
  if (t == NULL || pred == NULL || out_left == NULL || out_right == NULL ||
      t->config == NULL) {
    return DS_ERR_NULL;
  }
  if (out_left == out_right) {
    return DS_ERR_INVALID;
  }
  c = t->config;
  root = t->root;
  if (root == NULL || !pred(ft_m(c, root), user)) {
    if (root != NULL) {
      root->refs++;
    }
    if (out_left == t || out_right == t) {
      ft_release(c, root);
    }
    out_left->root = root;
    out_left->config = c;
    out_right->root = NULL;
    out_right->config = c;
    return DS_OK;
  }
  s.c = c;
  s.pred = pred;
  s.user = user;
  s.scratch = ft_scratch(c);
  if (s.scratch == NULL) {
    return DS_ERR_ALLOC;
  }
  ok = ft_split_tree(&s, (const unsigned char *)c->identity, root, 0, &l, &x,
                     &r);
  ds_context_free(c->context, s.scratch);
  if (!ok) {
    return DS_ERR_ALLOC;
  }
  ok = ft_push_front(c, r, x, &rr);
  fi_release(c, x);
  ft_release(c, r);
  if (!ok) {
    ft_release(c, l);
    return DS_ERR_ALLOC;
  }
  if (out_left == t || out_right == t) {
    ft_release(c, root);
  }
  out_left->root = l;
  out_left->config = c;
  out_right->root = rr;
  out_right->config = c;
  return DS_OK;
}

ds_status_t ds_ftree_lookup_status(const ds_ftree_t *t, ds_ftree_pred_t pred,
                                   void *user, const void **out_elem) {
  cfg_t *c;
  ft_split_ctx_t s;
  fi_t *x;
  unsigned char *acc;
  if (t == NULL || pred == NULL || out_elem == NULL || t->config == NULL) {
    return DS_ERR_NULL;
  }
  *out_elem = NULL;
  c = t->config;
  if (t->root == NULL || !pred(ft_m(c, t->root), user)) {
    return DS_NOT_FOUND;
  }
  s.c = c;
  s.pred = pred;
  s.user = user;
  s.scratch = ft_scratch(c);
  acc = (unsigned char *)ds_context_alloc(
      c->context, c->measure_size == 0 ? 1 : c->measure_size);
  if (s.scratch == NULL || acc == NULL) {
    ds_context_free(c->context, s.scratch);
    ds_context_free(c->context, acc);
    return DS_ERR_ALLOC;
  }
  memcpy(acc, c->identity, c->measure_size);
  x = ft_lookup_tree(&s, acc, t->root, 0);
  ds_context_free(c->context, s.scratch);
  ds_context_free(c->context, acc);
  if (x == NULL) {
    return DS_ERR_INTERNAL; /* deeper than FT_LEVELS: not reachable */
  }
  *out_elem = fi_e(c, x);
  return DS_OK;
}

const void *ds_ftree_lookup(const ds_ftree_t *t, ds_ftree_pred_t pred,
                            void *user) {
  const void *e = NULL;
  return ds_ftree_lookup_status(t, pred, user, &e) == DS_OK ? e : NULL;
}

static bool ft_visit_item(cfg_t *c, fi_t *x, ds_ftree_visit_t visit,
                          void *user) {
  int i;
  if (x->kind == 0) {
    return visit(fi_e(c, x), user);
  }
  for (i = 0; i < x->kind; i++) {
    if (!ft_visit_item(c, x->c[i], visit, user)) {
      return false;
    }
  }
  return true;
}

static bool ft_visit_tree(cfg_t *c, ft_t *t, ds_ftree_visit_t visit,
                          void *user) {
  size_t i;
  if (t == NULL) {
    return true;
  }
  for (i = 0; i < t->npr; i++) {
    if (!ft_visit_item(c, t->pr[i], visit, user)) {
      return false;
    }
  }
  if (!ft_visit_tree(c, t->mid, visit, user)) {
    return false;
  }
  for (i = 0; i < t->nsf; i++) {
    if (!ft_visit_item(c, t->sf[i], visit, user)) {
      return false;
    }
  }
  return true;
}

ds_status_t ds_ftree_visit(const ds_ftree_t *t, ds_ftree_visit_t visit,
                           void *user) {
  if (t == NULL || visit == NULL || t->config == NULL) {
    return DS_ERR_NULL;
  }
  return ft_visit_tree(t->config, t->root, visit, user) ? DS_OK : DS_STOP;
}
