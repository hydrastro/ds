#include "dfuds.h"
#include <string.h>

#define W (8 * sizeof(unsigned long))
#define NONE DS_BP_NONE

static void df_set(unsigned long *w, size_t i) { w[i / W] |= 1UL << (i % W); }

ds_status_t ds_dfuds_from_parens(ds_dfuds_t *t, const unsigned long *words,
                                 size_t bits, const ds_rs_ops_t *rs_ops,
                                 ds_context_t *context) {
  ds_status_t st;
  if (t == NULL || (bits > 0 && words == NULL)) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (bits % 2 != 0) {
    return DS_ERR_INVALID;
  }
  st = ds_parens_build(&t->p, words, bits, rs_ops, context);
  if (st != DS_OK) {
    return st;
  }
  /* one tree: excess positive until the very end, where it is zero */
  if (bits > 0 &&
      ((bits > 1 && ds_parens_range_min(&t->p, 0, bits - 2, NULL) < 1) ||
       ds_parens_excess(&t->p, bits - 1) != 0 || !ds_parens_bit(&t->p, 0))) {
    ds_parens_destroy(&t->p);
    return DS_ERR_INVALID;
  }
  t->nodes = bits / 2;
  return DS_OK;
}

ds_status_t ds_dfuds_from_parents(ds_dfuds_t *t, const size_t *parent, size_t n,
                                  size_t *out_number, const ds_rs_ops_t *rs_ops,
                                  ds_context_t *context) {
  size_t *first = NULL;
  size_t *list = NULL;
  size_t *stack = NULL;
  unsigned long *w = NULL;
  size_t root = NONE;
  size_t i;
  size_t pos = 0;
  size_t seen = 0;
  ds_status_t st = DS_OK;
  if (t == NULL || (n > 0 && parent == NULL)) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (n > ((size_t)-1) / 4 - 1) {
    return DS_ERR_OVERFLOW;
  }
  for (i = 0; i < n; i++) {
    if (parent[i] == NONE) {
      if (root != NONE) {
        return DS_ERR_INVALID;
      }
      root = i;
    } else if (parent[i] >= n) {
      return DS_ERR_INVALID;
    }
  }
  if (n > 0 && root == NONE) {
    return DS_ERR_INVALID;
  }
  first = (size_t *)ds_context_calloc(context, n + 2, sizeof(size_t));
  list = (size_t *)ds_context_alloc(context, (n + 1) * sizeof(size_t));
  stack = (size_t *)ds_context_alloc(context, (n + 1) * sizeof(size_t));
  w = (unsigned long *)ds_context_calloc(context, (2 * n) / W + 1,
                                         sizeof(unsigned long));
  if (first == NULL || list == NULL || stack == NULL || w == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < n; i++) {
    if (parent[i] != NONE) {
      first[parent[i] + 1]++;
    }
  }
  for (i = 0; i < n; i++) {
    first[i + 1] += first[i];
  }
  for (i = 0; i < n; i++) {
    if (parent[i] != NONE) {
      list[first[parent[i]]++] = i;
    }
  }
  /* first[v] now ends v's children; shift back to starts */
  for (i = n; i > 0; i--) {
    first[i] = first[i - 1];
  }
  first[0] = 0;
  if (n > 0) {
    size_t sp = 0;
    df_set(w, pos++); /* the leading open */
    stack[sp++] = root;
    while (sp > 0) {
      size_t v = stack[--sp];
      size_t d = first[v + 1] - first[v];
      size_t k;
      if (seen >= n) {
        st = DS_ERR_INVALID;
        goto done;
      }
      if (out_number != NULL) {
        out_number[v] = seen;
      }
      seen++;
      for (k = 0; k < d; k++) {
        df_set(w, pos++);
      }
      pos++; /* the close */
      /* children in reverse so the first is visited first */
      for (k = d; k > 0; k--) {
        if (sp >= n) {
          st = DS_ERR_INVALID;
          goto done;
        }
        stack[sp++] = list[first[v] + k - 1];
      }
    }
    if (seen != n) {
      st = DS_ERR_INVALID;
      goto done;
    }
  }
  st = ds_parens_build(&t->p, w, 2 * n, rs_ops, context);
  if (st == DS_OK) {
    t->nodes = n;
  }
done:
  ds_context_free(context, first);
  ds_context_free(context, list);
  ds_context_free(context, stack);
  ds_context_free(context, w);
  return st;
}

void ds_dfuds_destroy(ds_dfuds_t *t) {
  if (t == NULL) {
    return;
  }
  ds_parens_destroy(&t->p);
  t->nodes = 0;
}

size_t ds_dfuds_nodes(const ds_dfuds_t *t) { return t == NULL ? 0 : t->nodes; }

size_t ds_dfuds_position(const ds_dfuds_t *t, size_t v) {
  if (t == NULL || v >= t->nodes) {
    return NONE;
  }
  return v == 0 ? 1 : ds_rs_select0(&t->p.rs, v - 1) + 1;
}

