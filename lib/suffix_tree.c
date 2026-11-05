#include "suffix_tree.h"
#include <limits.h>
#include <string.h>

#define ST_NONE (~(size_t)0)
#define ST_LEAF_END (~(size_t)0)

static void *st_alloc(ds_context_t *ctx, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > ((size_t)-1) / size) {
    return NULL;
  }
  return ds_context_alloc(ctx, count * size);
}

static size_t st_hash(size_t node, size_t sym, size_t mask) {
  size_t k = node * 1000003u + sym;
  k ^= k >> 16;
  k *= 2654435761UL;
  k ^= k >> 13;
  return k & mask;
}

static size_t st_find_slot(const ds_suffix_tree_t *t, size_t node, size_t sym) {
  size_t i = st_hash(node, sym, t->hash_size - 1);
  while (t->hval[i] != ST_NONE &&
         (t->hkey_node[i] != node || t->hkey_sym[i] != sym)) {
    i = (i + 1) & (t->hash_size - 1);
  }
  return i;
}

static size_t st_child(const ds_suffix_tree_t *t, size_t node, size_t sym) {
  return t->hval[st_find_slot(t, node, sym)];
}

static void st_set_child(ds_suffix_tree_t *t, size_t node, size_t sym,
                         size_t child) {
  size_t i = st_find_slot(t, node, sym);
  t->hkey_node[i] = node;
  t->hkey_sym[i] = sym;
  t->hval[i] = child;
}

static size_t st_new_node(ds_suffix_tree_t *t, size_t start, size_t end,
                          size_t suffix) {
  size_t id = t->nodes++;
  t->start[id] = start;
  t->end[id] = end;
  t->link[id] = 0;
  t->suffix[id] = suffix;
  return id;
}

static size_t st_edge_len(const ds_suffix_tree_t *t, size_t node, size_t pos) {
  size_t e = t->end[node] == ST_LEAF_END ? pos : t->end[node];
  return e - t->start[node] + 1;
}

void ds_suffix_tree_destroy(ds_suffix_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->text);
  ds_context_free(t->context, t->string_start);
  ds_context_free(t->context, t->start);
  ds_context_free(t->context, t->hkey_node);
  ds_context_free(t->context, t->child_off);
  memset(t, 0, sizeof(*t));
}

/* Ukkonen over t->text. */
static ds_status_t st_ukkonen(ds_suffix_tree_t *t) {
  size_t n = t->length;
  size_t cap = 2 * n + 2;
  size_t hs = 1;
  size_t pos;
  size_t active_node = 0;
  size_t active_edge = 0;
  size_t active_len = 0;
  size_t remainder = 0;
  size_t last_new;
  size_t nx;
  size_t el;
  size_t split;
  size_t leaf;
  size_t i;
  while (hs < 2 * cap) {
    hs *= 2;
  }
  t->start = (size_t *)st_alloc(t->context, 4 * cap, sizeof(size_t));
  t->hkey_node = (size_t *)st_alloc(t->context, 3 * hs, sizeof(size_t));
  if (t->start == NULL || t->hkey_node == NULL) {
    return DS_ERR_ALLOC;
  }
  t->end = t->start + cap;
  t->link = t->end + cap;
  t->suffix = t->link + cap;
  t->node_capacity = cap;
  t->hkey_sym = t->hkey_node + hs;
  t->hval = t->hkey_sym + hs;
  t->hash_size = hs;
  for (i = 0; i < hs; i++) {
    t->hval[i] = ST_NONE;
  }
  t->nodes = 0;
  st_new_node(t, 0, 0, ST_NONE); /* root */
  for (pos = 0; pos < n; pos++) {
    remainder++;
    last_new = ST_NONE;
    while (remainder > 0) {
      if (active_len == 0) {
        active_edge = pos;
      }
      nx = st_child(t, active_node, t->text[active_edge]);
      if (nx == ST_NONE) {
        leaf = st_new_node(t, pos, ST_LEAF_END, pos + 1 - remainder);
        st_set_child(t, active_node, t->text[active_edge], leaf);
        if (last_new != ST_NONE) {
          t->link[last_new] = active_node;
          last_new = ST_NONE;
        }
      } else {
        el = st_edge_len(t, nx, pos);
        if (active_len >= el) {
          active_edge += el;
          active_len -= el;
          active_node = nx;
          continue;
        }
        if (t->text[t->start[nx] + active_len] == t->text[pos]) {
          if (last_new != ST_NONE && active_node != 0) {
            t->link[last_new] = active_node;
            last_new = ST_NONE;
          }
          active_len++;
          break;
        }
        split = st_new_node(t, t->start[nx], t->start[nx] + active_len - 1,
                            ST_NONE);
        st_set_child(t, active_node, t->text[active_edge], split);
        leaf = st_new_node(t, pos, ST_LEAF_END, pos + 1 - remainder);
        st_set_child(t, split, t->text[pos], leaf);
        t->start[nx] += active_len;
        st_set_child(t, split, t->text[t->start[nx]], nx);
        if (last_new != ST_NONE) {
          t->link[last_new] = split;
        }
        last_new = split;
      }
      remainder--;
      if (active_node == 0 && active_len > 0) {
        active_len--;
        active_edge = pos + 1 - remainder;
      } else if (active_node != 0) {
        active_node = t->link[active_node];
      }
    }
  }
  /* close the leaves */
  for (i = 0; i < t->nodes; i++) {
    if (t->end[i] == ST_LEAF_END) {
      t->end[i] = n - 1;
    }
  }
  return DS_OK;
}

