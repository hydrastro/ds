#include "mway_btree.h"
#include <string.h>

#define MW_NONE (~(size_t)0)

static unsigned char *mw_key(const ds_mway_btree_t *t, size_t v, size_t i) {
  return t->keys + (v * t->order + i) * t->elem_size;
}

static size_t *mw_child(const ds_mway_btree_t *t, size_t v) {
  return t->child + v * (t->order + 1);
}

static size_t mw_min_keys(const ds_mway_btree_t *t) {
  return (t->order + 1) / 2 - 1;
}

ds_status_t ds_mway_btree_init(ds_mway_btree_t *t, size_t elem_size,
                               ds_mway_compare_func_t compare, void *user,
                               size_t order, ds_context_t *context) {
  if (t == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || order < 3 || order > 65536) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->root = MW_NONE;
  t->free_list = MW_NONE;
  t->order = order;
  t->elem_size = elem_size;
  t->compare = compare;
  t->user = user;
  t->context = context;
  t->tmp = (unsigned char *)ds_context_alloc(context, elem_size);
  return t->tmp == NULL ? DS_ERR_ALLOC : DS_OK;
}

void ds_mway_btree_destroy(ds_mway_btree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->keys);
  ds_context_free(t->context, t->child);
  ds_context_free(t->context, t->count);
  ds_context_free(t->context, t->leaf);
  ds_context_free(t->context, t->path);
  ds_context_free(t->context, t->tmp);
  t->keys = t->leaf = t->tmp = NULL;
  t->child = t->count = t->path = NULL;
  t->root = t->free_list = MW_NONE;
  t->nodes = t->capacity = t->size = t->height = t->path_capacity = 0;
}

