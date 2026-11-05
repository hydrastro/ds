#include "rbst.h"
#include <string.h>

#define RB_NONE DS_RBST_NONE

static unsigned char *rb_at(const ds_rbst_t *t, size_t v) {
  return t->data + v * t->elem_size;
}

static size_t rb_size(const ds_rbst_t *t, size_t v) {
  return v == RB_NONE ? 0 : t->size[v];
}

static void rb_fix(ds_rbst_t *t, size_t v) {
  t->size[v] = rb_size(t, t->left[v]) + rb_size(t, t->right[v]) + 1;
}

/* Uniform in [0, n), n >= 1. */
static size_t rb_rand(ds_rbst_t *t, size_t n) {
  unsigned long x;
  unsigned long hi;
  unsigned long lo;
  if (t->random != NULL) {
    hi = t->random(t->random_state) & 0x7FFFFFFFUL;
    lo = t->random(t->random_state) & 0x7FFFFFFFUL;
  } else {
    x = t->seed;
    x ^= (x << 13) & 0xFFFFFFFFUL;
    x ^= x >> 17;
    x ^= (x << 5) & 0xFFFFFFFFUL;
    hi = x & 0x7FFFFFFFUL;
    x ^= (x << 13) & 0xFFFFFFFFUL;
    x ^= x >> 17;
    x ^= (x << 5) & 0xFFFFFFFFUL;
    t->seed = x & 0xFFFFFFFFUL;
    lo = x & 0x7FFFFFFFUL;
  }
  return (size_t)(((size_t)hi << 15) ^ (size_t)lo) % n;
}

ds_status_t ds_rbst_init(ds_rbst_t *t, size_t elem_size,
                         ds_rbst_compare_func_t compare, void *user,
                         ds_rbst_random_func_t random, void *random_state,
                         ds_context_t *context) {
  if (t == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->root = RB_NONE;
  t->free_list = RB_NONE;
  t->elem_size = elem_size;
  t->compare = compare;
  t->user = user;
  t->random = random;
  t->random_state = random_state;
  t->seed = 2463534242UL;
  t->context = context;
  return DS_OK;
}

void ds_rbst_destroy(ds_rbst_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->data);
  ds_context_free(t->context, t->left);
  ds_context_free(t->context, t->right);
  ds_context_free(t->context, t->size);
  t->data = NULL;
  t->left = t->right = t->size = NULL;
  t->slots = t->capacity = 0;
  t->root = t->free_list = RB_NONE;
}

size_t ds_rbst_size(const ds_rbst_t *t) {
  return t == NULL ? 0 : rb_size(t, t->root);
}

