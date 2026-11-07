#include "implicit_treap.h"
#include <string.h>

typedef union it_align {
  long l;
  double d;
  void *p;
  long double ld;
} it_align_t;

struct ds_itreap_node {
  ds_itreap_node_t *left;
  ds_itreap_node_t *right;
  size_t count;
  unsigned long priority;
  unsigned char reversed; /* children still need swapping */
  unsigned char tagged;   /* children still need the tag */
};

#define IT_AT(n, off) ((unsigned char *)(n) + (off))

static size_t it_round(size_t n) {
  size_t a = sizeof(it_align_t);
  return (n + a - 1) / a * a;
}

static size_t it_count(const ds_itreap_node_t *n) {
  return n == NULL ? 0 : n->count;
}

static bool it_has_sum(const ds_itreap_t *t) {
  return t->config.summary_size > 0;
}

static bool it_has_tag(const ds_itreap_t *t) { return t->config.tag_size > 0; }

static void it_swap_bytes(unsigned char *a, unsigned char *b, size_t n) {
  unsigned char tmp;
  size_t i;
  for (i = 0; i < n; i++) {
    tmp = a[i];
    a[i] = b[i];
    b[i] = tmp;
  }
}

static unsigned long it_random(ds_itreap_t *t) {
  unsigned long x = t->seed;
  x ^= (x << 13) & 0xFFFFFFFFUL;
  x ^= x >> 17;
  x ^= (x << 5) & 0xFFFFFFFFUL;
  t->seed = x & 0xFFFFFFFFUL;
  return t->seed;
}

/* Reverse a whole subtree lazily: its own summaries swap now. */
static void it_flip(ds_itreap_t *t, ds_itreap_node_t *n) {
  n->reversed ^= 1U;
  if (it_has_sum(t)) {
    it_swap_bytes(IT_AT(n, t->sum_offset), IT_AT(n, t->rev_offset),
                  t->config.summary_size);
  }
}

/* Tag a whole subtree lazily: its element and summaries update now. */
static void it_tag(ds_itreap_t *t, ds_itreap_node_t *n, const void *tag) {
  void *user = t->config.user;
  t->config.tag_apply_elem(IT_AT(n, t->elem_offset), tag, user);
  if (it_has_sum(t)) {
    t->config.tag_apply_summary(IT_AT(n, t->sum_offset), tag, n->count, user);
    t->config.tag_apply_summary(IT_AT(n, t->rev_offset), tag, n->count, user);
  }
  if (n->tagged) {
    t->config.tag_compose(IT_AT(n, t->tag_offset), tag, user);
  } else {
    memcpy(IT_AT(n, t->tag_offset), tag, t->config.tag_size);
    n->tagged = 1;
  }
}

static void it_push(ds_itreap_t *t, ds_itreap_node_t *n) {
  ds_itreap_node_t *tmp;
  if (n->reversed) {
    tmp = n->left;
    n->left = n->right;
    n->right = tmp;
    if (n->left != NULL) {
      it_flip(t, n->left);
    }
    if (n->right != NULL) {
      it_flip(t, n->right);
    }
    n->reversed = 0;
  }
  if (n->tagged) {
    if (n->left != NULL) {
      it_tag(t, n->left, IT_AT(n, t->tag_offset));
    }
    if (n->right != NULL) {
      it_tag(t, n->right, IT_AT(n, t->tag_offset));
    }
    n->tagged = 0;
  }
}