size_t ds_mway_btree_size(const ds_mway_btree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_mway_btree_height(const ds_mway_btree_t *t) {
  return t == NULL ? 0 : t->height;
}

/* Room for `extra` more nodes beyond the slots handed out. */
static ds_status_t mw_reserve(ds_mway_btree_t *t, size_t extra) {
  size_t cap;
  void *p;
  if (extra <= t->capacity - t->nodes) {
    return DS_OK;
  }
  cap = t->capacity < 8 ? 8 : t->capacity;
  while (cap - t->nodes < extra) {
    if (cap > ((size_t)-1) / 2) {
      return DS_ERR_OVERFLOW;
    }
    cap *= 2;
  }
  if (cap > ((size_t)-1) / (t->order + 1) ||
      cap * t->order > ((size_t)-1) / t->elem_size ||
      cap * (t->order + 1) > ((size_t)-1) / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  p = ds_context_realloc(t->context, t->keys, cap * t->order * t->elem_size);
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->keys = (unsigned char *)p;
  p = ds_context_realloc(t->context, t->child,
                         cap * (t->order + 1) * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->child = (size_t *)p;
  p = ds_context_realloc(t->context, t->count, cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->count = (size_t *)p;
  p = ds_context_realloc(t->context, t->leaf, cap);
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->leaf = (unsigned char *)p;
  t->capacity = cap;
  return DS_OK;
}

static ds_status_t mw_node(ds_mway_btree_t *t, bool leaf, size_t *out) {
  size_t v;
  ds_status_t st;
  if (t->free_list != MW_NONE) {
    v = t->free_list;
    t->free_list = t->count[v];
  } else {
    st = mw_reserve(t, 1);
    if (st != DS_OK) {
      return st;
    }
    v = t->nodes++;
  }
  t->count[v] = 0;
  t->leaf[v] = (unsigned char)leaf;
  *out = v;
  return DS_OK;
}

static void mw_free(ds_mway_btree_t *t, size_t v) {
  t->count[v] = t->free_list;
  t->free_list = v;
}

static ds_status_t mw_path(ds_mway_btree_t *t) {
  size_t need = 2 * (t->height + 2);
  void *p;
  if (need <= t->path_capacity) {
    return DS_OK;
  }
  p = ds_context_realloc(t->context, t->path, need * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->path = (size_t *)p;
  t->path_capacity = need;
  return DS_OK;
}

/* First index i with key <= keys[i] in node v; *found if equal. */
static size_t mw_search(const ds_mway_btree_t *t, size_t v, const void *key,
                        bool *found) {
  size_t lo = 0;
  size_t hi = t->count[v];
  size_t mid;
  int c;
  *found = false;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    c = t->compare(mw_key(t, v, mid), key, t->user);
    if (c < 0) {
      lo = mid + 1;
    } else {
      if (c == 0) {
        *found = true;
        return mid;
      }
      hi = mid;
    }
  }
  return lo;
}

ds_status_t ds_mway_btree_find(const ds_mway_btree_t *t, const void *key,
                               void *out) {
  size_t v;
  size_t i;
  bool found;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = t->root;
  while (v != MW_NONE) {
    i = mw_search(t, v, key, &found);
    if (found) {
      if (out != NULL) {
        memcpy(out, mw_key(t, v, i), t->elem_size);
      }
      return DS_OK;
    }
    v = t->leaf[v] ? MW_NONE : mw_child(t, v)[i];
  }
  return DS_NOT_FOUND;
}

/* Insert key (and right child rc) at position i of node v (room for one
 * extra key is always there). */
static void mw_insert_at(ds_mway_btree_t *t, size_t v, size_t i,
                         const void *key, size_t rc) {
  size_t *c = mw_child(t, v);
  memmove(mw_key(t, v, i + 1), mw_key(t, v, i),
          (t->count[v] - i) * t->elem_size);
  memcpy(mw_key(t, v, i), key, t->elem_size);
  if (!t->leaf[v]) {
    memmove(c + i + 2, c + i + 1, (t->count[v] - i) * sizeof(size_t));
    c[i + 1] = rc;
  }
  t->count[v]++;
}

ds_status_t ds_mway_btree_insert(ds_mway_btree_t *t, const void *elem) {
  size_t v;
  size_t i;
  size_t depth = 0;
  size_t r;
  size_t mid;
  size_t nr;
  size_t rc = MW_NONE;
  bool found;
  ds_status_t st;
  if (t == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root == MW_NONE) {
    st = mw_node(t, true, &v);
    if (st != DS_OK) {
      return st;
    }
    memcpy(mw_key(t, v, 0), elem, t->elem_size);
    t->count[v] = 1;
    t->root = v;
    t->size = 1;
    t->height = 1;
    return DS_OK;
  }
  st = mw_path(t);
  if (st != DS_OK) {
    return st;
  }
  v = t->root;
  for (;;) {
    i = mw_search(t, v, elem, &found);
    if (found) {
      return DS_EXISTS;
    }
    t->path[2 * depth] = v;
    t->path[2 * depth + 1] = i;
    depth++;
    if (t->leaf[v]) {
      break;
    }
    v = mw_child(t, v)[i];
  }
  /* room for every split this insertion might cause, so none fails */
  st = mw_reserve(t, depth + 1);
  if (st != DS_OK) {
    return st;
  }
  memcpy(t->tmp, elem, t->elem_size);
  while (depth > 0) {
    depth--;
    v = t->path[2 * depth];
    i = t->path[2 * depth + 1];
    mw_insert_at(t, v, i, t->tmp, rc);
    if (t->count[v] < t->order) {
      t->size++;
      return DS_OK;
    }
    /* overflow (order keys): split around the median */
    mid = t->order / 2;
    (void)mw_node(t, t->leaf[v] != 0, &nr); /* reserved above */
    t->count[nr] = t->count[v] - mid - 1;
    memcpy(mw_key(t, nr, 0), mw_key(t, v, mid + 1),
           t->count[nr] * t->elem_size);
    if (!t->leaf[v]) {
      memcpy(mw_child(t, nr), mw_child(t, v) + mid + 1,
             (t->count[nr] + 1) * sizeof(size_t));
    }
    memcpy(t->tmp, mw_key(t, v, mid), t->elem_size);
    t->count[v] = mid;
    rc = nr;
  }
  /* the root split: a new root */
  (void)mw_node(t, false, &r);
  memcpy(mw_key(t, r, 0), t->tmp, t->elem_size);
  t->count[r] = 1;
  mw_child(t, r)[0] = t->root;
  mw_child(t, r)[1] = rc;
  t->root = r;
  t->height++;
  t->size++;
  return DS_OK;
}

/* Fix an underflowing child ci of node p. */
static void mw_fix(ds_mway_btree_t *t, size_t p, size_t ci) {
  size_t *pc = mw_child(t, p);
  size_t c = pc[ci];
  size_t l;
  size_t r;
  size_t minK = mw_min_keys(t);
  size_t es = t->elem_size;
  if (ci > 0 && t->count[pc[ci - 1]] > minK) {
    /* borrow from the left sibling through the separator */
    l = pc[ci - 1];
    memmove(mw_key(t, c, 1), mw_key(t, c, 0), t->count[c] * es);
    memcpy(mw_key(t, c, 0), mw_key(t, p, ci - 1), es);
    memcpy(mw_key(t, p, ci - 1), mw_key(t, l, t->count[l] - 1), es);
    if (!t->leaf[c]) {
      memmove(mw_child(t, c) + 1, mw_child(t, c),
              (t->count[c] + 1) * sizeof(size_t));
      mw_child(t, c)[0] = mw_child(t, l)[t->count[l]];
    }
    t->count[c]++;
    t->count[l]--;
    return;
  }
  if (ci < t->count[p] && t->count[pc[ci + 1]] > minK) {
    r = pc[ci + 1];
    memcpy(mw_key(t, c, t->count[c]), mw_key(t, p, ci), es);
    memcpy(mw_key(t, p, ci), mw_key(t, r, 0), es);
    memmove(mw_key(t, r, 0), mw_key(t, r, 1), (t->count[r] - 1) * es);
    if (!t->leaf[c]) {
      mw_child(t, c)[t->count[c] + 1] = mw_child(t, r)[0];
      memmove(mw_child(t, r), mw_child(t, r) + 1, t->count[r] * sizeof(size_t));
    }
    t->count[c]++;
    t->count[r]--;
    return;
  }
  /* merge with a sibling: left + separator + right */
  if (ci == 0) {
    ci = 1;
  }
  l = pc[ci - 1];
  r = pc[ci];
  memcpy(mw_key(t, l, t->count[l]), mw_key(t, p, ci - 1), es);
  memcpy(mw_key(t, l, t->count[l] + 1), mw_key(t, r, 0), t->count[r] * es);
  if (!t->leaf[l]) {
    memcpy(mw_child(t, l) + t->count[l] + 1, mw_child(t, r),
           (t->count[r] + 1) * sizeof(size_t));
  }
  t->count[l] += t->count[r] + 1;
  memmove(mw_key(t, p, ci - 1), mw_key(t, p, ci), (t->count[p] - ci) * es);
  memmove(pc + ci, pc + ci + 1, (t->count[p] - ci) * sizeof(size_t));
  t->count[p]--;
  mw_free(t, r);
}

ds_status_t ds_mway_btree_remove(ds_mway_btree_t *t, const void *key,
                                 void *out) {
  size_t v;
  size_t i;
  size_t depth = 0;
  size_t target;
  size_t ti;
  bool found = false;
  ds_status_t st;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root == MW_NONE) {
    return DS_NOT_FOUND;
  }
  st = mw_path(t);
  if (st != DS_OK) {
    return st;
  }
  v = t->root;
  for (;;) {
    i = mw_search(t, v, key, &found);
    t->path[2 * depth] = v;
    t->path[2 * depth + 1] = i;
    depth++;
    if (found || t->leaf[v]) {
      break;
    }
    v = mw_child(t, v)[i];
  }
  if (!found) {
    return DS_NOT_FOUND;
  }
  if (out != NULL) {
    memcpy(out, mw_key(t, v, i), t->elem_size);
  }
  if (!t->leaf[v]) {
    /* replace by the predecessor: the maximum of the left subtree */
    target = v;
    ti = i;
    v = mw_child(t, v)[i];
    while (!t->leaf[v]) {
      t->path[2 * depth] = v;
      t->path[2 * depth + 1] = t->count[v];
      depth++;
      v = mw_child(t, v)[t->count[v]];
    }
    t->path[2 * depth] = v;
    t->path[2 * depth + 1] = t->count[v] - 1;
    depth++;
    memcpy(mw_key(t, target, ti), mw_key(t, v, t->count[v] - 1), t->elem_size);
    i = t->count[v] - 1;
  }
  memmove(mw_key(t, v, i), mw_key(t, v, i + 1),
          (t->count[v] - i - 1) * t->elem_size);
  t->count[v]--;
  t->size--;
  /* rebalance upwards */
  depth--;
  while (depth > 0 && t->count[t->path[2 * depth]] < mw_min_keys(t)) {
    mw_fix(t, t->path[2 * (depth - 1)], t->path[2 * (depth - 1) + 1]);
    depth--;
  }
  if (t->count[t->root] == 0) {
    v = t->root;
    t->root = t->leaf[v] ? MW_NONE : mw_child(t, v)[0];
    mw_free(t, v);
    t->height--;
  }
  return DS_OK;
}

static ds_status_t mw_extreme(const ds_mway_btree_t *t, bool max, void *out) {
  size_t v;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root == MW_NONE) {
    return DS_EMPTY;
  }
  v = t->root;
  while (!t->leaf[v]) {
    v = mw_child(t, v)[max ? t->count[v] : 0];
  }
  memcpy(out, mw_key(t, v, max ? t->count[v] - 1 : 0), t->elem_size);
  return DS_OK;
}

ds_status_t ds_mway_btree_min(const ds_mway_btree_t *t, void *out) {
  return mw_extreme(t, false, out);
}

ds_status_t ds_mway_btree_max(const ds_mway_btree_t *t, void *out) {
  return mw_extreme(t, true, out);
}

static bool mw_each(const ds_mway_btree_t *t, size_t v,
                    bool (*visit)(const void *elem, void *user), void *user) {
  size_t i;
  for (i = 0; i <= t->count[v]; i++) {
    if (!t->leaf[v] && !mw_each(t, mw_child(t, v)[i], visit, user)) {
      return false;
    }
    if (i < t->count[v] && !visit(mw_key(t, v, i), user)) {
      return false;
    }
  }
  return true;
}

void ds_mway_btree_foreach(const ds_mway_btree_t *t,
                           bool (*visit)(const void *elem, void *user),
                           void *user) {
  if (t != NULL && visit != NULL && t->root != MW_NONE) {
    (void)mw_each(t, t->root, visit, user);
  }
}

static bool mw_check(const ds_mway_btree_t *t, size_t v, size_t depth,
                     const void *lo, const void *hi, size_t *count) {
  size_t i;
  if (t->count[v] > t->order - 1 ||
      (v != t->root && t->count[v] < mw_min_keys(t)) || t->count[v] == 0) {
    return false;
  }
  for (i = 0; i < t->count[v]; i++) {
    if ((i > 0 &&
         t->compare(mw_key(t, v, i - 1), mw_key(t, v, i), t->user) >= 0) ||
        (lo != NULL && t->compare(mw_key(t, v, i), lo, t->user) <= 0) ||
        (hi != NULL && t->compare(mw_key(t, v, i), hi, t->user) >= 0)) {
      return false;
    }
  }
  *count += t->count[v];
  if (t->leaf[v]) {
    return depth + 1 == t->height;
  }
  for (i = 0; i <= t->count[v]; i++) {
    if (!mw_check(t, mw_child(t, v)[i], depth + 1,
                  i == 0 ? lo : mw_key(t, v, i - 1),
                  i == t->count[v] ? hi : mw_key(t, v, i), count)) {
      return false;
    }
  }
  return true;
}

bool ds_mway_btree_valid(const ds_mway_btree_t *t) {
  size_t count = 0;
  if (t == NULL) {
    return false;
  }
  if (t->root == MW_NONE) {
    return t->size == 0 && t->height == 0;
  }
  return mw_check(t, t->root, 0, NULL, NULL, &count) && count == t->size;
}
