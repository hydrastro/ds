#include <dsa/core/checked.h>
#include <dsa/structures/aug_treap.h>
#include <dsa/structures/vec.h>
#include <string.h>

typedef union aug_align {
  long l;
  double d;
  void *p;
  long double ld;
} aug_align_t;

struct ds_aug_node {
  ds_aug_node_t *left;
  ds_aug_node_t *right;
  ds_aug_node_t *parent;
  size_t count;
  unsigned long priority;
};

#define AUG_AT(n, off) ((unsigned char *)(n) + (off))
#define AUG_CAT(n, off) ((const unsigned char *)(n) + (off))

static size_t aug_count(const ds_aug_node_t *n) {
  return n == NULL ? 0 : n->count;
}

static int aug_cmp(const ds_aug_treap_t *t, const void *a, const void *b) {
  return t->config.compare(a, b, t->config.user);
}

static const void *aug_key(const ds_aug_treap_t *t, const ds_aug_node_t *n) {
  return AUG_CAT(n, t->key_offset);
}

static const void *aug_sum(const ds_aug_treap_t *t, const ds_aug_node_t *n) {
  return n == NULL ? NULL : AUG_CAT(n, t->sum_offset);
}

static void aug_pull(ds_aug_treap_t *t, ds_aug_node_t *n) {
  size_t ss = t->config.summary_size;
  unsigned char *single = t->scratch;
  unsigned char *partial = t->scratch + ss;
  void *user = t->config.user;
  n->count = 1 + aug_count(n->left) + aug_count(n->right);
  if (ss == 0) {
    return;
  }
  t->config.summarize(single, AUG_AT(n, t->key_offset),
                      AUG_AT(n, t->value_offset), user);
  if (n->left != NULL) {
    t->config.combine(partial, aug_sum(t, n->left), single, user);
  } else {
    memcpy(partial, single, ss);
  }
  if (n->right != NULL) {
    t->config.combine(AUG_AT(n, t->sum_offset), partial, aug_sum(t, n->right),
                      user);
  } else {
    memcpy(AUG_AT(n, t->sum_offset), partial, ss);
  }
}

/* Iterative split/join; repair summaries along the changed parent spines. */
static void aug_split(ds_aug_treap_t *t, ds_aug_node_t *n, const void *key,
                      ds_aug_node_t **a, ds_aug_node_t **b) {
  ds_aug_node_t *left = NULL, *right = NULL, **ls = a, **rs = b;
  while (n != NULL) {
    ds_aug_node_t *next;
    if (aug_cmp(t, aug_key(t, n), key) < 0) {
      next = n->right;
      *ls = n;
      n->parent = left;
      left = n;
      ls = &n->right;
    } else {
      next = n->left;
      *rs = n;
      n->parent = right;
      right = n;
      rs = &n->left;
    }
    n = next;
  }
  *ls = *rs = NULL;
  for (; left != NULL; left = left->parent) {
    aug_pull(t, left);
  }
  for (; right != NULL; right = right->parent) {
    aug_pull(t, right);
  }
}

static ds_aug_node_t *aug_merge(ds_aug_treap_t *t, ds_aug_node_t *a,
                                ds_aug_node_t *b) {
  ds_aug_node_t *root = NULL, *tail = NULL, **slot = &root;
  while (a != NULL && b != NULL) {
    ds_aug_node_t *chosen;
    if (a->priority > b->priority) {
      chosen = a;
      a = a->right;
      *slot = chosen;
      slot = &chosen->right;
    } else {
      chosen = b;
      b = b->left;
      *slot = chosen;
      slot = &chosen->left;
    }
    chosen->parent = tail;
    tail = chosen;
  }
  *slot = a == NULL ? b : a;
  if (*slot != NULL) {
    (*slot)->parent = tail;
  }
  for (; tail != NULL; tail = tail->parent) {
    aug_pull(t, tail);
  }
  return root;
}

static ds_aug_node_t *aug_find(const ds_aug_treap_t *t, const void *key) {
  ds_aug_node_t *n = t->root;
  int c;
  while (n != NULL) {
    c = aug_cmp(t, key, aug_key(t, n));
    if (c == 0) {
      return n;
    }
    n = c < 0 ? n->left : n->right;
  }
  return NULL;
}

static void aug_pull_path(ds_aug_treap_t *t, ds_aug_node_t *n) {
  for (; n != NULL; n = n->parent) {
    aug_pull(t, n);
  }
}