static void it_pull(ds_itreap_t *t, ds_itreap_node_t *n) {
  size_t ss = t->config.summary_size;
  unsigned char *single = t->scratch;
  unsigned char *partial = t->scratch + ss;
  void *user = t->config.user;
  n->count = 1 + it_count(n->left) + it_count(n->right);
  if (!it_has_sum(t)) {
    return;
  }
  t->config.summarize(single, IT_AT(n, t->elem_offset), user);
  /* forward: left, self, right */
  if (n->left != NULL) {
    t->config.combine(partial, IT_AT(n->left, t->sum_offset), single, user);
  } else {
    memcpy(partial, single, ss);
  }
  if (n->right != NULL) {
    t->config.combine(IT_AT(n, t->sum_offset), partial,
                      IT_AT(n->right, t->sum_offset), user);
  } else {
    memcpy(IT_AT(n, t->sum_offset), partial, ss);
  }
  /* reversed: reversed right, self, reversed left */
  if (n->right != NULL) {
    t->config.combine(partial, IT_AT(n->right, t->rev_offset), single, user);
  } else {
    memcpy(partial, single, ss);
  }
  if (n->left != NULL) {
    t->config.combine(IT_AT(n, t->rev_offset), partial,
                      IT_AT(n->left, t->rev_offset), user);
  } else {
    memcpy(IT_AT(n, t->rev_offset), partial, ss);
  }
}

/* First k elements to *a, the rest to *b. */
static void it_split(ds_itreap_t *t, ds_itreap_node_t *n, size_t k,
                     ds_itreap_node_t **a, ds_itreap_node_t **b) {
  if (n == NULL) {
    *a = NULL;
    *b = NULL;
    return;
  }
  it_push(t, n);
  if (it_count(n->left) >= k) {
    it_split(t, n->left, k, a, &n->left);
    *b = n;
  } else {
    it_split(t, n->right, k - it_count(n->left) - 1, &n->right, b);
    *a = n;
  }
  it_pull(t, n);
}

static ds_itreap_node_t *it_merge(ds_itreap_t *t, ds_itreap_node_t *a,
                                  ds_itreap_node_t *b) {
  if (a == NULL) {
    return b;
  }
  if (b == NULL) {
    return a;
  }
  if (a->priority > b->priority) {
    it_push(t, a);
    a->right = it_merge(t, a->right, b);
    it_pull(t, a);
    return a;
  }
  it_push(t, b);
  b->left = it_merge(t, a, b->left);
  it_pull(t, b);
  return b;
}

static void it_free(ds_itreap_t *t, ds_itreap_node_t *n) {
  if (n == NULL) {
    return;
  }
  it_free(t, n->left);
  it_free(t, n->right);
  ds_context_free(t->config.context, n);
}

void ds_itreap_config_init(ds_itreap_config_t *config) {
  memset(config, 0, sizeof(*config));
}

