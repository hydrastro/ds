#include "link_cut_tree.h"
#include "checked.h"
#include <string.h>

#define LN DS_LCT_NONE

static unsigned char *lc_elem(ds_lct_t *t, size_t x) {
  return t->elems + x * t->config.elem_size;
}
static unsigned char *lc_sum(ds_lct_t *t, size_t x) {
  return t->sums + x * t->config.summary_size;
}
static unsigned char *lc_rsum(ds_lct_t *t, size_t x) {
  return t->rsums + x * t->config.summary_size;
}
static unsigned char *lc_tag(ds_lct_t *t, size_t x) {
  return t->tags + x * t->config.tag_size;
}

static bool lc_is_root(const ds_lct_t *t, size_t x) {
  size_t p = t->parent[x];
  return p == LN || (t->left[p] != x && t->right[p] != x);
}

static void lc_swap_bytes(unsigned char *a, unsigned char *b, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    unsigned char c = a[i];
    a[i] = b[i];
    b[i] = c;
  }
}

/* reverse x's whole splay subtree (lazily below x) */
static void lc_flip(ds_lct_t *t, size_t x) {
  size_t c;
  if (x == LN) {
    return;
  }
  c = t->left[x];
  t->left[x] = t->right[x];
  t->right[x] = c;
  if (t->config.summary_size > 0) {
    lc_swap_bytes(lc_sum(t, x), lc_rsum(t, x), t->config.summary_size);
  }
  t->rev[x] ^= 1;
}

static void lc_apply(ds_lct_t *t, size_t x, const void *tag) {
  if (x == LN) {
    return;
  }
  t->config.tag_apply_elem(lc_elem(t, x), tag, t->config.user);
  if (t->config.summary_size > 0) {
    t->config.tag_apply_summary(lc_sum(t, x), tag, t->size[x], t->config.user);
    t->config.tag_apply_summary(lc_rsum(t, x), tag, t->size[x], t->config.user);
  }
  if (t->tagged[x]) {
    t->config.tag_compose(lc_tag(t, x), tag, t->config.user);
  } else {
    memcpy(lc_tag(t, x), tag, t->config.tag_size);
    t->tagged[x] = 1;
  }
}

static void lc_push(ds_lct_t *t, size_t x) {
  if (t->rev[x]) {
    lc_flip(t, t->left[x]);
    lc_flip(t, t->right[x]);
    t->rev[x] = 0;
  }
  if (t->tagged[x]) {
    lc_apply(t, t->left[x], lc_tag(t, x));
    lc_apply(t, t->right[x], lc_tag(t, x));
    t->tagged[x] = 0;
  }
}

static void lc_pull(ds_lct_t *t, size_t x) {
  size_t l = t->left[x];
  size_t r = t->right[x];
  size_t s = t->config.summary_size;
  t->size[x] = 1 + (l == LN ? 0 : t->size[l]) + (r == LN ? 0 : t->size[r]);
  if (s > 0) {
    unsigned char *mid = t->scratch;
    unsigned char *tmp = t->scratch + s;
    t->config.summarize(mid, lc_elem(t, x), t->config.user);
    /* forward: left, x, right */
    if (l != LN) {
      t->config.combine(tmp, lc_sum(t, l), mid, t->config.user);
    } else {
      memcpy(tmp, mid, s);
    }
    if (r != LN) {
      t->config.combine(lc_sum(t, x), tmp, lc_sum(t, r), t->config.user);
    } else {
      memcpy(lc_sum(t, x), tmp, s);
    }
    /* reversed: right reversed, x, left reversed */
    if (r != LN) {
      t->config.combine(tmp, lc_rsum(t, r), mid, t->config.user);
    } else {
      memcpy(tmp, mid, s);
    }
    if (l != LN) {
      t->config.combine(lc_rsum(t, x), tmp, lc_rsum(t, l), t->config.user);
    } else {
      memcpy(lc_rsum(t, x), tmp, s);
    }
  }
}

static void lc_rotate(ds_lct_t *t, size_t x) {
  size_t p = t->parent[x];
  size_t g = t->parent[p];
  bool p_root = lc_is_root(t, p);
  if (t->left[p] == x) {
    t->left[p] = t->right[x];
    if (t->right[x] != LN) {
      t->parent[t->right[x]] = p;
    }
    t->right[x] = p;
  } else {
    t->right[p] = t->left[x];
    if (t->left[x] != LN) {
      t->parent[t->left[x]] = p;
    }
    t->left[x] = p;
  }
  t->parent[p] = x;
  t->parent[x] = g;
  if (!p_root) {
    if (t->left[g] == p) {
      t->left[g] = x;
    } else {
      t->right[g] = x;
    }
  }
  lc_pull(t, p);
  lc_pull(t, x);
}

