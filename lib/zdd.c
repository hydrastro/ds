#include "zdd.h"
#include "checked.h"
#include <string.h>

#define ZDD_NONE (~(size_t)0)
#define ZDD_CACHE_BITS 15

/* operations */
enum {
  Z_UNION,
  Z_INTER,
  Z_DIFF,
  Z_SYMDIFF,
  Z_SUB0,
  Z_SUB1,
  Z_CHANGE,
  Z_JOIN,
  Z_MEET,
  Z_NONSUP,
  Z_NONSUB,
  Z_MIN,
  Z_MAX,
  Z_SELF /* in a plan: the frame's own operation */
};

/* a frame of the operation stack, by word */
#define F_OP 0
#define F_F 1
#define F_G 2 /* the second family, or the level for subset/change */
#define F_STAGE 3
#define F_VAR 4
#define F_F0 5
#define F_F1 6
#define F_G0 7
#define F_G1 8
#define F_T0 9
#define F_T1 10
#define F_T2 11
#define ZF 12

/* one step of a plan: dst = op(frame[a], frame[b]) */
typedef struct zdd_step {
  unsigned char op;
  unsigned char a;
  unsigned char b;
  unsigned char dst;
} zdd_step_t;

/* The recursive case of each operation over the cofactors at the top
 * level v, F = F0 u v.F1 and G = G0 u v.G1 (F1 empty when F does not test
 * v), ending in mk(v, T0, T1):
 *   union, intersect, diff, symdiff   op(F0, G0), op(F1, G1)
 *   subset0, subset1, change          op(F0), op(F1), above v's level
 *   join    F0.G0,   F1.(G0 u G1) u F0.G1
 *   meet    F0^(G0 u G1) u F1^G0,   F1^G1
 *   nonsup  F0 nonsup G0,   F1 nonsup (G0 u G1)
 *   nonsub  F0 nonsub (G0 u G1),   F1 nonsub G1
 *   minimal min(F0),   min(F1) nonsup F0
 *   maximal max(F0) nonsub F1,   max(F1)                              */
static const zdd_step_t zdd_plan_pair[] = {{Z_SELF, F_F0, F_G0, F_T0},
                                           {Z_SELF, F_F1, F_G1, F_T1}};
static const zdd_step_t zdd_plan_var[] = {{Z_SELF, F_F0, F_G, F_T0},
                                          {Z_SELF, F_F1, F_G, F_T1}};
static const zdd_step_t zdd_plan_join[] = {{Z_JOIN, F_F0, F_G0, F_T0},
                                           {Z_UNION, F_G0, F_G1, F_T1},
                                           {Z_JOIN, F_F1, F_T1, F_T1},
                                           {Z_JOIN, F_F0, F_G1, F_T2},
                                           {Z_UNION, F_T1, F_T2, F_T1}};
static const zdd_step_t zdd_plan_meet[] = {{Z_UNION, F_G0, F_G1, F_T0},
                                           {Z_MEET, F_F0, F_T0, F_T0},
                                           {Z_MEET, F_F1, F_G0, F_T1},
                                           {Z_UNION, F_T0, F_T1, F_T0},
                                           {Z_MEET, F_F1, F_G1, F_T1}};
static const zdd_step_t zdd_plan_nonsup[] = {{Z_NONSUP, F_F0, F_G0, F_T0},
                                             {Z_UNION, F_G0, F_G1, F_T1},
                                             {Z_NONSUP, F_F1, F_T1, F_T1}};
static const zdd_step_t zdd_plan_nonsub[] = {{Z_UNION, F_G0, F_G1, F_T0},
                                             {Z_NONSUB, F_F0, F_T0, F_T0},
                                             {Z_NONSUB, F_F1, F_G1, F_T1}};
static const zdd_step_t zdd_plan_min[] = {{Z_MIN, F_F0, F_G, F_T0},
                                          {Z_MIN, F_F1, F_G, F_T1},
                                          {Z_NONSUP, F_T1, F_F0, F_T1}};
static const zdd_step_t zdd_plan_max[] = {{Z_MAX, F_F0, F_G, F_T0},
                                          {Z_NONSUB, F_T0, F_F1, F_T0},
                                          {Z_MAX, F_F1, F_G, F_T1}};

