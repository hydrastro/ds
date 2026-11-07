#include "tst.h"
#include <string.h>

#define TS_NONE (~(size_t)0)

ds_status_t ds_tst_init(ds_tst_t *t, ds_context_t *context) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  t->root = TS_NONE;
  t->free_list = TS_NONE;
  t->context = context;
  return DS_OK;
}

void ds_tst_destroy(ds_tst_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->split);
  ds_context_free(t->context, t->lo);
  ds_context_free(t->context, t->eq);
  ds_context_free(t->context, t->hi);
  ds_context_free(t->context, t->value);
  ds_context_free(t->context, t->terminal);
  ds_context_free(t->context, t->stack);
  ds_tst_init(t, t->context);
}

size_t ds_tst_size(const ds_tst_t *t) { return t == NULL ? 0 : t->keys; }

static ds_status_t ts_stack(ds_tst_t *t, size_t need) {
  size_t cap;
  void *p;
  if (need <= t->stack_capacity) {
    return DS_OK;
  }
  cap = t->stack_capacity < 32 ? 32 : t->stack_capacity;
  while (cap < need) {
    if (cap > ((size_t)-1) / 2 / sizeof(size_t)) {
      return DS_ERR_OVERFLOW;
    }
    cap *= 2;
  }
  p = ds_context_realloc(t->context, t->stack, cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->stack = (size_t *)p;
  t->stack_capacity = cap;
  return DS_OK;
}

static ds_status_t ts_node(ds_tst_t *t, unsigned char c, size_t *out) {
  size_t cap;
  void *p;
  size_t v;
  if (t->free_list != TS_NONE) {
    v = t->free_list;
    t->free_list = t->lo[v];
  } else {
    if (t->slots == t->capacity) {
      cap = t->capacity < 16 ? 16 : t->capacity * 2;
      if (cap <= t->capacity || cap > ((size_t)-1) / sizeof(size_t)) {
        return DS_ERR_OVERFLOW;
      }
      p = ds_context_realloc(t->context, t->split, cap);
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->split = (unsigned char *)p;
      p = ds_context_realloc(t->context, t->terminal, cap);
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->terminal = (unsigned char *)p;
      p = ds_context_realloc(t->context, t->lo, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->lo = (size_t *)p;
      p = ds_context_realloc(t->context, t->eq, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->eq = (size_t *)p;
      p = ds_context_realloc(t->context, t->hi, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->hi = (size_t *)p;
      p = ds_context_realloc(t->context, t->value, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->value = (size_t *)p;
      t->capacity = cap;
    }
    v = t->slots++;
  }
  t->split[v] = c;
  t->terminal[v] = 0;
  t->lo[v] = t->eq[v] = t->hi[v] = TS_NONE;
  t->value[v] = 0;
  *out = v;
  return DS_OK;
}

ds_status_t ds_tst_insert(ds_tst_t *t, const unsigned char *key, size_t len,
                          size_t value) {
  size_t *link;
  size_t v;
  size_t n;
  size_t i = 0;
  size_t link_node = TS_NONE;
  int link_kind = 0; /* 0 root, 1 lo, 2 eq, 3 hi */
  ds_status_t st;
  if (t == NULL || (key == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  if (len == 0) {
    t->empty_value = value;
    if (t->has_empty) {
      return DS_EXISTS;
    }
    t->has_empty = true;
    t->keys++;
    return DS_OK;
  }
  v = t->root;
  for (;;) {
    if (v == TS_NONE) {
      st = ts_node(t, key[i], &n);
      if (st != DS_OK) {
        return st;
      }
      /* arrays may have moved: resolve the link now */
      link = link_kind == 0   ? &t->root
             : link_kind == 1 ? &t->lo[link_node]
             : link_kind == 2 ? &t->eq[link_node]
                              : &t->hi[link_node];
      *link = n;
      v = n;
    }
    if (key[i] < t->split[v]) {
      link_node = v;
      link_kind = 1;
      v = t->lo[v];
    } else if (key[i] > t->split[v]) {
      link_node = v;
      link_kind = 3;
      v = t->hi[v];
    } else if (i + 1 < len) {
      link_node = v;
      link_kind = 2;
      v = t->eq[v];
      i++;
    } else {
      t->value[v] = value;
      if (t->terminal[v]) {
        return DS_EXISTS;
      }
      t->terminal[v] = 1;
      t->keys++;
      return DS_OK;
    }
  }
}

static size_t ts_walk(const ds_tst_t *t, const unsigned char *key, size_t len) {
  size_t v = t->root;
  size_t i = 0;
  while (v != TS_NONE) {
    if (key[i] < t->split[v]) {
      v = t->lo[v];
    } else if (key[i] > t->split[v]) {
      v = t->hi[v];
    } else if (i + 1 < len) {
      v = t->eq[v];
      i++;
    } else {
      return v;
    }
  }
  return TS_NONE;
}

ds_status_t ds_tst_find(const ds_tst_t *t, const unsigned char *key, size_t len,
                        size_t *out_value) {
  size_t v;
  if (t == NULL || (key == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  if (len == 0) {
    if (!t->has_empty) {
      return DS_NOT_FOUND;
    }
    if (out_value != NULL) {
      *out_value = t->empty_value;
    }
    return DS_OK;
  }
  v = ts_walk(t, key, len);
  if (v == TS_NONE || !t->terminal[v]) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL) {
    *out_value = t->value[v];
  }
  return DS_OK;
}

/* Replace the link from parent (kind) to v by r. */
static void ts_relink(ds_tst_t *t, size_t parent, int kind, size_t r) {
  if (parent == TS_NONE) {
    t->root = r;
  } else if (kind == 1) {
    t->lo[parent] = r;
  } else if (kind == 2) {
    t->eq[parent] = r;
  } else {
    t->hi[parent] = r;
  }
}

ds_status_t ds_tst_remove(ds_tst_t *t, const unsigned char *key, size_t len) {
  size_t v;
  size_t i = 0;
  size_t depth = 0;
  size_t parent;
  size_t r;
  size_t m;
  size_t mp;
  int kind;
  ds_status_t st;
  if (t == NULL || (key == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  if (len == 0) {
    if (!t->has_empty) {
      return DS_NOT_FOUND;
    }
    t->has_empty = false;
    t->keys--;
    return DS_OK;
  }
  /* record the path: pairs (node, kind of link into it) */
  v = t->root;
  kind = 0;
  while (v != TS_NONE) {
    st = ts_stack(t, 2 * depth + 2);
    if (st != DS_OK) {
      return st;
    }
    t->stack[2 * depth] = v;
    t->stack[2 * depth + 1] = (size_t)kind;
    depth++;
    if (key[i] < t->split[v]) {
      kind = 1;
      v = t->lo[v];
    } else if (key[i] > t->split[v]) {
      kind = 3;
      v = t->hi[v];
    } else if (i + 1 < len) {
      kind = 2;
      v = t->eq[v];
      i++;
    } else {
      break;
    }
  }
  if (v == TS_NONE || !t->terminal[v]) {
    return DS_NOT_FOUND;
  }
  t->terminal[v] = 0;
  t->keys--;
  /* prune upwards: a node without eq child that is not terminal is
   * spliced out of its lo/hi binary tree */
  while (depth > 0) {
    v = t->stack[2 * (depth - 1)];
    kind = (int)t->stack[2 * (depth - 1) + 1];
    parent = depth > 1 ? t->stack[2 * (depth - 2)] : TS_NONE;
    if (t->terminal[v] || t->eq[v] != TS_NONE) {
      break;
    }
    if (t->lo[v] == TS_NONE) {
      r = t->hi[v];
    } else if (t->hi[v] == TS_NONE) {
      r = t->lo[v];
    } else {
      /* replace by the maximum of the low subtree */
      mp = v;
      m = t->lo[v];
      while (t->hi[m] != TS_NONE) {
        mp = m;
        m = t->hi[m];
      }
      if (mp != v) {
        t->hi[mp] = t->lo[m];
        t->lo[m] = t->lo[v];
      }
      t->hi[m] = t->hi[v];
      r = m;
    }
    ts_relink(t, parent, kind, r);
    t->lo[v] = t->free_list;
    t->free_list = v;
    /* only an eq link going dead lets the pruning continue upwards */
    if (kind != 2 || r != TS_NONE) {
      break;
    }
    depth--;
  }
  return DS_OK;
}

/* ---- searches ------------------------------------------------------------ */

typedef struct ts_q {
  int mode; /* 0 enumerate, 1 partial match, 2 near */
  const unsigned char *pat;
  size_t len;
  unsigned char wild;
  size_t d;
} ts_q_t;

/* Depth-first, in order: frames (node, pos, dist, state) on t->stack. */
static ds_status_t ts_search(ds_tst_t *t, size_t start, size_t pos0,
                             const ts_q_t *q, unsigned char *buf,
                             size_t buf_len, ds_tst_visit_func_t visit,
                             void *user) {
  size_t top = 0;
  size_t v;
  size_t pos;
  size_t dist;
  size_t state;
  size_t nd;
  unsigned char c = 0;
  bool any;
  ds_status_t st;
  if (start == TS_NONE) {
    return DS_OK;
  }
  st = ts_stack(t, 4);
  if (st != DS_OK) {
    return st;
  }
  t->stack[0] = start;
  t->stack[1] = pos0;
  t->stack[2] = 0;
  t->stack[3] = 0;
  top = 1;
  while (top > 0) {
    v = t->stack[4 * (top - 1)];
    pos = t->stack[4 * (top - 1) + 1];
    dist = t->stack[4 * (top - 1) + 2];
    state = t->stack[4 * (top - 1) + 3]++;
    if (q->mode != 0) {
      c = q->pat[pos];
    }
    any = q->mode == 1 && c == q->wild;
    nd = TS_NONE;
    if (state == 0) {
      /* low child */
      if (q->mode == 0 || any || (q->mode == 1 && c < t->split[v]) ||
          (q->mode == 2 && (dist < q->d || c < t->split[v]))) {
        nd = t->lo[v];
      }
    } else if (state == 1) {
      /* the node itself, then its equal child */
      size_t ndist = dist;
      bool ok = true;
      if (q->mode == 1) {
        ok = any || c == t->split[v];
      } else if (q->mode == 2) {
        ndist = dist + (c != t->split[v]);
        ok = ndist <= q->d;
      }
      if (ok) {
        if (pos >= buf_len) {
          return DS_ERR_RANGE;
        }
        buf[pos] = t->split[v];
        if (t->terminal[v] && (q->mode == 0 || pos + 1 == q->len) &&
            !visit(buf, pos + 1, t->value[v], user)) {
          return DS_STOP;
        }
        if (q->mode == 0 || pos + 1 < q->len) {
          nd = t->eq[v];
          if (nd != TS_NONE) {
            st = ts_stack(t, 4 * top + 4);
            if (st != DS_OK) {
              return st;
            }
            t->stack[4 * top] = nd;
            t->stack[4 * top + 1] = pos + 1;
            t->stack[4 * top + 2] = ndist;
            t->stack[4 * top + 3] = 0;
            top++;
          }
          continue;
        }
      }
      continue;
    } else if (state == 2) {
      if (q->mode == 0 || any || (q->mode == 1 && c > t->split[v]) ||
          (q->mode == 2 && (dist < q->d || c > t->split[v]))) {
        nd = t->hi[v];
      }
    } else {
      top--;
      continue;
    }
    if (nd != TS_NONE) {
      st = ts_stack(t, 4 * top + 4);
      if (st != DS_OK) {
        return st;
      }
      t->stack[4 * top] = nd;
      t->stack[4 * top + 1] = pos;
      t->stack[4 * top + 2] = dist;
      t->stack[4 * top + 3] = 0;
      top++;
    }
  }
  return DS_OK;
}

ds_status_t ds_tst_prefix(ds_tst_t *t, const unsigned char *prefix, size_t len,
                          unsigned char *buf, size_t buf_len,
                          ds_tst_visit_func_t visit, void *user) {
  ts_q_t q;
  size_t v;
  if (t == NULL || visit == NULL || (prefix == NULL && len > 0) ||
      (buf == NULL && buf_len > 0)) {
    return DS_ERR_NULL;
  }
  if (len > buf_len) {
    return DS_ERR_RANGE;
  }
  if (len > 0) {
    memmove(buf, prefix, len);
  }
  q.mode = 0;
  q.pat = NULL;
  q.len = 0;
  q.wild = 0;
  q.d = 0;
  if (len == 0) {
    if (t->has_empty && !visit(buf, 0, t->empty_value, user)) {
      return DS_STOP;
    }
    return ts_search(t, t->root, 0, &q, buf, buf_len, visit, user);
  }
  v = ts_walk(t, prefix, len);
  if (v == TS_NONE) {
    return DS_OK;
  }
  if (t->terminal[v] && !visit(buf, len, t->value[v], user)) {
    return DS_STOP;
  }
  return ts_search(t, t->eq[v], len, &q, buf, buf_len, visit, user);
}

static ds_status_t ts_pattern(ds_tst_t *t, const unsigned char *pattern,
                              size_t len, ts_q_t *q, unsigned char *buf,
                              ds_tst_visit_func_t visit, void *user) {
  if (t == NULL || visit == NULL || (pattern == NULL && len > 0) ||
      (buf == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  if (len == 0) {
    if (t->has_empty && !visit(buf, 0, t->empty_value, user)) {
      return DS_STOP;
    }
    return DS_OK;
  }
  q->pat = pattern;
  q->len = len;
  return ts_search(t, t->root, 0, q, buf, len, visit, user);
}

ds_status_t ds_tst_partial_match(ds_tst_t *t, const unsigned char *pattern,
                                 size_t len, unsigned char wildcard,
                                 unsigned char *buf, ds_tst_visit_func_t visit,
                                 void *user) {
  ts_q_t q;
  q.mode = 1;
  q.wild = wildcard;
  q.d = 0;
  return ts_pattern(t, pattern, len, &q, buf, visit, user);
}

ds_status_t ds_tst_near(ds_tst_t *t, const unsigned char *key, size_t len,
                        size_t d, unsigned char *buf, ds_tst_visit_func_t visit,
                        void *user) {
  ts_q_t q;
  q.mode = 2;
  q.wild = 0;
  q.d = d;
  return ts_pattern(t, key, len, &q, buf, visit, user);
}