ds_status_t ds_itreap_init(ds_itreap_t *treap,
                           const ds_itreap_config_t *config) {
  size_t header;
  if (treap == NULL || config == NULL) {
    return DS_ERR_NULL;
  }
  if (config->elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (config->summary_size > 0 &&
      (config->summarize == NULL || config->combine == NULL)) {
    return DS_ERR_NULL;
  }
  if (config->tag_size > 0 &&
      (config->tag_apply_elem == NULL || config->tag_compose == NULL ||
       (config->summary_size > 0 && config->tag_apply_summary == NULL))) {
    return DS_ERR_NULL;
  }
  treap->config = *config;
  if (treap->config.context == NULL) {
    treap->config.context = ds_default_context();
  }
  header = it_round(sizeof(ds_itreap_node_t));
  treap->elem_offset = header;
  treap->sum_offset = treap->elem_offset + it_round(config->elem_size);
  treap->rev_offset = treap->sum_offset + it_round(config->summary_size);
  treap->tag_offset = treap->rev_offset + it_round(config->summary_size);
  treap->node_bytes = treap->tag_offset + it_round(config->tag_size);
  treap->root = NULL;
  treap->seed = 0x2545F491UL;
  treap->scratch = (unsigned char *)ds_context_alloc(
      treap->config.context,
      config->summary_size > 0 ? 2 * config->summary_size : 1);
  return treap->scratch == NULL ? DS_ERR_ALLOC : DS_OK;
}

void ds_itreap_destroy(ds_itreap_t *treap) {
  if (treap == NULL) {
    return;
  }
  it_free(treap, treap->root);
  treap->root = NULL;
  ds_context_free(treap->config.context, treap->scratch);
  treap->scratch = NULL;
}

size_t ds_itreap_size(const ds_itreap_t *treap) {
  return it_count(treap->root);
}

ds_status_t ds_itreap_insert(ds_itreap_t *treap, size_t index,
                             const void *elem) {
  ds_itreap_node_t *n;
  ds_itreap_node_t *left;
  ds_itreap_node_t *right;
  if (elem == NULL) {
    return DS_ERR_NULL;
  }
  if (index > ds_itreap_size(treap)) {
    return DS_ERR_RANGE;
  }
  n = (ds_itreap_node_t *)ds_context_alloc(treap->config.context,
                                           treap->node_bytes);
  if (n == NULL) {
    return DS_ERR_ALLOC;
  }
  n->left = NULL;
  n->right = NULL;
  n->reversed = 0;
  n->tagged = 0;
  n->priority = it_random(treap);
  memcpy(IT_AT(n, treap->elem_offset), elem, treap->config.elem_size);
  it_pull(treap, n);
  it_split(treap, treap->root, index, &left, &right);
  treap->root = it_merge(treap, it_merge(treap, left, n), right);
  return DS_OK;
}

ds_status_t ds_itreap_push_back(ds_itreap_t *treap, const void *elem) {
  return ds_itreap_insert(treap, ds_itreap_size(treap), elem);
}

/* Cut [first, last) out as *mid; *left and *right keep the rest. */
static ds_status_t it_cut(ds_itreap_t *t, size_t first, size_t last,
                          ds_itreap_node_t **left, ds_itreap_node_t **mid,
                          ds_itreap_node_t **right) {
  ds_itreap_node_t *rest;
  if (first >= last || last > it_count(t->root)) {
    return DS_ERR_RANGE;
  }
  it_split(t, t->root, first, left, &rest);
  it_split(t, rest, last - first, mid, right);
  return DS_OK;
}

static void it_join(ds_itreap_t *t, ds_itreap_node_t *left,
                    ds_itreap_node_t *mid, ds_itreap_node_t *right) {
  t->root = it_merge(t, it_merge(t, left, mid), right);
}

ds_status_t ds_itreap_erase(ds_itreap_t *treap, size_t index, void *out) {
  ds_itreap_node_t *left;
  ds_itreap_node_t *mid;
  ds_itreap_node_t *right;
  ds_status_t status = it_cut(treap, index, index + 1, &left, &mid, &right);
  if (status != DS_OK) {
    return status;
  }
  if (out != NULL) {
    memcpy(out, IT_AT(mid, treap->elem_offset), treap->config.elem_size);
  }
  ds_context_free(treap->config.context, mid);
  treap->root = it_merge(treap, left, right);
  return DS_OK;
}

void *ds_itreap_at(ds_itreap_t *treap, size_t index) {
  ds_itreap_node_t *n = treap->root;
  size_t lc;
  if (index >= it_count(n)) {
    return NULL;
  }
  for (;;) {
    it_push(treap, n);
    lc = it_count(n->left);
    if (index < lc) {
      n = n->left;
    } else if (index == lc) {
      return IT_AT(n, treap->elem_offset);
    } else {
      index -= lc + 1;
      n = n->right;
    }
  }
}

ds_status_t ds_itreap_get(ds_itreap_t *treap, size_t index, void *out) {
  void *elem;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  elem = ds_itreap_at(treap, index);
  if (elem == NULL) {
    return DS_ERR_RANGE;
  }
  memcpy(out, elem, treap->config.elem_size);
  return DS_OK;
}

ds_status_t ds_itreap_set(ds_itreap_t *treap, size_t index, const void *elem) {
  ds_itreap_node_t *left;
  ds_itreap_node_t *mid;
  ds_itreap_node_t *right;
  ds_status_t status;
  if (elem == NULL) {
    return DS_ERR_NULL;
  }
  status = it_cut(treap, index, index + 1, &left, &mid, &right);
  if (status != DS_OK) {
    return status;
  }
  memmove(IT_AT(mid, treap->elem_offset), elem, treap->config.elem_size);
  it_pull(treap, mid);
  it_join(treap, left, mid, right);
  return DS_OK;
}

ds_status_t ds_itreap_query(ds_itreap_t *treap, size_t first, size_t last,
                            void *out_summary) {
  ds_itreap_node_t *left;
  ds_itreap_node_t *mid;
  ds_itreap_node_t *right;
  ds_status_t status;
  if (out_summary == NULL) {
    return DS_ERR_NULL;
  }
  if (!it_has_sum(treap)) {
    return DS_ERR_UNSUPPORTED;
  }
  status = it_cut(treap, first, last, &left, &mid, &right);
  if (status != DS_OK) {
    return status;
  }
  memcpy(out_summary, IT_AT(mid, treap->sum_offset),
         treap->config.summary_size);
  it_join(treap, left, mid, right);
  return DS_OK;
}

ds_status_t ds_itreap_reverse(ds_itreap_t *treap, size_t first, size_t last) {
  ds_itreap_node_t *left;
  ds_itreap_node_t *mid;
  ds_itreap_node_t *right;
  ds_status_t status;
  if (first > last || last > ds_itreap_size(treap)) {
    return DS_ERR_RANGE;
  }
  if (last - first < 2) {
    return DS_OK; /* nothing to reverse */
  }
  status = it_cut(treap, first, last, &left, &mid, &right);
  if (status != DS_OK) {
    return status;
  }
  it_flip(treap, mid);
  it_join(treap, left, mid, right);
  return DS_OK;
}

ds_status_t ds_itreap_apply(ds_itreap_t *treap, size_t first, size_t last,
                            const void *tag) {
  ds_itreap_node_t *left;
  ds_itreap_node_t *mid;
  ds_itreap_node_t *right;
  ds_status_t status;
  if (tag == NULL) {
    return DS_ERR_NULL;
  }
  if (!it_has_tag(treap)) {
    return DS_ERR_UNSUPPORTED;
  }
  status = it_cut(treap, first, last, &left, &mid, &right);
  if (status != DS_OK) {
    return status;
  }
  it_tag(treap, mid, tag);
  it_join(treap, left, mid, right);
  return DS_OK;
}

ds_status_t ds_itreap_split(ds_itreap_t *treap, size_t index,
                            ds_itreap_t *out_tail) {
  ds_itreap_node_t *left;
  ds_itreap_node_t *right;
  ds_status_t status;
  if (out_tail == NULL) {
    return DS_ERR_NULL;
  }
  if (index > ds_itreap_size(treap)) {
    return DS_ERR_RANGE;
  }
  status = ds_itreap_init(out_tail, &treap->config);
  if (status != DS_OK) {
    return status;
  }
  it_split(treap, treap->root, index, &left, &right);
  treap->root = left;
  out_tail->root = right;
  out_tail->seed = it_random(treap) | 1UL;
  return DS_OK;
}

ds_status_t ds_itreap_append(ds_itreap_t *treap, ds_itreap_t *other) {
  if (treap == NULL || other == NULL) {
    return DS_ERR_NULL;
  }
  if (treap->config.elem_size != other->config.elem_size ||
      treap->config.summary_size != other->config.summary_size ||
      treap->config.tag_size != other->config.tag_size) {
    return DS_ERR_INVALID;
  }
  treap->root = it_merge(treap, treap->root, other->root);
  other->root = NULL;
  return DS_OK;
}

static bool it_visit(ds_itreap_t *t, ds_itreap_node_t *n,
                     ds_itreap_visit_func_t visit, void *user, size_t *index) {
  if (n == NULL) {
    return true;
  }
  it_push(t, n);
  if (!it_visit(t, n->left, visit, user, index)) {
    return false;
  }
  if (!visit(IT_AT(n, t->elem_offset), (*index)++, user)) {
    return false;
  }
  return it_visit(t, n->right, visit, user, index);
}

ds_status_t ds_itreap_visit(ds_itreap_t *treap, ds_itreap_visit_func_t visit,
                            void *user) {
  size_t index = 0;
  if (treap == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  return it_visit(treap, treap->root, visit, user, &index) ? DS_OK : DS_STOP;
}