static const zdd_step_t *zdd_plan(unsigned op, size_t *steps) {
  switch (op) {
  case Z_SUB0:
  case Z_SUB1:
  case Z_CHANGE:
    *steps = 2;
    return zdd_plan_var;
  case Z_JOIN:
    *steps = 5;
    return zdd_plan_join;
  case Z_MEET:
    *steps = 5;
    return zdd_plan_meet;
  case Z_NONSUP:
    *steps = 3;
    return zdd_plan_nonsup;
  case Z_NONSUB:
    *steps = 3;
    return zdd_plan_nonsub;
  case Z_MIN:
    *steps = 3;
    return zdd_plan_min;
  case Z_MAX:
    *steps = 3;
    return zdd_plan_max;
  default:
    *steps = 2;
    return zdd_plan_pair;
  }
}

static void *zdd_array(ds_context_t *ctx, void *p, size_t count, size_t size) {
  size_t bytes;
  if (!ds_size_mul(count == 0 ? 1 : count, size, &bytes)) {
    return NULL;
  }
  return ds_context_realloc(ctx, p, bytes);
}

static size_t zdd_hash3(size_t a, size_t b, size_t c) {
  size_t h = a * 12582917u;
  h ^= b + 0x9e3779b9u + (h << 6) + (h >> 2);
  h ^= c * 2654435761UL + (h << 6) + (h >> 2);
  h ^= h >> 15;
  return h;
}

static size_t zdd_level(const ds_zdd_t *m, size_t n) {
  return n <= DS_ZDD_BASE ? m->vars : m->level_of[m->var[n]];
}

static ds_status_t zdd_rehash(ds_zdd_t *m, size_t size) {
  size_t *t = (size_t *)zdd_array(m->context, NULL, size, sizeof(size_t));
  size_t i;
  if (t == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < size; i++) {
    t[i] = ZDD_NONE;
  }
  for (i = 2; i < m->nodes; i++) {
    size_t h = zdd_hash3(m->var[i], m->low[i], m->high[i]) & (size - 1);
    while (t[h] != ZDD_NONE) {
      h = (h + 1) & (size - 1);
    }
    t[h] = i;
  }
  ds_context_free(m->context, m->unique);
  m->unique = t;
  m->unique_size = size;
  return DS_OK;
}

