#include "interval_tree.h"
#include <string.h>

/* Entry key layout: [low][high][id], with id aligned for unsigned long. */

typedef struct it_query {
  const ds_interval_tree_t *tree;
  const void *low;
  const void *high;
  ds_interval_visit_func_t visit;
  void *user;
} itq_t;

static int itv_cmp(const ds_interval_tree_t *t, const void *a, const void *b) {
  return t->compare(a, b, t->user);
}

static int itv_key_compare(const void *a, const void *b, void *user) {
  const ds_interval_tree_t *t = (const ds_interval_tree_t *)user;
  const unsigned char *x = (const unsigned char *)a;
  const unsigned char *y = (const unsigned char *)b;
  unsigned long ix;
  unsigned long iy;
  int c = itv_cmp(t, x, y);
  if (c != 0) {
    return c;
  }
  c = itv_cmp(t, x + t->endpoint_size, y + t->endpoint_size);
  if (c != 0) {
    return c;
  }
  memcpy(&ix, x + t->id_offset, sizeof(ix));
  memcpy(&iy, y + t->id_offset, sizeof(iy));
  return (ix > iy) - (ix < iy);
}

static void itv_summarize(void *out, const void *key, const void *value,
                          void *user) {
  const ds_interval_tree_t *t = (const ds_interval_tree_t *)user;
  (void)value;
  memcpy(out, (const unsigned char *)key + t->endpoint_size, t->endpoint_size);
}

static void itv_combine(void *out, const void *left, const void *right,
                        void *user) {
  const ds_interval_tree_t *t = (const ds_interval_tree_t *)user;
  memcpy(out, itv_cmp(t, left, right) >= 0 ? left : right, t->endpoint_size);
}

static size_t itv_round(size_t n, size_t a) { return (n + a - 1) / a * a; }

ds_status_t ds_interval_tree_init(ds_interval_tree_t *tree,
                                  size_t endpoint_size, size_t value_size,
                                  int (*compare)(const void *a, const void *b,
                                                 void *user),
                                  void *user, ds_context_t *context) {
  ds_aug_treap_config_t config;
  ds_status_t status;
  if (tree == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (endpoint_size == 0) {
    return DS_ERR_INVALID;
  }
  if (endpoint_size > ((size_t)-1 - 2 * sizeof(unsigned long)) / 2) {
    return DS_ERR_OVERFLOW; /* 2 endpoints + padding + id must fit */
  }
  tree->endpoint_size = endpoint_size;
  tree->id_offset = itv_round(2 * endpoint_size, sizeof(unsigned long));
  tree->compare = compare;
  tree->user = user;
  tree->next_id = 1;
  ds_aug_treap_config_init(&config);
  config.key_size = tree->id_offset + sizeof(unsigned long);
  config.value_size = value_size;
  config.compare = itv_key_compare;
  config.summary_size = endpoint_size;
  config.summarize = itv_summarize;
  config.combine = itv_combine;
  config.user = tree;
  config.context = context;
  status = ds_aug_treap_init(&tree->treap, &config);
  if (status != DS_OK) {
    return status;
  }
  tree->scratch = (unsigned char *)ds_context_alloc(tree->treap.config.context,
                                                    config.key_size);
  if (tree->scratch == NULL) {
    ds_aug_treap_destroy(&tree->treap);
    return DS_ERR_ALLOC;
  }
  return DS_OK;
}

void ds_interval_tree_destroy(ds_interval_tree_t *tree) {
  if (tree == NULL) {
    return;
  }
  ds_context_free(tree->treap.config.context, tree->scratch);
  tree->scratch = NULL;
  ds_aug_treap_destroy(&tree->treap);
}

static void itv_make_key(ds_interval_tree_t *t, const void *low,
                         const void *high, ds_interval_id_t id) {
  memcpy(t->scratch, low, t->endpoint_size);
  memcpy(t->scratch + t->endpoint_size, high, t->endpoint_size);
  memcpy(t->scratch + t->id_offset, &id, sizeof(id));
}

ds_status_t ds_interval_tree_insert(ds_interval_tree_t *tree, const void *low,
                                    const void *high, const void *value,
                                    ds_interval_id_t *out_id) {
  ds_status_t status;
  ds_interval_id_t id;
  if (low == NULL || high == NULL) {
    return DS_ERR_NULL;
  }
  if (itv_cmp(tree, low, high) > 0) {
    return DS_ERR_INVALID; /* low must not exceed high */
  }
  id = tree->next_id++;
  itv_make_key(tree, low, high, id);
  status = ds_aug_treap_insert(&tree->treap, tree->scratch, value);
  if (status == DS_OK && out_id != NULL) {
    *out_id = id;
  }
  return status;
}

ds_status_t ds_interval_tree_remove(ds_interval_tree_t *tree, const void *low,
                                    const void *high, ds_interval_id_t id,
                                    void *out_value) {
  if (low == NULL || high == NULL) {
    return DS_ERR_NULL;
  }
  itv_make_key(tree, low, high, id);
  return ds_aug_treap_remove(&tree->treap, tree->scratch, out_value);
}

static int itv_descend(const void *key, void *value, const void *left_max,
                       const void *right_max, void *user) {
  const itq_t *q = (const itq_t *)user;
  const ds_interval_tree_t *t = q->tree;
  const unsigned char *k = (const unsigned char *)key;
  const unsigned char *k_high = k + t->endpoint_size;
  ds_interval_id_t id;
  int flags = 0;
  /* Left subtree: worth entering only if something there ends at or after
   * the query's low end. */
  if (left_max != NULL && itv_cmp(t, left_max, q->low) >= 0) {
    flags |= DS_AUG_LEFT;
  }
  /* This interval overlaps when low <= q.high and high >= q.low. */
  if (itv_cmp(t, k, q->high) <= 0 && itv_cmp(t, k_high, q->low) >= 0) {
    memcpy(&id, k + t->id_offset, sizeof(id));
    if (!q->visit(k, k_high, value, id, q->user)) {
      return DS_AUG_STOP;
    }
  }
  /* Right subtree: its lows are >= this low, and it must reach q.low. */
  if (right_max != NULL && itv_cmp(t, k, q->high) <= 0 &&
      itv_cmp(t, right_max, q->low) >= 0) {
    flags |= DS_AUG_RIGHT;
  }
  return flags;
}

ds_status_t ds_interval_tree_overlaps(const ds_interval_tree_t *tree,
                                      const void *low, const void *high,
                                      ds_interval_visit_func_t visit,
                                      void *user) {
  itq_t q;
  if (tree == NULL || low == NULL || high == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  q.tree = tree;
  q.low = low;
  q.high = high;
  q.visit = visit;
  q.user = user;
  return ds_aug_treap_descend(&tree->treap, itv_descend, &q);
}

ds_status_t ds_interval_tree_stab(const ds_interval_tree_t *tree,
                                  const void *point,
                                  ds_interval_visit_func_t visit, void *user) {
  return ds_interval_tree_overlaps(tree, point, point, visit, user);
}

size_t ds_interval_tree_size(const ds_interval_tree_t *tree) {
  return ds_aug_treap_size(&tree->treap);
}
