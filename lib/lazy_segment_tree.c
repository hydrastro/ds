#include "lazy_segment_tree.h"
#include <string.h>

#define LS_D(t, k) ((t)->d + (k) * (t)->config.summary_size)
#define LS_LZ(t, k) ((t)->lz + (k) * (t)->config.tag_size)

/* out = combine(a, b), out may alias neither */
static void ls_op(const ds_lazy_segtree_t *t, void *out, const void *a,
                  const void *b) {
  t->config.combine(out, a, b, t->config.user);
}

static void ls_update(ds_lazy_segtree_t *t, size_t k) {
  ls_op(t, LS_D(t, k), LS_D(t, 2 * k), LS_D(t, 2 * k + 1));
}

static void ls_all_apply(ds_lazy_segtree_t *t, size_t k, const void *tag) {
  if (t->cnt[k] == 0) {
    return; /* padding keeps the identity */
  }
  t->config.tag_apply_summary(LS_D(t, k), tag, t->cnt[k], t->config.user);
  if (k < t->size) {
    if (t->has[k]) {
      t->config.tag_compose(LS_LZ(t, k), tag, t->config.user);
    } else {
      memcpy(LS_LZ(t, k), tag, t->config.tag_size);
      t->has[k] = 1;
    }
  }
}

static void ls_push(ds_lazy_segtree_t *t, size_t k) {
  if (t->config.tag_size > 0 && t->has[k]) {
    ls_all_apply(t, 2 * k, LS_LZ(t, k));
    ls_all_apply(t, 2 * k + 1, LS_LZ(t, k));
    t->has[k] = 0;
  }
}

void ds_lazy_segtree_config_init(ds_lazy_segtree_config_t *config) {
  if (config != NULL) {
    memset(config, 0, sizeof(*config));
  }
}