static void lc_splay(ds_lct_t *t, size_t x) {
  size_t top = 0;
  size_t y = x;
  /* push pending work down from the splay root */
  t->stack[top++] = y;
  while (!lc_is_root(t, y)) {
    y = t->parent[y];
    t->stack[top++] = y;
  }
  while (top > 0) {
    lc_push(t, t->stack[--top]);
  }
  while (!lc_is_root(t, x)) {
    size_t p = t->parent[x];
    if (!lc_is_root(t, p)) {
      size_t g = t->parent[p];
      bool zigzig = (t->left[g] == p) == (t->left[p] == x);
      lc_rotate(t, zigzig ? p : x);
    }
    lc_rotate(t, x);
  }
}

/* make the root..x path preferred; returns the last splay root met */
static size_t lc_access(ds_lct_t *t, size_t x) {
  size_t last = LN;
  size_t y;
  for (y = x; y != LN; y = t->parent[y]) {
    lc_splay(t, y);
    t->right[y] = last;
    lc_pull(t, y);
    last = y;
  }
  lc_splay(t, x);
  return last;
}

static void lc_make_root(ds_lct_t *t, size_t x) {
  (void)lc_access(t, x);
  lc_flip(t, x);
}

static size_t lc_find_root(ds_lct_t *t, size_t x) {
  (void)lc_access(t, x);
  lc_push(t, x);
  while (t->left[x] != LN) {
    x = t->left[x];
    lc_push(t, x);
  }
  lc_splay(t, x);
  return x;
}

void ds_lct_config_init(ds_lct_config_t *config) {
  if (config != NULL) {
    memset(config, 0, sizeof(*config));
  }
}

ds_status_t ds_lct_init(ds_lct_t *t, size_t n, const void *elems,
                        const ds_lct_config_t *config) {
  ds_lct_config_t c;
  ds_context_t *ctx;
  size_t es;
  size_t ss;
  size_t ts;
  size_t i;
  size_t b_idx, b_flags, b_elems, b_sums, b_tags, b_scratch;
  if (t == NULL || config == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  c = *config;
  if (n == 0) {
    return DS_ERR_INVALID;
  }
  if (c.summary_size > 0 && (c.summarize == NULL || c.combine == NULL)) {
    return DS_ERR_NULL;
  }
  if (c.tag_size > 0 && (c.tag_apply_elem == NULL || c.tag_compose == NULL ||
                         (c.summary_size > 0 && c.tag_apply_summary == NULL))) {
    return DS_ERR_NULL;
  }
  es = c.elem_size;
  ss = c.summary_size;
  ts = c.tag_size;
  /* every allocation size, checked: 5n indices, 2n flags, n elements,
   * 2n summaries (forward and reversed), n tags, 3 scratch summaries */
  if (!ds_size_mul(5 * sizeof(size_t), n, &b_idx) ||
      !ds_size_mul(2, n, &b_flags) || !ds_size_mul_add(n, es, 1, &b_elems) ||
      !ds_size_mul(2, n, &b_sums) || !ds_size_mul_add(b_sums, ss, 1, &b_sums) ||
      !ds_size_mul_add(n, ts, 1, &b_tags) ||
      !ds_size_mul_add(3, ss, 1, &b_scratch)) {
    return DS_ERR_OVERFLOW;
  }
  ctx = c.context;
  t->config = c;
  t->n = n;
  t->left = (size_t *)ds_context_alloc(ctx, b_idx);
  t->rev = (unsigned char *)ds_context_calloc(ctx, b_flags, 1);
  t->elems = (unsigned char *)ds_context_calloc(ctx, b_elems, 1);
  t->sums = (unsigned char *)ds_context_alloc(ctx, b_sums);
  t->tags = (unsigned char *)ds_context_alloc(ctx, b_tags);
  t->scratch = (unsigned char *)ds_context_alloc(ctx, b_scratch);
  if (t->left == NULL || t->rev == NULL || t->elems == NULL ||
      t->sums == NULL || t->tags == NULL || t->scratch == NULL) {
    ds_lct_destroy(t);
    return DS_ERR_ALLOC;
  }
  t->right = t->left + n;
  t->parent = t->right + n;
  t->size = t->parent + n;
  t->stack = t->size + n;
  t->tagged = t->rev + n;
  t->rsums = t->sums + n * ss;
  if (elems != NULL && es > 0) {
    memcpy(t->elems, elems, n * es);
  }
  for (i = 0; i < n; i++) {
    t->left[i] = t->right[i] = t->parent[i] = LN;
    lc_pull(t, i);
  }
  return DS_OK;
}

void ds_lct_destroy(ds_lct_t *t) {
  ds_context_t *ctx;
  if (t == NULL) {
    return;
  }
  ctx = t->config.context;
  ds_context_free(ctx, t->left);
  ds_context_free(ctx, t->rev);
  ds_context_free(ctx, t->elems);
  ds_context_free(ctx, t->sums);
  ds_context_free(ctx, t->tags);
  ds_context_free(ctx, t->scratch);
  memset(t, 0, sizeof(*t));
}

static bool lc_ok(const ds_lct_t *t, size_t v) { return t != NULL && v < t->n; }

ds_status_t ds_lct_link(ds_lct_t *t, size_t u, size_t v) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (!lc_ok(t, u) || !lc_ok(t, v)) {
    return DS_ERR_RANGE;
  }
  lc_make_root(t, u);
  if (lc_find_root(t, v) == u) {
    return DS_ERR_STATE;
  }
  t->parent[u] = v;
  return DS_OK;
}

