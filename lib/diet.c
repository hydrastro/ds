#include "diet.h"
#include <limits.h>
#include <string.h>

#define DT_NONE (~(size_t)0)

ds_status_t ds_diet_init(ds_diet_t *d, ds_context_t *context) {
  if (d == NULL) {
    return DS_ERR_NULL;
  }
  memset(d, 0, sizeof(*d));
  d->root = DT_NONE;
  d->free_list = DT_NONE;
  d->context = context;
  return DS_OK;
}

void ds_diet_destroy(ds_diet_t *d) {
  if (d == NULL) {
    return;
  }
  ds_context_free(d->context, d->lo);
  ds_context_free(d->context, d->hi);
  ds_context_free(d->context, d->left);
  ds_context_free(d->context, d->right);
  ds_context_free(d->context, d->height);
  ds_diet_init(d, d->context);
}

void ds_diet_clear(ds_diet_t *d) {
  if (d != NULL) {
    d->root = d->free_list = DT_NONE;
    d->intervals = d->slots = 0;
  }
}

static int dt_h(const ds_diet_t *d, size_t v) {
  return v == DT_NONE ? 0 : d->height[v];
}

static void dt_fix(ds_diet_t *d, size_t v) {
  int a = dt_h(d, d->left[v]);
  int b = dt_h(d, d->right[v]);
  d->height[v] = (unsigned char)((a > b ? a : b) + 1);
}

static size_t dt_rot_right(ds_diet_t *d, size_t v) {
  size_t l = d->left[v];
  d->left[v] = d->right[l];
  d->right[l] = v;
  dt_fix(d, v);
  dt_fix(d, l);
  return l;
}

static size_t dt_rot_left(ds_diet_t *d, size_t v) {
  size_t r = d->right[v];
  d->right[v] = d->left[r];
  d->left[r] = v;
  dt_fix(d, v);
  dt_fix(d, r);
  return r;
}

static size_t dt_balance(ds_diet_t *d, size_t v) {
  int bf;
  dt_fix(d, v);
  bf = dt_h(d, d->left[v]) - dt_h(d, d->right[v]);
  if (bf > 1) {
    if (dt_h(d, d->left[d->left[v]]) < dt_h(d, d->right[d->left[v]])) {
      d->left[v] = dt_rot_left(d, d->left[v]);
    }
    return dt_rot_right(d, v);
  }
  if (bf < -1) {
    if (dt_h(d, d->right[d->right[v]]) < dt_h(d, d->left[d->right[v]])) {
      d->right[v] = dt_rot_right(d, d->right[v]);
    }
    return dt_rot_left(d, v);
  }
  return v;
}

