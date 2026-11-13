#include "spiral_storage.h"
#include <math.h>
#include <string.h>

#define SP_NONE (~(size_t)0)

/* The active window is [first, floor(d first) + 1). */
static unsigned long sp_end(const ds_spiral_map_t *m, unsigned long first) {
  double e = m->growth * (double)first; /* positive: truncation floors */
  return (unsigned long)e + 1;
}

/* The key's spiral coordinate is y = d^h in [1, d); its slot is floor(v)
 * for v = y d^k, the first of y, y d, y d^2, ... that reaches first (that
 * is, d^x for x = ceil(S - h) + h). Computing v by the same repeated
 * multiplication every time keeps slot boundaries consistent as first
 * moves. */
static unsigned long sp_slot(const ds_spiral_map_t *m, unsigned long hv,
                             unsigned long first) {
  double h = (double)(hv & 0xFFFFFFFFUL) / 4294967296.0;
  double v = exp(h * log(m->growth));
  double f = (double)first;
  unsigned long end = sp_end(m, first);
  unsigned long k;
  if (v < 1.0) {
    v = 1.0;
  }
  while (v < f) {
    v *= m->growth;
  }
  k = (unsigned long)v;
  if (k < first) {
    return first;
  }
  return k < end ? k : end - 1;
}

static size_t sp_phys(const ds_spiral_map_t *m, unsigned long slot) {
  return (size_t)(slot % (unsigned long)m->cap);
}

static void *sp_key(const ds_spiral_map_t *m, size_t e) {
  return m->keys + e * m->key_size;
}

static void *sp_value(const ds_spiral_map_t *m, size_t e) {
  return m->value_size == 0 ? NULL : m->values + e * m->value_size;
}

ds_status_t ds_spiral_map_init(ds_spiral_map_t *m, size_t key_size,
                               size_t value_size, ds_spiral_hash_func_t hash,
                               ds_spiral_equal_func_t equal, void *user,
                               double growth, double max_load,
                               ds_context_t *context) {
  size_t i;
  if (m == NULL || hash == NULL || equal == NULL) {
    return DS_ERR_NULL;
  }
  if (!(growth > 0.0)) {
    growth = 2.0;
  }
  if (!(max_load > 0.0)) {
    max_load = 2.0;
  }
  if (key_size == 0 || !(growth > 1.0) || growth > 16.0 || !(max_load < 1e9)) {
    return DS_ERR_INVALID;
  }
  memset(m, 0, sizeof(*m));
  m->key_size = key_size;
  m->value_size = value_size;
  m->hash = hash;
  m->equal = equal;
  m->user = user;
  m->growth = growth;
  m->max_load = max_load;
  m->first = 1;
  while (sp_end(m, m->first) - m->first < 1) {
    m->first++;
  }
  m->cap = 8;
  while (m->cap < sp_end(m, m->first) - m->first) {
    m->cap *= 2;
  }
  m->head = (size_t *)ds_context_alloc(context, m->cap * sizeof(size_t));
  if (m->head == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < m->cap; i++) {
    m->head[i] = SP_NONE;
  }
  m->free_list = SP_NONE;
  m->context = context;
  return DS_OK;
}

void ds_spiral_map_destroy(ds_spiral_map_t *m) {
  if (m == NULL) {
    return;
  }
  ds_context_free(m->context, m->head);
  ds_context_free(m->context, m->keys);
  ds_context_free(m->context, m->values);
  ds_context_free(m->context, m->next);
  memset(m, 0, sizeof(*m));
}

size_t ds_spiral_map_size(const ds_spiral_map_t *m) {
  return m == NULL ? 0 : m->size;
}

unsigned long ds_spiral_map_first_slot(const ds_spiral_map_t *m) {
  return m == NULL ? 0 : m->first;
}

unsigned long ds_spiral_map_end_slot(const ds_spiral_map_t *m) {
  return m == NULL ? 0 : sp_end(m, m->first);
}

unsigned long ds_spiral_map_slot_of(const ds_spiral_map_t *m, const void *key) {
  if (m == NULL || key == NULL) {
    return 0;
  }
  return sp_slot(m, m->hash(key, m->user), m->first);
}