ds_status_t ds_lazy_segtree_init(ds_lazy_segtree_t *t, size_t n,
                                 const void *values,
                                 const ds_lazy_segtree_config_t *config) {
  size_t s;
  size_t k;
  size_t ss;
  ds_context_t *ctx;
  if (t == NULL || config == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  ss = config->summary_size;
  if (ss == 0 || config->identity == NULL || config->combine == NULL) {
    return config->summary_size == 0 ? DS_ERR_INVALID : DS_ERR_NULL;
  }
  if (config->tag_size > 0 &&
      (config->tag_apply_summary == NULL || config->tag_compose == NULL)) {
    return DS_ERR_NULL;
  }
  s = 1;
  t->log = 0;
  while (s < n) {
    if (s > (size_t)-1 / 4 / (ss > sizeof(size_t) ? ss : sizeof(size_t))) {
      return DS_ERR_OVERFLOW;
    }
    s *= 2;
    t->log++;
  }
  t->config = *config;
  t->n = n;
  t->size = s;
  ctx = config->context;
  t->d = (unsigned char *)ds_context_alloc(ctx, 2 * s * ss);
  t->lz = (unsigned char *)ds_context_alloc(ctx, s * config->tag_size + 1);
  t->has = (unsigned char *)ds_context_calloc(ctx, s, 1);
  t->cnt = (size_t *)ds_context_calloc(ctx, 2 * s, sizeof(size_t));
  t->tmp = (unsigned char *)ds_context_alloc(ctx, 3 * ss);
  if (t->d == NULL || t->lz == NULL || t->has == NULL || t->cnt == NULL ||
      t->tmp == NULL) {
    ds_lazy_segtree_destroy(t);
    return DS_ERR_ALLOC;
  }
  for (k = 0; k < s; k++) {
    if (values != NULL && k < n) {
      memcpy(LS_D(t, s + k), (const unsigned char *)values + k * ss, ss);
    } else {
      memcpy(LS_D(t, s + k), config->identity, ss);
    }
    t->cnt[s + k] = k < n ? 1 : 0;
  }
  for (k = s; k-- > 1;) {
    t->cnt[k] = t->cnt[2 * k] + t->cnt[2 * k + 1];
    ls_update(t, k);
  }
  return DS_OK;
}

void ds_lazy_segtree_destroy(ds_lazy_segtree_t *t) {
  ds_context_t *ctx;
  if (t == NULL) {
    return;
  }
  ctx = t->config.context;
  ds_context_free(ctx, t->d);
  ds_context_free(ctx, t->lz);
  ds_context_free(ctx, t->has);
  ds_context_free(ctx, t->cnt);
  ds_context_free(ctx, t->tmp);
  memset(t, 0, sizeof(*t));
}

static void ls_push_path(ds_lazy_segtree_t *t, size_t p) {
  unsigned i;
  for (i = t->log; i >= 1; i--) {
    ls_push(t, p >> i);
  }
}

ds_status_t ds_lazy_segtree_set(ds_lazy_segtree_t *t, size_t i,
                                const void *value) {
  size_t p;
  unsigned j;
  if (t == NULL || value == NULL) {
    return DS_ERR_NULL;
  }
  if (i >= t->n) {
    return DS_ERR_RANGE;
  }
  p = i + t->size;
  ls_push_path(t, p);
  memmove(LS_D(t, p), value, t->config.summary_size);
  for (j = 1; j <= t->log; j++) {
    ls_update(t, p >> j);
  }
  return DS_OK;
}

ds_status_t ds_lazy_segtree_get(ds_lazy_segtree_t *t, size_t i, void *out) {
  size_t p;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (i >= t->n) {
    return DS_ERR_RANGE;
  }
  p = i + t->size;
  ls_push_path(t, p);
  memcpy(out, LS_D(t, p), t->config.summary_size);
  return DS_OK;
}

static void ls_push_bounds(ds_lazy_segtree_t *t, size_t l, size_t r) {
  unsigned i;
  for (i = t->log; i >= 1; i--) {
    if (((l >> i) << i) != l) {
      ls_push(t, l >> i);
    }
    if (((r >> i) << i) != r) {
      ls_push(t, (r - 1) >> i);
    }
  }
}

ds_status_t ds_lazy_segtree_query(ds_lazy_segtree_t *t, size_t first,
                                  size_t last, void *out) {
  size_t ss;
  unsigned char *sml;
  unsigned char *smr;
  unsigned char *x;
  size_t l;
  size_t r;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (first > last || last > t->n) {
    return DS_ERR_RANGE;
  }
  ss = t->config.summary_size;
  sml = t->tmp;
  smr = t->tmp + ss;
  x = t->tmp + 2 * ss;
  memcpy(sml, t->config.identity, ss);
  memcpy(smr, t->config.identity, ss);
  if (first < last) {
    l = first + t->size;
    r = last + t->size;
    ls_push_bounds(t, l, r);
    while (l < r) {
      if (l & 1) {
        ls_op(t, x, sml, LS_D(t, l));
        memcpy(sml, x, ss);
        l++;
      }
      if (r & 1) {
        --r;
        ls_op(t, x, LS_D(t, r), smr);
        memcpy(smr, x, ss);
      }
      l >>= 1;
      r >>= 1;
    }
  }
  ls_op(t, out, sml, smr);
  return DS_OK;
}

const void *ds_lazy_segtree_all(const ds_lazy_segtree_t *t) {
  return t == NULL ? NULL : LS_D(t, 1);
}

ds_status_t ds_lazy_segtree_apply(ds_lazy_segtree_t *t, size_t first,
                                  size_t last, const void *tag) {
  size_t l;
  size_t r;
  size_t l2;
  size_t r2;
  unsigned i;
  if (t == NULL || tag == NULL) {
    return DS_ERR_NULL;
  }
  if (t->config.tag_size == 0) {
    return DS_ERR_UNSUPPORTED;
  }
  if (first > last || last > t->n) {
    return DS_ERR_RANGE;
  }
  if (first == last) {
    return DS_OK;
  }
  l = first + t->size;
  r = last + t->size;
  ls_push_bounds(t, l, r);
  l2 = l;
  r2 = r;
  while (l < r) {
    if (l & 1) {
      ls_all_apply(t, l++, tag);
    }
    if (r & 1) {
      ls_all_apply(t, --r, tag);
    }
    l >>= 1;
    r >>= 1;
  }
  l = l2;
  r = r2;
  for (i = 1; i <= t->log; i++) {
    if (((l >> i) << i) != l) {
      ls_update(t, l >> i);
    }
    if (((r >> i) << i) != r) {
      ls_update(t, (r - 1) >> i);
    }
  }
  return DS_OK;
}

size_t ds_lazy_segtree_max_right(ds_lazy_segtree_t *t, size_t first,
                                 ds_lazy_segtree_pred_fn pred, void *user) {
  size_t ss;
  unsigned char *sm;
  unsigned char *x;
  size_t l;
  if (t == NULL || pred == NULL || first >= t->n) {
    return t == NULL ? 0 : t->n;
  }
  ss = t->config.summary_size;
  sm = t->tmp;
  x = t->tmp + ss;
  memcpy(sm, t->config.identity, ss);
  l = first + t->size;
  ls_push_path(t, l);
  do {
    while (l % 2 == 0) {
      l >>= 1;
    }
    ls_op(t, x, sm, LS_D(t, l));
    if (!pred(x, user)) {
      while (l < t->size) {
        ls_push(t, l);
        l = 2 * l;
        ls_op(t, x, sm, LS_D(t, l));
        if (pred(x, user)) {
          memcpy(sm, x, ss);
          l++;
        }
      }
      return l - t->size;
    }
    memcpy(sm, x, ss);
    l++;
  } while ((l & (~l + 1)) != l);
  return t->n;
}

size_t ds_lazy_segtree_min_left(ds_lazy_segtree_t *t, size_t last,
                                ds_lazy_segtree_pred_fn pred, void *user) {
  size_t ss;
  unsigned char *sm;
  unsigned char *x;
  size_t r;
  if (t == NULL || pred == NULL || last == 0) {
    return 0;
  }
  if (last > t->n) {
    last = t->n;
  }
  ss = t->config.summary_size;
  sm = t->tmp;
  x = t->tmp + ss;
  memcpy(sm, t->config.identity, ss);
  r = last + t->size;
  ls_push_path(t, r - 1);
  do {
    r--;
    while (r > 1 && (r % 2) != 0) {
      r >>= 1;
    }
    ls_op(t, x, LS_D(t, r), sm);
    if (!pred(x, user)) {
      while (r < t->size) {
        ls_push(t, r);
        r = 2 * r + 1;
        ls_op(t, x, LS_D(t, r), sm);
        if (pred(x, user)) {
          memcpy(sm, x, ss);
          r--;
        }
      }
      return r + 1 - t->size;
    }
    memcpy(sm, x, ss);
  } while ((r & (~r + 1)) != r);
  return 0;
}