static ds_status_t dt_node(ds_diet_t *d, unsigned long lo, unsigned long hi,
                           size_t *out) {
  size_t cap;
  void *p;
  size_t v;
  if (d->free_list != DT_NONE) {
    v = d->free_list;
    d->free_list = d->left[v];
  } else {
    if (d->slots == d->capacity) {
      cap = d->capacity < 8 ? 8 : d->capacity * 2;
      if (cap <= d->capacity || cap > ((size_t)-1) / sizeof(size_t)) {
        return DS_ERR_OVERFLOW;
      }
      p = ds_context_realloc(d->context, d->lo, cap * sizeof(unsigned long));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      d->lo = (unsigned long *)p;
      p = ds_context_realloc(d->context, d->hi, cap * sizeof(unsigned long));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      d->hi = (unsigned long *)p;
      p = ds_context_realloc(d->context, d->left, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      d->left = (size_t *)p;
      p = ds_context_realloc(d->context, d->right, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      d->right = (size_t *)p;
      p = ds_context_realloc(d->context, d->height, cap);
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      d->height = (unsigned char *)p;
      d->capacity = cap;
    }
    v = d->slots++;
  }
  d->lo[v] = lo;
  d->hi[v] = hi;
  d->left[v] = d->right[v] = DT_NONE;
  d->height[v] = 1;
  *out = v;
  return DS_OK;
}

/* Insert node n (disjoint from everything in the tree). */
static size_t dt_insert(ds_diet_t *d, size_t v, size_t n) {
  if (v == DT_NONE) {
    return n;
  }
  if (d->lo[n] < d->lo[v]) {
    d->left[v] = dt_insert(d, d->left[v], n);
  } else {
    d->right[v] = dt_insert(d, d->right[v], n);
  }
  return dt_balance(d, v);
}

static size_t dt_take_min(ds_diet_t *d, size_t v, size_t *out_min) {
  if (d->left[v] == DT_NONE) {
    *out_min = v;
    return d->right[v];
  }
  d->left[v] = dt_take_min(d, d->left[v], out_min);
  return dt_balance(d, v);
}

/* Delete the node whose interval starts at lo; it is recycled. */
static size_t dt_delete(ds_diet_t *d, size_t v, unsigned long lo) {
  size_t m;
  size_t r;
  if (v == DT_NONE) {
    return DT_NONE;
  }
  if (lo < d->lo[v]) {
    d->left[v] = dt_delete(d, d->left[v], lo);
  } else if (lo > d->lo[v]) {
    d->right[v] = dt_delete(d, d->right[v], lo);
  } else {
    if (d->left[v] == DT_NONE || d->right[v] == DT_NONE) {
      r = d->left[v] == DT_NONE ? d->right[v] : d->left[v];
    } else {
      d->right[v] = dt_take_min(d, d->right[v], &m);
      d->left[m] = d->left[v];
      d->right[m] = d->right[v];
      r = dt_balance(d, m);
    }
    d->left[v] = d->free_list;
    d->free_list = v;
    return r;
  }
  return dt_balance(d, v);
}

/* First interval (smallest lo) with hi >= x, or DT_NONE. */
static size_t dt_first_reaching(const ds_diet_t *d, unsigned long x) {
  size_t v = d->root;
  size_t best = DT_NONE;
  while (v != DT_NONE) {
    if (d->hi[v] >= x) {
      best = v;
      v = d->left[v];
    } else {
      v = d->right[v];
    }
  }
  return best;
}

ds_status_t ds_diet_insert_range(ds_diet_t *d, unsigned long lo,
                                 unsigned long hi) {
  unsigned long a;
  unsigned long b;
  size_t v;
  size_t n;
  ds_status_t st;
  if (d == NULL) {
    return DS_ERR_NULL;
  }
  if (lo > hi) {
    return DS_ERR_INVALID;
  }
  /* reserve first so the merge cannot fail halfway */
  st = dt_node(d, lo, hi, &n);
  if (st != DS_OK) {
    return st;
  }
  a = lo;
  b = hi;
  for (;;) {
    v = dt_first_reaching(d, lo == 0 ? 0 : lo - 1);
    if (v == DT_NONE || (hi != ULONG_MAX && d->lo[v] > hi + 1)) {
      break;
    }
    if (d->lo[v] < a) {
      a = d->lo[v];
    }
    if (d->hi[v] > b) {
      b = d->hi[v];
    }
    d->root = dt_delete(d, d->root, d->lo[v]);
    d->intervals--;
  }
  d->lo[n] = a;
  d->hi[n] = b;
  d->root = dt_insert(d, d->root, n);
  d->intervals++;
  return DS_OK;
}

ds_status_t ds_diet_insert(ds_diet_t *d, unsigned long x) {
  return ds_diet_insert_range(d, x, x);
}

ds_status_t ds_diet_remove_range(ds_diet_t *d, unsigned long lo,
                                 unsigned long hi) {
  size_t v;
  size_t n;
  unsigned long a;
  unsigned long b;
  bool any = false;
  ds_status_t st;
  if (d == NULL) {
    return DS_ERR_NULL;
  }
  if (lo > hi) {
    return DS_ERR_INVALID;
  }
  for (;;) {
    v = dt_first_reaching(d, lo);
    if (v == DT_NONE || d->lo[v] > hi) {
      break;
    }
    any = true;
    a = d->lo[v];
    b = d->hi[v];
    if (a < lo && b > hi) {
      /* split: keep [a, lo - 1] in place, add [hi + 1, b] */
      st = dt_node(d, hi + 1, b, &n);
      if (st != DS_OK) {
        return st;
      }
      d->hi[v] = lo - 1;
      d->root = dt_insert(d, d->root, n);
      d->intervals++;
      break;
    }
    if (a < lo) {
      d->hi[v] = lo - 1; /* trim the right end */
      continue;
    }
    if (b > hi) {
      /* trim the left end: lo changes, so reinsert */
      d->root = dt_delete(d, d->root, a);
      st = dt_node(d, hi + 1, b, &n);
      if (st != DS_OK) {
        d->intervals--;
        return st;
      }
      d->root = dt_insert(d, d->root, n);
      break;
    }
    d->root = dt_delete(d, d->root, a);
    d->intervals--;
  }
  return any ? DS_OK : DS_NOT_FOUND;
}

ds_status_t ds_diet_remove(ds_diet_t *d, unsigned long x) {
  return ds_diet_remove_range(d, x, x);
}

ds_status_t ds_diet_interval_of(const ds_diet_t *d, unsigned long x,
                                unsigned long *out_lo, unsigned long *out_hi) {
  size_t v;
  if (d == NULL) {
    return DS_ERR_NULL;
  }
  v = dt_first_reaching(d, x);
  if (v == DT_NONE || d->lo[v] > x) {
    return DS_NOT_FOUND;
  }
  if (out_lo != NULL) {
    *out_lo = d->lo[v];
  }
  if (out_hi != NULL) {
    *out_hi = d->hi[v];
  }
  return DS_OK;
}

bool ds_diet_contains(const ds_diet_t *d, unsigned long x) {
  return ds_diet_interval_of(d, x, NULL, NULL) == DS_OK;
}

size_t ds_diet_intervals(const ds_diet_t *d) {
  return d == NULL ? 0 : d->intervals;
}

typedef struct dt_sum {
  unsigned long total;
} dt_sum_t;

static bool dt_add(unsigned long lo, unsigned long hi, void *user) {
  dt_sum_t *s = (dt_sum_t *)user;
  unsigned long n = hi - lo;
  if (n == ULONG_MAX || s->total > ULONG_MAX - n - 1) {
    s->total = ULONG_MAX;
    return false;
  }
  s->total += n + 1;
  return true;
}

unsigned long ds_diet_count(const ds_diet_t *d) {
  dt_sum_t s;
  s.total = 0;
  ds_diet_foreach(d, dt_add, &s);
  return s.total;
}

ds_status_t ds_diet_min(const ds_diet_t *d, unsigned long *out) {
  size_t v;
  if (d == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (d->root == DT_NONE) {
    return DS_EMPTY;
  }
  for (v = d->root; d->left[v] != DT_NONE; v = d->left[v]) {
  }
  *out = d->lo[v];
  return DS_OK;
}

ds_status_t ds_diet_max(const ds_diet_t *d, unsigned long *out) {
  size_t v;
  if (d == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (d->root == DT_NONE) {
    return DS_EMPTY;
  }
  for (v = d->root; d->right[v] != DT_NONE; v = d->right[v]) {
  }
  *out = d->hi[v];
  return DS_OK;
}

static bool dt_each(const ds_diet_t *d, size_t v, ds_diet_visit_func_t f,
                    void *user) {
  if (v == DT_NONE) {
    return true;
  }
  return dt_each(d, d->left[v], f, user) && f(d->lo[v], d->hi[v], user) &&
         dt_each(d, d->right[v], f, user);
}

void ds_diet_foreach(const ds_diet_t *d, ds_diet_visit_func_t f, void *user) {
  if (d != NULL && f != NULL) {
    (void)dt_each(d, d->root, f, user);
  }
}
