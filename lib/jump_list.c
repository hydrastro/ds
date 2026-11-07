#include "jump_list.h"
#include <stdlib.h>
#include <string.h>

#define JL_NONE (~(size_t)0)

static size_t jl_icbrt(size_t i) {
  size_t c = 1;
  while ((c + 1) * (c + 1) * (c + 1) <= i) {
    c++;
  }
  return c;
}

/* faster for large i: start near the real cube root */
static size_t jl_cbrt(size_t i) {
  size_t lo = 1;
  size_t hi = 1;
  if (i < 64) {
    return jl_icbrt(i);
  }
  while (hi * hi * hi <= i && hi < 2642245) {
    hi *= 2;
  }
  lo = hi / 2;
  while (lo + 1 < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (mid * mid * mid <= i) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return lo;
}

static void *jl_elem(const ds_jump_list_t *l, size_t v) {
  return l->data + v * l->elem_size;
}

ds_status_t ds_jump_list_init(ds_jump_list_t *l, size_t elem_size,
                              ds_jump_list_compare_func_t compare, void *user,
                              ds_context_t *context) {
  if (l == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(l, 0, sizeof(*l));
  l->elem_size = elem_size;
  l->compare = compare;
  l->user = user;
  l->free_list = JL_NONE;
  l->head = l->tail = JL_NONE;
  l->context = context;
  return DS_OK;
}

void ds_jump_list_destroy(ds_jump_list_t *l) {
  if (l == NULL) {
    return;
  }
  ds_context_free(l->context, l->data);
  ds_context_free(l->context, l->next);
  ds_context_free(l->context, l->prev);
  ds_context_free(l->context, l->jump);
  ds_context_free(l->context, l->back);
  ds_context_free(l->context, l->cubes);
  ds_context_free(l->context, l->window);
  memset(l, 0, sizeof(*l));
}

size_t ds_jump_list_size(const ds_jump_list_t *l) {
  return l == NULL ? 0 : l->size;
}

size_t ds_jump_list_steps(const ds_jump_list_t *l) {
  return l == NULL ? 0 : l->steps;
}

/* the last item with key <= key (and its index), or none (index 0) */
static size_t jl_search(ds_jump_list_t *l, const void *key, size_t *out_i) {
  size_t c;
  size_t v;
  size_t i;
  l->steps = 0;
  *out_i = 0;
  if (l->size == 0) {
    return JL_NONE;
  }
  c = l->cube_count;
  v = l->cubes[c - 1];
  while (c > 1 && l->compare(key, jl_elem(l, v), l->user) < 0) {
    v = l->back[v];
    c--;
    l->steps++;
  }
  if (l->compare(key, jl_elem(l, v), l->user) < 0) {
    return JL_NONE; /* before the first item */
  }
  i = c * c * c;
  while (l->jump[v] != JL_NONE &&
         l->compare(jl_elem(l, l->jump[v]), key, l->user) <= 0) {
    i += jl_cbrt(i);
    v = l->jump[v];
    l->steps++;
  }
  while (l->next[v] != JL_NONE &&
         l->compare(jl_elem(l, l->next[v]), key, l->user) <= 0) {
    v = l->next[v];
    i++;
    l->steps++;
  }
  *out_i = i;
  return v;
}

/* Re-aim the jumps of items lo..hi (1-based), the item at lo being v. */
static ds_status_t jl_rejump(ds_jump_list_t *l, size_t v, size_t lo,
                             size_t hi) {
  size_t far;
  size_t n = l->size;
  size_t i;
  size_t w = 0;
  if (lo < 1) {
    lo = 1;
  }
  if (hi > n) {
    hi = n;
  }
  if (lo > hi) {
    return DS_OK;
  }
  far = hi + jl_cbrt(hi);
  if (far > n) {
    far = n;
  }
  if (far - lo + 1 > l->window_capacity) {
    size_t nc = (far - lo + 1) * 2;
    void *p = ds_context_realloc(l->context, l->window, nc * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    l->window = (size_t *)p;
    l->window_capacity = nc;
  }
  for (i = lo; i <= far; i++) {
    l->window[w++] = v;
    v = l->next[v];
  }
  for (i = lo; i <= hi; i++) {
    size_t t = i + jl_cbrt(i);
    l->jump[l->window[i - lo]] = t <= n ? l->window[t - lo] : JL_NONE;
  }
  return DS_OK;
}

/* the item at index i, walking from v at index vi (a short walk) */
static size_t jl_walk_to(const ds_jump_list_t *l, size_t v, size_t vi,
                         size_t i) {
  while (vi > i) {
    v = l->prev[v];
    vi--;
  }
  while (vi < i) {
    v = l->next[v];
    vi++;
  }
  return v;
}

static void jl_relink_cubes(ds_jump_list_t *l) {
  size_t c;
  for (c = 0; c < l->cube_count; c++) {
    l->back[l->cubes[c]] = c == 0 ? JL_NONE : l->cubes[c - 1];
  }
}

/* re-aim the region around index p and the tail */
static ds_status_t jl_fix_jumps(ds_jump_list_t *l, size_t v, size_t p) {
  size_t cp = jl_cbrt(p > 0 ? p : 1) + 1;
  size_t lo = p > cp ? p - cp : 1;
  size_t n = l->size;
  ds_status_t st;
  if (n == 0) {
    return DS_OK;
  }
  if (p > n) {
    p = n;
  }
  st = jl_rejump(l, jl_walk_to(l, v, p, lo), lo, p);
  if (st == DS_OK) {
    size_t cn = jl_cbrt(n) + 2;
    size_t tlo = n > cn ? n - cn : 1;
    st = jl_rejump(l, jl_walk_to(l, l->tail, n, tlo), tlo, n);
  }
  return st;
}

ds_status_t ds_jump_list_insert(ds_jump_list_t *l, const void *elem) {
  size_t pi;
  size_t pred;
  size_t v;
  size_t p;
  size_t c;
  ds_status_t st;
  if (l == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  pred = jl_search(l, elem, &pi);
  if (pred != JL_NONE && l->compare(jl_elem(l, pred), elem, l->user) == 0) {
    return DS_EXISTS;
  }
  if (l->free_list != JL_NONE) {
    v = l->free_list;
    l->free_list = l->next[v];
  } else {
    if (l->nodes == l->capacity) {
      size_t nc = l->capacity < 8 ? 16 : l->capacity * 2;
      void *q;
#define JL_RE(field, type, per)                                                \
  q = ds_context_realloc(l->context, l->field, nc * (per));                    \
  if (q == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  l->field = (type *)q;
      JL_RE(data, unsigned char, l->elem_size)
      JL_RE(next, size_t, sizeof(size_t))
      JL_RE(prev, size_t, sizeof(size_t))
      JL_RE(jump, size_t, sizeof(size_t))
      JL_RE(back, size_t, sizeof(size_t))
#undef JL_RE
      l->capacity = nc;
    }
    v = l->nodes++;
  }
  if (l->cube_count == l->cube_capacity) {
    size_t nc = l->cube_capacity < 4 ? 8 : l->cube_capacity * 2;
    void *q = ds_context_realloc(l->context, l->cubes, nc * sizeof(size_t));
    if (q == NULL) {
      l->next[v] = l->free_list;
      l->free_list = v;
      return DS_ERR_ALLOC;
    }
    l->cubes = (size_t *)q;
    l->cube_capacity = nc;
  }
  memcpy(jl_elem(l, v), elem, l->elem_size);
  l->jump[v] = l->back[v] = JL_NONE;
  l->prev[v] = pred;
  l->next[v] = pred == JL_NONE ? l->head : l->next[pred];
  if (l->next[v] != JL_NONE) {
    l->prev[l->next[v]] = v;
  } else {
    l->tail = v;
  }
  if (pred == JL_NONE) {
    l->head = v;
  } else {
    l->next[pred] = v;
  }
  l->size++;
  p = pi + 1;
  /* cubes at or behind p move one item back; the old cube items there
   * had jump distance c - 1 and now need c */
  for (c = 1; c <= l->cube_count; c++) {
    if (c * c * c >= p) {
      size_t old = l->cubes[c - 1];
      size_t x = l->prev[old];
      l->back[old] = JL_NONE;
      l->cubes[c - 1] = x;
      if (x != v && l->jump[x] != JL_NONE) {
        l->jump[x] = l->next[l->jump[x]];
      }
    }
  }
  c = l->cube_count + 1;
  if (c * c * c == l->size) {
    l->cubes[l->cube_count++] = l->tail;
  }
  jl_relink_cubes(l);
  st = jl_fix_jumps(l, v, p);
  return st;
}

ds_status_t ds_jump_list_remove(ds_jump_list_t *l, const void *key) {
  size_t p;
  size_t v;
  size_t c;
  size_t anchor;
  size_t anchor_i;
  if (l == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = jl_search(l, key, &p);
  if (v == JL_NONE || l->compare(jl_elem(l, v), key, l->user) != 0) {
    return DS_NOT_FOUND;
  }
  /* cubes at or behind p move one item forward: the old cube items there
   * (not v) keep jump distance c but now need c - 1 */
  for (c = 1; c <= l->cube_count; c++) {
    if (c * c * c >= p) {
      size_t old = l->cubes[c - 1];
      l->back[old] = JL_NONE;
      if (old != v && l->jump[old] != JL_NONE) {
        size_t tgt = l->jump[old];
        l->jump[old] = l->prev[tgt] == v ? l->prev[v] : l->prev[tgt];
      }
      l->cubes[c - 1] = l->next[old];
    }
  }
  /* unlink v */
  if (l->prev[v] != JL_NONE) {
    l->next[l->prev[v]] = l->next[v];
  } else {
    l->head = l->next[v];
  }
  if (l->next[v] != JL_NONE) {
    l->prev[l->next[v]] = l->prev[v];
  } else {
    l->tail = l->prev[v];
  }
  anchor = l->prev[v] != JL_NONE ? l->prev[v] : l->next[v];
  anchor_i = l->prev[v] != JL_NONE ? p - 1 : p;
  l->next[v] = l->free_list;
  l->free_list = v;
  l->size--;
  if (l->cube_count > 0 &&
      l->cube_count * l->cube_count * l->cube_count > l->size) {
    l->cube_count--;
  }
  jl_relink_cubes(l);
  if (l->size == 0) {
    return DS_OK;
  }
  return jl_fix_jumps(l, anchor, anchor_i);
}

ds_status_t ds_jump_list_find(ds_jump_list_t *l, const void *key, void *out) {
  size_t i;
  size_t v;
  if (l == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = jl_search(l, key, &i);
  if (v == JL_NONE || l->compare(jl_elem(l, v), key, l->user) != 0) {
    return DS_NOT_FOUND;
  }
  if (out != NULL) {
    memcpy(out, jl_elem(l, v), l->elem_size);
  }
  return DS_OK;
}

ds_status_t ds_jump_list_floor(ds_jump_list_t *l, const void *key, void *out) {
  size_t i;
  size_t v;
  if (l == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = jl_search(l, key, &i);
  if (v == JL_NONE) {
    return DS_NOT_FOUND;
  }
  if (out != NULL) {
    memcpy(out, jl_elem(l, v), l->elem_size);
  }
  return DS_OK;
}

ds_status_t ds_jump_list_walk(const ds_jump_list_t *l,
                              ds_jump_list_visit_func_t visit, void *user) {
  size_t v;
  if (l == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  for (v = l->head; v != JL_NONE; v = l->next[v]) {
    if (!visit(jl_elem(l, v), user)) {
      return DS_STOP;
    }
  }
  return DS_OK;
}

bool ds_jump_list_valid(const ds_jump_list_t *l) {
  size_t *at;
  size_t v;
  size_t i;
  size_t n;
  bool ok = true;
  if (l == NULL) {
    return false;
  }
  n = l->size;
  at = (size_t *)ds_context_alloc(l->context, (n + 2) * sizeof(size_t));
  if (at == NULL) {
    return false;
  }
  i = 1;
  for (v = l->head; v != JL_NONE && ok; v = l->next[v]) {
    if (i > n) {
      ok = false;
      break;
    }
    at[i] = v;
    ok = (i == 1 ? l->prev[v] == JL_NONE : l->prev[v] == at[i - 1]) &&
         (i == 1 ||
          l->compare(jl_elem(l, at[i - 1]), jl_elem(l, v), l->user) < 0);
    i++;
  }
  ok = ok && i == n + 1 && (n == 0 || l->tail == at[n]);
  for (i = 1; i <= n && ok; i++) {
    size_t t = i + jl_cbrt(i);
    size_t c = jl_cbrt(i);
    bool cube = c * c * c == i;
    ok = l->jump[at[i]] == (t <= n ? at[t] : JL_NONE) &&
         l->back[at[i]] ==
             (cube && c > 1 ? at[(c - 1) * (c - 1) * (c - 1)] : JL_NONE);
  }
  if (ok) {
    size_t m = n == 0 ? 0 : jl_cbrt(n);
    ok = l->cube_count == m;
    for (i = 1; i <= m && ok; i++) {
      ok = l->cubes[i - 1] == at[i * i * i];
    }
  }
  ds_context_free(l->context, at);
  return ok;
}