ds_status_t ds_suffix_tree_build_multi(ds_suffix_tree_t *t,
                                       const unsigned char *const *strs,
                                       const size_t *lens, size_t count,
                                       ds_context_t *context) {
  size_t total = 0;
  size_t i;
  size_t j;
  size_t k = 0;
  ds_status_t st;
  if (t == NULL || (count > 0 && (strs == NULL || lens == NULL))) {
    return DS_ERR_NULL;
  }
  if (count == 0) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->context = context;
  for (i = 0; i < count; i++) {
    if (strs[i] == NULL && lens[i] > 0) {
      return DS_ERR_NULL;
    }
    if (lens[i] > ((size_t)-1) / 4 - total - 1) {
      return DS_ERR_OVERFLOW;
    }
    total += lens[i] + 1;
  }
  t->text = (size_t *)st_alloc(context, total, sizeof(size_t));
  t->string_start = (size_t *)st_alloc(context, count + 1, sizeof(size_t));
  if (t->text == NULL || t->string_start == NULL) {
    ds_suffix_tree_destroy(t);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < count; i++) {
    t->string_start[i] = k;
    for (j = 0; j < lens[i]; j++) {
      t->text[k++] = count + strs[i][j];
    }
    t->text[k++] = i;
  }
  t->string_start[count] = k;
  t->length = total;
  t->strings = count;
  st = st_ukkonen(t);
  if (st != DS_OK) {
    ds_suffix_tree_destroy(t);
  }
  return st;
}

ds_status_t ds_suffix_tree_build(ds_suffix_tree_t *t, const unsigned char *s,
                                 size_t n, ds_context_t *context) {
  const unsigned char *strs[1];
  size_t lens[1];
  if (s == NULL && n > 0) {
    return DS_ERR_NULL;
  }
  strs[0] = s;
  lens[0] = n;
  return ds_suffix_tree_build_multi(t, strs, lens, 1, context);
}

/* Ordered children (CSR), sorted by first symbol. */
static ds_status_t st_children(ds_suffix_tree_t *t) {
  size_t i;
  size_t j;
  size_t p;
  size_t v;
  if (t->child_off != NULL) {
    return DS_OK;
  }
  t->child_off =
      (size_t *)st_alloc(t->context, 2 * t->nodes + 2, sizeof(size_t));
  if (t->child_off == NULL) {
    return DS_ERR_ALLOC;
  }
  t->child = t->child_off + t->nodes + 1;
  memset(t->child_off, 0, (t->nodes + 1) * sizeof(size_t));
  for (i = 0; i < t->hash_size; i++) {
    if (t->hval[i] != ST_NONE) {
      t->child_off[t->hkey_node[i] + 1]++;
    }
  }
  for (i = 0; i < t->nodes; i++) {
    t->child_off[i + 1] += t->child_off[i];
  }
  for (i = 0; i < t->hash_size; i++) {
    if (t->hval[i] != ST_NONE) {
      t->child[t->child_off[t->hkey_node[i]]++] = t->hval[i];
    }
  }
  for (i = t->nodes; i > 0; i--) {
    t->child_off[i] = t->child_off[i - 1];
  }
  t->child_off[0] = 0;
  /* insertion sort each child list by first symbol (lists are short) */
  for (p = 0; p < t->nodes; p++) {
    for (i = t->child_off[p] + 1; i < t->child_off[p + 1]; i++) {
      v = t->child[i];
      for (j = i; j > t->child_off[p] &&
                  t->text[t->start[t->child[j - 1]]] > t->text[t->start[v]];
           j--) {
        t->child[j] = t->child[j - 1];
      }
      t->child[j] = v;
    }
  }
  return DS_OK;
}

