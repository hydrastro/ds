#include <dsa/core/stage.h>
#include <dsa/structures/rbst.h>
#include <limits.h>
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

static unsigned long rb_bits(ds_rbst_t *t) {
  unsigned long x;
  if (t->random != NULL)
    return t->random(t->random_state) & 0x7FFFFFFFUL;
  x = t->seed;
  x ^= (x << 13) & 0xFFFFFFFFUL;
  x ^= x >> 17;
  x ^= (x << 5) & 0xFFFFFFFFUL;
  t->seed = x & 0xFFFFFFFFUL;
  return t->seed & 0x7FFFFFFFUL;
}

/* Rejection removes modulo bias; every size_t bit participates. */
static size_t rb_rand(ds_rbst_t *t, size_t n) {
  size_t x, i, threshold = ((size_t)0 - n) % n;
  do {
    unsigned long bits = 0;
    unsigned int left = 0;
    x = 0;
    for (i = 0; i < sizeof(size_t) * CHAR_BIT; i++) {
      if (left == 0) {
        bits = rb_bits(t);
        left = 31;
      }
      x = (x << 1) | (size_t)(bits & 1UL);
      bits >>= 1;
      left--;
    }
  } while (x < threshold);
  return x % n;
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
  t->context = context != NULL ? context : ds_default_context();
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
  ds_context_free(t->context, t->parent);
  t->data = NULL;
  t->left = t->right = t->size = t->parent = NULL;
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
      p = ds_context_realloc(t->context, t->parent, cap * sizeof(size_t));
      if (p == NULL)
        return DS_ERR_ALLOC;
      t->parent = (size_t *)p;
      t->capacity = cap;
    }
    v = t->slots++;
  }
  memcpy(rb_at(t, v), elem, t->elem_size);
  t->left[v] = t->right[v] = t->parent[v] = RB_NONE;
  t->size[v] = 1;
  *out = v;
  return DS_OK;
}

/* Split spines carry parent links for bottom-up size repair. */
static void rb_split(ds_rbst_t *t, size_t v, const void *key, size_t *lo,
                     size_t *hi) {
  size_t left = RB_NONE, right = RB_NONE;
  while (v != RB_NONE) {
    size_t next;
    if (t->compare(rb_at(t, v), key, t->user) < 0) {
      next = t->right[v];
      *lo = v;
      t->parent[v] = left;
      left = v;
      lo = &t->right[v];
    } else {
      next = t->left[v];
      *hi = v;
      t->parent[v] = right;
      right = v;
      hi = &t->left[v];
    }
    v = next;
  }
  *lo = *hi = RB_NONE;
  for (; left != RB_NONE; left = t->parent[left])
    rb_fix(t, left);
  for (; right != RB_NONE; right = t->parent[right])
    rb_fix(t, right);
}