/* the node (v, lo, hi), zero-suppressed: hi empty gives lo */
static ds_status_t zdd_mk(ds_zdd_t *m, size_t v, size_t lo, size_t hi,
                          size_t *out) {
  size_t h;
  size_t id;
  if (hi == DS_ZDD_EMPTY) {
    *out = lo;
    return DS_OK;
  }
  /* keep the table at most half full, growing before inserting */
  if (2 * (m->nodes + 1) > m->unique_size) {
    ds_status_t st = zdd_rehash(m, m->unique_size * 2);
    if (st != DS_OK) {
      return st;
    }
  }
  h = zdd_hash3(v, lo, hi) & (m->unique_size - 1);
  while (m->unique[h] != ZDD_NONE) {
    id = m->unique[h];
    if (m->var[id] == v && m->low[id] == lo && m->high[id] == hi) {
      *out = id;
      return DS_OK;
    }
    h = (h + 1) & (m->unique_size - 1);
  }
  if (m->nodes == m->capacity) {
    size_t cap = m->capacity * 2;
    size_t *g = (size_t *)zdd_array(m->context, m->var, cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    m->var = g;
    g = (size_t *)zdd_array(m->context, m->low, cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    m->low = g;
    g = (size_t *)zdd_array(m->context, m->high, cap, sizeof(size_t));
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
  return DS_OK;
}

ds_status_t ds_zdd_init(ds_zdd_t *m, size_t vars, const size_t *order,
                        ds_context_t *context) {
  size_t i;
  if (m == NULL) {
    return DS_ERR_NULL;
  }
  memset(m, 0, sizeof(*m));
  if (vars > ((size_t)-1) / 4) {
    return DS_ERR_OVERFLOW;
  }
  m->vars = vars;
  m->context = context;
  m->level_of = (size_t *)zdd_array(context, NULL, 2 * vars, sizeof(size_t));
  m->mark = (unsigned char *)ds_context_calloc(context, vars + 1, 1);
  m->capacity = 64;
  m->var = (size_t *)zdd_array(context, NULL, m->capacity, sizeof(size_t));
  m->low = (size_t *)zdd_array(context, NULL, m->capacity, sizeof(size_t));
  m->high = (size_t *)zdd_array(context, NULL, m->capacity, sizeof(size_t));
  m->cache_size = (size_t)1 << ZDD_CACHE_BITS;
  m->cache =
      (size_t *)zdd_array(context, NULL, 4 * m->cache_size, sizeof(size_t));
  if (m->level_of == NULL || m->mark == NULL || m->var == NULL ||
      m->low == NULL || m->high == NULL || m->cache == NULL) {
    ds_zdd_destroy(m);
    return DS_ERR_ALLOC;
  }
  m->var_at_level = m->level_of + vars;
  for (i = 0; i < vars; i++) {
    m->level_of[i] = ZDD_NONE;
  }
  for (i = 0; i < vars; i++) {
    size_t v = order != NULL ? order[i] : i;
    if (v >= vars || m->level_of[v] != ZDD_NONE) {
      ds_zdd_destroy(m);
      return DS_ERR_INVALID;
    }
    m->level_of[v] = i;
    m->var_at_level[i] = v;
  }
  for (i = 0; i < 4 * m->cache_size; i += 4) {
    m->cache[i + 2] = ZDD_NONE; /* no operation: an empty entry */
  }
  m->var[0] = m->var[1] = vars;
  m->low[0] = m->high[0] = 0;
  m->low[1] = m->high[1] = 1;
  m->nodes = 2;
  if (zdd_rehash(m, 128) != DS_OK) {
    ds_zdd_destroy(m);
    return DS_ERR_ALLOC;
  }
  return DS_OK;
}

void ds_zdd_destroy(ds_zdd_t *m) {
  if (m == NULL) {
    return;
  }
  ds_context_free(m->context, m->level_of);
  ds_context_free(m->context, m->mark);
  ds_context_free(m->context, m->var);
  ds_context_free(m->context, m->low);
  ds_context_free(m->context, m->high);
  ds_context_free(m->context, m->unique);
  ds_context_free(m->context, m->cache);
  ds_context_free(m->context, m->stack);
  memset(m, 0, sizeof(*m));
}

/* ---- the operation engine ---- */

static ds_status_t zdd_push(ds_zdd_t *m, size_t *top, unsigned op, size_t f,
                            size_t g) {
  size_t *fr;
  if ((*top + 1) * ZF > m->stack_capacity) {
    size_t cap = m->stack_capacity < 64 * ZF ? 64 * ZF : m->stack_capacity * 2;
    size_t *s = (size_t *)zdd_array(m->context, m->stack, cap, sizeof(size_t));
    if (s == NULL) {
      return DS_ERR_ALLOC;
    }
    m->stack = s;
    m->stack_capacity = cap;
  }
  fr = m->stack + *top * ZF;
  fr[F_OP] = op;
  fr[F_F] = f;
  fr[F_G] = g;
  fr[F_STAGE] = 0;
  (*top)++;
  return DS_OK;
}

static bool zdd_has_empty(const ds_zdd_t *m, size_t g) {
  while (g > DS_ZDD_BASE) {
    g = m->low[g];
  }
  return g == DS_ZDD_BASE;
}

/* the answer without recursion, when there is one */
static bool zdd_terminal(const ds_zdd_t *m, unsigned op, size_t f, size_t g,
                         size_t *res) {
  switch (op) {
  case Z_UNION:
    *res = f == DS_ZDD_EMPTY ? g : f;
    return f == DS_ZDD_EMPTY || g == DS_ZDD_EMPTY || f == g;
  case Z_INTER:
    *res = f == g ? f : DS_ZDD_EMPTY;
    return f == DS_ZDD_EMPTY || g == DS_ZDD_EMPTY || f == g;
  case Z_DIFF:
    *res = g == DS_ZDD_EMPTY ? f : DS_ZDD_EMPTY;
    return f == DS_ZDD_EMPTY || g == DS_ZDD_EMPTY || f == g;
  case Z_SYMDIFF:
    *res = f == g ? DS_ZDD_EMPTY : (f == DS_ZDD_EMPTY ? g : f);
    return f == DS_ZDD_EMPTY || g == DS_ZDD_EMPTY || f == g;
  case Z_JOIN:
    if (f == DS_ZDD_EMPTY || g == DS_ZDD_EMPTY) {
      *res = DS_ZDD_EMPTY;
      return true;
    }
    *res = f == DS_ZDD_BASE ? g : f;
    return f == DS_ZDD_BASE || g == DS_ZDD_BASE;
  case Z_MEET:
    if (f == DS_ZDD_EMPTY || g == DS_ZDD_EMPTY) {
      *res = DS_ZDD_EMPTY;
      return true;
    }
    *res = DS_ZDD_BASE; /* the empty set meets anything in the empty set */
    return f == DS_ZDD_BASE || g == DS_ZDD_BASE;
  case Z_NONSUP:
    if (g == DS_ZDD_EMPTY || f == DS_ZDD_EMPTY) {
      *res = f;
      return true;
    }
    if (g == DS_ZDD_BASE || f == g) {
      *res = DS_ZDD_EMPTY; /* every set contains the empty set, or itself */
      return true;
    }
    if (f == DS_ZDD_BASE) {
      *res = zdd_has_empty(m, g) ? DS_ZDD_EMPTY : DS_ZDD_BASE;
      return true;
    }
    return false;
  case Z_NONSUB:
    if (g == DS_ZDD_EMPTY || f == DS_ZDD_EMPTY) {
      *res = f;
      return true;
    }
    if (f == g || f == DS_ZDD_BASE) {
      *res = DS_ZDD_EMPTY; /* in itself, or the empty set in anything */
      return true;
    }
    return false;
  case Z_MIN:
  case Z_MAX:
    *res = f;
    return f <= DS_ZDD_BASE;
  default:
    return false;
  }
}

static bool zdd_commutes(unsigned op) {
  return op == Z_UNION || op == Z_INTER || op == Z_SYMDIFF || op == Z_JOIN ||
         op == Z_MEET;
}

static ds_status_t zdd_run(ds_zdd_t *m, unsigned op, size_t f, size_t g,
                           size_t *out) {
  size_t top = 0;
  size_t ret = ZDD_NONE;
  ds_status_t st = zdd_push(m, &top, op, f, g);
  while (st == DS_OK && top > 0) {
    size_t *fr = m->stack + (top - 1) * ZF;
    unsigned fop = (unsigned)fr[F_OP];
    size_t stage = fr[F_STAGE];
    size_t res = ZDD_NONE;
    size_t c;
    if (stage == 0) {
      size_t a = fr[F_F], b = fr[F_G];
      if (zdd_commutes(fop) && a > b) {
        fr[F_F] = b;
        fr[F_G] = a;
        a = fr[F_F];
        b = fr[F_G];
      }
      if (!zdd_terminal(m, fop, a, b, &res)) {
        res = ZDD_NONE; /* the terminal test may have set it */
        c = zdd_hash3(a, b, fop) & (m->cache_size - 1);
        if (m->cache[4 * c] == a && m->cache[4 * c + 1] == b &&
            m->cache[4 * c + 2] == fop) {
          res = m->cache[4 * c + 3];
        }
      }
      if (res == ZDD_NONE &&
          (fop == Z_SUB0 || fop == Z_SUB1 || fop == Z_CHANGE)) {
        /* b is the level of the variable */
        size_t la = zdd_level(m, a);
        size_t v = m->var_at_level[b];
        if (la > b) {
          /* no set of a contains v */
          if (fop == Z_CHANGE) {
            st = zdd_mk(m, v, DS_ZDD_EMPTY, a, &res);
          } else {
            res = fop == Z_SUB0 ? a : DS_ZDD_EMPTY;
          }
        } else if (la == b) {
          if (fop == Z_CHANGE) {
            st = zdd_mk(m, v, m->high[a], m->low[a], &res);
          } else {
            res = fop == Z_SUB0 ? m->low[a] : m->high[a];
          }
        }
      }
      if (st != DS_OK) {
        break;
      }
      if (res == ZDD_NONE) {
        /* cofactors at the top level */
        size_t lv;
        if (fop == Z_SUB0 || fop == Z_SUB1 || fop == Z_CHANGE || fop == Z_MIN ||
            fop == Z_MAX) {
          lv = zdd_level(m, a);
          fr[F_G0] = fr[F_G1] = DS_ZDD_EMPTY;
        } else {
          size_t la = zdd_level(m, a), lb = zdd_level(m, b);
          lv = la < lb ? la : lb;
          fr[F_G0] = lb == lv ? m->low[b] : b;
          fr[F_G1] = lb == lv ? m->high[b] : DS_ZDD_EMPTY;
        }
        fr[F_F0] = zdd_level(m, a) == lv ? m->low[a] : a;
        fr[F_F1] = zdd_level(m, a) == lv ? m->high[a] : DS_ZDD_EMPTY;
        fr[F_VAR] = m->var_at_level[lv];
        fr[F_STAGE] = 1;
        continue;
      }
    } else {
      size_t steps;
      const zdd_step_t *plan = zdd_plan(fop, &steps);
      if (stage >= 2) {
        fr[plan[stage - 2].dst] = ret;
      }
      if (stage - 1 < steps) {
        const zdd_step_t *s = &plan[stage - 1];
        unsigned sop = s->op == Z_SELF ? fop : s->op;
        size_t x = fr[s->a];
        size_t y = fr[s->b];
        fr[F_STAGE] = stage + 1; /* before the push may move the stack */
        st = zdd_push(m, &top, sop, x, y);
        continue;
      }
      st = zdd_mk(m, fr[F_VAR], fr[F_T0], fr[F_T1], &res);
      if (st != DS_OK) {
        break;
      }
      c = zdd_hash3(fr[F_F], fr[F_G], fop) & (m->cache_size - 1);
      m->cache[4 * c] = fr[F_F];
      m->cache[4 * c + 1] = fr[F_G];
      m->cache[4 * c + 2] = fop;
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

/* ---- public operations ---- */

static ds_status_t zdd_binary(ds_zdd_t *m, unsigned op, size_t f, size_t g,
                              size_t *out) {
  if (m == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (f >= m->nodes || g >= m->nodes) {
    return DS_ERR_RANGE;
  }
  return zdd_run(m, op, f, g, out);
}

static ds_status_t zdd_by_var(ds_zdd_t *m, unsigned op, size_t f, size_t v,
                              size_t *out) {
  if (m == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (f >= m->nodes || v >= m->vars) {
    return DS_ERR_RANGE;
  }
  return zdd_run(m, op, f, m->level_of[v], out);
}

ds_status_t ds_zdd_union(ds_zdd_t *m, size_t f, size_t g, size_t *out) {
  return zdd_binary(m, Z_UNION, f, g, out);
}

ds_status_t ds_zdd_intersect(ds_zdd_t *m, size_t f, size_t g, size_t *out) {
  return zdd_binary(m, Z_INTER, f, g, out);
}

ds_status_t ds_zdd_diff(ds_zdd_t *m, size_t f, size_t g, size_t *out) {
  return zdd_binary(m, Z_DIFF, f, g, out);
}

ds_status_t ds_zdd_symdiff(ds_zdd_t *m, size_t f, size_t g, size_t *out) {
  return zdd_binary(m, Z_SYMDIFF, f, g, out);
}

ds_status_t ds_zdd_subset0(ds_zdd_t *m, size_t f, size_t v, size_t *out) {
  return zdd_by_var(m, Z_SUB0, f, v, out);
}

ds_status_t ds_zdd_subset1(ds_zdd_t *m, size_t f, size_t v, size_t *out) {
  return zdd_by_var(m, Z_SUB1, f, v, out);
}

ds_status_t ds_zdd_change(ds_zdd_t *m, size_t f, size_t v, size_t *out) {
  return zdd_by_var(m, Z_CHANGE, f, v, out);
}

ds_status_t ds_zdd_join(ds_zdd_t *m, size_t f, size_t g, size_t *out) {
  return zdd_binary(m, Z_JOIN, f, g, out);
}

ds_status_t ds_zdd_meet(ds_zdd_t *m, size_t f, size_t g, size_t *out) {
  return zdd_binary(m, Z_MEET, f, g, out);
}

ds_status_t ds_zdd_nonsup(ds_zdd_t *m, size_t f, size_t g, size_t *out) {
  return zdd_binary(m, Z_NONSUP, f, g, out);
}

ds_status_t ds_zdd_nonsub(ds_zdd_t *m, size_t f, size_t g, size_t *out) {
  return zdd_binary(m, Z_NONSUB, f, g, out);
}

ds_status_t ds_zdd_minimal(ds_zdd_t *m, size_t f, size_t *out) {
  return zdd_binary(m, Z_MIN, f, DS_ZDD_EMPTY, out);
}

ds_status_t ds_zdd_maximal(ds_zdd_t *m, size_t f, size_t *out) {
  return zdd_binary(m, Z_MAX, f, DS_ZDD_EMPTY, out);
}

ds_status_t ds_zdd_single(ds_zdd_t *m, size_t v, size_t *out) {
  if (m == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (v >= m->vars) {
    return DS_ERR_RANGE;
  }
  return zdd_mk(m, v, DS_ZDD_EMPTY, DS_ZDD_BASE, out);
}

ds_status_t ds_zdd_set(ds_zdd_t *m, const size_t *set, size_t k, size_t *out) {
  size_t i;
  size_t r = DS_ZDD_BASE;
  ds_status_t st = DS_OK;
  if (m == NULL || out == NULL || (set == NULL && k > 0)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < k; i++) {
    if (set[i] >= m->vars) {
      return DS_ERR_RANGE;
    }
  }
  for (i = 0; i < k; i++) {
    m->mark[m->level_of[set[i]]] = 1;
  }
  /* bottom up: the deepest level first */
  for (i = m->vars; i-- > 0;) {
    if (m->mark[i]) {
      m->mark[i] = 0;
      if (st == DS_OK) {
        st = zdd_mk(m, m->var_at_level[i], DS_ZDD_EMPTY, r, &r);
      }
    }
  }
  if (st == DS_OK) {
    *out = r;
  }
  return st;
}

/* ---- queries ---- */

ds_status_t ds_zdd_count(ds_zdd_t *m, size_t f, double *out) {
  double *cnt;
  size_t *stack;
  unsigned char *done;
  size_t top = 0;
  if (m == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (f >= m->nodes) {
    return DS_ERR_RANGE;
  }
  cnt = (double *)zdd_array(m->context, NULL, m->nodes, sizeof(double));
  stack = (size_t *)zdd_array(m->context, NULL, m->nodes, sizeof(size_t));
  done = (unsigned char *)ds_context_calloc(m->context, m->nodes, 1);
  if (cnt == NULL || stack == NULL || done == NULL) {
    ds_context_free(m->context, cnt);
    ds_context_free(m->context, stack);
    ds_context_free(m->context, done);
    return DS_ERR_ALLOC;
  }
  cnt[0] = 0.0;
  cnt[1] = 1.0;
  done[0] = done[1] = 1;
  stack[top++] = f;
  while (top > 0) {
    size_t x = stack[top - 1];
    if (done[x]) {
      top--;
    } else if (!done[m->low[x]]) {
      stack[top++] = m->low[x];
    } else if (!done[m->high[x]]) {
      stack[top++] = m->high[x];
    } else {
      cnt[x] = cnt[m->low[x]] + cnt[m->high[x]];
      done[x] = 1;
      top--;
    }
  }
  *out = cnt[f];
  ds_context_free(m->context, cnt);
  ds_context_free(m->context, stack);
  ds_context_free(m->context, done);
  return DS_OK;
}

bool ds_zdd_contains(ds_zdd_t *m, size_t f, const size_t *set, size_t k) {
  size_t i;
  size_t want = 0;
  size_t met = 0;
  if (m == NULL || f >= m->nodes || (set == NULL && k > 0)) {
    return false;
  }
  for (i = 0; i < k; i++) {
    if (set[i] >= m->vars) {
      return false;
    }
  }
  for (i = 0; i < k; i++) {
    if (!m->mark[set[i]]) {
      m->mark[set[i]] = 1;
      want++;
    }
  }
  /* a variable the path skips is absent from its set */
  while (f > DS_ZDD_BASE) {
    if (m->mark[m->var[f]]) {
      met++;
      f = m->high[f];
    } else {
      f = m->low[f];
    }
  }
  for (i = 0; i < k; i++) {
    m->mark[set[i]] = 0;
  }
  return f == DS_ZDD_BASE && met == want;
}

ds_status_t ds_zdd_any(const ds_zdd_t *m, size_t f, size_t *out, size_t cap,
                       size_t *k) {
  size_t n = 0;
  size_t x;
  if (m == NULL || k == NULL || (out == NULL && cap > 0)) {
    return DS_ERR_NULL;
  }
  *k = 0;
  if (f >= m->nodes) {
    return DS_ERR_RANGE;
  }
  if (f == DS_ZDD_EMPTY) {
    return DS_NOT_FOUND;
  }
  /* every family but the empty one is reached by a path to BASE: prefer
   * low edges, which are never empty below a node of a nonempty family
   * unless the high edge is taken */
  for (x = f; x > DS_ZDD_BASE;
       x = m->low[x] != DS_ZDD_EMPTY ? m->low[x] : m->high[x]) {
    if (m->low[x] == DS_ZDD_EMPTY) {
      n++;
    }
  }
  *k = n;
  if (n > cap) {
    return DS_ERR_RANGE;
  }
  n = 0;
  for (x = f; x > DS_ZDD_BASE;
       x = m->low[x] != DS_ZDD_EMPTY ? m->low[x] : m->high[x]) {
    if (m->low[x] == DS_ZDD_EMPTY) {
      out[n++] = m->var[x];
    }
  }
  return DS_OK;
}

ds_status_t ds_zdd_foreach(ds_zdd_t *m, size_t f, ds_zdd_visit_fn visit,
                           void *user) {
  size_t *node;
  unsigned char *state;
  size_t *path;
  size_t top = 0;
  size_t len = 0;
  ds_status_t st = DS_OK;
  if (m == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (f >= m->nodes) {
    return DS_ERR_RANGE;
  }
  if (f == DS_ZDD_EMPTY) {
    return DS_OK;
  }
  /* a path tests each level at most once: vars + 1 frames */
  node = (size_t *)zdd_array(m->context, NULL, m->vars + 1, sizeof(size_t));
  path = (size_t *)zdd_array(m->context, NULL, m->vars + 1, sizeof(size_t));
  state = (unsigned char *)ds_context_calloc(m->context, m->vars + 1, 1);
  if (node == NULL || path == NULL || state == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  node[top] = f;
  state[top] = 0;
  top++;
  while (top > 0) {
    size_t x = node[top - 1];
    if (x == DS_ZDD_BASE) {
      top--;
      if (!visit(path, len, user)) {
        st = DS_STOP;
        break;
      }
    } else if (state[top - 1] == 0) {
      state[top - 1] = 1;
      if (m->low[x] != DS_ZDD_EMPTY) {
        node[top] = m->low[x];
        state[top] = 0;
        top++;
      }
    } else if (state[top - 1] == 1) {
      state[top - 1] = 2;
      path[len++] = m->var[x];
      node[top] = m->high[x];
      state[top] = 0;
      top++;
    } else {
      len--;
      top--;
    }
  }
done:
  ds_context_free(m->context, node);
  ds_context_free(m->context, path);
  ds_context_free(m->context, state);
  return st;
}

size_t ds_zdd_node_count(ds_zdd_t *m, size_t f) {
  unsigned char *seen;
  size_t *stack;
  size_t top = 0;
  size_t count = 0;
  if (m == NULL || f >= m->nodes) {
    return 0;
  }
  seen = (unsigned char *)ds_context_calloc(m->context, m->nodes, 1);
  stack = (size_t *)zdd_array(m->context, NULL, m->nodes, sizeof(size_t));
  if (seen == NULL || stack == NULL) {
    ds_context_free(m->context, seen);
    ds_context_free(m->context, stack);
    return 0;
  }
  stack[top++] = f;
  seen[f] = 1;
  while (top > 0) {
    size_t x = stack[--top];
    count++;
    if (x <= DS_ZDD_BASE) {
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
