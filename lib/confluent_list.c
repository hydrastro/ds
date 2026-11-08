#include "confluent_list.h"
#include <stdbool.h>
#include <string.h>

#define CE DS_CONFLUENT_EMPTY
#define CL_STACK 160 /* > 1.45 log2(SIZE_MAX) + 2 */

typedef struct cl_src {
  const void *ext; /* element bytes, or NULL to copy node's value */
  size_t node;
} cl_src_t;

typedef struct cl {
  ds_confluent_store_t *s;
  ds_status_t st;
} cl_t;

ds_status_t ds_confluent_store_init(ds_confluent_store_t *s, size_t elem_size,
                                    ds_context_t *context) {
  if (s == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(s, 0, sizeof(*s));
  s->elem_size = elem_size;
  s->free_list = CE;
  s->context = context;
  return DS_OK;
}

void ds_confluent_store_destroy(ds_confluent_store_t *s) {
  if (s == NULL) {
    return;
  }
  ds_context_free(s->context, s->value);
  ds_context_free(s->context, s->left);
  ds_context_free(s->context, s->right);
  ds_context_free(s->context, s->size);
  ds_context_free(s->context, s->refs);
  ds_context_free(s->context, s->height);
  ds_context_free(s->context, s->work);
  memset(s, 0, sizeof(*s));
}

size_t ds_confluent_store_nodes(const ds_confluent_store_t *s) {
  return s == NULL ? 0 : s->live;
}

void ds_confluent_retain(ds_confluent_store_t *s, size_t list) {
  if (s != NULL && list != CE) {
    s->refs[list]++;
  }
}

void ds_confluent_release(ds_confluent_store_t *s, size_t list) {
  size_t top = 0;
  if (s == NULL || list == CE) {
    return;
  }
  if (s->work_capacity == 0) {
    s->work = (size_t *)ds_context_alloc(s->context, 64 * sizeof(size_t));
    if (s->work == NULL) {
      return; /* out of memory: leak rather than corrupt */
    }
    s->work_capacity = 64;
  }
  s->work[top++] = list;
  while (top > 0) {
    size_t t = s->work[--top];
    size_t k;
    size_t child[2];
    if (--s->refs[t] > 0) {
      continue;
    }
    child[0] = s->left[t];
    child[1] = s->right[t];
    for (k = 0; k < 2; k++) {
      if (child[k] == CE) {
        continue;
      }
      if (top == s->work_capacity) {
        size_t *w = (size_t *)ds_context_realloc(
            s->context, s->work, 2 * s->work_capacity * sizeof(size_t));
        if (w == NULL) {
          continue; /* leak this subtree */
        }
        s->work = w;
        s->work_capacity *= 2;
      }
      s->work[top++] = child[k];
    }
    s->left[t] = s->free_list;
    s->free_list = t;
    s->live--;
  }
}

static size_t cl_size(const ds_confluent_store_t *s, size_t t) {
  return t == CE ? 0 : s->size[t];
}

static unsigned cl_h(const ds_confluent_store_t *s, size_t t) {
  return t == CE ? 0u : s->height[t];
}

size_t ds_confluent_length(const ds_confluent_store_t *s, size_t list) {
  return s == NULL ? 0 : cl_size(s, list);
}

unsigned ds_confluent_height(const ds_confluent_store_t *s, size_t list) {
  return s == NULL ? 0u : cl_h(s, list);
}

static ds_status_t cl_grow(ds_confluent_store_t *s) {
  size_t nc = s->capacity < 8 ? 16 : s->capacity * 2;
  void *p;
  if (nc > (size_t)-1 / (s->elem_size > sizeof(size_t) ? s->elem_size
                                                       : sizeof(size_t))) {
    return DS_ERR_OVERFLOW;
  }
#define CL_GROW(field, type)                                                   \
  p = ds_context_realloc(s->context, s->field, nc * sizeof(type));             \
  if (p == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  s->field = (type *)p
  CL_GROW(left, size_t);
  CL_GROW(right, size_t);
  CL_GROW(size, size_t);
  CL_GROW(refs, size_t);
  CL_GROW(height, unsigned char);
#undef CL_GROW
  p = ds_context_realloc(s->context, s->value, nc * s->elem_size);
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  s->value = (unsigned char *)p;
  s->capacity = nc;
  return DS_OK;
}

/* new node over l and r (both consumed) */
static size_t cl_mk(cl_t *c, size_t l, cl_src_t src, size_t r) {
  ds_confluent_store_t *s = c->s;
  size_t v;
  size_t sl = cl_size(s, l);
  size_t sr = cl_size(s, r);
  unsigned hl = cl_h(s, l);
  unsigned hr = cl_h(s, r);
  if (c->st == DS_OK && sl > (size_t)-1 - 1 - sr) {
    c->st = DS_ERR_OVERFLOW;
  }
  if (c->st == DS_OK && s->free_list == CE && s->nodes == s->capacity) {
    c->st = cl_grow(s);
  }
  if (c->st != DS_OK) {
    ds_confluent_release(s, l);
    ds_confluent_release(s, r);
    return CE;
  }
  if (s->free_list != CE) {
    v = s->free_list;
    s->free_list = s->left[v];
  } else {
    v = s->nodes++;
  }
  if (src.ext != NULL) {
    memcpy(s->value + v * s->elem_size, src.ext, s->elem_size);
  } else if (src.node != v) {
    memcpy(s->value + v * s->elem_size, s->value + src.node * s->elem_size,
           s->elem_size);
  }
  s->left[v] = l;
  s->right[v] = r;
  s->size[v] = sl + sr + 1;
  s->height[v] = (unsigned char)((hl > hr ? hl : hr) + 1);
  s->refs[v] = 1;
  s->live++;
  return v;
}

static cl_src_t cl_node_src(size_t node) {
  cl_src_t src;
  src.ext = NULL;
  src.node = node;
  return src;
}

/* l and r consumed; their heights differ by at most 2 */
static size_t cl_balance(cl_t *c, size_t l, cl_src_t src, size_t r) {
  ds_confluent_store_t *s = c->s;
  unsigned hl = cl_h(s, l);
  unsigned hr = cl_h(s, r);
  size_t res;
  if (hl > hr + 1) {
    size_t ll = s->left[l];
    size_t lr = s->right[l];
    ds_confluent_retain(s, ll);
    ds_confluent_retain(s, lr);
    if (cl_h(s, ll) >= cl_h(s, lr)) {
      size_t right = cl_mk(c, lr, src, r);
      res = cl_mk(c, ll, cl_node_src(l), right);
    } else {
      size_t lrl = s->left[lr];
      size_t lrr = s->right[lr];
      size_t a;
      size_t b;
      ds_confluent_retain(s, lrl);
      ds_confluent_retain(s, lrr);
      a = cl_mk(c, ll, cl_node_src(l), lrl);
      b = cl_mk(c, lrr, src, r);
      res = cl_mk(c, a, cl_node_src(lr), b);
      ds_confluent_release(s, lr);
    }
    ds_confluent_release(s, l);
    return res;
  }
  if (hr > hl + 1) {
    size_t rl = s->left[r];
    size_t rr = s->right[r];
    ds_confluent_retain(s, rl);
    ds_confluent_retain(s, rr);
    if (cl_h(s, rr) >= cl_h(s, rl)) {
      size_t left = cl_mk(c, l, src, rl);
      res = cl_mk(c, left, cl_node_src(r), rr);
    } else {
      size_t rll = s->left[rl];
      size_t rlr = s->right[rl];
      size_t a;
      size_t b;
      ds_confluent_retain(s, rll);
      ds_confluent_retain(s, rlr);
      a = cl_mk(c, l, src, rll);
      b = cl_mk(c, rlr, cl_node_src(r), rr);
      res = cl_mk(c, a, cl_node_src(rl), b);
      ds_confluent_release(s, rl);
    }
    ds_confluent_release(s, r);
    return res;
  }
  return cl_mk(c, l, src, r);
}

/* AVL join: l, src, r in order (l and r consumed) */
static size_t cl_join(cl_t *c, size_t l, cl_src_t src, size_t r) {
  ds_confluent_store_t *s = c->s;
  unsigned hl = cl_h(s, l);
  unsigned hr = cl_h(s, r);
  size_t res;
  if (hl > hr + 1) {
    size_t ll = s->left[l];
    size_t lr = s->right[l];
    size_t t;
    ds_confluent_retain(s, ll);
    ds_confluent_retain(s, lr);
    t = cl_join(c, lr, src, r);
    res = cl_balance(c, ll, cl_node_src(l), t);
    ds_confluent_release(s, l);
    return res;
  }
  if (hr > hl + 1) {
    size_t rl = s->left[r];
    size_t rr = s->right[r];
    size_t t;
    ds_confluent_retain(s, rl);
    ds_confluent_retain(s, rr);
    t = cl_join(c, l, src, rl);
    res = cl_balance(c, t, cl_node_src(r), rr);
    ds_confluent_release(s, r);
    return res;
  }
  return cl_mk(c, l, src, r);
}

/* t borrowed; *L gets the first i elements, *R the rest */
static void cl_split(cl_t *c, size_t t, size_t i, size_t *L, size_t *R) {
  ds_confluent_store_t *s = c->s;
  size_t sl;
  if (t == CE) {
    *L = *R = CE;
    return;
  }
  sl = cl_size(s, s->left[t]);
  if (i <= sl) {
    size_t rp;
    cl_split(c, s->left[t], i, L, &rp);
    ds_confluent_retain(s, s->right[t]);
    *R = cl_join(c, rp, cl_node_src(t), s->right[t]);
  } else {
    size_t lp;
    cl_split(c, s->right[t], i - sl - 1, &lp, R);
    ds_confluent_retain(s, s->left[t]);
    *L = cl_join(c, s->left[t], cl_node_src(t), lp);
  }
}

/* concatenation of owned a and b */
static size_t cl_join2(cl_t *c, size_t a, size_t b) {
  size_t first;
  size_t rest;
  size_t res;
  if (a == CE) {
    return b;
  }
  if (b == CE) {
    return a;
  }
  cl_split(c, b, 1, &first, &rest);
  ds_confluent_release(c->s, b);
  if (first == CE) {
    ds_confluent_release(c->s, a);
    ds_confluent_release(c->s, rest);
    return CE;
  }
  res = cl_join(c, a, cl_node_src(first), rest);
  ds_confluent_release(c->s, first);
  return res;
}

static ds_status_t cl_finish(cl_t *c, size_t res, size_t *out) {
  if (c->st != DS_OK) {
    ds_confluent_release(c->s, res);
    return c->st;
  }
  *out = res;
  return DS_OK;
}

static bool cl_valid(const ds_confluent_store_t *s, size_t list) {
  return list == CE || (list < s->nodes && s->refs[list] > 0);
}

static size_t cl_build(cl_t *c, const unsigned char *elems, size_t lo,
                       size_t hi) {
  size_t mid;
  size_t l;
  size_t r;
  cl_src_t src;
  if (lo >= hi || c->st != DS_OK) {
    return CE;
  }
  mid = lo + (hi - lo) / 2;
  l = cl_build(c, elems, lo, mid);
  r = cl_build(c, elems, mid + 1, hi);
  src.ext = elems + mid * c->s->elem_size;
  src.node = CE;
  return cl_mk(c, l, src, r);
}

ds_status_t ds_confluent_from_array(ds_confluent_store_t *s, const void *elems,
                                    size_t count, size_t *out) {
  cl_t c;
  unsigned char *copy;
  size_t res;
  if (s == NULL || out == NULL || (count > 0 && elems == NULL)) {
    return DS_ERR_NULL;
  }
  if (count > (size_t)-1 / s->elem_size) {
    return DS_ERR_OVERFLOW;
  }
  /* the input may live in this store; copy it first */
  copy =
      (unsigned char *)ds_context_alloc(s->context, count * s->elem_size + 1);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  if (count > 0) {
    memcpy(copy, elems, count * s->elem_size);
  }
  c.s = s;
  c.st = DS_OK;
  res = cl_build(&c, copy, 0, count);
  ds_context_free(s->context, copy);
  return cl_finish(&c, res, out);
}

const void *ds_confluent_get(const ds_confluent_store_t *s, size_t list,
                             size_t index) {
  size_t t = list;
  if (s == NULL || !cl_valid(s, list) || index >= cl_size(s, list)) {
    return NULL;
  }
  for (;;) {
    size_t sl = cl_size(s, s->left[t]);
    if (index < sl) {
      t = s->left[t];
    } else if (index == sl) {
      return s->value + t * s->elem_size;
    } else {
      index -= sl + 1;
      t = s->right[t];
    }
  }
}

static size_t cl_set(cl_t *c, size_t t, size_t i, const void *elem) {
  ds_confluent_store_t *s = c->s;
  size_t sl = cl_size(s, s->left[t]);
  if (i < sl) {
    size_t nl = cl_set(c, s->left[t], i, elem);
    ds_confluent_retain(s, s->right[t]);
    return cl_mk(c, nl, cl_node_src(t), s->right[t]);
  }
  if (i == sl) {
    cl_src_t src;
    src.ext = elem;
    src.node = CE;
    ds_confluent_retain(s, s->left[t]);
    ds_confluent_retain(s, s->right[t]);
    return cl_mk(c, s->left[t], src, s->right[t]);
  }
  {
    size_t nr = cl_set(c, s->right[t], i - sl - 1, elem);
    ds_confluent_retain(s, s->left[t]);
    return cl_mk(c, s->left[t], cl_node_src(t), nr);
  }
}

/* copy of an element given by pointer (it may point into the store) */
static unsigned char *cl_copy_elem(ds_confluent_store_t *s, const void *e) {
  unsigned char *b =
      (unsigned char *)ds_context_alloc(s->context, s->elem_size);
  if (b != NULL) {
    memcpy(b, e, s->elem_size);
  }
  return b;
}

ds_status_t ds_confluent_set(ds_confluent_store_t *s, size_t list, size_t index,
                             const void *elem, size_t *out) {
  cl_t c;
  unsigned char *e;
  size_t res;
  if (s == NULL || elem == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!cl_valid(s, list) || index >= cl_size(s, list)) {
    return DS_ERR_RANGE;
  }
  e = cl_copy_elem(s, elem);
  if (e == NULL) {
    return DS_ERR_ALLOC;
  }
  c.s = s;
  c.st = DS_OK;
  res = cl_set(&c, list, index, e);
  ds_context_free(s->context, e);
  return cl_finish(&c, res, out);
}

ds_status_t ds_confluent_insert(ds_confluent_store_t *s, size_t list,
                                size_t index, const void *elem, size_t *out) {
  cl_t c;
  unsigned char *e;
  size_t l;
  size_t r;
  size_t res;
  cl_src_t src;
  if (s == NULL || elem == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!cl_valid(s, list) || index > cl_size(s, list)) {
    return DS_ERR_RANGE;
  }
  e = cl_copy_elem(s, elem);
  if (e == NULL) {
    return DS_ERR_ALLOC;
  }
  c.s = s;
  c.st = DS_OK;
  cl_split(&c, list, index, &l, &r);
  src.ext = e;
  src.node = CE;
  res = cl_join(&c, l, src, r);
  ds_context_free(s->context, e);
  return cl_finish(&c, res, out);
}

ds_status_t ds_confluent_push_front(ds_confluent_store_t *s, size_t list,
                                    const void *elem, size_t *out) {
  return ds_confluent_insert(s, list, 0, elem, out);
}

ds_status_t ds_confluent_push_back(ds_confluent_store_t *s, size_t list,
                                   const void *elem, size_t *out) {
  return ds_confluent_insert(s, list, s == NULL ? 0 : cl_size(s, list), elem,
                             out);
}

ds_status_t ds_confluent_erase(ds_confluent_store_t *s, size_t list,
                               size_t index, size_t *out) {
  cl_t c;
  size_t l;
  size_t r;
  size_t m;
  size_t r2;
  if (s == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!cl_valid(s, list) || index >= cl_size(s, list)) {
    return DS_ERR_RANGE;
  }
  c.s = s;
  c.st = DS_OK;
  cl_split(&c, list, index, &l, &r);
  cl_split(&c, r, 1, &m, &r2);
  ds_confluent_release(s, r);
  ds_confluent_release(s, m);
  return cl_finish(&c, cl_join2(&c, l, r2), out);
}

ds_status_t ds_confluent_concat(ds_confluent_store_t *s, size_t a, size_t b,
                                size_t *out) {
  cl_t c;
  if (s == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!cl_valid(s, a) || !cl_valid(s, b)) {
    return DS_ERR_RANGE;
  }
  if (cl_size(s, a) > (size_t)-1 - cl_size(s, b)) {
    return DS_ERR_OVERFLOW;
  }
  c.s = s;
  c.st = DS_OK;
  ds_confluent_retain(s, a);
  ds_confluent_retain(s, b);
  return cl_finish(&c, cl_join2(&c, a, b), out);
}

ds_status_t ds_confluent_split(ds_confluent_store_t *s, size_t list,
                               size_t index, size_t *out_left,
                               size_t *out_right) {
  cl_t c;
  size_t l;
  size_t r;
  if (s == NULL || out_left == NULL || out_right == NULL) {
    return DS_ERR_NULL;
  }
  if (!cl_valid(s, list) || index > cl_size(s, list)) {
    return DS_ERR_RANGE;
  }
  c.s = s;
  c.st = DS_OK;
  cl_split(&c, list, index, &l, &r);
  if (c.st != DS_OK) {
    ds_confluent_release(s, l);
    ds_confluent_release(s, r);
    return c.st;
  }
  *out_left = l;
  *out_right = r;
  return DS_OK;
}

ds_status_t ds_confluent_slice(ds_confluent_store_t *s, size_t list,
                               size_t from, size_t count, size_t *out) {
  cl_t c;
  size_t a;
  size_t b;
  size_t m;
  size_t rest;
  if (s == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!cl_valid(s, list) || from > cl_size(s, list) ||
      count > cl_size(s, list) - from) {
    return DS_ERR_RANGE;
  }
  c.s = s;
  c.st = DS_OK;
  cl_split(&c, list, from, &a, &b);
  ds_confluent_release(s, a);
  cl_split(&c, b, count, &m, &rest);
  ds_confluent_release(s, b);
  ds_confluent_release(s, rest);
  return cl_finish(&c, m, out);
}

ds_status_t ds_confluent_read(const ds_confluent_store_t *s, size_t list,
                              size_t from, size_t count, void *out) {
  size_t stack[CL_STACK];
  size_t top = 0;
  size_t t = list;
  size_t i = from;
  unsigned char *o = (unsigned char *)out;
  if (s == NULL || (count > 0 && out == NULL)) {
    return DS_ERR_NULL;
  }
  if (!cl_valid(s, list) || from > cl_size(s, list) ||
      count > cl_size(s, list) - from) {
    return DS_ERR_RANGE;
  }
  if (count == 0) {
    return DS_OK;
  }
  /* descend to position `from`, stacking the nodes still to visit */
  while (t != CE) {
    size_t sl = cl_size(s, s->left[t]);
    if (i < sl) {
      stack[top++] = t;
      t = s->left[t];
    } else if (i == sl) {
      stack[top++] = t;
      break;
    } else {
      i -= sl + 1;
      t = s->right[t];
    }
  }
  while (count > 0 && top > 0) {
    size_t u;
    t = stack[--top];
    memcpy(o, s->value + t * s->elem_size, s->elem_size);
    o += s->elem_size;
    count--;
    for (u = s->right[t]; u != CE; u = s->left[u]) {
      stack[top++] = u;
    }
  }
  return DS_OK;
}