static ds_status_t rb_node(ds_rbst_t *t, const void *elem, size_t *out) {
  size_t cap;
  void *p;
  size_t v;
  if (t->free_list != RB_NONE) {
    v = t->free_list;
    t->free_list = t->left[v];
  } else {
    if (t->slots == t->capacity) {
      cap = t->capacity < 8 ? 8 : t->capacity * 2;
      if (cap <= t->capacity || cap > ((size_t)-1) / t->elem_size ||
          cap > ((size_t)-1) / sizeof(size_t)) {
        return DS_ERR_OVERFLOW;
      }
      p = ds_context_realloc(t->context, t->data, cap * t->elem_size);
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->data = (unsigned char *)p;
      p = ds_context_realloc(t->context, t->left, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->left = (size_t *)p;
      p = ds_context_realloc(t->context, t->right, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->right = (size_t *)p;
      p = ds_context_realloc(t->context, t->size, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->size = (size_t *)p;
      t->capacity = cap;
    }
    v = t->slots++;
  }
  memcpy(rb_at(t, v), elem, t->elem_size);
  t->left[v] = t->right[v] = RB_NONE;
  t->size[v] = 1;
  *out = v;
  return DS_OK;
}

/* Split subtree v by key into < key (*lo) and >= key (*hi). */
static void rb_split(ds_rbst_t *t, size_t v, const void *key, size_t *lo,
                     size_t *hi) {
  if (v == RB_NONE) {
    *lo = *hi = RB_NONE;
    return;
  }
  if (t->compare(rb_at(t, v), key, t->user) < 0) {
    rb_split(t, t->right[v], key, &t->right[v], hi);
    rb_fix(t, v);
    *lo = v;
  } else {
    rb_split(t, t->left[v], key, lo, &t->left[v]);
    rb_fix(t, v);
    *hi = v;
  }
}

/* Join a and b (every key of a smaller): random root by size. */
static size_t rb_join(ds_rbst_t *t, size_t a, size_t b) {
  size_t m;
  size_t n;
  if (a == RB_NONE) {
    return b;
  }
  if (b == RB_NONE) {
    return a;
  }
  m = t->size[a];
  n = t->size[b];
  if (rb_rand(t, m + n) < m) {
    t->right[a] = rb_join(t, t->right[a], b);
    rb_fix(t, a);
    return a;
  }
  t->left[b] = rb_join(t, a, t->left[b]);
  rb_fix(t, b);
  return b;
}

static bool rb_contains(const ds_rbst_t *t, const void *key) {
  size_t v = t->root;
  int c;
  while (v != RB_NONE) {
    c = t->compare(key, rb_at(t, v), t->user);
    if (c == 0) {
      return true;
    }
    v = c < 0 ? t->left[v] : t->right[v];
  }
  return false;
}

static size_t rb_insert(ds_rbst_t *t, size_t v, size_t n) {
  int c;
  if (v == RB_NONE) {
    return n;
  }
  if (rb_rand(t, t->size[v] + 1) == 0) {
    rb_split(t, v, rb_at(t, n), &t->left[n], &t->right[n]);
    rb_fix(t, n);
    return n;
  }
  c = t->compare(rb_at(t, n), rb_at(t, v), t->user);
  if (c < 0) {
    t->left[v] = rb_insert(t, t->left[v], n);
  } else {
    t->right[v] = rb_insert(t, t->right[v], n);
  }
  rb_fix(t, v);
  return v;
}

ds_status_t ds_rbst_insert(ds_rbst_t *t, const void *elem) {
  size_t n;
  ds_status_t st;
  if (t == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (rb_contains(t, elem)) {
    return DS_EXISTS;
  }
  st = rb_node(t, elem, &n);
  if (st != DS_OK) {
    return st;
  }
  t->root = rb_insert(t, t->root, n);
  return DS_OK;
}

static size_t rb_remove(ds_rbst_t *t, size_t v, const void *key, void *out,
                        bool *found) {
  int c;
  size_t r;
  if (v == RB_NONE) {
    return RB_NONE;
  }
  c = t->compare(key, rb_at(t, v), t->user);
  if (c == 0) {
    *found = true;
    if (out != NULL) {
      memcpy(out, rb_at(t, v), t->elem_size);
    }
    r = rb_join(t, t->left[v], t->right[v]);
    t->left[v] = t->free_list;
    t->free_list = v;
    return r;
  }
  if (c < 0) {
    t->left[v] = rb_remove(t, t->left[v], key, out, found);
  } else {
    t->right[v] = rb_remove(t, t->right[v], key, out, found);
  }
  if (*found) {
    rb_fix(t, v);
  }
  return v;
}

ds_status_t ds_rbst_remove(ds_rbst_t *t, const void *key, void *out) {
  bool found = false;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  t->root = rb_remove(t, t->root, key, out, &found);
  return found ? DS_OK : DS_NOT_FOUND;
}

ds_status_t ds_rbst_find(const ds_rbst_t *t, const void *key, void *out) {
  size_t v;
  int c;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = t->root;
  while (v != RB_NONE) {
    c = t->compare(key, rb_at(t, v), t->user);
    if (c == 0) {
      if (out != NULL) {
        memcpy(out, rb_at(t, v), t->elem_size);
      }
      return DS_OK;
    }
    v = c < 0 ? t->left[v] : t->right[v];
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_rbst_select(const ds_rbst_t *t, size_t k, void *out) {
  size_t v;
  size_t ls;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (k >= ds_rbst_size(t)) {
    return DS_ERR_RANGE;
  }
  v = t->root;
  for (;;) {
    ls = rb_size(t, t->left[v]);
    if (k < ls) {
      v = t->left[v];
    } else if (k == ls) {
      break;
    } else {
      k -= ls + 1;
      v = t->right[v];
    }
  }
  if (out != NULL) {
    memcpy(out, rb_at(t, v), t->elem_size);
  }
  return DS_OK;
}

size_t ds_rbst_rank(const ds_rbst_t *t, const void *key) {
  size_t v;
  size_t r = 0;
  int c;
  if (t == NULL || key == NULL) {
    return 0;
  }
  v = t->root;
  while (v != RB_NONE) {
    c = t->compare(key, rb_at(t, v), t->user);
    if (c == 0) {
      return r + rb_size(t, t->left[v]);
    }
    if (c < 0) {
      v = t->left[v];
    } else {
      r += rb_size(t, t->left[v]) + 1;
      v = t->right[v];
    }
  }
  return r;
}

static size_t rb_height(const ds_rbst_t *t, size_t v) {
  size_t a;
  size_t b;
  if (v == RB_NONE) {
    return 0;
  }
  a = rb_height(t, t->left[v]);
  b = rb_height(t, t->right[v]);
  return (a > b ? a : b) + 1;
}

size_t ds_rbst_height(const ds_rbst_t *t) {
  return t == NULL ? 0 : rb_height(t, t->root);
}

/* Copy the subtree v of src into dst, keeping its shape. */
static ds_status_t rb_copy(ds_rbst_t *dst, const ds_rbst_t *src, size_t v,
                           size_t *out) {
  size_t n;
  size_t l;
  size_t r;
  ds_status_t st;
  *out = RB_NONE;
  if (v == RB_NONE) {
    return DS_OK;
  }
  st = rb_node(dst, rb_at(src, v), &n);
  if (st != DS_OK) {
    return st;
  }
  /* dst's arrays may move while copying: link through locals */
  st = rb_copy(dst, src, src->left[v], &l);
  dst->left[n] = l;
  if (st == DS_OK) {
    st = rb_copy(dst, src, src->right[v], &r);
    dst->right[n] = r;
  }
  rb_fix(dst, n);
  *out = n;
  return st;
}

static void rb_free_subtree(ds_rbst_t *t, size_t v) {
  size_t l;
  size_t r;
  /* release without a stack: rotate left children up */
  while (v != RB_NONE) {
    l = t->left[v];
    if (l != RB_NONE) {
      t->left[v] = t->right[l];
      t->right[l] = v;
      v = l;
    } else {
      r = t->right[v];
      t->left[v] = t->free_list;
      t->free_list = v;
      v = r;
    }
  }
}

ds_status_t ds_rbst_split(ds_rbst_t *t, const void *key, ds_rbst_t *out) {
  size_t lo;
  size_t hi;
  size_t copy = RB_NONE;
  ds_status_t st;
  if (t == NULL || key == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (out == t || out->elem_size != t->elem_size || out->root != RB_NONE) {
    return DS_ERR_INVALID;
  }
  rb_split(t, t->root, key, &lo, &hi);
  st = rb_copy(out, t, hi, &copy);
  if (st != DS_OK) {
    /* undo: put the halves back together */
    rb_free_subtree(out, copy);
    t->root = rb_join(t, lo, hi);
    return st;
  }
  out->root = copy;
  rb_free_subtree(t, hi);
  t->root = lo;
  return DS_OK;
}

ds_status_t ds_rbst_join(ds_rbst_t *t, ds_rbst_t *other) {
  size_t v;
  size_t copy = RB_NONE;
  ds_status_t st;
  if (t == NULL || other == NULL) {
    return DS_ERR_NULL;
  }
  if (t == other || t->elem_size != other->elem_size) {
    return DS_ERR_INVALID;
  }
  if (other->root == RB_NONE) {
    return DS_OK;
  }
  if (t->root != RB_NONE) {
    /* max of t must be below min of other */
    size_t a = t->root;
    size_t b = other->root;
    while (t->right[a] != RB_NONE) {
      a = t->right[a];
    }
    while (other->left[b] != RB_NONE) {
      b = other->left[b];
    }
    if (t->compare(rb_at(t, a), rb_at(other, b), t->user) >= 0) {
      return DS_ERR_INVALID;
    }
  }
  st = rb_copy(t, other, other->root, &copy);
  if (st != DS_OK) {
    rb_free_subtree(t, copy);
    return st;
  }
  t->root = rb_join(t, t->root, copy);
  v = other->root;
  rb_free_subtree(other, v);
  other->root = RB_NONE;
  return DS_OK;
}

static bool rb_each(const ds_rbst_t *t, size_t v,
                    bool (*visit)(const void *elem, void *user), void *user) {
  if (v == RB_NONE) {
    return true;
  }
  return rb_each(t, t->left[v], visit, user) && visit(rb_at(t, v), user) &&
         rb_each(t, t->right[v], visit, user);
}

void ds_rbst_foreach(const ds_rbst_t *t,
                     bool (*visit)(const void *elem, void *user), void *user) {
  if (t != NULL && visit != NULL) {
    (void)rb_each(t, t->root, visit, user);
  }
}

static bool rb_check(const ds_rbst_t *t, size_t v, const void *lo,
                     const void *hi) {
  if (v == RB_NONE) {
    return true;
  }
  if ((lo != NULL && t->compare(rb_at(t, v), lo, t->user) <= 0) ||
      (hi != NULL && t->compare(rb_at(t, v), hi, t->user) >= 0) ||
      t->size[v] != rb_size(t, t->left[v]) + rb_size(t, t->right[v]) + 1) {
    return false;
  }
  return rb_check(t, t->left[v], lo, rb_at(t, v)) &&
         rb_check(t, t->right[v], rb_at(t, v), hi);
}

bool ds_rbst_valid(const ds_rbst_t *t) {
  return t != NULL && rb_check(t, t->root, NULL, NULL);
}