static void aug_free(ds_aug_treap_t *t, ds_aug_node_t *n) {
  while (n != NULL) {
    if (n->left != NULL) {
      ds_aug_node_t *left = n->left;
      n->left = left->right;
      left->right = n;
      n = left;
    } else {
      ds_aug_node_t *right = n->right;
      ds_context_free(t->config.context, n);
      n = right;
    }
  }
}

static unsigned long aug_random(ds_aug_treap_t *t) {
  unsigned long x = t->seed;
  x ^= (x << 13) & 0xFFFFFFFFUL;
  x ^= x >> 17;
  x ^= (x << 5) & 0xFFFFFFFFUL;
  t->seed = x & 0xFFFFFFFFUL;
  return t->seed;
}

void ds_aug_treap_config_init(ds_aug_treap_config_t *config) {
  memset(config, 0, sizeof(*config));
}

ds_status_t ds_aug_treap_init(ds_aug_treap_t *treap,
                              const ds_aug_treap_config_t *config) {
  if (treap == NULL || config == NULL || config->compare == NULL) {
    return DS_ERR_NULL;
  }
  if (config->key_size == 0) {
    return DS_ERR_INVALID;
  }
  if (config->summary_size > 0 &&
      (config->summarize == NULL || config->combine == NULL)) {
    return DS_ERR_NULL;
  }
  {
    /* validate the whole layout before keeping anything */
    size_t a = sizeof(aug_align_t);
    size_t header;
    size_t k;
    size_t v;
    size_t sm;
    size_t value_off;
    size_t sum_off;
    size_t bytes;
    size_t scratch;
    if (!ds_size_align(sizeof(ds_aug_node_t), a, &header) ||
        !ds_size_align(config->key_size, a, &k) ||
        !ds_size_align(config->value_size, a, &v) ||
        !ds_size_align(config->summary_size, a, &sm) ||
        !ds_size_add(header, k, &value_off) ||
        !ds_size_add(value_off, v, &sum_off) ||
        !ds_size_add(sum_off, sm, &bytes) ||
        !ds_size_mul(config->summary_size, 2, &scratch)) {
      return DS_ERR_OVERFLOW;
    }
    treap->config = *config;
    if (treap->config.context == NULL) {
      treap->config.context = ds_default_context();
    }
    treap->key_offset = header;
    treap->value_offset = value_off;
    treap->sum_offset = sum_off;
    treap->node_bytes = bytes;
  }
  treap->root = NULL;
  treap->seed = 0x6B43A9B5UL;
  treap->scratch = (unsigned char *)ds_context_alloc(
      treap->config.context,
      config->summary_size > 0 ? 2 * config->summary_size : 1);
  return treap->scratch == NULL ? DS_ERR_ALLOC : DS_OK;
}

void ds_aug_treap_destroy(ds_aug_treap_t *treap) {
  if (treap == NULL) {
    return;
  }
  aug_free(treap, treap->root);
  treap->root = NULL;
  ds_context_free(treap->config.context, treap->scratch);
  treap->scratch = NULL;
}

size_t ds_aug_treap_size(const ds_aug_treap_t *treap) {
  return aug_count(treap->root);
}