/* Node below the end of p's path (the path may end inside its edge). */
static size_t st_match(const ds_suffix_tree_t *t, const unsigned char *p,
                       size_t m) {
  size_t node = 0;
  size_t i = 0;
  size_t k;
  while (i < m) {
    node = st_child(t, node, t->strings + p[i]);
    if (node == ST_NONE) {
      return ST_NONE;
    }
    for (k = t->start[node]; k <= t->end[node] && i < m; k++, i++) {
      if (t->text[k] != t->strings + p[i]) {
        return ST_NONE;
      }
    }
  }
  return node;
}

bool ds_suffix_tree_contains(const ds_suffix_tree_t *t, const unsigned char *p,
                             size_t m) {
  if (t == NULL || (p == NULL && m > 0) || t->nodes == 0) {
    return false;
  }
  return m == 0 || st_match(t, p, m) != ST_NONE;
}

static size_t st_string_of(const ds_suffix_tree_t *t, size_t pos) {
  size_t lo = 0;
  size_t hi = t->strings;
  size_t mid;
  while (hi - lo > 1) {
    mid = lo + (hi - lo) / 2;
    if (t->string_start[mid] <= pos) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return lo;
}

ds_status_t ds_suffix_tree_locate(ds_suffix_tree_t *t, const unsigned char *p,
                                  size_t m, ds_suffix_tree_visit_func_t visit,
                                  void *user) {
  size_t *stack;
  size_t top = 0;
  size_t node;
  size_t i;
  size_t s;
  ds_status_t st;
  if (t == NULL || visit == NULL || (p == NULL && m > 0)) {
    return DS_ERR_NULL;
  }
  if (m == 0) {
    return DS_ERR_INVALID;
  }
  node = st_match(t, p, m);
  if (node == ST_NONE) {
    return DS_OK;
  }
  st = st_children(t);
  if (st != DS_OK) {
    return st;
  }
  stack = (size_t *)st_alloc(t->context, t->nodes, sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  stack[top++] = node;
  while (top > 0) {
    node = stack[--top];
    if (t->suffix[node] != ST_NONE) {
      s = st_string_of(t, t->suffix[node]);
      if (!visit(s, t->suffix[node] - t->string_start[s], user)) {
        break;
      }
      continue;
    }
    for (i = t->child_off[node]; i < t->child_off[node + 1]; i++) {
      stack[top++] = t->child[i];
    }
  }
  ds_context_free(t->context, stack);
  return DS_OK;
}

static bool st_count_visit(size_t string, size_t offset, void *user) {
  (void)string;
  (void)offset;
  (*(size_t *)user)++;
  return true;
}

ds_status_t ds_suffix_tree_count(ds_suffix_tree_t *t, const unsigned char *p,
                                 size_t m, size_t *out_count) {
  if (out_count == NULL) {
    return DS_ERR_NULL;
  }
  *out_count = 0;
  if (t != NULL && m == 0) {
    *out_count = t->length - t->strings + 1;
    return DS_OK;
  }
  return ds_suffix_tree_locate(t, p, m, st_count_visit, out_count);
}

/* Depth-first pass computing string depths (iterative). */
static ds_status_t st_depths(ds_suffix_tree_t *t, size_t *depth,
                             size_t *order) {
  size_t top = 0;
  size_t n_order = 0;
  size_t node;
  size_t i;
  size_t c;
  size_t *stack = (size_t *)st_alloc(t->context, t->nodes, sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  depth[0] = 0;
  stack[top++] = 0;
  while (top > 0) {
    node = stack[--top];
    order[n_order++] = node;
    for (i = t->child_off[node]; i < t->child_off[node + 1]; i++) {
      c = t->child[i];
      depth[c] = depth[node] + (t->end[c] - t->start[c] + 1);
      stack[top++] = c;
    }
  }
  ds_context_free(t->context, stack);
  return DS_OK;
}

/* Any leaf below each node (to recover a position). */
static void st_any_leaf(const ds_suffix_tree_t *t, const size_t *order,
                        size_t *leaf) {
  size_t k;
  size_t node;
  for (k = t->nodes; k-- > 0;) {
    node = order[k];
    if (t->suffix[node] != ST_NONE) {
      leaf[node] = t->suffix[node];
    } else {
      leaf[node] = leaf[t->child[t->child_off[node]]];
    }
  }
}

ds_status_t ds_suffix_tree_longest_repeat(ds_suffix_tree_t *t,
                                          size_t *out_string,
                                          size_t *out_offset,
                                          size_t *out_length) {
  size_t *depth;
  size_t *order;
  size_t *leaf;
  size_t best = 0;
  size_t bn = 0;
  size_t v;
  size_t s;
  ds_status_t st;
  if (t == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  st = st_children(t);
  if (st != DS_OK) {
    return st;
  }
  depth = (size_t *)st_alloc(t->context, 3 * t->nodes, sizeof(size_t));
  if (depth == NULL) {
    return DS_ERR_ALLOC;
  }
  order = depth + t->nodes;
  leaf = order + t->nodes;
  st = st_depths(t, depth, order);
  if (st == DS_OK) {
    st_any_leaf(t, order, leaf);
    for (v = 1; v < t->nodes; v++) {
      if (t->suffix[v] == ST_NONE && depth[v] > best) {
        best = depth[v];
        bn = v;
      }
    }
    *out_length = best;
    s = best > 0 ? st_string_of(t, leaf[bn]) : 0;
    if (out_string != NULL) {
      *out_string = s;
    }
    if (out_offset != NULL) {
      *out_offset = best > 0 ? leaf[bn] - t->string_start[s] : 0;
    }
  }
  ds_context_free(t->context, depth);
  return st;
}

ds_status_t ds_suffix_tree_longest_common(ds_suffix_tree_t *t,
                                          size_t *out_string,
                                          size_t *out_offset,
                                          size_t *out_length) {
  size_t words;
  unsigned long *bits;
  size_t *depth;
  size_t *order;
  size_t *leaf;
  size_t best = 0;
  size_t bn = 0;
  size_t k;
  size_t w;
  size_t node;
  size_t i;
  size_t s;
  size_t bitsz = sizeof(unsigned long) * CHAR_BIT;
  int all;
  ds_status_t st;
  if (t == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  st = st_children(t);
  if (st != DS_OK) {
    return st;
  }
  words = t->strings / bitsz + 1;
  if (t->nodes > ((size_t)-1) / words / sizeof(unsigned long)) {
    return DS_ERR_OVERFLOW;
  }
  depth = (size_t *)st_alloc(t->context, 3 * t->nodes, sizeof(size_t));
  bits = (unsigned long *)ds_context_calloc(t->context, t->nodes * words,
                                            sizeof(unsigned long));
  if (depth == NULL || bits == NULL) {
    ds_context_free(t->context, depth);
    ds_context_free(t->context, bits);
    return DS_ERR_ALLOC;
  }
  order = depth + t->nodes;
  leaf = order + t->nodes;
  st = st_depths(t, depth, order);
  if (st == DS_OK) {
    st_any_leaf(t, order, leaf);
    for (k = t->nodes; k-- > 0;) {
      node = order[k];
      if (t->suffix[node] != ST_NONE) {
        s = st_string_of(t, t->suffix[node]);
        bits[node * words + s / bitsz] |= 1UL << (s % bitsz);
        continue;
      }
      for (i = t->child_off[node]; i < t->child_off[node + 1]; i++) {
        for (w = 0; w < words; w++) {
          bits[node * words + w] |= bits[t->child[i] * words + w];
        }
      }
      all = 1;
      for (s = 0; s < t->strings && all; s++) {
        all = (bits[node * words + s / bitsz] >> (s % bitsz)) & 1UL ? 1 : 0;
      }
      if (all && node != 0 && depth[node] > best) {
        best = depth[node];
        bn = node;
      }
    }
    /* a single string: its leaves' paths end in its terminator */
    if (t->strings == 1) {
      best = t->length - 1;
      bn = 0;
    }
    *out_length = best;
    s = (best > 0 && t->strings > 1) ? st_string_of(t, leaf[bn]) : 0;
    if (out_string != NULL) {
      *out_string = s;
    }
    if (out_offset != NULL) {
      *out_offset =
          (best > 0 && t->strings > 1) ? leaf[bn] - t->string_start[s] : 0;
    }
  }
  ds_context_free(t->context, depth);
  ds_context_free(t->context, bits);
  return st;
}

ds_status_t ds_suffix_tree_suffix_array(ds_suffix_tree_t *t, size_t *out_sa) {
  size_t *stack;
  size_t top = 0;
  size_t node;
  size_t i;
  size_t k = 0;
  ds_status_t st;
  if (t == NULL || out_sa == NULL) {
    return DS_ERR_NULL;
  }
  if (t->strings != 1) {
    return DS_ERR_INVALID;
  }
  st = st_children(t);
  if (st != DS_OK) {
    return st;
  }
  stack = (size_t *)st_alloc(t->context, t->nodes, sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  stack[top++] = 0;
  while (top > 0) {
    node = stack[--top];
    if (t->suffix[node] != ST_NONE) {
      if (t->suffix[node] < t->length - 1) {
        out_sa[k++] = t->suffix[node];
      }
      continue;
    }
    for (i = t->child_off[node + 1]; i-- > t->child_off[node];) {
      stack[top++] = t->child[i];
    }
  }
  ds_context_free(t->context, stack);
  return DS_OK;
}

ds_status_t ds_bwt(const unsigned char *s, size_t n, unsigned char *out,
                   size_t *out_primary, ds_context_t *context) {
  ds_suffix_tree_t t;
  size_t *sa;
  size_t i;
  size_t k = 0;
  ds_status_t st;
  if ((s == NULL && n > 0) || out_primary == NULL || (out == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  st = ds_suffix_tree_build(&t, s, n, context);
  if (st != DS_OK) {
    return st;
  }
  sa = (size_t *)st_alloc(context, n, sizeof(size_t));
  if (sa == NULL) {
    ds_suffix_tree_destroy(&t);
    return DS_ERR_ALLOC;
  }
  st = ds_suffix_tree_suffix_array(&t, sa);
  if (st == DS_OK) {
    /* row 0 is the suffix "$", preceded by s[n - 1] */
    if (n > 0) {
      out[k++] = s[n - 1];
    }
    *out_primary = 0;
    for (i = 0; i < n; i++) {
      if (sa[i] == 0) {
        *out_primary = i + 1;
      } else {
        out[k++] = s[sa[i] - 1];
      }
    }
  }
  ds_context_free(context, sa);
  ds_suffix_tree_destroy(&t);
  return st;
}

ds_status_t ds_inverse_bwt(const unsigned char *bwt, size_t n, size_t primary,
                           unsigned char *out, ds_context_t *context) {
  size_t c[257];
  size_t *rank;
  size_t i;
  size_t r;
  size_t k;
  size_t seen[256];
  unsigned char ch;
  if ((bwt == NULL || out == NULL) && n > 0) {
    return DS_ERR_NULL;
  }
  if (primary > n || (n > 0 && primary == 0)) {
    return n == 0 && primary == 0 ? DS_OK : DS_ERR_INVALID;
  }
  if (n == 0) {
    return DS_OK;
  }
  rank = (size_t *)st_alloc(context, n + 1, sizeof(size_t));
  if (rank == NULL) {
    return DS_ERR_ALLOC;
  }
  /* L' = bwt with the marker at row primary; row index -> bwt index */
  memset(seen, 0, sizeof(seen));
  memset(c, 0, sizeof(c));
  for (i = 0; i <= n; i++) {
    if (i == primary) {
      rank[i] = 0;
      continue;
    }
    ch = bwt[i < primary ? i : i - 1];
    rank[i] = seen[ch]++;
  }
  /* C[ch] = 1 (the marker) + characters smaller than ch */
  c[0] = 1;
  for (k = 0; k < 256; k++) {
    c[k + 1] = c[k] + seen[k];
  }
  r = 0;
  for (k = n; k-- > 0;) {
    if (r == primary) {
      ds_context_free(context, rank);
      return DS_ERR_INVALID;
    }
    ch = bwt[r < primary ? r : r - 1];
    out[k] = ch;
    r = c[ch] + rank[r];
  }
  ds_context_free(context, rank);
  return r == primary ? DS_OK : DS_ERR_INVALID;
}

ds_status_t ds_inverse_suffix_array(const size_t *sa, size_t n, size_t *out) {
  size_t i;
  if (n > 0 && (sa == NULL || out == NULL)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n; i++) {
    out[i] = ST_NONE;
  }
  for (i = 0; i < n; i++) {
    if (sa[i] >= n || out[sa[i]] != ST_NONE) {
      return DS_ERR_INVALID;
    }
    out[sa[i]] = i;
  }
  return DS_OK;
}