/* Ensure the physical array holds the window [first, end); remap if not. */
static ds_status_t sp_reserve(ds_spiral_map_t *m, unsigned long first,
                              unsigned long end) {
  size_t ncap = m->cap;
  size_t *nh;
  size_t i;
  unsigned long j;
  if ((size_t)(end - first) <= m->cap) {
    return DS_OK;
  }
  while (ncap < (size_t)(end - first)) {
    if (ncap > ((size_t)-1) / 2 / sizeof(size_t)) {
      return DS_ERR_OVERFLOW;
    }
    ncap *= 2;
  }
  nh = (size_t *)ds_context_alloc(m->context, ncap * sizeof(size_t));
  if (nh == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < ncap; i++) {
    nh[i] = SP_NONE;
  }
  for (j = m->first; j < sp_end(m, m->first); j++) {
    nh[(size_t)(j % (unsigned long)ncap)] = m->head[sp_phys(m, j)];
  }
  ds_context_free(m->context, m->head);
  m->head = nh;
  m->cap = ncap;
  return DS_OK;
}

/* move every entry of the chain to its slot for the window at first */
static void sp_respread(ds_spiral_map_t *m, size_t chain, unsigned long first) {
  while (chain != SP_NONE) {
    size_t nx = m->next[chain];
    unsigned long s = sp_slot(m, m->hash(sp_key(m, chain), m->user), first);
    size_t p = (size_t)(s % (unsigned long)m->cap);
    m->next[chain] = m->head[p];
    m->head[p] = chain;
    chain = nx;
  }
}

/* the largest first slot whose window ends where doubles still count
 * every slot exactly and unsigned long can number it */
static double sp_first_limit(const ds_spiral_map_t *m) {
  double top = 4503599627370496.0; /* 2^52 */
  if ((double)(~0UL) < top) {
    top = (double)(~0UL);
  }
  return top / m->growth - 2.0;
}

static ds_status_t sp_grow(ds_spiral_map_t *m) {
  unsigned long old_end = sp_end(m, m->first);
  unsigned long nf = m->first + 1;
  unsigned long ne;
  size_t chain;
  unsigned long j;
  ds_status_t st;
  if ((double)nf > sp_first_limit(m)) {
    return DS_ERR_OVERFLOW;
  }
  ne = sp_end(m, nf);
  st = sp_reserve(m, m->first, ne);
  if (st != DS_OK) {
    return st;
  }
  chain = m->head[sp_phys(m, m->first)];
  m->head[sp_phys(m, m->first)] = SP_NONE;
  for (j = old_end; j < ne; j++) {
    m->head[sp_phys(m, j)] = SP_NONE;
  }
  m->first = nf;
  sp_respread(m, chain, nf);
  return DS_OK;
}

static void sp_shrink(ds_spiral_map_t *m) {
  unsigned long nf = m->first - 1;
  unsigned long oe = sp_end(m, m->first);
  double lod = m->growth * (double)nf;
  unsigned long lo = (unsigned long)lod;
  unsigned long j;
  size_t gather = SP_NONE;
  /* keys from the boundary slot up may move back down to slot nf */
  if (lo < m->first) {
    lo = m->first;
  }
  for (j = lo; j < oe; j++) {
    size_t c = m->head[sp_phys(m, j)];
    m->head[sp_phys(m, j)] = SP_NONE;
    while (c != SP_NONE) {
      size_t nx = m->next[c];
      m->next[c] = gather;
      gather = c;
      c = nx;
    }
  }
  m->head[sp_phys(m, nf)] = SP_NONE;
  m->first = nf;
  sp_respread(m, gather, nf);
}

static size_t sp_find(const ds_spiral_map_t *m, const void *key,
                      size_t *out_prev) {
  unsigned long s = sp_slot(m, m->hash(key, m->user), m->first);
  size_t e = m->head[sp_phys(m, s)];
  size_t prev = SP_NONE;
  while (e != SP_NONE && !m->equal(sp_key(m, e), key, m->user)) {
    prev = e;
    e = m->next[e];
  }
  if (out_prev != NULL) {
    *out_prev = prev;
  }
  return e;
}

