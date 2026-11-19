#include <dsa/core/checked.h>
#include <dsa/structures/implicit_treap.h>
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
  ds_itreap_node_t *parent;
  size_t count;
  unsigned long priority;
  unsigned char reversed; /* children still need swapping */
  unsigned char tagged;   /* children still need the tag */
};

#define IT_AT(n, off) ((unsigned char *)(n) + (off))

static size_t it_count(const ds_itreap_node_t *n) {
  return n == NULL ? 0 : n->count;
}

static bool it_has_sum(const ds_itreap_t *t) {
  return t->config.summary_size > 0;
}

static bool it_has_tag(const ds_itreap_t *t) { return t->config.tag_size > 0; }

/* The scratch block holds two summaries for it_pull, then (aligned) the
 * copy of the tag that ds_itreap_apply works from; init has checked that
 * these sizes do not overflow. */
static unsigned char *it_tag_copy(const ds_itreap_t *t) {
  size_t a = sizeof(it_align_t);
  return t->scratch + (2 * t->config.summary_size + a - 1) / a * a;
}

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

/* Split and merge walk changed spines; parent links repair summaries. */
static void it_split(ds_itreap_t *t, ds_itreap_node_t *n, size_t k,
                     ds_itreap_node_t **a, ds_itreap_node_t **b) {
  ds_itreap_node_t *left = NULL, *right = NULL, **ls = a, **rs = b;
  while (n != NULL) {
    ds_itreap_node_t *next;
    size_t count;
    it_push(t, n);
    count = it_count(n->left);
    if (k > count) {
      k -= count + 1;
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
  *ls = NULL;
  *rs = NULL;
  for (; left != NULL; left = left->parent) {
    it_pull(t, left);
  }
  for (; right != NULL; right = right->parent) {
    it_pull(t, right);
  }
}

static ds_itreap_node_t *it_merge(ds_itreap_t *t, ds_itreap_node_t *a,
                                  ds_itreap_node_t *b) {
  ds_itreap_node_t *root = NULL, *tail = NULL, **slot = &root;
  while (a != NULL && b != NULL) {
    ds_itreap_node_t *chosen;
    if (a->priority > b->priority) {
      it_push(t, a);
      chosen = a;
      a = a->right;
      *slot = chosen;
      slot = &chosen->right;
    } else {
      it_push(t, b);
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
    it_pull(t, tail);
  }
  return root;
}

static void it_free(ds_itreap_t *t, ds_itreap_node_t *n) {
  /* Tags need not be evaluated to release storage. */
  while (n != NULL) {
    ds_itreap_node_t *next;
    if (n->left != NULL) {
      next = n->left;
      n->left = next->right;
      next->right = n;
    } else {
      next = n->right;
      ds_context_free(t->config.context, n);
    }
    n = next;
  }
}

void ds_itreap_config_init(ds_itreap_config_t *config) {
  memset(config, 0, sizeof(*config));
}

ds_status_t ds_itreap_init(ds_itreap_t *treap,
                           const ds_itreap_config_t *config) {
  size_t header;
  size_t scratch;
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
  {
    /* validate the whole layout before keeping anything */
    size_t a = sizeof(it_align_t);
    size_t e;
    size_t sm;
    size_t tg;
    size_t sum_off;
    size_t rev_off;
    size_t tag_off;
    size_t bytes;
    size_t sums;
    size_t tag_room;
    if (!ds_size_align(sizeof(ds_itreap_node_t), a, &header) ||
        !ds_size_align(config->elem_size, a, &e) ||
        !ds_size_align(config->summary_size, a, &sm) ||
        !ds_size_align(config->tag_size, a, &tg) ||
        !ds_size_add(header, e, &sum_off) ||
        !ds_size_add(sum_off, sm, &rev_off) ||
        !ds_size_add(rev_off, sm, &tag_off) ||
        !ds_size_add(tag_off, tg, &bytes) ||
        !ds_size_mul(config->summary_size, 2, &sums) ||
        !ds_size_align(sums, a, &tag_room) ||
        !ds_size_add(tag_room, config->tag_size, &scratch)) {
      return DS_ERR_OVERFLOW;
    }
    treap->config = *config;
    if (treap->config.context == NULL) {
      treap->config.context = ds_default_context();
    }
    treap->elem_offset = header;
    treap->sum_offset = sum_off;
    treap->rev_offset = rev_off;
    treap->tag_offset = tag_off;
    treap->node_bytes = bytes;
  }
  treap->root = NULL;
  treap->seed = 0x2545F491UL;
  treap->scratch = (unsigned char *)ds_context_alloc(
      treap->config.context, scratch > 0 ? scratch : 1);
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
  if (treap == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (ds_itreap_size(treap) == (size_t)-1) {
    return DS_ERR_OVERFLOW;
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
  n->parent = NULL;
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
  unsigned char *copy;
  ds_status_t status;
  if (tag == NULL) {
    return DS_ERR_NULL;
  }
  if (!it_has_tag(treap)) {
    return DS_ERR_UNSUPPORTED;
  }
  if (first == last && last <= ds_itreap_size(treap)) {
    return DS_OK; /* nothing to tag */
  }
  /* The tag may be an element of this treap, even one inside the range
   * (add element k to every element): tagging the range changes it before
   * the summaries and the pending tag read it, so they read a copy. */
  copy = it_tag_copy(treap);
  memcpy(copy, tag, treap->config.tag_size);
  status = it_cut(treap, first, last, &left, &mid, &right);
  if (status != DS_OK) {
    return status;
  }
  it_tag(treap, mid, copy);
  it_join(treap, left, mid, right);
  return DS_OK;
}

ds_status_t ds_itreap_split(ds_itreap_t *treap, size_t index,
                            ds_itreap_t *out_tail) {
  ds_itreap_node_t *left;
  ds_itreap_node_t *right;
  ds_status_t status;
  if (treap == NULL || out_tail == NULL) {
    return DS_ERR_NULL;
  }
  if (out_tail == treap) {
    return DS_ERR_INVALID;
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

bool ds_itreap_compatible(const ds_itreap_t *a, const ds_itreap_t *b) {
  const ds_itreap_config_t *x;
  const ds_itreap_config_t *y;
  if (a == NULL || b == NULL) {
    return false;
  }
  x = &a->config;
  y = &b->config;
  return x->elem_size == y->elem_size && x->summary_size == y->summary_size &&
         x->tag_size == y->tag_size && x->summarize == y->summarize &&
         x->combine == y->combine && x->tag_apply_elem == y->tag_apply_elem &&
         x->tag_apply_summary == y->tag_apply_summary &&
         x->tag_compose == y->tag_compose && x->user == y->user &&
         x->context == y->context;
}

ds_status_t ds_itreap_refresh(ds_itreap_t *treap, size_t index) {
  ds_itreap_node_t *left;
  ds_itreap_node_t *mid;
  ds_itreap_node_t *right;
  ds_status_t status;
  if (treap == NULL) {
    return DS_ERR_NULL;
  }
  status = it_cut(treap, index, index + 1, &left, &mid, &right);
  if (status != DS_OK) {
    return status;
  }
  it_pull(treap, mid);
  it_join(treap, left, mid, right);
  return DS_OK;
}

ds_status_t ds_itreap_append(ds_itreap_t *treap, ds_itreap_t *other) {
  if (treap == NULL || other == NULL) {
    return DS_ERR_NULL;
  }
  if (treap == other || !ds_itreap_compatible(treap, other)) {
    return DS_ERR_INVALID;
  }
  if (ds_itreap_size(other) > (size_t)-1 - ds_itreap_size(treap)) {
    return DS_ERR_OVERFLOW;
  }
  treap->root = it_merge(treap, treap->root, other->root);
  other->root = NULL;
  return DS_OK;
}

ds_status_t ds_itreap_visit(ds_itreap_t *treap, ds_itreap_visit_func_t visit,
                            void *user) {
  ds_itreap_node_t *n;
  size_t index = 0;
  if (treap == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  n = treap->root;
  while (n != NULL) {
    it_push(treap, n);
    if (n->left == NULL) {
      break;
    }
    n = n->left;
  }
  while (n != NULL) {
    if (!visit(IT_AT(n, treap->elem_offset), index++, user)) {
      return DS_STOP;
    }
    if (n->right != NULL) {
      n = n->right;
      for (;;) {
        it_push(treap, n);
        if (n->left == NULL) {
          break;
        }
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