size_t ds_dfuds_node_at(const ds_dfuds_t *t, size_t position) {
  size_t v;
  if (t == NULL || position == 0 || position >= t->p.n) {
    return NONE;
  }
  v = ds_rs_rank0(&t->p.rs, position);
  return ds_dfuds_position(t, v) == position ? v : NONE;
}

/* the close ending v's description */
static size_t df_end(const ds_dfuds_t *t, size_t v) {
  return ds_rs_select0(&t->p.rs, v);
}

size_t ds_dfuds_degree(const ds_dfuds_t *t, size_t v) {
  size_t p = ds_dfuds_position(t, v);
  return p == NONE ? 0 : df_end(t, v) - p;
}

size_t ds_dfuds_child(const ds_dfuds_t *t, size_t v, size_t k) {
  size_t p = ds_dfuds_position(t, v);
  size_t e;
  if (p == NONE) {
    return NONE;
  }
  e = df_end(t, v);
  if (k >= e - p) {
    return NONE;
  }
  /* the open k + 1 places before the end of v's run is matched by the
   * close just before the k-th child's description */
  return ds_rs_rank0(&t->p.rs, ds_parens_find_close(&t->p, e - k - 1) + 1);
}

size_t ds_dfuds_first_child(const ds_dfuds_t *t, size_t v) {
  return ds_dfuds_child(t, v, 0);
}

size_t ds_dfuds_last_child(const ds_dfuds_t *t, size_t v) {
  size_t d = ds_dfuds_degree(t, v);
  return d == 0 ? NONE : ds_dfuds_child(t, v, d - 1);
}

/* the open in the parent's run that stands for v */
static size_t df_open(const ds_dfuds_t *t, size_t p) {
  return ds_parens_find_open(&t->p, p - 1);
}

size_t ds_dfuds_parent(const ds_dfuds_t *t, size_t v) {
  size_t p = ds_dfuds_position(t, v);
  if (p == NONE || v == 0) {
    return NONE;
  }
  return ds_rs_rank0(&t->p.rs, df_open(t, p));
}

size_t ds_dfuds_child_rank(const ds_dfuds_t *t, size_t v) {
  size_t p = ds_dfuds_position(t, v);
  size_t o;
  if (p == NONE || v == 0) {
    return 0;
  }
  o = df_open(t, p);
  return df_end(t, ds_rs_rank0(&t->p.rs, o)) - o - 1;
}

size_t ds_dfuds_next_sibling(const ds_dfuds_t *t, size_t v) {
  size_t p = ds_dfuds_position(t, v);
  size_t o;
  size_t parent;
  if (p == NONE || v == 0) {
    return NONE;
  }
  o = df_open(t, p);
  parent = ds_rs_rank0(&t->p.rs, o);
  if (o == ds_dfuds_position(t, parent)) {
    return NONE; /* v is the last child */
  }
  return ds_rs_rank0(&t->p.rs, ds_parens_find_close(&t->p, o - 1) + 1);
}

size_t ds_dfuds_prev_sibling(const ds_dfuds_t *t, size_t v) {
  size_t p = ds_dfuds_position(t, v);
  size_t o;
  if (p == NONE || v == 0) {
    return NONE;
  }
  o = df_open(t, p);
  if (!ds_parens_bit(&t->p, o + 1)) {
    return NONE; /* v is the first child */
  }
  return ds_rs_rank0(&t->p.rs, ds_parens_find_close(&t->p, o + 1) + 1);
}

bool ds_dfuds_is_leaf(const ds_dfuds_t *t, size_t v) {
  size_t p = ds_dfuds_position(t, v);
  return p != NONE && !ds_parens_bit(&t->p, p);
}

size_t ds_dfuds_subtree_size(const ds_dfuds_t *t, size_t v) {
  size_t p = ds_dfuds_position(t, v);
  size_t j;
  if (p == NONE) {
    return 0;
  }
  j = ds_parens_fwd_search(&t->p, p - 1, -1);
  return (j - p) / 2 + 1;
}

bool ds_dfuds_is_ancestor(const ds_dfuds_t *t, size_t u, size_t v) {
  if (t == NULL || u >= t->nodes || v >= t->nodes) {
    return false;
  }
  return u <= v && v < u + ds_dfuds_subtree_size(t, u);
}

size_t ds_dfuds_lca(const ds_dfuds_t *t, size_t u, size_t v) {
  size_t m;
  if (t == NULL || u >= t->nodes || v >= t->nodes) {
    return NONE;
  }
  if (u > v) {
    size_t x = u;
    u = v;
    v = x;
  }
  if (ds_dfuds_is_ancestor(t, u, v)) {
    return u;
  }
  m = ds_parens_rmq(&t->p, ds_dfuds_position(t, u),
                    ds_dfuds_position(t, v) - 1);
  return ds_dfuds_parent(t, ds_rs_rank0(&t->p.rs, m + 1));
}

size_t ds_dfuds_bytes(const ds_dfuds_t *t) {
  return t == NULL ? 0 : ds_parens_bytes(&t->p);
}