ds_status_t ds_spiral_map_put(ds_spiral_map_t *m, const void *key,
                              const void *value) {
  size_t e;
  size_t p;
  unsigned long s;
  if (m == NULL || key == NULL || (m->value_size > 0 && value == NULL)) {
    return DS_ERR_NULL;
  }
  e = sp_find(m, key, NULL);
  if (e != SP_NONE) {
    if (m->value_size > 0) {
      memcpy(sp_value(m, e), value, m->value_size);
    }
    return DS_EXISTS;
  }
  if (m->free_list != SP_NONE) {
    e = m->free_list;
    m->free_list = m->next[e];
  } else {
    if (m->entries == m->entry_capacity) {
      size_t nc = m->entry_capacity < 8 ? 16 : m->entry_capacity * 2;
      void *q = ds_context_realloc(m->context, m->keys, nc * m->key_size);
      if (q == NULL) {
        return DS_ERR_ALLOC;
      }
      m->keys = (unsigned char *)q;
      if (m->value_size > 0) {
        q = ds_context_realloc(m->context, m->values, nc * m->value_size);
        if (q == NULL) {
          return DS_ERR_ALLOC;
        }
        m->values = (unsigned char *)q;
      }
      q = ds_context_realloc(m->context, m->next, nc * sizeof(size_t));
      if (q == NULL) {
        return DS_ERR_ALLOC;
      }
      m->next = (size_t *)q;
      m->entry_capacity = nc;
    }
    e = m->entries++;
  }
  memcpy(sp_key(m, e), key, m->key_size);
  if (m->value_size > 0) {
    memcpy(sp_value(m, e), value, m->value_size);
  }
  s = sp_slot(m, m->hash(key, m->user), m->first);
  p = sp_phys(m, s);
  m->next[e] = m->head[p];
  m->head[p] = e;
  m->size++;
  while ((double)m->size >
         m->max_load * (double)(sp_end(m, m->first) - m->first)) {
    if (sp_grow(m) != DS_OK) {
      break; /* no memory or no more slot numbers: correct, just fuller */
    }
  }
  return DS_OK;
}

ds_status_t ds_spiral_map_get(const ds_spiral_map_t *m, const void *key,
                              void *out_value) {
  size_t e;
  if (m == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  e = sp_find(m, key, NULL);
  if (e == SP_NONE) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && m->value_size > 0) {
    memcpy(out_value, sp_value(m, e), m->value_size);
  }
  return DS_OK;
}

ds_status_t ds_spiral_map_remove(ds_spiral_map_t *m, const void *key) {
  size_t e;
  size_t prev;
  if (m == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  e = sp_find(m, key, &prev);
  if (e == SP_NONE) {
    return DS_NOT_FOUND;
  }
  if (prev == SP_NONE) {
    unsigned long s = sp_slot(m, m->hash(key, m->user), m->first);
    m->head[sp_phys(m, s)] = m->next[e];
  } else {
    m->next[prev] = m->next[e];
  }
  m->next[e] = m->free_list;
  m->free_list = e;
  m->size--;
  while (m->first > 1 &&
         (double)m->size <
             m->max_load / 4.0 * (double)(sp_end(m, m->first) - m->first) &&
         sp_end(m, m->first - 1) > m->first - 1 + 1) {
    sp_shrink(m);
  }
  return DS_OK;
}

ds_status_t ds_spiral_map_walk(ds_spiral_map_t *m, ds_spiral_visit_func_t visit,
                               void *user) {
  unsigned long j;
  if (m == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  for (j = m->first; j < sp_end(m, m->first); j++) {
    size_t e;
    for (e = m->head[sp_phys(m, j)]; e != SP_NONE; e = m->next[e]) {
      if (!visit(sp_key(m, e), sp_value(m, e), user)) {
        return DS_STOP;
      }
    }
  }
  return DS_OK;
}

bool ds_spiral_map_valid(const ds_spiral_map_t *m) {
  unsigned long j;
  size_t count = 0;
  if (m == NULL || m->head == NULL) {
    return false;
  }
  if ((size_t)(sp_end(m, m->first) - m->first) > m->cap) {
    return false;
  }
  for (j = m->first; j < sp_end(m, m->first); j++) {
    size_t e;
    for (e = m->head[sp_phys(m, j)]; e != SP_NONE; e = m->next[e]) {
      if (sp_slot(m, m->hash(sp_key(m, e), m->user), m->first) != j) {
        return false;
      }
      if (++count > m->size) {
        return false;
      }
    }
  }
  return count == m->size;
}
