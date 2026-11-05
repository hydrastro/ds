#include "bdd.h"
#include <string.h>

#define BDD_NONE (~(size_t)0)
#define BDD_CACHE_BITS 14
#define BDD_FRAME 8 /* f, g, h, v, stage, r0, unused, unused */

static void *bdd_realloc(ds_context_t *ctx, void *p, size_t count,
                         size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > ((size_t)-1) / size) {
    return NULL;
  }
  return ds_context_realloc(ctx, p, count * size);
}

static size_t bdd_hash3(size_t a, size_t b, size_t c) {
  size_t h = a * 12582917u;
  h ^= b + 0x9e3779b9u + (h << 6) + (h >> 2);
  h ^= c * 2654435761UL + (h << 6) + (h >> 2);
  h ^= h >> 15;
  return h;
}

static size_t bdd_level(const ds_bdd_t *m, size_t n) {
  return n <= DS_BDD_TRUE ? m->vars : m->level_of[m->var[n]];
}

static ds_status_t bdd_rehash(ds_bdd_t *m, size_t size) {
  size_t *t;
  size_t i;
  size_t h;
  t = (size_t *)bdd_realloc(m->context, NULL, size, sizeof(size_t));
  if (t == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < size; i++) {
    t[i] = BDD_NONE;
  }
  for (i = 2; i < m->nodes; i++) {
    h = bdd_hash3(m->var[i], m->low[i], m->high[i]) & (size - 1);
    while (t[h] != BDD_NONE) {
      h = (h + 1) & (size - 1);
    }
    t[h] = i;
  }
  ds_context_free(m->context, m->unique);
  m->unique = t;
  m->unique_size = size;
  return DS_OK;
}