static ds_status_t aug_store(ds_aug_treap_t *t, const void *key,
                             const void *value, bool overwrite) {
  ds_aug_node_t *n;
  ds_aug_node_t *left;
  ds_aug_node_t *right;
  if (key == NULL || (value == NULL && t->config.value_size > 0)) {
    return DS_ERR_NULL;
  }
  n = aug_find(t, key);
  if (n != NULL) {
    if (overwrite && t->config.value_size > 0) {
      memmove(AUG_AT(n, t->value_offset), value, t->config.value_size);
      aug_pull_path(
          t, n); /* use the found node even if key aliased its old value */
    }
    return DS_EXISTS;
  }
  if (aug_count(t->root) == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  n = (ds_aug_node_t *)ds_context_alloc(t->config.context, t->node_bytes);
  if (n == NULL) {
    return DS_ERR_ALLOC;
  }
  n->left = NULL;
  n->right = NULL;
  n->parent = NULL;
  n->priority = aug_random(t);
  memcpy(AUG_AT(n, t->key_offset), key, t->config.key_size);
  if (t->config.value_size > 0) {
    memcpy(AUG_AT(n, t->value_offset), value, t->config.value_size);
  }
  aug_pull(t, n);
  aug_split(t, t->root, key, &left, &right);
  t->root = aug_merge(t, aug_merge(t, left, n), right);
  return DS_OK;
}

ds_status_t ds_aug_treap_insert(ds_aug_treap_t *treap, const void *key,
                                const void *value) {
  return aug_store(treap, key, value, false);
}

ds_status_t ds_aug_treap_put(ds_aug_treap_t *treap, const void *key,
                             const void *value) {
  return aug_store(treap, key, value, true);
}

ds_status_t ds_aug_treap_remove(ds_aug_treap_t *t, const void *key,
                                void *out_value) {
  ds_aug_node_t *found, *parent, *merged;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  found = aug_find(t, key);
  if (found == NULL) {
    return DS_NOT_FOUND;
  }
  parent = found->parent;
  if (out_value != NULL && t->config.value_size > 0) {
    memmove(out_value, AUG_AT(found, t->value_offset), t->config.value_size);
  }
  merged = aug_merge(t, found->left, found->right);
  if (parent == NULL) {
    t->root = merged;
  } else if (parent->left == found) {
    parent->left = merged;
  } else {
    parent->right = merged;
  }
  if (merged != NULL) {
    merged->parent = parent;
  }
  aug_pull_path(t, parent);
  ds_context_free(t->config.context, found);
  return DS_OK;
}

void *ds_aug_treap_get(const ds_aug_treap_t *treap, const void *key) {
  ds_aug_node_t *n = aug_find(treap, key);
  return n == NULL ? NULL : AUG_AT(n, treap->value_offset);
}

size_t ds_aug_treap_rank(const ds_aug_treap_t *treap, const void *key) {
  const ds_aug_node_t *n = treap->root;
  size_t rank = 0;
  while (n != NULL) {
    if (aug_cmp(treap, aug_key(treap, n), key) < 0) {
      rank += aug_count(n->left) + 1;
      n = n->right;
    } else {
      n = n->left;
    }
  }
  return rank;
}

ds_status_t ds_aug_treap_select(const ds_aug_treap_t *treap, size_t k,
                                const void **out_key, void **out_value) {
  ds_aug_node_t *n = treap->root;
  size_t lc;
  if (k >= aug_count(n)) {
    return DS_ERR_RANGE;
  }
  for (;;) {
    lc = aug_count(n->left);
    if (k < lc) {
      n = n->left;
    } else if (k == lc) {
      break;
    } else {
      k -= lc + 1;
      n = n->right;
    }
  }
  if (out_key != NULL) {
    *out_key = aug_key(treap, n);
  }
  if (out_value != NULL) {
    *out_value = AUG_AT(n, treap->value_offset);
  }
  return DS_OK;
}

const void *ds_aug_treap_summary(const ds_aug_treap_t *treap) {
  return treap->config.summary_size == 0 ? NULL : aug_sum(treap, treap->root);
}

/* Summary of keys in [low, high), combined left to right, read-only. */
typedef struct aug_acc {
  bool has;
  unsigned char *out;
} aug_acc_t;

static void aug_acc_add(const ds_aug_treap_t *t, aug_acc_t *acc,
                        const void *summary, unsigned char *tmp) {
  if (!acc->has) {
    memcpy(acc->out, summary, t->config.summary_size);
    acc->has = true;
  } else {
    t->config.combine(tmp, acc->out, summary, t->config.user);
    memcpy(acc->out, tmp, t->config.summary_size);
  }
}

static void aug_acc_prepend(const ds_aug_treap_t *t, aug_acc_t *acc,
                            const void *summary, unsigned char *tmp) {
  if (!acc->has) {
    memcpy(acc->out, summary, t->config.summary_size);
    acc->has = true;
  } else {
    t->config.combine(tmp, summary, acc->out, t->config.user);
    memcpy(acc->out, tmp, t->config.summary_size);
  }
}

/* Find one in-range pivot, then visit the two boundary paths. Whole subtree
 * summaries between them are combined in key order (not assumed commutative).
 */
static void aug_range(const ds_aug_treap_t *t, const ds_aug_node_t *n,
                      const void *low, const void *high, aug_acc_t *acc,
                      unsigned char *tmp, unsigned char *single) {
  const ds_aug_node_t *x;
  while (n != NULL) {
    if (aug_cmp(t, aug_key(t, n), low) < 0) {
      n = n->right;
    } else if (aug_cmp(t, aug_key(t, n), high) >= 0) {
      n = n->left;
    } else {
      break;
    }
  }
  if (n == NULL) {
    return;
  }
  x = n->left;
  while (x != NULL) {
    if (aug_cmp(t, aug_key(t, x), low) >= 0) {
      if (x->right != NULL) {
        aug_acc_prepend(t, acc, aug_sum(t, x->right), tmp);
      }
      t->config.summarize(single, aug_key(t, x), AUG_CAT(x, t->value_offset),
                          t->config.user);
      aug_acc_prepend(t, acc, single, tmp);
      x = x->left;
    } else {
      x = x->right;
    }
  }
  t->config.summarize(single, aug_key(t, n), AUG_CAT(n, t->value_offset),
                      t->config.user);
  aug_acc_add(t, acc, single, tmp);
  x = n->right;
  while (x != NULL) {
    if (aug_cmp(t, aug_key(t, x), high) < 0) {
      if (x->left != NULL) {
        aug_acc_add(t, acc, aug_sum(t, x->left), tmp);
      }
      t->config.summarize(single, aug_key(t, x), AUG_CAT(x, t->value_offset),
                          t->config.user);
      aug_acc_add(t, acc, single, tmp);
      x = x->right;
    } else {
      x = x->left;
    }
  }
}

ds_status_t ds_aug_treap_range_summary(const ds_aug_treap_t *treap,
                                       const void *low, const void *high,
                                       void *out_summary) {
  aug_acc_t acc;
  unsigned char *work;
  size_t ss = treap->config.summary_size;
  if (low == NULL || high == NULL || out_summary == NULL) {
    return DS_ERR_NULL;
  }
  if (ss == 0) {
    return DS_ERR_UNSUPPORTED;
  }
  work = (unsigned char *)ds_context_alloc(treap->config.context, 2 * ss);
  if (work == NULL) {
    return DS_ERR_ALLOC;
  }
  acc.has = false;
  acc.out = (unsigned char *)out_summary;
  aug_range(treap, treap->root, low, high, &acc, work, work + ss);
  ds_context_free(treap->config.context, work);
  return acc.has ? DS_OK : DS_EMPTY;
}

ds_status_t ds_aug_treap_descend(const ds_aug_treap_t *t,
                                 ds_aug_descend_func_t descend, void *user) {
  ds_aug_node_t *n;
  ds_vec_t pending;
  ds_status_t st;
  if (t == NULL || descend == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_vec_init(&pending, sizeof(n), t->config.context);
  n = t->root;
  while (st == DS_OK && (n != NULL || pending.size != 0)) {
    int flags;
    if (n == NULL) {
      (void)ds_vec_pop(&pending, &n);
    }
    flags = descend(aug_key(t, n), AUG_AT(n, t->value_offset),
                    t->config.summary_size > 0 ? aug_sum(t, n->left) : NULL,
                    t->config.summary_size > 0 ? aug_sum(t, n->right) : NULL,
                    user);
    if ((flags & DS_AUG_STOP) != 0) {
      st = DS_STOP;
      break;
    }
    if ((flags & DS_AUG_LEFT) != 0 && n->left != NULL) {
      if ((flags & DS_AUG_RIGHT) != 0 && n->right != NULL) {
        st = ds_vec_push(&pending, &n->right);
      }
      n = n->left;
    } else {
      n = (flags & DS_AUG_RIGHT) != 0 ? n->right : NULL;
    }
  }
  ds_vec_destroy(&pending);
  return st;
}

ds_status_t ds_aug_treap_visit(const ds_aug_treap_t *t,
                               ds_aug_visit_func_t visit, void *user) {
  ds_aug_node_t *n;
  if (t == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  n = t->root;
  if (n != NULL) {
    while (n->left != NULL) {
      n = n->left;
    }
  }
  while (n != NULL) {
    if (!visit(aug_key(t, n), AUG_AT(n, t->value_offset), user)) {
      return DS_STOP;
    }
    if (n->right != NULL) {
      n = n->right;
      while (n->left != NULL) {
        n = n->left;
      }
    } else {
      while (n->parent != NULL && n == n->parent->right) {
        n = n->parent;
      }
      n = n->parent;
    }
  }
  return DS_OK;
}

ds_status_t ds_aug_treap_refresh(ds_aug_treap_t *treap, const void *key) {
  if (treap == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  if (aug_find(treap, key) == NULL) {
    return DS_NOT_FOUND;
  }
  aug_pull_path(treap, aug_find(treap, key));
  return DS_OK;
}