static size_t rb_join(ds_rbst_t *t, size_t a, size_t b) {
  size_t root, tail = RB_NONE, *slot = &root;
  while (a != RB_NONE && b != RB_NONE) {
    size_t chosen;
    if (rb_rand(t, t->size[a] + t->size[b]) < t->size[a]) {
      chosen = a;
      a = t->right[a];
      *slot = chosen;
      slot = &t->right[chosen];
    } else {
      chosen = b;
      b = t->left[b];
      *slot = chosen;
      slot = &t->left[chosen];
    }
    t->parent[chosen] = tail;
    tail = chosen;
  }
  *slot = a == RB_NONE ? b : a;
  if (*slot != RB_NONE)
    t->parent[*slot] = tail;
  for (; tail != RB_NONE; tail = t->parent[tail])
    rb_fix(t, tail);
  return root;
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

static size_t rb_insert(ds_rbst_t *t, size_t root, size_t n) {
  size_t v = root, parent = RB_NONE, *slot = &root;
  while (v != RB_NONE) {
    if (rb_rand(t, t->size[v] + 1) == 0) {
      rb_split(t, v, rb_at(t, n), &t->left[n], &t->right[n]);
      if (t->left[n] != RB_NONE)
        t->parent[t->left[n]] = n;
      if (t->right[n] != RB_NONE)
        t->parent[t->right[n]] = n;
      rb_fix(t, n);
      break;
    }
    parent = v;
    slot = t->compare(rb_at(t, n), rb_at(t, v), t->user) < 0 ? &t->left[v]
                                                             : &t->right[v];
    v = *slot;
  }
  *slot = n;
  t->parent[n] = parent;
  for (; parent != RB_NONE; parent = t->parent[parent])
    rb_fix(t, parent);
  return root;
}

ds_status_t ds_rbst_insert(ds_rbst_t *t, const void *elem) {
  size_t n;
  ds_stage_t staged;
  ds_status_t st;
  if (t == NULL || elem == NULL)
    return DS_ERR_NULL;
  if (rb_contains(t, elem))
    return DS_EXISTS;
  if (ds_rbst_size(t) == (size_t)-1)
    return DS_ERR_OVERFLOW;
  st = ds_stage_copy(&staged, t->context, true, elem, t->elem_size, NULL, 0);
  if (st != DS_OK)
    return st;
  st = rb_node(t, staged.key, &n);
  ds_stage_release(&staged);
  if (st != DS_OK)
    return st;
  t->root = rb_insert(t, t->root, n);
  return DS_OK;
}

ds_status_t ds_rbst_remove(ds_rbst_t *t, const void *key, void *out) {
  size_t v, parent, replacement;
  if (t == NULL || key == NULL)
    return DS_ERR_NULL;
  v = t->root;
  while (v != RB_NONE) {
    int c = t->compare(key, rb_at(t, v), t->user);
    if (c == 0)
      break;
    v = c < 0 ? t->left[v] : t->right[v];
  }
  if (v == RB_NONE)
    return DS_NOT_FOUND;
  parent = t->parent[v];
  if (out != NULL)
    memmove(out, rb_at(t, v), t->elem_size);
  replacement = rb_join(t, t->left[v], t->right[v]);
  if (replacement != RB_NONE)
    t->parent[replacement] = parent;
  if (parent == RB_NONE)
    t->root = replacement;
  else if (t->left[parent] == v)
    t->left[parent] = replacement;
  else
    t->right[parent] = replacement;
  t->left[v] = t->free_list;
  t->free_list = v;
  for (; parent != RB_NONE; parent = t->parent[parent])
    rb_fix(t, parent);
  return DS_OK;
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

size_t ds_rbst_height(const ds_rbst_t *t) {
  size_t v, previous = RB_NONE, depth = 1, height = 0;
  if (t == NULL)
    return 0;
  v = t->root;
  while (v != RB_NONE) {
    size_t next;
    if (previous == t->parent[v]) {
      if (depth > height)
        height = depth;
      if (t->left[v] != RB_NONE)
        next = t->left[v];
      else if (t->right[v] != RB_NONE)
        next = t->right[v];
      else
        next = t->parent[v];
    } else if (previous == t->left[v] && t->right[v] != RB_NONE)
      next = t->right[v];
    else
      next = t->parent[v];
    if (next == t->parent[v])
      depth--;
    else
      depth++;
    previous = v;
    v = next;
  }
  return height;
}

/* Mirror the subtree through paired parent walks. Partial output is linked
 * immediately, so the caller can release it after any allocation failure. */
static ds_status_t rb_copy(ds_rbst_t *dst, const ds_rbst_t *src, size_t root,
                           size_t *out) {
  size_t s = root, d, child, next;
  ds_status_t st;
  *out = RB_NONE;
  if (root == RB_NONE)
    return DS_OK;
  st = rb_node(dst, rb_at(src, s), &d);
  if (st != DS_OK)
    return st;
  *out = d;
  for (;;) {
    dst->size[d] = src->size[s];
    if (src->left[s] != RB_NONE) {
      next = src->left[s];
      st = rb_node(dst, rb_at(src, next), &child);
      if (st != DS_OK)
        return st;
      dst->left[d] = child;
      dst->parent[child] = d;
      s = next;
      d = child;
      continue;
    }
    for (;;) {
      if (src->right[s] != RB_NONE) {
        next = src->right[s];
        st = rb_node(dst, rb_at(src, next), &child);
        if (st != DS_OK)
          return st;
        dst->right[d] = child;
        dst->parent[child] = d;
        s = next;
        d = child;
        break;
      }
      for (;;) {
        size_t old = s;
        if (s == root)
          return DS_OK;
        s = src->parent[s];
        d = dst->parent[d];
        if (src->left[s] == old)
          break;
      }
    }
  }
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
  if (out == t || out->elem_size != t->elem_size || out->root != RB_NONE ||
      out->compare != t->compare || out->user != t->user) {
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
  if (t == other || t->elem_size != other->elem_size ||
      t->compare != other->compare || t->user != other->user) {
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
  if (ds_rbst_size(other) > (size_t)-1 - ds_rbst_size(t))
    return DS_ERR_OVERFLOW;
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

static size_t rb_first(const ds_rbst_t *t) {
  size_t v = t->root;
  if (v != RB_NONE)
    while (t->left[v] != RB_NONE)
      v = t->left[v];
  return v;
}
static size_t rb_next(const ds_rbst_t *t, size_t v) {
  if (t->right[v] != RB_NONE) {
    v = t->right[v];
    while (t->left[v] != RB_NONE)
      v = t->left[v];
    return v;
  }
  while (t->parent[v] != RB_NONE && t->right[t->parent[v]] == v)
    v = t->parent[v];
  return t->parent[v];
}
void ds_rbst_foreach(const ds_rbst_t *t,
                     bool (*visit)(const void *elem, void *user), void *user) {
  size_t v;
  if (t == NULL || visit == NULL)
    return;
  for (v = rb_first(t); v != RB_NONE; v = rb_next(t, v))
    if (!visit(rb_at(t, v), user))
      break;
}
bool ds_rbst_valid(const ds_rbst_t *t) {
  size_t v, previous = RB_NONE;
  if (t == NULL)
    return false;
  if (t->root != RB_NONE && t->parent[t->root] != RB_NONE)
    return false;
  for (v = rb_first(t); v != RB_NONE; v = rb_next(t, v)) {
    if ((previous != RB_NONE &&
         t->compare(rb_at(t, previous), rb_at(t, v), t->user) >= 0) ||
        (t->left[v] != RB_NONE && t->parent[t->left[v]] != v) ||
        (t->right[v] != RB_NONE && t->parent[t->right[v]] != v) ||
        t->size[v] != rb_size(t, t->left[v]) + rb_size(t, t->right[v]) + 1)
      return false;
    previous = v;
  }
  return true;
}