ds_status_t ds_lct_cut(ds_lct_t *t, size_t u, size_t v) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (!lc_ok(t, u) || !lc_ok(t, v)) {
    return DS_ERR_RANGE;
  }
  lc_make_root(t, u);
  (void)lc_access(t, v);
  /* edge u-v iff u is v's predecessor on the root path, adjacent */
  if (t->left[v] != u) {
    return DS_ERR_STATE;
  }
  lc_push(t, u);
  if (t->right[u] != LN) {
    return DS_ERR_STATE;
  }
  t->left[v] = LN;
  t->parent[u] = LN;
  lc_pull(t, v);
  return DS_OK;
}

bool ds_lct_connected(ds_lct_t *t, size_t u, size_t v) {
  if (!lc_ok(t, u) || !lc_ok(t, v)) {
    return false;
  }
  return u == v || lc_find_root(t, u) == lc_find_root(t, v);
}

ds_status_t ds_lct_reroot(ds_lct_t *t, size_t r) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (!lc_ok(t, r)) {
    return DS_ERR_RANGE;
  }
  lc_make_root(t, r);
  return DS_OK;
}

size_t ds_lct_find_root(ds_lct_t *t, size_t v) {
  return lc_ok(t, v) ? lc_find_root(t, v) : LN;
}

size_t ds_lct_lca(ds_lct_t *t, size_t u, size_t v) {
  if (!lc_ok(t, u) || !lc_ok(t, v) || !ds_lct_connected(t, u, v)) {
    return LN;
  }
  (void)lc_access(t, u);
  return lc_access(t, v);
}

static ds_status_t lc_expose_path(ds_lct_t *t, size_t u, size_t v) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (!lc_ok(t, u) || !lc_ok(t, v)) {
    return DS_ERR_RANGE;
  }
  if (!ds_lct_connected(t, u, v)) {
    return DS_ERR_STATE;
  }
  lc_make_root(t, u);
  (void)lc_access(t, v);
  return DS_OK;
}

ds_status_t ds_lct_path_query(ds_lct_t *t, size_t u, size_t v,
                              void *out_summary) {
  ds_status_t st;
  if (t == NULL || out_summary == NULL) {
    return DS_ERR_NULL;
  }
  if (t->config.summary_size == 0) {
    return DS_ERR_UNSUPPORTED;
  }
  st = lc_expose_path(t, u, v);
  if (st == DS_OK) {
    memcpy(out_summary, lc_sum(t, v), t->config.summary_size);
  }
  return st;
}

ds_status_t ds_lct_path_apply(ds_lct_t *t, size_t u, size_t v,
                              const void *tag) {
  ds_status_t st;
  if (t == NULL || tag == NULL) {
    return DS_ERR_NULL;
  }
  if (t->config.tag_size == 0) {
    return DS_ERR_UNSUPPORTED;
  }
  st = lc_expose_path(t, u, v);
  if (st == DS_OK) {
    lc_apply(t, v, tag);
  }
  return st;
}

size_t ds_lct_path_length(ds_lct_t *t, size_t u, size_t v) {
  if (lc_expose_path(t, u, v) != DS_OK) {
    return 0;
  }
  return t->size[v];
}

ds_status_t ds_lct_get(ds_lct_t *t, size_t v, void *out_elem) {
  if (t == NULL || out_elem == NULL) {
    return DS_ERR_NULL;
  }
  if (!lc_ok(t, v)) {
    return DS_ERR_RANGE;
  }
  (void)lc_access(t, v); /* pushes every pending tag above v */
  lc_push(t, v);
  memcpy(out_elem, lc_elem(t, v), t->config.elem_size);
  return DS_OK;
}

ds_status_t ds_lct_set(ds_lct_t *t, size_t v, const void *elem) {
  if (t == NULL || (t->config.elem_size > 0 && elem == NULL)) {
    return DS_ERR_NULL;
  }
  if (!lc_ok(t, v)) {
    return DS_ERR_RANGE;
  }
  (void)lc_access(t, v);
  memcpy(lc_elem(t, v), elem, t->config.elem_size);
  lc_pull(t, v);
  return DS_OK;
}