static ds_status_t bdd_mk(ds_bdd_t *m, size_t v, size_t lo, size_t hi,
                          size_t *out) {
  size_t h;
  size_t id;
  ds_status_t st;
  if (lo == hi) {
    *out = lo;
    return DS_OK;
  }
  h = bdd_hash3(v, lo, hi) & (m->unique_size - 1);
  while (m->unique[h] != BDD_NONE) {
    id = m->unique[h];
    if (m->var[id] == v && m->low[id] == lo && m->high[id] == hi) {
      *out = id;
      return DS_OK;
    }
    h = (h + 1) & (m->unique_size - 1);
  }
  if (m->nodes == m->capacity) {
    size_t cap = m->capacity * 2;
    size_t *g;
    g = (size_t *)bdd_realloc(m->context, m->var, cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    m->var = g;
    g = (size_t *)bdd_realloc(m->context, m->low, cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    m->low = g;
    g = (size_t *)bdd_realloc(m->context, m->high, cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    m->high = g;
    m->capacity = cap;
  }
  id = m->nodes++;
  m->var[id] = v;
  m->low[id] = lo;
  m->high[id] = hi;
  m->unique[h] = id;
  *out = id;
  if (2 * m->nodes > m->unique_size) {
    st = bdd_rehash(m, m->unique_size * 2);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}

ds_status_t ds_bdd_init(ds_bdd_t *m, size_t vars, const size_t *order,
                        ds_context_t *context) {
  size_t i;
  if (m == NULL) {
    return DS_ERR_NULL;
  }
  memset(m, 0, sizeof(*m));
  m->vars = vars;
  m->context = context;
  m->level_of =
      (size_t *)bdd_realloc(context, NULL, 2 * vars + 1, sizeof(size_t));
  m->capacity = 64;
  m->var = (size_t *)bdd_realloc(context, NULL, m->capacity, sizeof(size_t));
  m->low = (size_t *)bdd_realloc(context, NULL, m->capacity, sizeof(size_t));
  m->high = (size_t *)bdd_realloc(context, NULL, m->capacity, sizeof(size_t));
  m->cache_size = (size_t)1 << BDD_CACHE_BITS;
  m->cache =
      (size_t *)bdd_realloc(context, NULL, 4 * m->cache_size, sizeof(size_t));
  if (m->level_of == NULL || m->var == NULL || m->low == NULL ||
      m->high == NULL || m->cache == NULL) {
    ds_bdd_destroy(m);
    return DS_ERR_ALLOC;
  }
  m->var_at_level = m->level_of + vars;
  for (i = 0; i < vars; i++) {
    m->level_of[i] = BDD_NONE;
  }
  for (i = 0; i < vars; i++) {
    size_t v = order != NULL ? order[i] : i;
    if (v >= vars || m->level_of[v] != BDD_NONE) {
      ds_bdd_destroy(m);
      return DS_ERR_INVALID;
    }
    m->level_of[v] = i;
    m->var_at_level[i] = v;
  }
  for (i = 0; i < 4 * m->cache_size; i += 4) {
    m->cache[i] = BDD_NONE;
  }
  m->var[0] = vars;
  m->var[1] = vars;
  m->low[0] = m->high[0] = 0;
  m->low[1] = m->high[1] = 1;
  m->nodes = 2;
  m->unique_size = 0;
  if (bdd_rehash(m, 128) != DS_OK) {
    ds_bdd_destroy(m);
    return DS_ERR_ALLOC;
  }
  return DS_OK;
}

void ds_bdd_destroy(ds_bdd_t *m) {
  if (m == NULL) {
    return;
  }
  ds_context_free(m->context, m->level_of);
  ds_context_free(m->context, m->var);
  ds_context_free(m->context, m->low);
  ds_context_free(m->context, m->high);
  ds_context_free(m->context, m->unique);
  ds_context_free(m->context, m->cache);
  ds_context_free(m->context, m->stack);
  memset(m, 0, sizeof(*m));
}

ds_status_t ds_bdd_var(ds_bdd_t *m, size_t v, size_t *out) {
  if (m == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (v >= m->vars) {
    return DS_ERR_RANGE;
  }
  return bdd_mk(m, v, DS_BDD_FALSE, DS_BDD_TRUE, out);
}

ds_status_t ds_bdd_nvar(ds_bdd_t *m, size_t v, size_t *out) {
  if (m == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (v >= m->vars) {
    return DS_ERR_RANGE;
  }
  return bdd_mk(m, v, DS_BDD_TRUE, DS_BDD_FALSE, out);
}

static int bdd_valid(const ds_bdd_t *m, size_t n) {
  return n < m->nodes;
}

static ds_status_t bdd_push(ds_bdd_t *m, size_t *top, size_t f, size_t g,
                            size_t h) {
  size_t *s;
  if ((*top + 1) * BDD_FRAME > m->stack_capacity) {
    size_t cap = m->stack_capacity < 64 * BDD_FRAME ? 64 * BDD_FRAME
                                                    : m->stack_capacity * 2;
    s = (size_t *)bdd_realloc(m->context, m->stack, cap, sizeof(size_t));
    if (s == NULL) {
      return DS_ERR_ALLOC;
    }
    m->stack = s;
    m->stack_capacity = cap;
  }
  s = m->stack + *top * BDD_FRAME;
  s[0] = f;
  s[1] = g;
  s[2] = h;
  s[4] = 0;
  (*top)++;
  return DS_OK;
}

static size_t bdd_cof(const ds_bdd_t *m, size_t n, size_t level, int hi) {
  if (bdd_level(m, n) != level) {
    return n;
  }
  return hi ? m->high[n] : m->low[n];
}

ds_status_t ds_bdd_ite(ds_bdd_t *m, size_t f, size_t g, size_t h, size_t *out) {
  size_t top = 0;
  size_t ret = BDD_NONE;
  size_t *fr;
  size_t res;
  size_t lv;
  size_t l;
  size_t c;
  size_t a;
  size_t b;
  size_t d;
  ds_status_t st;
  if (m == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!bdd_valid(m, f) || !bdd_valid(m, g) || !bdd_valid(m, h)) {
    return DS_ERR_RANGE;
  }
  st = bdd_push(m, &top, f, g, h);
  while (st == DS_OK && top > 0) {
    fr = m->stack + (top - 1) * BDD_FRAME;
    a = fr[0];
    b = fr[1];
    d = fr[2];
    res = BDD_NONE;
    if (fr[4] == 0) {
      if (a == DS_BDD_TRUE) {
        res = b;
      } else if (a == DS_BDD_FALSE) {
        res = d;
      } else if (b == d) {
        res = b;
      } else if (b == DS_BDD_TRUE && d == DS_BDD_FALSE) {
        res = a;
      } else {
        c = bdd_hash3(a, b, d) & (m->cache_size - 1);
        if (m->cache[4 * c] == a && m->cache[4 * c + 1] == b &&
            m->cache[4 * c + 2] == d) {
          res = m->cache[4 * c + 3];
        }
      }
      if (res == BDD_NONE) {
        lv = bdd_level(m, a);
        l = bdd_level(m, b);
        if (l < lv) {
          lv = l;
        }
        l = bdd_level(m, d);
        if (l < lv) {
          lv = l;
        }
        fr[3] = lv;
        fr[4] = 1;
        st = bdd_push(m, &top, bdd_cof(m, a, lv, 0), bdd_cof(m, b, lv, 0),
                      bdd_cof(m, d, lv, 0));
        continue;
      }
    } else if (fr[4] == 1) {
      fr[5] = ret;
      fr[4] = 2;
      lv = fr[3];
      st = bdd_push(m, &top, bdd_cof(m, a, lv, 1), bdd_cof(m, b, lv, 1),
                    bdd_cof(m, d, lv, 1));
      continue;
    } else {
      st = bdd_mk(m, m->var_at_level[fr[3]], fr[5], ret, &res);
      if (st != DS_OK) {
        break;
      }
      c = bdd_hash3(a, b, d) & (m->cache_size - 1);
      m->cache[4 * c] = a;
      m->cache[4 * c + 1] = b;
      m->cache[4 * c + 2] = d;
      m->cache[4 * c + 3] = res;
    }
    top--;
    ret = res;
  }
  if (st == DS_OK) {
    *out = ret;
  }
  return st;
}

ds_status_t ds_bdd_not(ds_bdd_t *m, size_t f, size_t *out) {
  return ds_bdd_ite(m, f, DS_BDD_FALSE, DS_BDD_TRUE, out);
}

/* op(fixed, g) as a function of g from two truth-table bits. */
static ds_status_t bdd_half(ds_bdd_t *m, unsigned b0, unsigned b1, size_t g,
                            size_t *out) {
  if (b0 == b1) {
    *out = b0 ? DS_BDD_TRUE : DS_BDD_FALSE;
    return DS_OK;
  }
  if (b1) {
    *out = g;
    return DS_OK;
  }
  return ds_bdd_not(m, g, out);
}

ds_status_t ds_bdd_apply(ds_bdd_t *m, unsigned table, size_t f, size_t g,
                         size_t *out) {
  size_t hi = 0;
  size_t lo = 0;
  ds_status_t st;
  if (m == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!bdd_valid(m, f) || !bdd_valid(m, g)) {
    return DS_ERR_RANGE;
  }
  st = bdd_half(m, (table >> 2) & 1u, (table >> 3) & 1u, g, &hi);
  if (st == DS_OK) {
    st = bdd_half(m, table & 1u, (table >> 1) & 1u, g, &lo);
  }
  if (st == DS_OK) {
    st = ds_bdd_ite(m, f, hi, lo, out);
  }
  return st;
}

ds_status_t ds_bdd_and(ds_bdd_t *m, size_t f, size_t g, size_t *out) {
  return ds_bdd_apply(m, DS_BDD_AND, f, g, out);
}

ds_status_t ds_bdd_or(ds_bdd_t *m, size_t f, size_t g, size_t *out) {
  return ds_bdd_apply(m, DS_BDD_OR, f, g, out);
}

ds_status_t ds_bdd_xor(ds_bdd_t *m, size_t f, size_t g, size_t *out) {
  return ds_bdd_apply(m, DS_BDD_XOR, f, g, out);
}

ds_status_t ds_bdd_restrict(ds_bdd_t *m, size_t f, size_t v, bool value,
                            size_t *out) {
  size_t *memo;
  size_t *stack;
  size_t n0;
  size_t top = 0;
  size_t x;
  size_t lv;
  size_t i;
  ds_status_t st = DS_OK;
  if (m == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!bdd_valid(m, f) || v >= m->vars) {
    return DS_ERR_RANGE;
  }
  n0 = m->nodes;
  memo = (size_t *)bdd_realloc(m->context, NULL, 2 * n0, sizeof(size_t));
  if (memo == NULL) {
    return DS_ERR_ALLOC;
  }
  stack = memo + n0;
  for (i = 0; i < n0; i++) {
    memo[i] = BDD_NONE;
  }
  lv = m->level_of[v];
  stack[top++] = f;
  while (top > 0 && st == DS_OK) {
    x = stack[top - 1];
    if (memo[x] != BDD_NONE) {
      top--;
      continue;
    }
    if (x <= DS_BDD_TRUE || bdd_level(m, x) > lv) {
      memo[x] = x;
      top--;
      continue;
    }
    if (m->var[x] == v) {
      memo[x] = value ? m->high[x] : m->low[x];
      top--;
      continue;
    }
    if (memo[m->low[x]] == BDD_NONE) {
      stack[top++] = m->low[x];
      continue;
    }
    if (memo[m->high[x]] == BDD_NONE) {
      stack[top++] = m->high[x];
      continue;
    }
    st = bdd_mk(m, m->var[x], memo[m->low[x]], memo[m->high[x]], &memo[x]);
    top--;
  }
  if (st == DS_OK) {
    *out = memo[f];
  }
  ds_context_free(m->context, memo);
  return st;
}

ds_status_t ds_bdd_exists(ds_bdd_t *m, size_t f, size_t v, size_t *out) {
  size_t a = 0;
  size_t b = 0;
  ds_status_t st = ds_bdd_restrict(m, f, v, false, &a);
  if (st == DS_OK) {
    st = ds_bdd_restrict(m, f, v, true, &b);
  }
  if (st == DS_OK) {
    st = ds_bdd_or(m, a, b, out);
  }
  return st;
}

ds_status_t ds_bdd_forall(ds_bdd_t *m, size_t f, size_t v, size_t *out) {
  size_t a = 0;
  size_t b = 0;
  ds_status_t st = ds_bdd_restrict(m, f, v, false, &a);
  if (st == DS_OK) {
    st = ds_bdd_restrict(m, f, v, true, &b);
  }
  if (st == DS_OK) {
    st = ds_bdd_and(m, a, b, out);
  }
  return st;
}

bool ds_bdd_eval(const ds_bdd_t *m, size_t f, const unsigned char *assignment) {
  if (m == NULL || assignment == NULL || !bdd_valid(m, f)) {
    return false;
  }
  while (f > DS_BDD_TRUE) {
    f = assignment[m->var[f]] ? m->high[f] : m->low[f];
  }
  return f == DS_BDD_TRUE;
}

static double bdd_pow2(size_t e) {
  double r = 1.0;
  while (e-- > 0) {
    r *= 2.0;
  }
  return r;
}

double ds_bdd_sat_count(ds_bdd_t *m, size_t f) {
  double *cnt;
  size_t *stack;
  unsigned char *done;
  size_t top = 0;
  size_t x;
  size_t lx;
  double r;
  if (m == NULL || !bdd_valid(m, f)) {
    return 0.0;
  }
  cnt = (double *)bdd_realloc(m->context, NULL, m->nodes, sizeof(double));
  stack = (size_t *)bdd_realloc(m->context, NULL, m->nodes, sizeof(size_t));
  done = (unsigned char *)ds_context_calloc(m->context, m->nodes, 1);
  if (cnt == NULL || stack == NULL || done == NULL) {
    ds_context_free(m->context, cnt);
    ds_context_free(m->context, stack);
    ds_context_free(m->context, done);
    return -1.0;
  }
  cnt[0] = 0.0;
  cnt[1] = 1.0;
  done[0] = done[1] = 1;
  stack[top++] = f;
  while (top > 0) {
    x = stack[top - 1];
    if (done[x]) {
      top--;
      continue;
    }
    if (!done[m->low[x]]) {
      stack[top++] = m->low[x];
      continue;
    }
    if (!done[m->high[x]]) {
      stack[top++] = m->high[x];
      continue;
    }
    lx = bdd_level(m, x);
    cnt[x] = cnt[m->low[x]] * bdd_pow2(bdd_level(m, m->low[x]) - lx - 1) +
             cnt[m->high[x]] * bdd_pow2(bdd_level(m, m->high[x]) - lx - 1);
    done[x] = 1;
    top--;
  }
  r = cnt[f] * bdd_pow2(bdd_level(m, f));
  ds_context_free(m->context, cnt);
  ds_context_free(m->context, stack);
  ds_context_free(m->context, done);
  return r;
}

ds_status_t ds_bdd_any_sat(const ds_bdd_t *m, size_t f,
                           unsigned char *assignment) {
  size_t i;
  if (m == NULL || assignment == NULL) {
    return DS_ERR_NULL;
  }
  if (!bdd_valid(m, f)) {
    return DS_ERR_RANGE;
  }
  if (f == DS_BDD_FALSE) {
    return DS_NOT_FOUND;
  }
  for (i = 0; i < m->vars; i++) {
    assignment[i] = 2;
  }
  while (f > DS_BDD_TRUE) {
    if (m->high[f] != DS_BDD_FALSE) {
      assignment[m->var[f]] = 1;
      f = m->high[f];
    } else {
      assignment[m->var[f]] = 0;
      f = m->low[f];
    }
  }
  return DS_OK;
}

size_t ds_bdd_node_count(ds_bdd_t *m, size_t f) {
  unsigned char *seen;
  size_t *stack;
  size_t top = 0;
  size_t count = 0;
  size_t x;
  if (m == NULL || !bdd_valid(m, f)) {
    return 0;
  }
  seen = (unsigned char *)ds_context_calloc(m->context, m->nodes, 1);
  stack = (size_t *)bdd_realloc(m->context, NULL, m->nodes, sizeof(size_t));
  if (seen == NULL || stack == NULL) {
    ds_context_free(m->context, seen);
    ds_context_free(m->context, stack);
    return 0;
  }
  stack[top++] = f;
  seen[f] = 1;
  while (top > 0) {
    x = stack[--top];
    count++;
    if (x <= DS_BDD_TRUE) {
      continue;
    }
    if (!seen[m->low[x]]) {
      seen[m->low[x]] = 1;
      stack[top++] = m->low[x];
    }
    if (!seen[m->high[x]]) {
      seen[m->high[x]] = 1;
      stack[top++] = m->high[x];
    }
  }
  ds_context_free(m->context, seen);
  ds_context_free(m->context, stack);
  return count;
}
